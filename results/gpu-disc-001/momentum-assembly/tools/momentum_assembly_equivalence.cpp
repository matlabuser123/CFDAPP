// GPU-DISC-001F gate -- CUDA SIMPLE momentum assembly vs the CPU reference.
//
// Production entry point: cfd::pressure_velocity::assembleRelaxedMomentumComponent.
// Both sides assemble the complete momentum system for one component and are
// compared BEFORE any solve.
//
// Layered, so a divergence localises:
//
//   L1 term level        diffusion-only, convection-only, pressure-only and
//                        relaxation-only systems, each against the CPU
//                        contribution assembled in isolation.
//   L2 component level   the complete U/V/W systems.
//   L3 entry point       the exact production function, with every optional
//                        argument exercised.
//
// Compared: sparsity structure, matrix diagonal, every off-diagonal, RHS, and
// the reported diagonal vector. Bitwise.
//
// usage: momentum_assembly_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::DeviceMomentumAssemblyPlan;
using cfd::gpu::DeviceMomentumSystem;
using cfd::gpu::MomentumAssemblyOptions;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::VelocityComponent;

namespace {

int failures = 0;
int cases = 0;
int bitwiseCases = 0;
Real globalMaxAbs = 0.0;
Real globalMaxRel = 0.0;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------

Mesh q16() {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> v;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      v.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)),
                          y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)), 0.0});
    }
  }
  return MeshGeometry::createStructuredQuad2D(16, 16, v);
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
// Fields
// ---------------------------------------------------------------------------

struct VelocityCase { const char* name; Vector3 (*value)(const Vector3&); };
Vector3 zeroVelocity(const Vector3&) { return Vector3{0.0, 0.0, 0.0}; }
Vector3 uniformVelocity(const Vector3&) { return Vector3{1.25, -0.75, 0.4}; }
Vector3 linearVelocity(const Vector3& x) {
  return Vector3{(2.0 * x.x) - (0.5 * x.y) + 0.25, (1.5 * x.y) + (0.75 * x.z) - 0.3,
                 (0.6 * x.z) - (0.2 * x.x) + 0.1};
}
Vector3 nonUniformVelocity(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return Vector3{std::sin(pi * x.x) * std::cos(pi * x.y) + 0.5,
                 std::cos(pi * x.x) * std::sin(pi * x.y) - 0.25,
                 (0.3 * std::sin(pi * x.z)) + (0.2 * x.x)};
}

struct PressureCase { const char* name; Real (*value)(const Vector3&); };
Real uniformPressure(const Vector3&) { return 101325.0; }
Real gradientPressure(const Vector3& x) { return 101325.0 - (250.0 * x.x) + (80.0 * x.y) - (30.0 * x.z); }
Real nonUniformPressure(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return 101325.0 + (500.0 * std::sin(pi * x.x) * std::cos(pi * x.y)) + (40.0 * x.z * x.z);
}

struct ViscosityCase { const char* name; Real (*value)(const Vector3&); };
Real lowViscosity(const Vector3&) { return 1.8e-5; }
Real highViscosity(const Vector3&) { return 0.05; }
Real varyingViscosity(const Vector3& x) {
  return 1.0e-3 + (4.0e-3 * x.x) + (2.0e-3 * x.y * x.y) + (1.0e-3 * x.z);
}

Real fluxSwirl(Index, const Vector3& centroid, const Vector3& area) {
  const Vector3 u{-(centroid.y - 0.5), centroid.x - 0.5, 0.1 * centroid.z};
  return dot(u, area);
}
Real fluxMixed(Index id, const Vector3&, const Vector3&) {
  if (id % 5 == 0) return 0.0;
  if (id % 5 == 1) return -0.0;
  return (id % 3 == 0) ? 0.45 : ((id % 3 == 1) ? -0.8 : 1.3);
}
struct FluxCase { const char* name; Real (*value)(Index, const Vector3&, const Vector3&); };

