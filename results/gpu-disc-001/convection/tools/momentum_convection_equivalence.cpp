// GPU-DISC-001D closure gate -- CUDA momentum convection vs the CPU reference.
//
// CPU reference: cfd::physics::assembleConvectionContribution, the production
// convection contribution of one velocity component. Both paths assemble into a
// zero-initialised matrix and RHS, so what is compared is the convection term
// itself.
//
// This is a CONVECTION-ONLY comparison. No diffusion, transient, source,
// relaxation or pressure-gradient term is assembled on either side.
//
// The requirement is BITWISE equality of the sparsity pattern, every stored
// coefficient and every RHS entry. The velocity gradient LinearUpwind depends on
// (computeVelocityGradient -- a different operator from the scalar gradient of
// 001B) is compared directly as well.
//
// usage: momentum_convection_equivalence [--quick]

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
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::algebra::SparseMatrixBuilder;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::gpu::DeviceMomentumConvectionPlan;
using cfd::gpu::DeviceConvectionSystem;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::VelocityComponent;

namespace {

int failures = 0;
int cases = 0;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------

std::vector<Vector3> gridVertices(Index n, Real length) {
  std::vector<Vector3> v;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      v.push_back(Vector3{static_cast<Real>(i) / static_cast<Real>(n) * length,
                          static_cast<Real>(j) / static_cast<Real>(n) * length, 0.0});
    }
  }
  return v;
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
  std::vector<Vector3> vertices = gridVertices(n, 1.0);
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
// Velocity fields, fluxes, vector boundary conditions
// ---------------------------------------------------------------------------

struct VelocityCase {
  const char* name;
  Vector3 (*value)(const Vector3&);
};
Vector3 constantVelocity(const Vector3&) { return Vector3{1.25, -0.75, 0.4}; }
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
// A discontinuity, so the TVD limiter is genuinely active.
Vector3 stepVelocity(const Vector3& x) {
  return x.x < 0.5 ? Vector3{1.0, 0.5, 0.25} : Vector3{2.0, -0.5, 0.75};
}

struct FluxCase {
  const char* name;
  Real (*value)(Index, const Vector3&, const Vector3&);
};
Real fluxPositive(Index, const Vector3&, const Vector3&) { return 0.7; }
Real fluxNegative(Index, const Vector3&, const Vector3&) { return -0.7; }
Real fluxMixed(Index id, const Vector3&, const Vector3&) {
  return (id % 3 == 0) ? 0.45 : ((id % 3 == 1) ? -0.8 : 1.3);
}
Real fluxWithZeros(Index id, const Vector3&, const Vector3&) {
  if (id % 5 == 0) return 0.0;
  if (id % 5 == 1) return -0.0;
  return (id % 2 == 0) ? 0.6 : -0.9;
}
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

struct BoundaryCase {
  const char* name;
  BoundaryConditionSet (*build)(const Mesh&);
};

BoundaryConditionSet allWall(const Mesh& mesh) {
  BoundaryConditionSet set;
  for (const auto& patch : mesh.boundaryPatches()) {
    set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>());
  }
  return set;
}
BoundaryConditionSet movingWallAndInlet(const Mesh& mesh) {
  BoundaryConditionSet set;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (i % 2 == 0) {
      set.set(mesh, patch.name(),
              std::make_unique<cfd::boundary::MovingWall>(Vector3{1.0 + 0.1 * static_cast<Real>(i),
                                                                  -0.2, 0.05}));
    } else {
      set.set(mesh, patch.name(),
              std::make_unique<cfd::boundary::Inlet>(Vector3{0.8, 0.3, -0.1}));
    }
    ++i;
  }
  return set;
}
// Every vector condition on one mesh: Wall, MovingWall, Inlet, Outlet, Symmetry.
BoundaryConditionSet allFiveKinds(const Mesh& mesh) {
  BoundaryConditionSet set;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    switch (i % 5) {
      case 0: set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>()); break;
      case 1:
        set.set(mesh, patch.name(),
                std::make_unique<cfd::boundary::MovingWall>(Vector3{1.5, -0.25, 0.1}));
        break;
      case 2:
        set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Inlet>(Vector3{0.9, 0.4, -0.2}));
        break;
      case 3: set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Outlet>()); break;
      default: set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Symmetry>()); break;
    }
    ++i;
  }
  return set;
}
BoundaryConditionSet outletAndSymmetry(const Mesh& mesh) {
  BoundaryConditionSet set;
  std::size_t i = 0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (i % 2 == 0) {
      set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Outlet>());
    } else {
      set.set(mesh, patch.name(), std::make_unique<cfd::boundary::Symmetry>());
    }
    ++i;
  }
  return set;
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

