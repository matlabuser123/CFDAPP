// GPU-DISC-001D gate -- CUDA convection vs the CPU reference, in three layers.
//
//   L1  the scheme primitives, compared DIRECTLY against the production CPU
//       functions over a swept input space (quickFaceValue,
//       linearUpwindFaceValue, smoothnessRatio, vanLeerLimiter). These are the
//       functions MomentumEquation.cpp itself calls.
//   L2  the full four-scheme face-value machinery end to end, against
//       cfd::discretization::convection() -- upwind selection, far-upstream
//       lookup, high-order value, Sweby/van Leer limiting, clamp, ghost
//       boundary value, cell sum.
//   L3  the production scalar implicit assembly (matrix diagonal, off-diagonal,
//       RHS) against thermal::assembleThermalConvectionContribution, both the
//       constant-cp and per-cell-cp overloads.
//
// The requirement is BITWISE equality throughout. Branch predicates here are
// exact (`Ff >= 0.0`, `localGradient == 0.0`, the clamp comparisons), so a
// rounding difference would flip a stencil rather than merely perturb a value.
//
// usage: convection_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/thermal/EnergyEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::SparseMatrixBuilder;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::gpu::DeviceConvectionPlan;
using cfd::gpu::DeviceConvectionSystem;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

int failures = 0;
int cases = 0;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// ---------------------------------------------------------------------------
// L1 -- primitives
// ---------------------------------------------------------------------------

std::vector<Real> upload(const std::vector<Real>& host, cfd::gpu::DeviceBuffer<Real>& buffer) {
  buffer.uploadFrom(host.data(), static_cast<Index>(host.size()));
  return host;
}

