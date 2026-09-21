// GPU-DISC-001G gate -- CUDA momentum response coefficient vs the CPU.
//
// CPU reference: cfd::pressure_velocity::computeMomentumResponseCoefficient,
//     d_P = V_P / aP_P
//
// Two levels:
//
//   L1 synthetic     diagonals constructed directly, including the extremes of
//                    the VALID domain (very small, very large, uniform,
//                    nonuniform), on 2D and 3D meshes of several sizes.
//   L2 integrated    CPU momentum assembly -> CPU response, against
//                    CUDA momentum assembly -> CUDA response. This exercises
//                    the whole upstream chain, including the relaxation
//                    dependence, which reaches this function only through the
//                    diagonal it is handed.
//
// Bitwise. A single division of the same operands cannot round differently.
//
// usage: momentum_response_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::VelocityComponent;

namespace {

int failures = 0;
int syntheticCases = 0;
int integratedCases = 0;
std::size_t valuesCompared = 0;
std::size_t bitwiseValues = 0;
Real globalMaxAbs = 0.0;
Real globalMaxRel = 0.0;

struct Coverage {
  int component[3] = {0, 0, 0};
  int twoD = 0, threeD = 0;
  int alphaValues = 0;
  int smallDiagonal = 0, largeDiagonal = 0, uniformDiagonal = 0, nonUniformDiagonal = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

void compare(const ScalarField& cpu, const std::vector<Real>& gpu, std::size_t& differing,
             Real& maxAbs, Real& maxRel, Real& scale) {
  for (std::size_t c = 0; c < cpu.size(); ++c) {
    ++valuesCompared;
    if (sameBits(cpu[c], gpu[c])) ++bitwiseValues; else ++differing;
    const Real diff = std::abs(cpu[c] - gpu[c]);
    maxAbs = std::max(maxAbs, diff);
    scale = std::max(scale, std::abs(cpu[c]));
    if (std::abs(cpu[c]) > 0.0) maxRel = std::max(maxRel, diff / std::abs(cpu[c]));
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);
}

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
// L1 -- synthetic diagonals across the valid domain
// ---------------------------------------------------------------------------

struct DiagonalCase {
  const char* name;
  Real (*value)(Index, Index);  // (cellId, cellCount) -> aP
};
Real uniformDiagonal(Index, Index) { return 1.0; }
Real uniformOddDiagonal(Index, Index) { return 3.7; }
Real nonUniformDiagonal(Index c, Index n) {
  return 0.5 + (2.5 * static_cast<Real>(c) / static_cast<Real>(n)) + (0.125 * (c % 7));
}
// The small end of the valid domain: positive, finite, nonzero, but tiny.
Real smallDiagonal(Index c, Index) { return 1e-300 * (1.0 + static_cast<Real>(c % 5)); }
// The large end.
Real largeDiagonal(Index c, Index) { return 1e300 * (1.0 + static_cast<Real>(c % 3)); }
// A denormal-adjacent value, still strictly positive and finite.
Real denormalAdjacentDiagonal(Index c, Index) {
  return std::numeric_limits<Real>::min() * (1.0 + static_cast<Real>(c % 4));
}

void runSynthetic(const std::string& meshName, const Mesh& mesh, const DiagonalCase& diagonalCase) {
  const Index nc = mesh.numberOfCells();
  cfd::algebra::Vector diagonal(nc);
  std::vector<Real> host(nc);
  for (Index c = 0; c < nc; ++c) {
    const Real value = diagonalCase.value(c, nc);
    diagonal[c] = value;
    host[c] = value;
  }
  const auto cpu = cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, diagonal);

  cfd::gpu::DeviceMesh deviceMesh;
  deviceMesh.upload(mesh);
  cfd::gpu::DeviceBuffer<Real> dDiagonal, dResponse;
  dDiagonal.uploadFrom(host.data(), nc);
  cfd::gpu::computeMomentumResponseCoefficientDevice(deviceMesh, dDiagonal, dResponse);
  std::vector<Real> gpu(nc);
  dResponse.downloadTo(gpu.data(), dResponse.size());