struct Coverage {
  int component[3] = {0, 0, 0};
  int scheme[4] = {0, 0, 0, 0};
  int boundaryKind[3] = {0, 0, 0};  // constant, identity, symmetry
  int reversedPairs = 0;
  int reversalChanged = 0;
  int gradientCases = 0;
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
  cfd::gpu::DeviceVelocity device;
  const Index n = static_cast<Index>(velocity.size());
  std::vector<Real> x(n), y(n), z(n);
  for (Index c = 0; c < n; ++c) { x[c] = velocity[c].x; y[c] = velocity[c].y; z[c] = velocity[c].z; }
  device.x.uploadFrom(x.data(), n);
  device.y.uploadFrom(y.data(), n);
  device.z.uploadFrom(z.data(), n);
  return device;
}

// Compares the velocity gradient directly -- LinearUpwind depends on it, and it
// is a different operator from the scalar gradient 001B qualified.
void runGradientCase(const std::string& meshName, const Mesh& mesh,
                     const DeviceMomentumConvectionPlan& plan, const char* bcName,
                     const BoundaryConditionSet& boundaries, const VelocityCase& velocityCase) {
  const Index nc = mesh.numberOfCells();
  VectorField velocity(nc);
  for (Index c = 0; c < nc; ++c) velocity[c] = velocityCase.value(mesh.cell(c).centroid());
  const auto cpu = cfd::discretization::computeVelocityGradient(mesh, velocity, boundaries);

  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceVelocityGradient gpu;
  cfd::gpu::computeVelocityGradientDevice(plan, deviceVelocity, gpu);

  const auto pull = [&](const cfd::gpu::DeviceBuffer<Real>& b) {
    std::vector<Real> host(static_cast<std::size_t>(b.size()));
    if (!host.empty()) b.downloadTo(host.data(), b.size());
    return host;
  };
  const auto ux = pull(gpu.gradUx), uy = pull(gpu.gradUy), uz = pull(gpu.gradUz);
  const auto vx = pull(gpu.gradVx), vy = pull(gpu.gradVy), vz = pull(gpu.gradVz);
  const auto wx = pull(gpu.gradWx), wy = pull(gpu.gradWy), wz = pull(gpu.gradWz);

  std::size_t differing = 0;
  Real maxAbs = 0.0, scale = 0.0;
  const bool threeD = mesh.dimension() == 3;
  for (Index c = 0; c < nc; ++c) {
    const Real want[9] = {cpu.gradU[c].x, cpu.gradU[c].y, cpu.gradU[c].z,
                          cpu.gradV[c].x, cpu.gradV[c].y, cpu.gradV[c].z,
                          threeD ? cpu.gradW[c].x : 0.0, threeD ? cpu.gradW[c].y : 0.0,
                          threeD ? cpu.gradW[c].z : 0.0};
    const Real got[9] = {ux[c], uy[c], uz[c], vx[c], vy[c], vz[c],
                         threeD ? wx[c] : 0.0, threeD ? wy[c] : 0.0, threeD ? wz[c] : 0.0};
    for (int k = 0; k < 9; ++k) {
      if (!sameBits(want[k], got[k])) ++differing;
      maxAbs = std::max(maxAbs, std::abs(want[k] - got[k]));
      scale = std::max(scale, std::abs(want[k]));
    }
  }
  ++cases;
  ++coverage.gradientCases;
  if (differing != 0) ++failures;
  std::printf("  %s %-16s %-14s velocityGradient %-13s differing=%-5zu maxAbs=%-10.3g scale=%.4g\n",
              differing == 0 ? "PASS" : "FAIL", meshName.c_str(), bcName, velocityCase.name,
              differing, maxAbs, scale);
}

