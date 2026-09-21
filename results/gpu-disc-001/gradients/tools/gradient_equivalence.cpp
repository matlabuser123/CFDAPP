// GPU-DISC-001B gate -- CUDA Green-Gauss gradient vs the CPU reference.
//
// The requirement is BITWISE equality, not agreement to a tolerance. The device
// path evaluates the same expressions, in the same association order, on the
// same inputs, accumulating each cell's sum over that cell's own faces in CSR
// order -- which is `cell.faceIds()` order. Nothing about that should round
// differently, so any difference at all is a defect to investigate, and the
// gate says so rather than reaching for an epsilon.
//
// Error metrics are still reported for every case, because "bitwise equal" with
// no magnitudes attached is not evidence -- a case that fails needs to say how
// badly, and a case that passes needs to show the comparison actually ran over
// real, non-trivial numbers.
//
// The probe is proven non-vacuous at the end: a single perturbed value must be
// caught by the same comparator that declares the real cases equal.
//
// usage: gradient_equivalence

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/gpu/DeviceGradient.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MultiBlockSpec.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::gpu::DeviceGradientPlan;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

int failures = 0;
int cases = 0;

// ---------------------------------------------------------------------------
// Meshes. Reproduced from tests/unit/discretization/test_gradient_boundary_
// consistency.cpp so the device path is exercised on exactly the geometry the
// CPU gradient was qualified on, rather than on meshes invented for the GPU.
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

Mesh quad(Index n, Real length, const Vector3& offset) {
  return MeshGeometry::createStructuredQuad2D(n, n, gridVertices(n, length, offset));
}

// The distorted mesh Q16 of the GRAD-002 gates: non-orthogonal and skewed.
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

// Strictly interior vertices move tangentially, so every boundary face keeps its
// exact axis-aligned normal while the boundary cells' centroids leave it. This
// is the family that makes P12-GRAD-002's boundary transfer, and P12-MESH-001's
// oblique-Neumann treatment, actually fire.
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

// A three-block L-shaped domain: genuinely non-rectangular, with two internal
// block interfaces, so cell and face numbering is not the single-block pattern
// and the CSR row order the sum kernel walks is a real test rather than a
// relabelled Cartesian one.
//
//   +-------+
//   |   B   |          B  x in [0, 0.5],   y in [0.5, 1.0]
//   +-------+-------+   A1 x in [0, 0.5],   y in [0, 0.5]
//   |  A1   |  A2   |   A2 x in [0.5, 1.0], y in [0, 0.5]
//   +-------+-------+
//
// Shared sides are generated from the same arithmetic on both blocks, which the
// multi-block builder requires to agree bitwise.
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
// Fields and boundary conditions
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

// Dirichlet everywhere, or Neumann everywhere. Both are exercised on every mesh:
// Dirichlet drives the boundary face value directly, while Neumann is what makes
// an oblique boundary face oblique (a value-prescribing condition is never
// oblique -- see obliqueNeumannFace).
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

// Alternating Dirichlet/Neumann patches, so a single mesh carries BOTH boundary
// encodings at once and the per-face `kind` lookup has to be right per face
// rather than per mesh. This is also the configuration where some boundary
// faces are oblique and others, on the same mesh, are not.
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
  std::size_t differingComponents = 0;
  Real maxAbs = 0.0;
  Real maxRel = 0.0;
  Real l2 = 0.0;
  Index worstCell = 0;
  int worstComponent = 0;
  Real worstCpu = 0.0;
  Real worstGpu = 0.0;
};