  std::size_t differing = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
  compare(cpu, gpu, differing, maxAbs, maxRel, scale);
  ++syntheticCases;
  if (differing != 0) ++failures;
  const std::string name = diagonalCase.name;
  if (name.find("small") != std::string::npos || name.find("denormal") != std::string::npos)
    ++coverage.smallDiagonal;
  if (name.find("large") != std::string::npos) ++coverage.largeDiagonal;
  if (name.find("uniform") == 0) ++coverage.uniformDiagonal;
  if (name.find("nonuniform") != std::string::npos) ++coverage.nonUniformDiagonal;
  std::printf("  %s L1 %-16s %-22s cells=%-6zu differing=%-4zu maxAbs=%-10.3g maxRel=%-10.3g "
              "scale=%.4g\n",
              differing == 0 ? "PASS" : "FAIL", meshName.c_str(), diagonalCase.name,
              mesh.numberOfCells(), differing, maxAbs, maxRel, scale);
}

// ---------------------------------------------------------------------------
// L2 -- the integrated chain
// ---------------------------------------------------------------------------

Real fluxSwirl(Index, const Vector3& centroid, const Vector3& area) {
  const Vector3 u{-(centroid.y - 0.5), centroid.x - 0.5, 0.1 * centroid.z};
  return dot(u, area);
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
BoundaryConditionSet pressureSet(const Mesh& mesh) {
  BoundaryConditionSet s;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    if (i % 2 == 0) s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedValue>(101325.0));
    else s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  return s;
}

void runIntegrated(const std::string& meshName, const Mesh& mesh, Index component, Index scheme,
                   Real alpha) {
  const Index nc = mesh.numberOfCells();
  const BoundaryConditionSet velocityBoundaries = allFive(mesh);
  const BoundaryConditionSet pressureBoundaries = pressureSet(mesh);

  VectorField velocity(nc);
  ScalarField pressure(nc), viscosity(nc), previous(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    const Real pi = cfd::constants::pi;
    velocity[c] = Vector3{std::sin(pi * x.x) * std::cos(pi * x.y) + 0.5,
                          std::cos(pi * x.x) * std::sin(pi * x.y) - 0.25,
                          (0.3 * std::sin(pi * x.z)) + (0.2 * x.x)};
    pressure[c] = 101325.0 - (250.0 * x.x) + (80.0 * x.y) - (30.0 * x.z);
    viscosity[c] = 1.0e-3 + (4.0e-3 * x.x) + (2.0e-3 * x.y * x.y);
    previous[c] = velocity[c].x + (0.125 * static_cast<Real>(c % 7)) - 0.3;
  }
  SurfaceField massFlux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    massFlux[f] = fluxSwirl(f, face.centroid(), face.areaVector());
  }

  // CPU chain: assembly -> response
  const auto cpuAssembly = cfd::pressure_velocity::assembleRelaxedMomentumComponent(
      mesh, velocity, pressure, massFlux, viscosity, velocityBoundaries, pressureBoundaries,
      static_cast<VelocityComponent>(component), previous, alpha, nullptr, nullptr,
      static_cast<ConvectionScheme>(scheme));
  const auto cpu =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, cpuAssembly.diagonal);

  // GPU chain: assembly -> response, both device-resident throughout
  cfd::gpu::DeviceMomentumAssemblyPlan plan;
  if (!plan.build(mesh, velocityBoundaries, pressureBoundaries)) {
    ++failures;
    std::printf("  FAIL L2 %-16s plan unusable: %s\n", meshName.c_str(),
                plan.unsupportedReason().c_str());
    return;
  }
  cfd::gpu::DeviceVelocity deviceVelocity;
  std::vector<Real> vx(nc), vy(nc), vz(nc);
  for (Index c = 0; c < nc; ++c) { vx[c] = velocity[c].x; vy[c] = velocity[c].y; vz[c] = velocity[c].z; }
  deviceVelocity.x.uploadFrom(vx.data(), nc);
  deviceVelocity.y.uploadFrom(vy.data(), nc);
  deviceVelocity.z.uploadFrom(vz.data(), nc);
  cfd::gpu::DeviceBuffer<Real> dPressure, dViscosity, dFlux, dPrevious;
  dPressure.uploadFrom(pressure.data(), nc);
  dViscosity.uploadFrom(viscosity.data(), nc);
  dFlux.uploadFrom(massFlux.data(), static_cast<Index>(massFlux.size()));
  dPrevious.uploadFrom(previous.data(), nc);

  cfd::gpu::MomentumAssemblyOptions options;
  options.component = component;
  options.convectionScheme = scheme;
  options.relaxationAlpha = alpha;
  cfd::gpu::DeviceMomentumSystem system;
  cfd::gpu::assembleRelaxedMomentumDevice(plan, deviceVelocity, dPressure, dViscosity, dFlux,
                                          dPrevious, nullptr, options, system);

  const std::uint64_t d2hBefore = cfd::gpu::gpuExecutionStats().deviceToHostCalls;
  cfd::gpu::DeviceBuffer<Real> dResponse;
  cfd::gpu::computeMomentumResponseCoefficientDevice(plan.convectionPlan().mesh(), system.diagonal,
                                                     dResponse);
  const std::uint64_t d2hDuring = cfd::gpu::gpuExecutionStats().deviceToHostCalls - d2hBefore;

  std::vector<Real> gpu(nc);
  dResponse.downloadTo(gpu.data(), dResponse.size());

  std::size_t differing = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
  compare(cpu, gpu, differing, maxAbs, maxRel, scale);
  ++integratedCases;
  ++coverage.component[component];
  if (mesh.dimension() == 3) ++coverage.threeD; else ++coverage.twoD;
  const bool ok = differing == 0 && d2hDuring == 0;
  if (!ok) ++failures;
  std::printf("  %s L2 %-16s %-2s %-13s a=%-5.3g cells=%-6zu differing=%-4zu maxAbs=%-10.3g "
              "maxRel=%-10.3g scale=%.4g\n",
              ok ? "PASS" : "FAIL", meshName.c_str(),
              component == 0 ? "U" : (component == 1 ? "V" : "W"),
              scheme == 0 ? "upwind" : (scheme == 1 ? "central" : (scheme == 2 ? "linear_upwind" : "quick")),
              alpha, mesh.numberOfCells(), differing, maxAbs, maxRel, scale);
  if (d2hDuring != 0) {
    std::printf("       device->host copies during the response step: %llu (must be 0)\n",
                static_cast<unsigned long long>(d2hDuring));
  }
}