void runPrimitives() {
  std::printf("=== L1: scheme primitives vs the production CPU functions ===\n");

  // A deliberately awkward sweep: powers of two, non-representable decimals,
  // negatives, and values that make the limiter's branch predicates fire.
  const std::vector<Real> sweep = {0.125, 0.3, 1.0, 2.5, 7.0, 0.0625, 1e-3, 1e3, 0.7, 3.25};
  const std::vector<Real> values = {-2.0, -0.5, 0.0, 0.5, 1.0, 2.75, 100.0, -1e-4, 3.0, -7.125};

  std::vector<Real> a, b, c, d, e, f, wantV;
  std::size_t checks = 0;

  // --- quickFaceValue ------------------------------------------------------
  a.clear(); b.clear(); c.clear(); d.clear(); e.clear(); f.clear(); wantV.clear();
  for (const Real hCU : sweep) {
    for (const Real hUf : sweep) {
      for (const Real hfD : sweep) {
        for (std::size_t i = 0; i < values.size(); ++i) {
          const Real phiC = values[i];
          const Real phiU = values[(i + 3) % values.size()];
          const Real phiD = values[(i + 7) % values.size()];
          a.push_back(phiC); b.push_back(hCU); c.push_back(phiU);
          d.push_back(hUf); e.push_back(phiD); f.push_back(hfD);
          wantV.push_back(cfd::discretization::quickFaceValue(phiC, hCU, phiU, hUf, phiD, hfD));
        }
      }
    }
  }
  {
    cfd::gpu::DeviceBuffer<Real> da, db, dc, dd, de, df, out;
    upload(a, da); upload(b, db); upload(c, dc); upload(d, dd); upload(e, de); upload(f, df);
    cfd::gpu::evaluateConvectionPrimitives(0, da, db, dc, dd, de, df, out);
    std::vector<Real> got(wantV.size());
    out.downloadTo(got.data(), out.size());
    std::size_t differing = 0;
    Real maxAbs = 0.0;
    for (std::size_t i = 0; i < wantV.size(); ++i) {
      if (!sameBits(wantV[i], got[i])) ++differing;
      maxAbs = std::max(maxAbs, std::abs(wantV[i] - got[i]));
    }
    checks += wantV.size();
    ++cases;
    if (differing != 0) ++failures;
    std::printf("  %s quickFaceValue        n=%-6zu differing=%zu maxAbs=%.3g\n",
                differing == 0 ? "PASS" : "FAIL", wantV.size(), differing, maxAbs);
  }

  // --- linearUpwindFaceValue ----------------------------------------------
  a.clear(); b.clear(); c.clear(); d.clear(); e.clear(); f.clear(); wantV.clear();
  for (const Real gx : values) {
    for (const Real gy : values) {
      for (const Real ox : sweep) {
        for (const Real oy : sweep) {
          const Real phiU = gx + oy;
          a.push_back(phiU); b.push_back(gx); c.push_back(gy); d.push_back(0.0);
          e.push_back(ox); f.push_back(oy);
          wantV.push_back(cfd::discretization::linearUpwindFaceValue(
              phiU, Vector3{gx, gy, 0.0}, Vector3{0.0, 0.0, 0.0}, Vector3{ox, oy, 0.0}));
        }
      }
    }
  }
  {
    cfd::gpu::DeviceBuffer<Real> da, db, dc, dd, de, df, out;
    upload(a, da); upload(b, db); upload(c, dc); upload(d, dd); upload(e, de); upload(f, df);
    cfd::gpu::evaluateConvectionPrimitives(1, da, db, dc, dd, de, df, out);
    std::vector<Real> got(wantV.size());
    out.downloadTo(got.data(), out.size());
    std::size_t differing = 0;
    Real maxAbs = 0.0;
    for (std::size_t i = 0; i < wantV.size(); ++i) {
      if (!sameBits(wantV[i], got[i])) ++differing;
      maxAbs = std::max(maxAbs, std::abs(wantV[i] - got[i]));
    }
    checks += wantV.size();
    ++cases;
    if (differing != 0) ++failures;
    std::printf("  %s linearUpwindFaceValue n=%-6zu differing=%zu maxAbs=%.3g\n",
                differing == 0 ? "PASS" : "FAIL", wantV.size(), differing, maxAbs);
  }

  // --- smoothnessRatio (nullopt encoded as NaN) ----------------------------
  a.clear(); b.clear(); c.clear(); d.clear(); e.clear(); f.clear(); wantV.clear();
  std::size_t nulloptCount = 0;
  for (const Real phiC : values) {
    for (const Real phiU : values) {
      for (const Real phiD : values) {  // includes phiD == phiU -> nullopt
        a.push_back(phiC); b.push_back(phiU); c.push_back(phiD);
        d.push_back(0.0); e.push_back(0.0); f.push_back(0.0);
        const auto r = cfd::discretization::smoothnessRatio(phiC, phiU, phiD);
        if (!r.has_value()) ++nulloptCount;
        wantV.push_back(r.has_value() ? *r : std::numeric_limits<Real>::quiet_NaN());
      }
    }
  }
  {
    cfd::gpu::DeviceBuffer<Real> da, db, dc, dd, de, df, out;
    upload(a, da); upload(b, db); upload(c, dc); upload(d, dd); upload(e, de); upload(f, df);
    cfd::gpu::evaluateConvectionPrimitives(2, da, db, dc, dd, de, df, out);
    std::vector<Real> got(wantV.size());
    out.downloadTo(got.data(), out.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < wantV.size(); ++i) {
      // NaN must agree as NaN; a finite value must agree bitwise.
      const bool bothNaN = std::isnan(wantV[i]) && std::isnan(got[i]);
      if (!bothNaN && !sameBits(wantV[i], got[i])) ++differing;
    }
    checks += wantV.size();
    ++cases;
    if (differing != 0 || nulloptCount == 0) ++failures;
    std::printf("  %s smoothnessRatio       n=%-6zu differing=%zu nullopt cases=%zu\n",
                (differing == 0 && nulloptCount > 0) ? "PASS" : "FAIL", wantV.size(), differing,
                nulloptCount);
  }

  // --- vanLeerLimiter ------------------------------------------------------
  a.clear(); b.clear(); c.clear(); d.clear(); e.clear(); f.clear(); wantV.clear();
  std::vector<Real> limiterInputs = {std::numeric_limits<Real>::quiet_NaN()};
  for (const Real v : values) limiterInputs.push_back(v);
  for (const Real v : sweep) limiterInputs.push_back(v);
  limiterInputs.push_back(0.0);
  limiterInputs.push_back(-0.0);
  for (const Real r : limiterInputs) {
    a.push_back(r); b.push_back(0.0); c.push_back(0.0);
    d.push_back(0.0); e.push_back(0.0); f.push_back(0.0);
    wantV.push_back(cfd::discretization::vanLeerLimiter(
        std::isnan(r) ? std::optional<Real>{} : std::optional<Real>{r}));
  }
  {
    cfd::gpu::DeviceBuffer<Real> da, db, dc, dd, de, df, out;
    upload(a, da); upload(b, db); upload(c, dc); upload(d, dd); upload(e, de); upload(f, df);
    cfd::gpu::evaluateConvectionPrimitives(3, da, db, dc, dd, de, df, out);
    std::vector<Real> got(wantV.size());
    out.downloadTo(got.data(), out.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < wantV.size(); ++i) {
      if (!sameBits(wantV[i], got[i])) ++differing;
    }
    checks += wantV.size();
    ++cases;
    if (differing != 0) ++failures;
    std::printf("  %s vanLeerLimiter        n=%-6zu differing=%zu (includes NaN/nullopt, +0, -0)\n",
                differing == 0 ? "PASS" : "FAIL", wantV.size(), differing);
  }
  std::printf("  L1 total scalar comparisons: %zu\n\n", checks);
}

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------