std::vector<Real> runAssemblyCase(const std::string& meshName, const Mesh& mesh,
                                  const DeviceMomentumConvectionPlan& plan, const char* bcName,
                                  const BoundaryConditionSet& boundaries,
                                  const VelocityCase& velocityCase, const FluxCase& flux, Real sign,
                                  Index component, Index scheme, bool print) {
  const Index nc = mesh.numberOfCells();
  VectorField velocity(nc);
  for (Index c = 0; c < nc; ++c) velocity[c] = velocityCase.value(mesh.cell(c).centroid());
  const SurfaceField massFlux = buildFlux(mesh, flux, sign);

  SparseMatrixBuilder builder(nc, nc);
  cfd::algebra::Vector rhs(nc, 0.0);
  cfd::physics::assembleConvectionContribution(mesh, massFlux, velocity, boundaries,
                                               static_cast<VelocityComponent>(component), builder,
                                               rhs, static_cast<ConvectionScheme>(scheme));
  const auto cpuMatrix = builder.build();

  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceBuffer<Real> dFlux;
  dFlux.uploadFrom(massFlux.data(), static_cast<Index>(massFlux.size()));
  DeviceConvectionSystem system;
  cfd::gpu::assembleMomentumConvectionDevice(plan, deviceVelocity, dFlux, component, scheme,
                                             system);

  std::vector<Index> gpuRowOffsets(system.rowOffsets.size()), gpuColumns(system.columnIndices.size());
  std::vector<Real> gpuValues(system.values.size()), gpuRhs(system.rhs.size());
  system.rowOffsets.downloadTo(gpuRowOffsets.data(), system.rowOffsets.size());
  if (!gpuColumns.empty()) system.columnIndices.downloadTo(gpuColumns.data(), system.columnIndices.size());
  if (!gpuValues.empty()) system.values.downloadTo(gpuValues.data(), system.values.size());
  system.rhs.downloadTo(gpuRhs.data(), system.rhs.size());

  std::size_t differingA = 0, differingRhs = 0, missing = 0, extraNonZero = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scaleA = 0.0, scaleR = 0.0;
  for (Index r = 0; r < nc; ++r) {
    Index g = gpuRowOffsets[r];
    for (Index k = cpuMatrix.rowOffsetsData()[r]; k < cpuMatrix.rowOffsetsData()[r + 1]; ++k) {
      const Index column = cpuMatrix.columnIndicesData()[k];
      while (g < gpuRowOffsets[r + 1] && gpuColumns[g] < column) {
        if (gpuValues[g] != 0.0) ++extraNonZero;
        ++g;
      }
      if (g >= gpuRowOffsets[r + 1] || gpuColumns[g] != column) { ++missing; continue; }
      const Real want = cpuMatrix.valuesData()[k];
      const Real got = gpuValues[g];
      if (!sameBits(want, got)) ++differingA;
      maxAbs = std::max(maxAbs, std::abs(want - got));
      scaleA = std::max(scaleA, std::abs(want));
      if (std::abs(want) > 0.0) maxRel = std::max(maxRel, std::abs(want - got) / std::abs(want));
      ++g;
    }
    for (; g < gpuRowOffsets[r + 1]; ++g) {
      if (gpuValues[g] != 0.0) ++extraNonZero;
    }
  }
  std::vector<Real> cpuRhs(nc);
  for (Index r = 0; r < nc; ++r) {
    cpuRhs[r] = rhs[r];
    if (!sameBits(rhs[r], gpuRhs[r])) ++differingRhs;
    maxAbs = std::max(maxAbs, std::abs(rhs[r] - gpuRhs[r]));
    scaleR = std::max(scaleR, std::abs(rhs[r]));
  }
  if (missing != 0 || extraNonZero != 0) ++coverage.patternMismatch;

  ++cases;
  ++coverage.component[component];
  ++coverage.scheme[scheme];
  const bool ok = differingA == 0 && differingRhs == 0 && missing == 0 && extraNonZero == 0;
  if (!ok) ++failures;
  if (print) {
    std::printf("  %s %-16s %-14s %-2s %-13s %-9s %-4s nnz=%-7zu A[d=%zu] rhs[d=%zu] "
                "maxAbs=%-10.3g maxRel=%-10.3g scaleA=%-9.4g scaleR=%-9.4g%s\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), bcName, componentName(component),
                schemeName(scheme), flux.name, sign > 0 ? "fwd" : "rev", cpuMatrix.nonZeros(),
                differingA, differingRhs, maxAbs, maxRel, scaleA, scaleR,
                (missing != 0 || extraNonZero != 0) ? "  PATTERN MISMATCH" : "");
  }
  return cpuRhs;
}