// Bitwise on the bits, metrics on the values. memcmp rather than == so that a
// NaN that matches bit for bit counts as equal and a +0.0/-0.0 split does not.
Metrics compare(const VectorField& cpu, const std::vector<Real>& gx, const std::vector<Real>& gy,
                const std::vector<Real>& gz) {
  Metrics m;
  Real sumSquares = 0.0;
  for (std::size_t c = 0; c < cpu.size(); ++c) {
    const Real cpuValue[3] = {cpu[c].x, cpu[c].y, cpu[c].z};
    const Real gpuValue[3] = {gx[c], gy[c], gz[c]};
    for (int k = 0; k < 3; ++k) {
      if (std::memcmp(&cpuValue[k], &gpuValue[k], sizeof(Real)) != 0) ++m.differingComponents;
      const Real diff = std::abs(cpuValue[k] - gpuValue[k]);
      sumSquares += diff * diff;
      const Real scale = std::abs(cpuValue[k]);
      const Real rel = scale > 0.0 ? diff / scale : diff;
      if (diff > m.maxAbs) {
        m.maxAbs = diff;
        m.worstCell = static_cast<Index>(c);
        m.worstComponent = k;
        m.worstCpu = cpuValue[k];
        m.worstGpu = gpuValue[k];
      }
      if (rel > m.maxRel) m.maxRel = rel;
    }
  }
  m.l2 = std::sqrt(sumSquares);
  return m;
}

std::vector<Real> download(const cfd::gpu::DeviceBuffer<Real>& buffer) {
  std::vector<Real> host(static_cast<std::size_t>(buffer.size()));
  if (!host.empty()) buffer.downloadTo(host.data(), buffer.size());
  return host;
}

// Largest |grad| the CPU produced, so the log can show the comparison ran over
// numbers that are actually non-trivial rather than a field of zeros.
Real magnitudeScale(const VectorField& g) {
  Real worst = 0.0;
  for (std::size_t c = 0; c < g.size(); ++c) {
    worst = std::max(worst, std::max(std::abs(g[c].x), std::max(std::abs(g[c].y), std::abs(g[c].z))));
  }
  return worst;
}

void runCase(const std::string& meshName, const Mesh& mesh, const std::string& bcName,
             const BoundaryConditionSet& boundaries, const FieldCase& field) {
  ScalarField phi(mesh.numberOfCells());
  for (Index c = 0; c < mesh.numberOfCells(); ++c) phi[c] = field.value(mesh.cell(c).centroid());

  const VectorField cpu = cfd::discretization::greenGaussGradient(
      mesh, phi, boundaries, cfd::discretization::kGreenGaussSkewCorrectionSweeps);

  DeviceGradientPlan plan;
  if (!plan.build(mesh, boundaries)) {
    ++cases;
    ++failures;
    std::printf("  FAIL %-22s %-9s %-13s plan unusable: %s\n", meshName.c_str(), bcName.c_str(),
                field.name, plan.unsupportedReason().c_str());
    return;
  }

  cfd::gpu::DeviceBuffer<Real> devicePhi;
  devicePhi.uploadFrom(phi.data(), static_cast<Index>(phi.size()));
  cfd::gpu::DeviceBuffer<Real> gx, gy, gz;
  const std::uint64_t d2hBefore = cfd::gpu::gpuExecutionStats().deviceToHostCalls;
  cfd::gpu::greenGaussGradientDevice(plan, devicePhi,
                                     cfd::discretization::kGreenGaussSkewCorrectionSweeps, gx, gy,
                                     gz);
  const std::uint64_t d2hDuringSolve =
      cfd::gpu::gpuExecutionStats().deviceToHostCalls - d2hBefore;

  // Boundary-adjacent cells are where every special treatment in this operator
  // lives, so they are counted and compared separately rather than being left
  // implicit in the whole-mesh number.
  std::size_t boundaryCells = 0;
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    for (const Index faceId : mesh.cell(c).faceIds()) {
      if (mesh.face(faceId).isBoundary()) {
        ++boundaryCells;
        break;
      }
    }
  }
  const Metrics m = compare(cpu, download(gx), download(gy), download(gz));
  ++cases;
  const bool ok = m.differingComponents == 0 && d2hDuringSolve == 0;
  if (!ok) ++failures;
  std::printf("  %s %-22s %-9s %-13s cells=%-6zu |grad|max=%-11.4g diff=%-6zu maxAbs=%-10.3g "
              "maxRel=%-10.3g L2=%-10.3g bcells=%zu\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), bcName.c_str(), field.name,
              mesh.numberOfCells(), magnitudeScale(cpu), m.differingComponents, m.maxAbs, m.maxRel,
              m.l2, boundaryCells);
  if (m.differingComponents != 0) {
    std::printf("       worst cell %zu component %c: cpu=%.17g gpu=%.17g\n",
                static_cast<std::size_t>(m.worstCell), "xyz"[m.worstComponent], m.worstCpu,
                m.worstGpu);
  }
  if (d2hDuringSolve != 0) {
    std::printf("       device->host copies during the gradient: %llu (must be 0 -- the result is "
                "supposed to stay device-resident)\n",
                static_cast<unsigned long long>(d2hDuringSolve));
  }
}