std::vector<Vector3> gridVertices(Index n, Real length, const Vector3& offset) {
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      vertices.push_back(Vector3{(static_cast<Real>(i) / static_cast<Real>(n) * length) + offset.x,
                                 (static_cast<Real>(j) / static_cast<Real>(n) * length) + offset.y,
                                 0.0});
    }
  }
  return vertices;
}

Mesh q16() {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      vertices.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                                 y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(16, 16, vertices);
}

Mesh sheared(Real amplitude) {
  const Index n = 16;
  std::vector<Vector3> vertices = gridVertices(n, 1.0, Vector3{});
  for (Index j = 1; j < n; ++j) {
    for (Index i = 1; i < n; ++i) {
      vertices[(j * (n + 1)) + i].x +=
          amplitude * (1.0 / static_cast<Real>(n)) * ((j % 2 == 0) ? 1.0 : -1.0);
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

Mesh warped3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(
      mesh, std::make_shared<cfd::mesh::SinusoidalMotion>(Vector3{0, 0, 0}, Vector3{1, 1, 1},
                                                          Vector3{0.05, 0.025, -0.0375},
                                                          cfd::constants::twoPi / 0.4));
  (void)motion.advance(0.1);
  return mesh;
}

// ---------------------------------------------------------------------------
// Fields, fluxes, boundary conditions
// ---------------------------------------------------------------------------

struct FieldCase {
  const char* name;
  Real (*value)(const Vector3&);
};
Real constantField(const Vector3&) { return 3.25; }
Real linearField(const Vector3& x) { return (2.0 * x.x) - (1.5 * x.y) + (0.75 * x.z) + 0.5; }
Real stepField(const Vector3& x) { return x.x < 0.5 ? 1.0 : 2.0; }
Real manufacturedField(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return std::sin(pi * x.x) * std::cos(pi * x.y) * (1.0 + (0.3 * x.z));
}

struct FluxCase {
  const char* name;
  // Owner-oriented flux for a face, given its id, centroid and area vector.
  Real (*value)(Index, const Vector3&, const Vector3&);
};
Real fluxPositive(Index, const Vector3&, const Vector3&) { return 0.7; }
Real fluxNegative(Index, const Vector3&, const Vector3&) { return -0.7; }
Real fluxMixed(Index id, const Vector3&, const Vector3&) {
  return (id % 3 == 0) ? 0.45 : ((id % 3 == 1) ? -0.8 : 1.3);
}
// Includes exact +0.0 and -0.0 so the `Ff >= 0.0` predicate is exercised on
// both zeros -- both must select the OWNER.
Real fluxWithZeros(Index id, const Vector3&, const Vector3&) {
  if (id % 5 == 0) return 0.0;
  if (id % 5 == 1) return -0.0;
  return (id % 2 == 0) ? 0.6 : -0.9;
}
// A physical-looking flux: u . Sf with a swirling velocity, so the sign varies
// with geometry rather than with the face index.
Real fluxSwirl(Index, const Vector3& centroid, const Vector3& area) {
  const Vector3 u{-(centroid.y - 0.5), centroid.x - 0.5, 0.1 * centroid.z};
  return dot(u, area);
}

SurfaceField buildFlux(const Mesh& mesh, const FluxCase& flux, Real sign) {
  SurfaceField field(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    field[f] = sign * flux.value(f, face.centroid(), face.areaVector());
  }
  return field;
}

BoundaryConditionSet dirichletSet(const Mesh& mesh) {
  BoundaryConditionSet set;
  Real v = 1.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    set.set(mesh, patch.name(), std::make_unique<FixedValue>(v));
    v += 0.5;
  }
  return set;
}

BoundaryConditionSet mixedSet(const Mesh& mesh) {
  BoundaryConditionSet set;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (i % 2 == 0) {
      set.set(mesh, patch.name(), std::make_unique<FixedValue>(1.0 + (0.5 * static_cast<Real>(i))));
    } else {
      set.set(mesh, patch.name(),
              std::make_unique<FixedGradient>(0.25 - (0.1 * static_cast<Real>(i))));
    }
    ++i;
  }
  return set;
}