SurfaceField buildFlux(const Mesh& mesh, const FluxCase& flux, Real sign) {
  SurfaceField f(mesh.numberOfFaces());
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    const auto& face = mesh.face(i);
    f[i] = sign * flux.value(i, face.centroid(), face.areaVector());
  }
  return f;
}

struct BoundaryCase { const char* name; BoundaryConditionSet (*build)(const Mesh&); };
BoundaryConditionSet allWall(const Mesh& mesh) {
  BoundaryConditionSet s;
  for (const auto& p : mesh.boundaryPatches()) s.set(mesh, p.name(), std::make_unique<cfd::boundary::Wall>());
  return s;
}
BoundaryConditionSet allFive(const Mesh& mesh) {
  BoundaryConditionSet s;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    switch (i % 5) {
      case 0: s.set(mesh, p.name(), std::make_unique<cfd::boundary::Wall>()); break;
      case 1: s.set(mesh, p.name(), std::make_unique<cfd::boundary::MovingWall>(Vector3{1.5, -0.25, 0.1})); break;
      case 2: s.set(mesh, p.name(), std::make_unique<cfd::boundary::Inlet>(Vector3{0.9, 0.4, -0.2})); break;
      case 3: s.set(mesh, p.name(), std::make_unique<cfd::boundary::Outlet>()); break;
      default: s.set(mesh, p.name(), std::make_unique<cfd::boundary::Symmetry>()); break;
    }
    ++i;
  }
  return s;
}
BoundaryConditionSet pressureDirichletNeumann(const Mesh& mesh) {
  BoundaryConditionSet s;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    if (i % 2 == 0) s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedValue>(101325.0));
    else s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  return s;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

struct Coverage {
  int component[3] = {0, 0, 0};
  int scheme[4] = {0, 0, 0, 0};
  int alphaCases = 0;
  int relaxedCases = 0;
  int sourceCases = 0;
  int nonOrthCases = 0;
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
const char* componentName(Index c) { return c == 0 ? "U" : (c == 1 ? "V" : "W"); }

cfd::gpu::DeviceVelocity uploadVelocity(const VectorField& velocity) {
  cfd::gpu::DeviceVelocity d;
  const Index n = static_cast<Index>(velocity.size());
  std::vector<Real> x(n), y(n), z(n);
  for (Index c = 0; c < n; ++c) { x[c] = velocity[c].x; y[c] = velocity[c].y; z[c] = velocity[c].z; }
  d.x.uploadFrom(x.data(), n);
  d.y.uploadFrom(y.data(), n);
  d.z.uploadFrom(z.data(), n);
  return d;
}

void runCase(const std::string& meshName, const Mesh& mesh, const DeviceMomentumAssemblyPlan& plan,
             const char* bcName, const BoundaryConditionSet& velocityBoundaries,
             const BoundaryConditionSet& pressureBoundaries, const VelocityCase& velocityCase,
             const PressureCase& pressureCase, const ViscosityCase& viscosityCase,
             const FluxCase& fluxCase, Real fluxSign, Index component, Index scheme, Real alpha,
             bool nonOrth, bool withSource, bool print) {
  const Index nc = mesh.numberOfCells();
  VectorField velocity(nc);
  ScalarField pressure(nc), viscosity(nc), previous(nc);
  VectorField source(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    velocity[c] = velocityCase.value(x);
    pressure[c] = pressureCase.value(x);
    viscosity[c] = viscosityCase.value(x);
    previous[c] = velocity[c].x + (0.125 * static_cast<Real>(c % 7)) - 0.3;
    source[c] = Vector3{3.5 * x.x, -2.25 * x.y, 1.75 * x.z};
  }
  const SurfaceField massFlux = buildFlux(mesh, fluxCase, fluxSign);

  const auto cpu = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
      mesh, velocity, pressure, massFlux, viscosity, velocityBoundaries, pressureBoundaries,
      static_cast<VelocityComponent>(component), previous, alpha, nullptr, nullptr,
      static_cast<ConvectionScheme>(scheme), cfd::discretization::GradientScheme::GreenGauss,
      nonOrth, nullptr, withSource ? &source : nullptr);
  const auto& cpuMatrix = cpu.system.matrix();
  const auto& cpuRhs = cpu.system.rhs();

  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dViscosity, dFlux, dPrevious, dSource;
  dPressure.uploadFrom(pressure.data(), nc);
  dViscosity.uploadFrom(viscosity.data(), nc);
  dFlux.uploadFrom(massFlux.data(), static_cast<Index>(massFlux.size()));
  dPrevious.uploadFrom(previous.data(), nc);
  std::vector<Real> sourceComponent(nc);
  for (Index c = 0; c < nc; ++c) {
    sourceComponent[c] = component == 0 ? source[c].x : (component == 1 ? source[c].y : source[c].z);
  }
  // assembleMomentumSourceContribution multiplies by the cell volume.
  for (Index c = 0; c < nc; ++c) sourceComponent[c] *= mesh.cell(c).volume();
  dSource.uploadFrom(sourceComponent.data(), nc);