void runMesh(const std::string& name, const Mesh& mesh, bool verbose) {
  const bool threeD = mesh.dimension() == 3;
  const std::vector<VelocityCase> velocities = {{"constant", constantVelocity},
                                                {"linear", linearVelocity},
                                                {"nonuniform", nonUniformVelocity},
                                                {"step", stepVelocity}};
  const std::vector<FluxCase> fluxes = {{"positive", fluxPositive},
                                        {"negative", fluxNegative},
                                        {"mixed", fluxMixed},
                                        {"zeros", fluxWithZeros},
                                        {"swirl", fluxSwirl}};
  const std::vector<BoundaryCase> boundarySets = {{"wall", allWall},
                                                  {"movingwall+inlet", movingWallAndInlet},
                                                  {"outlet+symmetry", outletAndSymmetry},
                                                  {"all-five", allFiveKinds}};

  for (const auto& bcCase : boundarySets) {
    const BoundaryConditionSet boundaries = bcCase.build(mesh);
    DeviceMomentumConvectionPlan plan;
    if (!plan.build(mesh, boundaries)) {
      ++failures;
      std::printf("  FAIL %-16s %-14s plan unusable: %s\n", name.c_str(), bcCase.name,
                  plan.unsupportedReason().c_str());
      continue;
    }
    if (plan.constantBoundaryFaces() > 0) ++coverage.boundaryKind[0];
    if (plan.identityBoundaryFaces() > 0) ++coverage.boundaryKind[1];
    if (plan.symmetryBoundaryFaces() > 0) ++coverage.boundaryKind[2];
    std::printf("       [%s/%s cells=%zu bcFaces(const=%zu identity=%zu symmetry=%zu) "
                "skewedFaces=%zu resident=%zu B]\n",
                name.c_str(), bcCase.name, mesh.numberOfCells(),
                static_cast<std::size_t>(plan.constantBoundaryFaces()),
                static_cast<std::size_t>(plan.identityBoundaryFaces()),
                static_cast<std::size_t>(plan.symmetryBoundaryFaces()),
                static_cast<std::size_t>(plan.skewedFaceCount()), plan.residentBytes());

    for (const auto& velocityCase : velocities) {
      runGradientCase(name, mesh, plan, bcCase.name, boundaries, velocityCase);
      for (const auto& flux : fluxes) {
        for (const Index scheme : {cfd::gpu::kConvectionUpwind, cfd::gpu::kConvectionCentral,
                                   cfd::gpu::kConvectionLinearUpwind, cfd::gpu::kConvectionQUICK}) {
          const Index maxComponent = threeD ? 3 : 2;
          for (Index component = 0; component < maxComponent; ++component) {
            const auto forward = runAssemblyCase(name, mesh, plan, bcCase.name, boundaries,
                                                 velocityCase, flux, 1.0, component, scheme,
                                                 verbose);
            const auto reversed = runAssemblyCase(name, mesh, plan, bcCase.name, boundaries,
                                                  velocityCase, flux, -1.0, component, scheme,
                                                  verbose);
            ++coverage.reversedPairs;
            bool changed = false;
            for (std::size_t c = 0; c < forward.size() && !changed; ++c) {
              if (!sameBits(forward[c], reversed[c])) changed = true;
            }
            if (changed) ++coverage.reversalChanged;
          }
        }
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001D closure: CUDA momentum convection vs CPU (bitwise) ===\n");
  std::printf("CPU reference: physics::assembleConvectionContribution\n");
  std::printf("plus discretization::computeVelocityGradient (LinearUpwind's dependency)\n\n");

  if (quick) {
    runMesh("distorted q16", q16(), false);
    runMesh("warped 3d 4", warped3D(4), false);
  } else {
    runMesh("cartesian2d 12", MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0), true);
    runMesh("cartesian2d 24", MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0), false);
    runMesh("graded2d 12",
            MeshGeometry::createGraded2D(
                12, 12, 1.0, 1.0,
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.15,
                                       cfd::mesh::GradingCluster::Start},
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.08,
                                       cfd::mesh::GradingCluster::Both}),
            false);
    runMesh("distorted q16", q16(), false);
    runMesh("sheared 0.35", sheared(0.35), false);
    runMesh("cartesian3d 5", MeshGeometry::createCartesian3D(5, 5, 5, 1.0, 1.0, 1.0), false);
    runMesh("warped 3d 4", warped3D(4), false);
  }

  std::printf("\n=== coverage ===\n");
  for (Index c = 0; c < 3; ++c) {
    std::printf("  component %-2s  %d cases\n", componentName(c), coverage.component[c]);
  }
  for (Index s = 0; s < 4; ++s) {
    std::printf("  scheme %-14s %d cases\n", schemeName(s), coverage.scheme[s]);
  }
  std::printf("  vector BC constant/identity/symmetry plans   %d / %d / %d\n",
              coverage.boundaryKind[0], coverage.boundaryKind[1], coverage.boundaryKind[2]);
  std::printf("  velocityGradient cases                       %d\n", coverage.gradientCases);
  std::printf("  reversed-flux pairs                          %d\n", coverage.reversedPairs);
  std::printf("  reversal actually changed the result         %d\n", coverage.reversalChanged);
  std::printf("  sparsity pattern mismatches                  %d\n", coverage.patternMismatch);

  for (Index s = 0; s < 4; ++s) {
    if (coverage.scheme[s] == 0) { ++failures; std::printf("  FAIL scheme %s never exercised\n", schemeName(s)); }
  }
  for (Index c = 0; c < 2; ++c) {
    if (coverage.component[c] == 0) { ++failures; std::printf("  FAIL component %s never exercised\n", componentName(c)); }
  }
  if (!quick && coverage.component[2] == 0) {
    ++failures;
    std::printf("  FAIL the W component was never exercised on a 3D mesh\n");
  }
  for (int k = 0; k < 3; ++k) {
    if (coverage.boundaryKind[k] == 0) {
      ++failures;
      std::printf("  FAIL vector boundary kind %d never exercised\n", k);
    }
  }
  if (coverage.reversalChanged == 0) {
    ++failures;
    std::printf("  FAIL reversing the flux never changed the result -- direction tests vacuous\n");
  }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "MOMENTUM CONVECTION EQUIVALENCE: PASS (bitwise)"
                                    : "MOMENTUM CONVECTION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