// ---------------------------------------------------------------------------
// L2 -- the four-scheme operator
// ---------------------------------------------------------------------------

struct Coverage {
  int schemeCases[4] = {0, 0, 0, 0};
  int reversedPairs = 0;
  int reversalChangedResult = 0;
  int zeroFluxCases = 0;
  int patternMismatch = 0;
};
Coverage coverage;

const char* schemeName(Index s) {
  switch (s) {
    case cfd::gpu::kConvectionUpwind: return "upwind";
    case cfd::gpu::kConvectionCentral: return "central";
    case cfd::gpu::kConvectionLinearUpwind: return "linear_upwind";
    default: return "quick";
  }
}

// Returns the CPU result so the caller can compare a reversed-flux pair.
std::vector<Real> runOperatorCase(const std::string& meshName, const Mesh& mesh,
                                  const DeviceConvectionPlan& plan, const char* bcName,
                                  const BoundaryConditionSet& boundaries, const FieldCase& field,
                                  const FluxCase& flux, Real sign, Index scheme, bool print) {
  const Index nc = mesh.numberOfCells();
  ScalarField phi(nc);
  for (Index c = 0; c < nc; ++c) phi[c] = field.value(mesh.cell(c).centroid());
  const SurfaceField massFlux = buildFlux(mesh, flux, sign);

  const auto cpu = cfd::discretization::convection(mesh, phi, massFlux, boundaries,
                                                   static_cast<ConvectionScheme>(scheme));

  cfd::gpu::DeviceBuffer<Real> dPhi, dFlux, dResult;
  dPhi.uploadFrom(phi.data(), nc);
  dFlux.uploadFrom(massFlux.data(), static_cast<Index>(massFlux.size()));
  const std::uint64_t d2hBefore = cfd::gpu::gpuExecutionStats().deviceToHostCalls;
  cfd::gpu::convectionDevice(plan, dPhi, dFlux, scheme, dResult);
  const std::uint64_t d2hDuring = cfd::gpu::gpuExecutionStats().deviceToHostCalls - d2hBefore;

  std::vector<Real> gpu(nc);
  dResult.downloadTo(gpu.data(), dResult.size());

  std::size_t differing = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
  Index worst = 0;
  for (Index c = 0; c < nc; ++c) {
    if (!sameBits(cpu[c], gpu[c])) ++differing;
    const Real diff = std::abs(cpu[c] - gpu[c]);
    if (diff > maxAbs) { maxAbs = diff; worst = c; }
    const Real magnitude = std::abs(cpu[c]);
    scale = std::max(scale, magnitude);
    const Real rel = magnitude > 0.0 ? diff / magnitude : diff;
    maxRel = std::max(maxRel, rel);
  }
  ++cases;
  ++coverage.schemeCases[scheme];
  const bool ok = differing == 0 && d2hDuring == 0;
  if (!ok) ++failures;
  if (print) {
    std::printf("  %s %-16s %-9s %-13s %-9s %-4s differing=%-5zu maxAbs=%-10.3g maxRel=%-10.3g "
                "scale=%.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), bcName, schemeName(scheme), flux.name,
                sign > 0 ? "fwd" : "rev", differing, maxAbs, maxRel, scale);
    if (!ok) {
      std::printf("       worst cell %zu: cpu=%.17g gpu=%.17g\n", static_cast<std::size_t>(worst),
                  cpu[worst], gpu[worst]);
    }
  }
  std::vector<Real> cpuOut(nc);
  for (Index c = 0; c < nc; ++c) cpuOut[c] = cpu[c];
  return cpuOut;
}