void runMesh(const std::string& name, const Mesh& mesh) {
  const std::vector<DiagonalCase> diagonals = {
      {"uniform 1.0", uniformDiagonal},
      {"uniform 3.7", uniformOddDiagonal},
      {"nonuniform", nonUniformDiagonal},
      {"small 1e-300", smallDiagonal},
      {"large 1e300", largeDiagonal},
      {"denormal-adjacent", denormalAdjacentDiagonal}};
  for (const auto& d : diagonals) runSynthetic(name, mesh, d);

  const Index maxComponent = mesh.dimension() == 3 ? 3 : 2;
  const std::vector<Real> alphas = {1.0, 0.7, 0.3, 0.05};
  coverage.alphaValues = static_cast<int>(alphas.size());
  for (Index component = 0; component < maxComponent; ++component) {
    for (const Index scheme : {0, 1, 2, 3}) {
      for (const Real alpha : alphas) {
        runIntegrated(name, mesh, component, scheme, alpha);
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001G: CUDA momentum response coefficient vs CPU (bitwise) ===\n");
  std::printf("CPU reference: pressure_velocity::computeMomentumResponseCoefficient, d = V/aP\n");
  std::printf("L1 synthetic diagonals | L2 CPU assembly->response vs CUDA assembly->response\n\n");

  if (quick) {
    runMesh("distorted q16", q16());
    runMesh("warped 3d 3", warped3D(3));
  } else {
    runMesh("cartesian2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0));
    runMesh("cartesian2d 24", MeshGeometry::createCartesian2D(24, 24, 1.0, 1.0));
    runMesh("graded2d 12",
            MeshGeometry::createGraded2D(
                12, 12, 1.0, 1.0,
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.15,
                                       cfd::mesh::GradingCluster::Start},
                cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, 1.08,
                                       cfd::mesh::GradingCluster::Both}));
    runMesh("distorted q16", q16());
    runMesh("cartesian3d 4", MeshGeometry::createCartesian3D(4, 4, 4, 1.0, 1.0, 1.0));
    runMesh("warped 3d 3", warped3D(3));
  }

  std::printf("\n=== coverage ===\n");
  std::printf("  component U/V/W integrated cases  %d / %d / %d\n", coverage.component[0],
              coverage.component[1], coverage.component[2]);
  std::printf("  2D / 3D integrated cases          %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  relaxation factors                %d (alpha = 1.0, 0.7, 0.3, 0.05)\n",
              coverage.alphaValues);
  std::printf("  uniform / nonuniform diagonals    %d / %d\n", coverage.uniformDiagonal,
              coverage.nonUniformDiagonal);
  std::printf("  small / large diagonal cases      %d / %d\n", coverage.smallDiagonal,
              coverage.largeDiagonal);
  std::printf("  synthetic cases                   %d\n", syntheticCases);
  std::printf("  integrated chain cases            %d\n", integratedCases);
  std::printf("  values compared                   %zu\n", valuesCompared);
  std::printf("  bitwise-identical values          %zu\n", bitwiseValues);
  std::printf("  max absolute discrepancy          %.3g\n", globalMaxAbs);
  std::printf("  max relative discrepancy          %.3g\n", globalMaxRel);

  for (Index c = 0; c < 2; ++c) {
    if (coverage.component[c] == 0) {
      ++failures;
      std::printf("  FAIL component %d never exercised\n", static_cast<int>(c));
    }
  }
  if (!quick && coverage.component[2] == 0) {
    ++failures;
    std::printf("  FAIL the W component was never exercised on a 3D mesh\n");
  }
  if (coverage.twoD == 0 || coverage.threeD == 0) {
    ++failures;
    std::printf("  FAIL 2D and 3D were not both exercised\n");
  }
  if (coverage.smallDiagonal == 0 || coverage.largeDiagonal == 0) {
    ++failures;
    std::printf("  FAIL the valid-domain extremes were not exercised\n");
  }
  if (bitwiseValues != valuesCompared) {
    std::printf("  NOTE %zu of %zu values were not bitwise identical\n",
                valuesCompared - bitwiseValues, valuesCompared);
  }

  std::printf("\ncases=%d failures=%d\n", syntheticCases + integratedCases, failures);
  std::printf("%s\n", failures == 0 ? "MOMENTUM RESPONSE EQUIVALENCE: PASS (bitwise)"
                                    : "MOMENTUM RESPONSE EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