// Which of the algorithm's branches this mesh/BC pair actually reaches. Without
// this, a run of PASS lines proves only that the paths that DID execute agree --
// it cannot distinguish "the oblique kernel is correct" from "the oblique kernel
// never ran". Reported per mesh and totalled at the end.
struct Coverage {
  int sweeping = 0;
  int withSkew = 0;
  int withOblique = 0;
  int withClaims = 0;
  int withTransfer = 0;
};
Coverage coverage;

void reportPaths(const std::string& name, const Mesh& mesh, const char* bcName,
                 const BoundaryConditionSet& boundaries) {
  DeviceGradientPlan plan;
  if (!plan.build(mesh, boundaries)) return;
  if (plan.sweepsNeeded()) ++coverage.sweeping;
  if (plan.skewedFaceCount() > 0) ++coverage.withSkew;
  if (plan.obliqueFaceCount() > 0) ++coverage.withOblique;
  if (plan.claimCount() > 0) ++coverage.withClaims;
  if (plan.boundaryTransferNeeded()) ++coverage.withTransfer;
  std::printf("       [%s/%-9s sweeps=%-3s skewedFaces=%-5zu obliqueFaces=%-5zu claims=%-5zu "
              "boundaryTransfer=%-3s resident=%zu B]\n",
              name.c_str(), bcName, plan.sweepsNeeded() ? "yes" : "no",
              static_cast<std::size_t>(plan.skewedFaceCount()),
              static_cast<std::size_t>(plan.obliqueFaceCount()),
              static_cast<std::size_t>(plan.claimCount()),
              plan.boundaryTransferNeeded() ? "yes" : "no", plan.residentBytes());
}

void runMesh(const std::string& name, const Mesh& mesh) {
  const std::vector<FieldCase> fields = {{"constant", constantField},
                                         {"linear", linearField},
                                         {"quadratic", quadraticField},
                                         {"manufactured", manufacturedField}};
  const BoundaryConditionSet dirichlet = dirichletSet(mesh);
  const BoundaryConditionSet neumann = neumannSet(mesh);
  const BoundaryConditionSet mixed = mixedSet(mesh);
  reportPaths(name, mesh, "dirichlet", dirichlet);
  reportPaths(name, mesh, "neumann", neumann);
  reportPaths(name, mesh, "mixed", mixed);
  for (const auto& field : fields) {
    runCase(name, mesh, "dirichlet", dirichlet, field);
    runCase(name, mesh, "neumann", neumann, field);
    runCase(name, mesh, "mixed", mixed, field);
  }
}