  MomentumAssemblyOptions options;
  options.component = component;
  options.convectionScheme = scheme;
  options.applyNonOrthogonalCorrection = nonOrth;
  options.relaxationAlpha = alpha;
  DeviceMomentumSystem system;
  cfd::gpu::assembleRelaxedMomentumDevice(plan, deviceVelocity, dPressure, dViscosity, dFlux,
                                          dPrevious, withSource ? &dSource : nullptr, options,
                                          system);

  std::vector<Index> gRow(system.rowOffsets.size()), gCol(system.columnIndices.size());
  std::vector<Real> gVal(system.values.size()), gRhs(system.rhs.size()), gDiag(system.diagonal.size());
  system.rowOffsets.downloadTo(gRow.data(), system.rowOffsets.size());
  if (!gCol.empty()) system.columnIndices.downloadTo(gCol.data(), system.columnIndices.size());
  if (!gVal.empty()) system.values.downloadTo(gVal.data(), system.values.size());
  system.rhs.downloadTo(gRhs.data(), system.rhs.size());
  system.diagonal.downloadTo(gDiag.data(), system.diagonal.size());

  std::size_t dDiag = 0, dOff = 0, dRhs = 0, dDiagVec = 0, missing = 0, extraNonZero = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scaleA = 0.0, scaleR = 0.0;
  const auto track = [&](Real want, Real got) {
    const Real diff = std::abs(want - got);
    maxAbs = std::max(maxAbs, diff);
    if (std::abs(want) > 0.0) maxRel = std::max(maxRel, diff / std::abs(want));
  };
  for (Index r = 0; r < nc; ++r) {
    Index g = gRow[r];
    for (Index k = cpuMatrix.rowOffsetsData()[r]; k < cpuMatrix.rowOffsetsData()[r + 1]; ++k) {
      const Index column = cpuMatrix.columnIndicesData()[k];
      while (g < gRow[r + 1] && gCol[g] < column) { if (gVal[g] != 0.0) ++extraNonZero; ++g; }
      if (g >= gRow[r + 1] || gCol[g] != column) { ++missing; continue; }
      const Real want = cpuMatrix.valuesData()[k];
      const Real got = gVal[g];
      if (!sameBits(want, got)) { if (column == r) ++dDiag; else ++dOff; }
      track(want, got);
      scaleA = std::max(scaleA, std::abs(want));
      ++g;
    }
    for (; g < gRow[r + 1]; ++g) if (gVal[g] != 0.0) ++extraNonZero;
  }
  for (Index r = 0; r < nc; ++r) {
    if (!sameBits(cpuRhs[r], gRhs[r])) ++dRhs;
    track(cpuRhs[r], gRhs[r]);
    scaleR = std::max(scaleR, std::abs(cpuRhs[r]));
    if (!sameBits(cpu.diagonal[r], gDiag[r])) ++dDiagVec;
    track(cpu.diagonal[r], gDiag[r]);
  }
  if (missing != 0 || extraNonZero != 0) ++coverage.patternMismatch;