// ---------------------------------------------------------------------------
// L3 -- the production scalar implicit assembly
// ---------------------------------------------------------------------------

void runAssemblyCase(const std::string& meshName, const Mesh& mesh,
                     const DeviceConvectionPlan& plan, const char* bcName,
                     const BoundaryConditionSet& boundaries, const FieldCase& field,
                     const FluxCase& flux, Real sign, bool cpField) {
  const Index nc = mesh.numberOfCells();
  ScalarField phi(nc), cp(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    phi[c] = field.value(x);
    cp[c] = 800.0 + (250.0 * x.x) + (90.0 * x.y * x.y) + (40.0 * x.z);
  }
  const Real cpScalar = 1005.0;
  const SurfaceField massFlux = buildFlux(mesh, flux, sign);

  SparseMatrixBuilder builder(nc, nc);
  cfd::algebra::Vector rhs(nc, 0.0);
  if (cpField) {
    cfd::thermal::assembleThermalConvectionContribution(mesh, cp, massFlux, phi, boundaries,
                                                        builder, rhs);
  } else {
    cfd::thermal::assembleThermalConvectionContribution(mesh, cpScalar, massFlux, phi, boundaries,
                                                        builder, rhs);
  }
  const auto cpuMatrix = builder.build();

  cfd::gpu::DeviceBuffer<Real> dPhi, dFlux, dCp;
  dPhi.uploadFrom(phi.data(), nc);
  dFlux.uploadFrom(massFlux.data(), static_cast<Index>(massFlux.size()));
  dCp.uploadFrom(cp.data(), nc);
  DeviceConvectionSystem system;
  cfd::gpu::assembleScalarConvectionDevice(plan, dPhi, dFlux, cpField ? &dCp : nullptr, cpScalar,
                                           system);

  std::vector<Index> gpuRowOffsets(system.rowOffsets.size()), gpuColumns(system.columnIndices.size());
  std::vector<Real> gpuValues(system.values.size()), gpuRhs(system.rhs.size());
  system.rowOffsets.downloadTo(gpuRowOffsets.data(), system.rowOffsets.size());
  if (!gpuColumns.empty()) system.columnIndices.downloadTo(gpuColumns.data(), system.columnIndices.size());
  if (!gpuValues.empty()) system.values.downloadTo(gpuValues.data(), system.values.size());
  system.rhs.downloadTo(gpuRhs.data(), system.rhs.size());

  // The CPU drops entries that accumulate to exactly 0.0. For convection that
  // DOES happen -- a face with zero flux contributes exactly zero -- so the
  // patterns are compared as CPU-subset-of-GPU rather than assumed equal.
  std::size_t differingValues = 0, differingRhs = 0, missingInGpu = 0, extraNonZero = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scaleM = 0.0, scaleR = 0.0;
  for (Index r = 0; r < nc; ++r) {
    // walk the GPU row (superset, ascending) alongside the CPU row (ascending)
    Index g = gpuRowOffsets[r];
    for (Index k = cpuMatrix.rowOffsetsData()[r]; k < cpuMatrix.rowOffsetsData()[r + 1]; ++k) {
      const Index column = cpuMatrix.columnIndicesData()[k];
      while (g < gpuRowOffsets[r + 1] && gpuColumns[g] < column) {
        if (gpuValues[g] != 0.0) ++extraNonZero;
        ++g;
      }
      if (g >= gpuRowOffsets[r + 1] || gpuColumns[g] != column) {
        ++missingInGpu;
        continue;
      }
      const Real want = cpuMatrix.valuesData()[k];
      const Real got = gpuValues[g];
      if (!sameBits(want, got)) ++differingValues;
      const Real diff = std::abs(want - got);
      maxAbs = std::max(maxAbs, diff);
      scaleM = std::max(scaleM, std::abs(want));
      if (std::abs(want) > 0.0) maxRel = std::max(maxRel, diff / std::abs(want));
      ++g;
    }
    for (; g < gpuRowOffsets[r + 1]; ++g) {
      if (gpuValues[g] != 0.0) ++extraNonZero;
    }
  }
  for (Index r = 0; r < nc; ++r) {
    if (!sameBits(rhs[r], gpuRhs[r])) ++differingRhs;
    maxAbs = std::max(maxAbs, std::abs(rhs[r] - gpuRhs[r]));
    scaleR = std::max(scaleR, std::abs(rhs[r]));
  }
  if (missingInGpu != 0 || extraNonZero != 0) ++coverage.patternMismatch;

  ++cases;
  const bool ok = differingValues == 0 && differingRhs == 0 && missingInGpu == 0 &&
                  extraNonZero == 0;
  if (!ok) ++failures;
  std::printf("  %s %-16s %-9s cp=%-8s %-9s %-4s nnz=%-7zu A[d=%zu] rhs[d=%zu] maxAbs=%-10.3g "
              "scaleA=%-9.4g scaleR=%-9.4g%s\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), bcName, cpField ? "field" : "scalar",
              flux.name, sign > 0 ? "fwd" : "rev", cpuMatrix.nonZeros(), differingValues,
              differingRhs, maxAbs, scaleM, scaleR,
              (missingInGpu != 0 || extraNonZero != 0) ? "  PATTERN MISMATCH" : "");
}