// The comparator must actually be able to fail. One component of one cell is
// perturbed by a single ULP -- the smallest difference that exists -- and the
// same memcmp must report it.
void proveNonVacuous() {
  std::printf("\n=== non-vacuity control ===\n");
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  ScalarField phi(mesh.numberOfCells());
  for (Index c = 0; c < mesh.numberOfCells(); ++c) phi[c] = linearField(mesh.cell(c).centroid());
  const BoundaryConditionSet boundaries = dirichletSet(mesh);
  const VectorField cpu = cfd::discretization::greenGaussGradient(
      mesh, phi, boundaries, cfd::discretization::kGreenGaussSkewCorrectionSweeps);

  std::vector<Real> gx(cpu.size()), gy(cpu.size()), gz(cpu.size());
  for (std::size_t c = 0; c < cpu.size(); ++c) {
    gx[c] = cpu[c].x;
    gy[c] = cpu[c].y;
    gz[c] = cpu[c].z;
  }
  const Metrics clean = compare(cpu, gx, gy, gz);
  gx[0] = std::nextafter(gx[0], std::numeric_limits<Real>::infinity());
  const Metrics dirty = compare(cpu, gx, gy, gz);

  const bool ok = clean.differingComponents == 0 && dirty.differingComponents == 1;
  if (!ok) ++failures;
  std::printf("  %s identical copy -> %zu differing, one-ULP perturbation -> %zu differing "
              "(maxAbs=%.3g)\n",
              ok ? "PASS" : "FAIL", clean.differingComponents, dirty.differingComponents,
              dirty.maxAbs);
}

}  // namespace

int main(int argc, char** argv) {
  // --quick keeps compute-sanitizer (racecheck in particular) tractable. The two
  // meshes it keeps between them reach EVERY kernel: distorted q16 has skewed
  // faces, oblique Neumann faces, claims and boundary transfer in 2D, and warped
  // 3d 6 has all four in 3D with a non-zero z component.
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  if (quick) {
    runMesh("distorted q16", q16(Vector3{}));
    runMesh("warped 3d 6", warped3D(6));
    std::printf("\nquick mode: cases=%d failures=%d\n", cases, failures);
    std::printf("%s\n", failures == 0 ? "GRADIENT EQUIVALENCE (quick): PASS (bitwise)"
                                      : "GRADIENT EQUIVALENCE (quick): FAIL");
    return failures == 0 ? 0 : 1;
  }

  std::printf("=== GPU-DISC-001B: CUDA gradient vs CPU reference (bitwise) ===\n\n");

  runMesh("cartesian2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0));
  runMesh("cartesian2d 40", MeshGeometry::createCartesian2D(40, 40, 1.0, 1.0));
  runMesh("quad translated", quad(16, 1.0, Vector3{1000.0, -250.0, 0.0}));
  runMesh("graded2d 16",
          MeshGeometry::createGraded2D(16, 16, 1.0, 1.0,
                                       cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric,
                                                              1.15,
                                                              cfd::mesh::GradingCluster::Start},
                                       cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric,
                                                              1.08,
                                                              cfd::mesh::GradingCluster::Both}));
  runMesh("distorted q16", q16(Vector3{}));
  // P12-GRAD-002's translated distorted mesh: the family whose discretization the
  // old exact geometric predicate changed under translation.
  runMesh("q16 translated", q16(Vector3{1000.0, -250.0, 0.0}));
  runMesh("sheared 0.35", sheared(0.35));
  runMesh("multiblock L", multiBlockL());
  runMesh("cartesian3d 8", MeshGeometry::createCartesian3D(8, 8, 8, 1.0, 1.0, 1.0));
  runMesh("planar skew 3d 6", planarSkew3D(6));
  runMesh("warped 3d 6", warped3D(6));

  proveNonVacuous();

  std::printf("\n=== path coverage (mesh/BC pairs reaching each branch) ===\n");
  std::printf("  sweep loop entered     %d\n", coverage.sweeping);
  std::printf("  skew-corrected faces   %d\n", coverage.withSkew);
  std::printf("  oblique Neumann faces  %d\n", coverage.withOblique);
  std::printf("  GRAD-002 claims        %d\n", coverage.withClaims);
  std::printf("  boundary transfer      %d\n", coverage.withTransfer);
  if (coverage.withSkew == 0 || coverage.withOblique == 0 || coverage.withClaims == 0 ||
      coverage.withTransfer == 0) {
    ++failures;
    std::printf("  FAIL a branch of the algorithm was never exercised -- the PASS lines above do "
                "not cover it\n");
  }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "GRADIENT EQUIVALENCE: PASS (bitwise)"
                                    : "GRADIENT EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
