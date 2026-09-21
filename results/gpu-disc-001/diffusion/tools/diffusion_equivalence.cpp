// GPU-DISC-001C gate -- CUDA implicit diffusion assembly vs the CPU reference.
//
// The CPU reference is turbulence::assembleScalarDiffusionContribution, the
// production assembly over the shared NonOrthogonalDiffusion face terms. Both
// paths assemble into a zero-initialised matrix and RHS, so what is compared is
// the diffusion contribution itself.
//
// The requirement is BITWISE equality of every stored coefficient and every RHS
// entry, AND exact agreement of the sparsity pattern. Error magnitudes are still
// reported for every case, because "equal" with no magnitudes attached is not
// evidence: a failure needs to say how far off it is, and a pass needs to show
// the comparison ran over real, non-trivial numbers.
//
// usage: diffusion_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/gpu/DeviceDiffusion.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/mesh/MultiBlockSpec.hpp"
#include "cfd/turbulence/KEpsilonEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::SparseMatrixBuilder;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::gpu::DeviceDiffusionPlan;
using cfd::gpu::DeviceDiffusionSystem;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

int failures = 0;
int cases = 0;

// ---------------------------------------------------------------------------
// Meshes -- the same families 001B used, so geometry coverage is comparable.
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

Mesh q16(const Vector3& offset) {
  const Real pi = cfd::constants::pi;
  std::vector<Vector3> vertices;
  for (Index j = 0; j <= 16; ++j) {
    for (Index i = 0; i <= 16; ++i) {
      const Real x = static_cast<Real>(i) / 16.0;
      const Real y = static_cast<Real>(j) / 16.0;
      vertices.push_back(Vector3{x + (0.03 * std::sin(pi * x) * std::sin(2.0 * pi * y)) + offset.x,
                                 y + (0.03 * std::sin(2.0 * pi * x) * std::sin(pi * y)) + offset.y,
                                 0.0});
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

class PlanarSkewMotion final : public cfd::mesh::PrescribedMotion {
 public:
  [[nodiscard]] Vector3 position(const Vector3& x, Real elapsed) const override {
    const Real pi = cfd::constants::pi;
    const Real y = x.y + (0.03 * std::sin(pi * x.y));
    const Vector3 target{x.x + (0.03 * std::sin(pi * x.x)) +
                             (0.03 * std::sin((2.0 * pi * x.y) + 0.3) * std::sin(pi * x.x)),
                         y, x.z + (0.3 * y)};
    return x + ((target - x) * elapsed);
  }
  [[nodiscard]] std::string description() const override { return "planar skew"; }
};

Mesh planarSkew3D(Index n) {
  Mesh mesh = MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0);
  cfd::mesh::MeshMotion motion(mesh, std::make_shared<PlanarSkewMotion>());
  (void)motion.advance(1.0);
  return mesh;
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

Mesh multiBlockL() {
  const auto block = [](Index nx, Index ny, Real x0, Real y0, Real h) {
    std::vector<Vector3> v;
    for (Index j = 0; j <= ny; ++j) {
      for (Index i = 0; i <= nx; ++i) {
        v.push_back(Vector3{x0 + (static_cast<Real>(i) * h), y0 + (static_cast<Real>(j) * h), 0.0});
      }
    }
    return v;
  };
  using cfd::mesh::BlockSide;
  cfd::mesh::MultiBlockSpec spec;
  spec.blocks.push_back({"a1", 4, 4, block(4, 4, 0.0, 0.0, 0.125)});
  spec.blocks.push_back({"a2", 4, 4, block(4, 4, 0.5, 0.0, 0.125)});
  spec.blocks.push_back({"b", 4, 4, block(4, 4, 0.0, 0.5, 0.125)});
  spec.interfaces.push_back({{0, BlockSide::Right}, {1, BlockSide::Left}, false});
  spec.interfaces.push_back({{0, BlockSide::Top}, {2, BlockSide::Bottom}, false});
  spec.patches.push_back({"west", {{0, BlockSide::Left}, {2, BlockSide::Left}}});
  spec.patches.push_back({"south", {{0, BlockSide::Bottom}, {1, BlockSide::Bottom}}});
  spec.patches.push_back({"east", {{1, BlockSide::Right}, {2, BlockSide::Right}}});
  spec.patches.push_back({"north", {{1, BlockSide::Top}, {2, BlockSide::Top}}});
  return MeshGeometry::createMultiBlock2D(spec);
}

// ---------------------------------------------------------------------------
// Fields, diffusivities, boundary conditions
// ---------------------------------------------------------------------------

struct FieldCase {
  const char* name;
  Real (*value)(const Vector3&);
};

Real constantField(const Vector3&) { return 3.25; }
Real linearField(const Vector3& x) { return (2.0 * x.x) - (1.5 * x.y) + (0.75 * x.z) + 0.5; }
Real quadraticField(const Vector3& x) {
  return (x.x * x.x) + (2.0 * x.y * x.y) - (0.5 * x.x * x.y) + (0.25 * x.z * x.z);
}
Real manufacturedField(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return std::sin(pi * x.x) * std::cos(pi * x.y) * (1.0 + (0.3 * x.z));
}

struct GammaCase {
  const char* name;
  Real (*value)(const Vector3&);
};

// The assembler rejects a non-positive or non-finite diffusivity, so both of
// these stay strictly positive everywhere.
Real uniformGamma(const Vector3&) { return 0.7; }
Real varyingGamma(const Vector3& x) {
  return 0.25 + (0.5 * x.x) + (0.3 * x.y * x.y) + (0.2 * x.z) +
         (0.1 * std::sin(cfd::constants::pi * x.x));
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

BoundaryConditionSet neumannSet(const Mesh& mesh) {
  BoundaryConditionSet set;
  Real g = 0.25;
  for (const auto& patch : mesh.boundaryPatches()) {
    set.set(mesh, patch.name(), std::make_unique<FixedGradient>(g));
    g -= 0.1;
  }
  return set;
}

// Both boundary branches on one mesh: prescribed-value patches take the
// P12-DIFF-002 reconstruction, gradient patches never do.
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
// Comparison
// ---------------------------------------------------------------------------

struct Metrics {
  std::size_t differing = 0;
  Real maxAbs = 0.0;
  Real maxRel = 0.0;
  Real scale = 0.0;  // largest |reference| seen, so a pass shows non-trivial numbers
  Index worstRow = 0;
  Index worstColumn = 0;
  Real worstCpu = 0.0;
  Real worstGpu = 0.0;
};

void accumulate(Metrics& m, Real cpu, Real gpu, Index row, Index column) {
  if (std::memcmp(&cpu, &gpu, sizeof(Real)) != 0) ++m.differing;
  const Real diff = std::abs(cpu - gpu);
  const Real magnitude = std::abs(cpu);
  m.scale = std::max(m.scale, magnitude);
  if (diff > m.maxAbs) {
    m.maxAbs = diff;
    m.worstRow = row;
    m.worstColumn = column;
    m.worstCpu = cpu;
    m.worstGpu = gpu;
  }
  const Real rel = magnitude > 0.0 ? diff / magnitude : diff;
  if (rel > m.maxRel) m.maxRel = rel;
}

std::vector<Real> downloadReal(const cfd::gpu::DeviceBuffer<Real>& buffer) {
  std::vector<Real> host(static_cast<std::size_t>(buffer.size()));
  if (!host.empty()) buffer.downloadTo(host.data(), buffer.size());
  return host;
}

std::vector<Index> downloadIndex(const cfd::gpu::DeviceBuffer<Index>& buffer) {
  std::vector<Index> host(static_cast<std::size_t>(buffer.size()));
  if (!host.empty()) buffer.downloadTo(host.data(), buffer.size());
  return host;
}

struct Coverage {
  int withHigherOrderBoundary = 0;
  int withCorrectableInternal = 0;
  int withCorrectionOn = 0;
  int withVaryingGamma = 0;
  int patternMismatch = 0;
};
Coverage coverage;

void runCase(const std::string& meshName, const Mesh& mesh, const char* bcName,
             const BoundaryConditionSet& boundaries, const FieldCase& field,
             const GammaCase& gamma, bool correction) {
  const Index nc = mesh.numberOfCells();
  ScalarField phi(nc), diffusivity(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    phi[c] = field.value(x);
    diffusivity[c] = gamma.value(x);
  }

  // --- CPU reference -------------------------------------------------------
  cfd::discretization::NonOrthogonalCorrectionOptions options;
  options.enabled = correction;
  SparseMatrixBuilder builder(nc, nc);
  cfd::algebra::Vector rhs(nc, 0.0);
  cfd::turbulence::assembleScalarDiffusionContribution(mesh, diffusivity, phi, boundaries, builder,
                                                       rhs, options);
  const auto cpuMatrix = builder.build();

  // --- GPU -----------------------------------------------------------------
  DeviceDiffusionPlan plan;
  if (!plan.build(mesh, boundaries)) {
    ++cases;
    ++failures;
    std::printf("  FAIL %-18s %-9s %-13s %-8s corr=%-3s plan unusable: %s\n", meshName.c_str(),
                bcName, field.name, gamma.name, correction ? "on" : "off",
                plan.unsupportedReason().c_str());
    return;
  }
  cfd::gpu::DeviceBuffer<Real> devicePhi, deviceGamma;
  devicePhi.uploadFrom(phi.data(), nc);
  deviceGamma.uploadFrom(diffusivity.data(), nc);
  DeviceDiffusionSystem system;
  const std::uint64_t d2hBefore = cfd::gpu::gpuExecutionStats().deviceToHostCalls;
  cfd::gpu::assembleScalarDiffusionDevice(plan, devicePhi, deviceGamma, correction, system);
  const std::uint64_t d2hDuring = cfd::gpu::gpuExecutionStats().deviceToHostCalls - d2hBefore;

  const auto gpuRowOffsets = downloadIndex(system.rowOffsets);
  const auto gpuColumns = downloadIndex(system.columnIndices);
  const auto gpuValues = downloadReal(system.values);
  const auto gpuRhs = downloadReal(system.rhs);

  // --- pattern -------------------------------------------------------------
  // The CPU drops an entry whose accumulated value is exactly 0.0, so its
  // pattern is value-dependent. For diffusion it should never drop one; that is
  // checked here rather than assumed.
  bool patternEqual = (gpuRowOffsets.size() == static_cast<std::size_t>(nc) + 1) &&
                      (cpuMatrix.nonZeros() == gpuValues.size());
  if (patternEqual) {
    for (Index r = 0; r <= nc && patternEqual; ++r) {
      if (cpuMatrix.rowOffsetsData()[r] != gpuRowOffsets[r]) patternEqual = false;
    }
    for (std::size_t k = 0; k < gpuColumns.size() && patternEqual; ++k) {
      if (cpuMatrix.columnIndicesData()[k] != gpuColumns[k]) patternEqual = false;
    }
  }
  if (!patternEqual) ++coverage.patternMismatch;

  // --- values, split the way the brief asks --------------------------------
  Metrics diagonal, offDiagonal, rhsMetrics;
  if (patternEqual) {
    for (Index r = 0; r < nc; ++r) {
      for (Index k = cpuMatrix.rowOffsetsData()[r]; k < cpuMatrix.rowOffsetsData()[r + 1]; ++k) {
        const Index column = cpuMatrix.columnIndicesData()[k];
        Metrics& target = (column == r) ? diagonal : offDiagonal;
        accumulate(target, cpuMatrix.valuesData()[k], gpuValues[k], r, column);
      }
    }
  }
  for (Index r = 0; r < nc; ++r) accumulate(rhsMetrics, rhs[r], gpuRhs[r], r, r);

  if (plan.higherOrderBoundaryFaces() > 0) ++coverage.withHigherOrderBoundary;
  if (plan.correctableInternalFaces() > 0) ++coverage.withCorrectableInternal;
  if (correction) ++coverage.withCorrectionOn;
  if (std::string(gamma.name) == "varying") ++coverage.withVaryingGamma;

  const std::size_t totalDiffering =
      diagonal.differing + offDiagonal.differing + rhsMetrics.differing;
  const bool ok = patternEqual && totalDiffering == 0 && d2hDuring == 0;
  ++cases;
  if (!ok) ++failures;
  std::printf(
      "  %s %-18s %-9s %-13s %-8s corr=%-3s nnz=%-7zu diag[d=%zu,abs=%.3g,scale=%.4g] "
      "off[d=%zu,abs=%.3g] rhs[d=%zu,abs=%.3g,scale=%.4g]%s\n",
      ok ? "PASS" : "FAIL", meshName.c_str(), bcName, field.name, gamma.name,
      correction ? "on" : "off", cpuMatrix.nonZeros(), diagonal.differing, diagonal.maxAbs,
      diagonal.scale, offDiagonal.differing, offDiagonal.maxAbs, rhsMetrics.differing,
      rhsMetrics.maxAbs, rhsMetrics.scale, patternEqual ? "" : "  PATTERN MISMATCH");
  if (!ok && patternEqual) {
    const Metrics& worst = (diagonal.maxAbs >= offDiagonal.maxAbs && diagonal.maxAbs >= rhsMetrics.maxAbs)
                               ? diagonal
                               : (offDiagonal.maxAbs >= rhsMetrics.maxAbs ? offDiagonal : rhsMetrics);
    std::printf("       worst entry (%zu,%zu): cpu=%.17g gpu=%.17g  maxRel=%.3g\n",
                static_cast<std::size_t>(worst.worstRow),
                static_cast<std::size_t>(worst.worstColumn), worst.worstCpu, worst.worstGpu,
                worst.maxRel);
  }
  if (d2hDuring != 0) {
    std::printf("       device->host copies during assembly: %llu (must be 0)\n",
                static_cast<unsigned long long>(d2hDuring));
  }
}

void runMesh(const std::string& name, const Mesh& mesh) {
  const std::vector<FieldCase> fields = {{"constant", constantField},
                                         {"linear", linearField},
                                         {"quadratic", quadraticField},
                                         {"manufactured", manufacturedField}};
  const std::vector<GammaCase> gammas = {{"uniform", uniformGamma}, {"varying", varyingGamma}};
  const BoundaryConditionSet dirichlet = dirichletSet(mesh);
  const BoundaryConditionSet neumann = neumannSet(mesh);
  const BoundaryConditionSet mixed = mixedSet(mesh);

  {
    DeviceDiffusionPlan plan;
    if (plan.build(mesh, mixed)) {
      std::printf("       [%s cells=%zu higherOrderBoundaryFaces=%zu correctableInternal=%zu "
                  "resident=%zu B]\n",
                  name.c_str(), mesh.numberOfCells(),
                  static_cast<std::size_t>(plan.higherOrderBoundaryFaces()),
                  static_cast<std::size_t>(plan.correctableInternalFaces()), plan.residentBytes());
    }
  }

  for (const auto& gamma : gammas) {
    for (const auto& field : fields) {
      for (const bool correction : {false, true}) {
        runCase(name, mesh, "dirichlet", dirichlet, field, gamma, correction);
        runCase(name, mesh, "neumann", neumann, field, gamma, correction);
        runCase(name, mesh, "mixed", mixed, field, gamma, correction);
      }
    }
  }
}

// The comparator must be able to fail: one ULP on one coefficient.
void proveNonVacuous() {
  std::printf("\n=== non-vacuity control ===\n");
  Metrics clean, dirty;
  const Real reference = 12.5;
  accumulate(clean, reference, reference, 0, 0);
  const Real perturbed = std::nextafter(reference, std::numeric_limits<Real>::infinity());
  accumulate(dirty, reference, perturbed, 0, 0);
  const bool ok = clean.differing == 0 && dirty.differing == 1;
  if (!ok) ++failures;
  std::printf("  %s identical -> %zu differing; one-ULP perturbation -> %zu differing (abs=%.3g)\n",
              ok ? "PASS" : "FAIL", clean.differing, dirty.differing, dirty.maxAbs);
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001C: CUDA implicit diffusion vs CPU reference (bitwise) ===\n");
  std::printf("CPU reference: turbulence::assembleScalarDiffusionContribution\n\n");

  if (quick) {
    runMesh("distorted q16", q16(Vector3{}));
    runMesh("warped 3d 6", warped3D(6));
    std::printf("\nquick mode: cases=%d failures=%d\n", cases, failures);
    std::printf("%s\n", failures == 0 ? "DIFFUSION EQUIVALENCE (quick): PASS (bitwise)"
                                      : "DIFFUSION EQUIVALENCE (quick): FAIL");
    return failures == 0 ? 0 : 1;
  }

  runMesh("cartesian2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  runMesh("cartesian2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0));
  runMesh("graded2d 16",
          MeshGeometry::createGraded2D(
              16, 16, 1.0, 1.0,
              cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.15,
                                     cfd::mesh::GradingCluster::Start},
              cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.08,
                                     cfd::mesh::GradingCluster::Both}));
  runMesh("distorted q16", q16(Vector3{}));
  runMesh("q16 translated", q16(Vector3{1000.0, -250.0, 0.0}));
  runMesh("sheared 0.35", sheared(0.35));
  runMesh("multiblock L", multiBlockL());
  // Two cells across x: the mesh shape behind the recorded MESH-005 defect in
  // the EXPLICIT operator. The implicit path ported here is a different
  // implementation; this case exists to show CPU and GPU agree on it, NOT to
  // fix or reproduce that defect. See audit.md section 6.
  runMesh("two-cell 2x8", MeshGeometry::createCartesian2D(2, 8, 1.0, 1.0));
  runMesh("cartesian3d 8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
  runMesh("planar skew 3d 6", planarSkew3D(6));
  runMesh("warped 3d 6", warped3D(6));

  proveNonVacuous();

  std::printf("\n=== path coverage (cases reaching each branch) ===\n");
  std::printf("  P12-DIFF-002 boundary reconstruction  %d\n", coverage.withHigherOrderBoundary);
  std::printf("  correctable internal faces            %d\n", coverage.withCorrectableInternal);
  std::printf("  non-orthogonal correction enabled     %d\n", coverage.withCorrectionOn);
  std::printf("  spatially varying diffusivity         %d\n", coverage.withVaryingGamma);
  std::printf("  CPU/GPU sparsity pattern mismatches   %d\n", coverage.patternMismatch);
  if (coverage.withHigherOrderBoundary == 0 || coverage.withCorrectableInternal == 0 ||
      coverage.withCorrectionOn == 0 || coverage.withVaryingGamma == 0) {
    ++failures;
    std::printf("  FAIL a branch was never exercised -- the PASS lines do not cover it\n");
  }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "DIFFUSION EQUIVALENCE: PASS (bitwise)"
                                    : "DIFFUSION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