void runMesh(const std::string& name, const Mesh& mesh, bool verbose) {
  const std::vector<FieldCase> fields = {{"constant", constantField},
                                         {"linear", linearField},
                                         {"step", stepField},
                                         {"manufactured", manufacturedField}};
  const std::vector<FluxCase> fluxes = {{"positive", fluxPositive},
                                        {"negative", fluxNegative},
                                        {"mixed", fluxMixed},
                                        {"zeros", fluxWithZeros},
                                        {"swirl", fluxSwirl}};
  const BoundaryConditionSet dirichlet = dirichletSet(mesh);
  const BoundaryConditionSet mixed = mixedSet(mesh);

  DeviceConvectionPlan plan;
  if (!plan.build(mesh, dirichlet)) {
    ++failures;
    std::printf("  FAIL %-16s plan unusable: %s\n", name.c_str(), plan.unsupportedReason().c_str());
    return;
  }
  DeviceConvectionPlan planMixed;
  if (!planMixed.build(mesh, mixed)) {
    ++failures;
    std::printf("  FAIL %-16s mixed plan unusable: %s\n", name.c_str(),
                planMixed.unsupportedReason().c_str());
    return;
  }
  std::printf("       [%s cells=%zu farUpstream(ownerUpwind=%zu, neighbourUpwind=%zu) "
              "resident=%zu B]\n",
              name.c_str(), mesh.numberOfCells(),
              static_cast<std::size_t>(plan.farUpstreamOwnerUpwind()),
              static_cast<std::size_t>(plan.farUpstreamNeighborUpwind()), plan.residentBytes());

  for (const auto& field : fields) {
    for (const auto& flux : fluxes) {
      for (const Index scheme : {cfd::gpu::kConvectionUpwind, cfd::gpu::kConvectionCentral,
                                 cfd::gpu::kConvectionLinearUpwind, cfd::gpu::kConvectionQUICK}) {
        // Forward and reversed flux, so a wrong owner/neighbour choice cannot
        // pass by symmetry.
        const auto forward = runOperatorCase(name, mesh, plan, "dirichlet", dirichlet, field, flux,
                                             1.0, scheme, verbose);
        const auto reversed = runOperatorCase(name, mesh, plan, "dirichlet", dirichlet, field, flux,
                                              -1.0, scheme, verbose);
        ++coverage.reversedPairs;
        bool changed = false;
        for (std::size_t c = 0; c < forward.size() && !changed; ++c) {
          if (!sameBits(forward[c], reversed[c])) changed = true;
        }
        if (changed) ++coverage.reversalChangedResult;
        if (std::string(flux.name) == "zeros") ++coverage.zeroFluxCases;
      }
      runOperatorCase(name, mesh, planMixed, "mixed", mixed, field, flux, 1.0,
                      cfd::gpu::kConvectionQUICK, verbose);
    }
    for (const auto& flux : fluxes) {
      runAssemblyCase(name, mesh, plan, "dirichlet", dirichlet, field, flux, 1.0, false);
      runAssemblyCase(name, mesh, plan, "dirichlet", dirichlet, field, flux, -1.0, true);
      runAssemblyCase(name, mesh, planMixed, "mixed", mixed, field, flux, 1.0, true);
    }
  }
}