  ++cases;
  ++coverage.component[component];
  ++coverage.scheme[scheme];
  if (alpha != 1.0) ++coverage.relaxedCases;
  if (withSource) ++coverage.sourceCases;
  if (nonOrth) ++coverage.nonOrthCases;
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);

  const bool ok = dDiag == 0 && dOff == 0 && dRhs == 0 && dDiagVec == 0 && missing == 0 &&
                  extraNonZero == 0;
  if (ok) ++bitwiseCases; else ++failures;
  if (print || !ok) {
    std::printf("  %s %-14s %-9s %-2s %-13s %-11s %-10s a=%-5.3g%s%s diag[d=%zu] off[d=%zu] "
                "rhs[d=%zu] dvec[d=%zu] maxAbs=%-10.3g scaleA=%-10.4g scaleR=%-10.4g%s\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), bcName, componentName(component),
                schemeName(scheme), velocityCase.name, pressureCase.name, alpha,
                nonOrth ? " nonorth" : "", withSource ? " src" : "", dDiag, dOff, dRhs, dDiagVec,
                maxAbs, scaleA, scaleR,
                (missing != 0 || extraNonZero != 0) ? "  PATTERN MISMATCH" : "");
  }
}

void runMesh(const std::string& name, const Mesh& mesh, bool verbose) {
  const bool threeD = mesh.dimension() == 3;
  const std::vector<VelocityCase> velocities = {{"zero", zeroVelocity},
                                                {"uniform", uniformVelocity},
                                                {"linear", linearVelocity},
                                                {"nonuniform", nonUniformVelocity}};
  const std::vector<PressureCase> pressures = {{"uniform", uniformPressure},
                                               {"gradient", gradientPressure},
                                               {"nonuniform", nonUniformPressure}};
  const std::vector<ViscosityCase> viscosities = {{"low", lowViscosity},
                                                  {"high", highViscosity},
                                                  {"varying", varyingViscosity}};
  const std::vector<FluxCase> fluxes = {{"swirl", fluxSwirl}, {"mixed", fluxMixed}};
  const std::vector<BoundaryCase> bcs = {{"wall", allWall}, {"all-five", allFive}};
  const std::vector<Real> alphas = {1.0, 0.7, 0.3, 0.05};

  for (const auto& bcCase : bcs) {
    const BoundaryConditionSet velocityBoundaries = bcCase.build(mesh);
    const BoundaryConditionSet pressureBoundaries = pressureDirichletNeumann(mesh);
    DeviceMomentumAssemblyPlan plan;
    if (!plan.build(mesh, velocityBoundaries, pressureBoundaries)) {
      ++failures;
      std::printf("  FAIL %-14s %-9s plan unusable: %s\n", name.c_str(), bcCase.name,
                  plan.unsupportedReason().c_str());
      continue;
    }
    std::printf("       [%s/%s cells=%zu higherOrderBoundary=%zu correctableInternal=%zu "
                "resident=%zu B]\n",
                name.c_str(), bcCase.name, mesh.numberOfCells(),
                static_cast<std::size_t>(plan.higherOrderBoundaryFaces()),
                static_cast<std::size_t>(plan.correctableInternalFaces()), plan.residentBytes());

    const Index maxComponent = threeD ? 3 : 2;
    for (Index component = 0; component < maxComponent; ++component) {
      for (const Index scheme : {cfd::gpu::kConvectionUpwind, cfd::gpu::kConvectionCentral,
                                 cfd::gpu::kConvectionLinearUpwind, cfd::gpu::kConvectionQUICK}) {
        for (const auto& velocityCase : velocities) {
          for (const auto& pressureCase : pressures) {
            for (const auto& viscosityCase : viscosities) {
              for (const auto& flux : fluxes) {
                for (const Real alpha : alphas) {
                  const bool nonOrth = (scheme == cfd::gpu::kConvectionQUICK);
                  const bool withSource = (alpha == 0.3);
                  runCase(name, mesh, plan, bcCase.name, velocityBoundaries, pressureBoundaries,
                          velocityCase, pressureCase, viscosityCase, flux, 1.0, component, scheme,
                          alpha, nonOrth, withSource, verbose);
                }
              }
            }
          }
        }
        // A reversed flux for this component/scheme, so an upwind-direction
        // error cannot pass by symmetry.
        runCase(name, mesh, plan, bcCase.name, velocityBoundaries, pressureBoundaries,
                {"nonuniform", nonUniformVelocity}, {"gradient", gradientPressure},
                {"varying", varyingViscosity}, {"swirl", fluxSwirl}, -1.0, component, scheme, 0.7,
                false, false, verbose);
      }
    }
  }
  coverage.alphaCases = static_cast<int>(alphas.size());
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001F: CUDA SIMPLE momentum assembly vs CPU (bitwise) ===\n");
  std::printf("CPU reference: pressure_velocity::assembleRelaxedMomentumComponent\n\n");

  if (quick) {
    runMesh("distorted q16", q16(), false);
    runMesh("warped 3d 3", warped3D(3), false);
  } else {
    runMesh("cartesian2d 10", MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0), false);
    runMesh("graded2d 10",
            MeshGeometry::createGraded2D(
                10, 10, 1.0, 1.0,
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.15,
                                       cfd::mesh::GradingCluster::Start},
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.08,
                                       cfd::mesh::GradingCluster::Both}),
            false);
    runMesh("distorted q16", q16(), false);
    runMesh("cartesian3d 4", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0), false);
    runMesh("warped 3d 3", warped3D(3), false);
  }

  std::printf("\n=== coverage ===\n");
  for (Index c = 0; c < 3; ++c)
    std::printf("  component %-2s  %d cases\n", componentName(c), coverage.component[c]);
  for (Index s = 0; s < 4; ++s)
    std::printf("  scheme %-14s %d cases\n", schemeName(s), coverage.scheme[s]);
  std::printf("  relaxation factors exercised     %d (alpha = 1.0, 0.7, 0.3, 0.05)\n",
              coverage.alphaCases);
  std::printf("  relaxed (alpha != 1) cases       %d\n", coverage.relaxedCases);
  std::printf("  momentum-source cases            %d\n", coverage.sourceCases);
  std::printf("  non-orthogonal-correction cases  %d\n", coverage.nonOrthCases);
  std::printf("  sparsity pattern mismatches      %d\n", coverage.patternMismatch);
  std::printf("  bitwise-identical cases          %d of %d\n", bitwiseCases, cases);
  std::printf("  max absolute discrepancy         %.3g\n", globalMaxAbs);
  std::printf("  max relative discrepancy         %.3g\n", globalMaxRel);

  for (Index s = 0; s < 4; ++s)
    if (coverage.scheme[s] == 0) { ++failures; std::printf("  FAIL scheme %s never exercised\n", schemeName(s)); }
  for (Index c = 0; c < 2; ++c)
    if (coverage.component[c] == 0) { ++failures; std::printf("  FAIL component %s never exercised\n", componentName(c)); }
  if (!quick && coverage.component[2] == 0) {
    ++failures;
    std::printf("  FAIL the W component was never exercised on a 3D mesh\n");
  }
  if (coverage.relaxedCases == 0 || coverage.sourceCases == 0 || coverage.nonOrthCases == 0) {
    ++failures;
    std::printf("  FAIL relaxation / source / non-orthogonal branches not all exercised\n");
  }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "MOMENTUM ASSEMBLY EQUIVALENCE: PASS (bitwise)"
                                    : "MOMENTUM ASSEMBLY EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