void proveNonVacuous() {
  std::printf("\n=== non-vacuity control ===\n");
  const Real reference = 12.5;
  const Real perturbed = std::nextafter(reference, std::numeric_limits<Real>::infinity());
  const bool ok = sameBits(reference, reference) && !sameBits(reference, perturbed);
  if (!ok) ++failures;
  std::printf("  %s identical bits compare equal; a one-ULP perturbation does not (abs=%.3g)\n",
              ok ? "PASS" : "FAIL", std::abs(reference - perturbed));
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001D: CUDA convection vs CPU reference (bitwise) ===\n");
  std::printf("L1 primitives | L2 four schemes vs discretization::convection()\n");
  std::printf("L3 scalar implicit assembly vs thermal::assembleThermalConvectionContribution\n\n");

  runPrimitives();

  std::printf("=== L2/L3: operators and assembly ===\n");
  if (quick) {
    runMesh("distorted q16", q16(), false);
    runMesh("warped 3d 5", warped3D(5), false);
  } else {
    runMesh("cartesian2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), true);
    runMesh("cartesian2d 32", MeshGeometry::createCartesian2D(32, 32, 1.0, 1.0), false);
    runMesh("graded2d 16",
            MeshGeometry::createGraded2D(
                16, 16, 1.0, 1.0,
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.15,
                                       cfd::mesh::GradingCluster::Start},
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.08,
                                       cfd::mesh::GradingCluster::Both}),
            false);
    runMesh("distorted q16", q16(), false);
    runMesh("sheared 0.35", sheared(0.35), false);
    runMesh("cartesian3d 6", MeshGeometry::createCartesian3D(6, 6, 6, 1.0, 1.0, 1.0), false);
    runMesh("warped 3d 5", warped3D(5), false);
  }

  proveNonVacuous();

  std::printf("\n=== coverage ===\n");
  for (const Index s : {cfd::gpu::kConvectionUpwind, cfd::gpu::kConvectionCentral,
                        cfd::gpu::kConvectionLinearUpwind, cfd::gpu::kConvectionQUICK}) {
    std::printf("  scheme %-14s %d operator cases\n", schemeName(s), coverage.schemeCases[s]);
  }
  std::printf("  reversed-flux pairs                %d\n", coverage.reversedPairs);
  std::printf("  reversal actually changed result   %d\n", coverage.reversalChangedResult);
  std::printf("  zero/negative-zero flux cases      %d\n", coverage.zeroFluxCases);
  std::printf("  assembly pattern mismatches        %d\n", coverage.patternMismatch);
  for (const Index s : {cfd::gpu::kConvectionUpwind, cfd::gpu::kConvectionCentral,
                        cfd::gpu::kConvectionLinearUpwind, cfd::gpu::kConvectionQUICK}) {
    if (coverage.schemeCases[s] == 0) {
      ++failures;
      std::printf("  FAIL scheme %s was never exercised\n", schemeName(s));
    }
  }
  if (coverage.reversalChangedResult == 0) {
    ++failures;
    std::printf("  FAIL reversing the flux never changed the result -- the direction tests are "
                "vacuous\n");
  }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "CONVECTION EQUIVALENCE: PASS (bitwise)"
                                    : "CONVECTION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
