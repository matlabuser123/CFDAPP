// GPU-DISC-001H gate -- CUDA predicted face flux / Rhie-Chow vs the CPU.
//
// CPU references:
//   physics::calculateMassFlux                      (the Linear path)
//   pressure_velocity::rhieChowFaceCorrection       (the correction alone)
//   pressure_velocity::rhieChowMassFlux             (the combined path)
//
// Layers:
//   L1 direct       both schemes against the CPU functions, over meshes,
//                   fields, densities and relaxation factors.
//   L2 integrated   CPU assembly -> response -> face flux, against
//                   CUDA assembly -> response -> face flux.
//   L3 invariants   orientation and conservation, checked explicitly rather
//                   than inferred from equality.
//   L4 checkerboard a field designed so the Rhie-Chow term dominates; it must
//                   fail if that term is removed.
//
// usage: face_flux_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
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
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/RhieChow.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;

namespace {

int failures = 0;
int cases = 0;
std::size_t valuesCompared = 0;
std::size_t bitwiseValues = 0;
Real globalMaxAbs = 0.0;
Real globalMaxRel = 0.0;

struct Coverage {
  int linearCases = 0, rhieChowCases = 0, correctionCases = 0;
  int twoD = 0, threeD = 0;
  int integrated = 0;
  int axisAlignedPlans = 0, generalPlans = 0;
  int orientationChecks = 0, conservationChecks = 0;
  int checkerboard = 0;
  int densities = 0, alphas = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

std::size_t compareFaces(const SurfaceField& cpu, const std::vector<Real>& gpu, Real& maxAbs,
                         Real& maxRel, Real& scale) {
  std::size_t differing = 0;
  for (std::size_t f = 0; f < cpu.size(); ++f) {
    ++valuesCompared;
    if (sameBits(cpu[f], gpu[f])) ++bitwiseValues; else ++differing;
    const Real diff = std::abs(cpu[f] - gpu[f]);
    maxAbs = std::max(maxAbs, diff);
    scale = std::max(scale, std::abs(cpu[f]));
    if (std::abs(cpu[f]) > 0.0) maxRel = std::max(maxRel, diff / std::abs(cpu[f]));
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);
  return differing;
}

std::vector<Real> pull(const cfd::gpu::DeviceBuffer<Real>& b) {
  std::vector<Real> h(static_cast<std::size_t>(b.size()));
  if (!h.empty()) b.downloadTo(h.data(), b.size());
  return h;
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
// Fields
// ---------------------------------------------------------------------------

struct VelocityCase { const char* name; Vector3 (*value)(const Vector3&); };
Vector3 zeroVelocity(const Vector3&) { return Vector3{0.0, 0.0, 0.0}; }
Vector3 uniformVelocity(const Vector3&) { return Vector3{1.25, -0.75, 0.4}; }
Vector3 nonUniformVelocity(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return Vector3{std::sin(pi * x.x) * std::cos(pi * x.y) + 0.5,
                 std::cos(pi * x.x) * std::sin(pi * x.y) - 0.25,
                 (0.3 * std::sin(pi * x.z)) + (0.2 * x.x)};
}
Vector3 reversedVelocity(const Vector3& x) {
  const Vector3 u = nonUniformVelocity(x);
  return Vector3{-u.x, -u.y, -u.z};
}

struct PressureCase { const char* name; Real (*value)(const Vector3&); };
Real uniformPressure(const Vector3&) { return 101325.0; }
Real linearPressure(const Vector3& x) { return 101325.0 - (250.0 * x.x) + (80.0 * x.y) - (30.0 * x.z); }
Real nonUniformPressure(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return 101325.0 + (500.0 * std::sin(pi * x.x) * std::cos(pi * x.y)) + (40.0 * x.z * x.z);
}
// A cell-to-cell oscillation: the classical checkerboard mode. Linear
// interpolation of its gradient is smooth while the COMPACT difference across a
// face is large, so the Rhie-Chow term -- which is exactly that difference minus
// the interpolated one -- dominates the flux here.
Real checkerboardPressure(const Vector3& x) {
  const Real i = std::floor(x.x * 16.0 + 0.5);
  const Real j = std::floor(x.y * 16.0 + 0.5);
  const Real k = std::floor(x.z * 16.0 + 0.5);
  const Real sign = std::fmod(i + j + k, 2.0) == 0.0 ? 1.0 : -1.0;
  return 101325.0 + (2000.0 * sign);
}

struct ResponseCase { const char* name; Real (*value)(const Vector3&); };
Real uniformResponse(const Vector3&) { return 0.02; }
Real nonUniformResponse(const Vector3& x) {
  return 0.005 + (0.03 * x.x) + (0.01 * x.y * x.y) + (0.004 * x.z);
}

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

// ---------------------------------------------------------------------------
// L1 -- direct
// ---------------------------------------------------------------------------

// Returns the CPU Rhie-Chow flux so the caller can compare configurations.
SurfaceField runDirect(const std::string& meshName, const Mesh& mesh,
                       const cfd::gpu::DeviceFaceFluxPlan& plan,
                       const BoundaryConditionSet& velocityBoundaries,
                       const BoundaryConditionSet& pressureBoundaries, const char* bcName,
                       const VelocityCase& velocityCase, const PressureCase& pressureCase,
                       const ResponseCase& responseCase, Real density, Real alpha, bool print) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
  VectorField velocity(nc);
  ScalarField pressure(nc), dU(nc), dV(nc), dW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    velocity[c] = velocityCase.value(x);
    pressure[c] = pressureCase.value(x);
    dU[c] = responseCase.value(x);
    dV[c] = responseCase.value(x) * 1.13;
    dW[c] = responseCase.value(x) * 0.87;
  }
  const FluidProperties fluid(density, 1.0e-3);
  const VectorField gradP =
      cfd::discretization::gradient(mesh, pressure, pressureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);

  // --- CPU ---------------------------------------------------------------
  const SurfaceField cpuLinear =
      cfd::physics::calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const SurfaceField cpuCorrection = cfd::pressure_velocity::rhieChowFaceCorrection(
      mesh, pressure, gradP, dU, dV, threeD ? &dW : nullptr, density, alpha);
  const SurfaceField cpuRhieChow = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, velocity, pressure, gradP, dU, dV, threeD ? &dW : nullptr, fluid, velocityBoundaries,
      alpha);

  // --- GPU ---------------------------------------------------------------
  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dGx, dGy, dGz, ddU, ddV, ddW;
  dPressure.uploadFrom(pressure.data(), nc);
  std::vector<Real> gx(nc), gy(nc), gz(nc);
  for (Index c = 0; c < nc; ++c) { gx[c] = gradP[c].x; gy[c] = gradP[c].y; gz[c] = gradP[c].z; }
  dGx.uploadFrom(gx.data(), nc);
  dGy.uploadFrom(gy.data(), nc);
  dGz.uploadFrom(gz.data(), nc);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);

  cfd::gpu::DeviceBuffer<Real> gpuLinear, gpuCorrection, gpuRhieChow;
  cfd::gpu::calculateMassFluxDevice(plan, deviceVelocity, density, gpuLinear);
  cfd::gpu::rhieChowFaceCorrectionDevice(plan, dPressure, dGx, dGy, dGz, ddU, ddV,
                                         threeD ? &ddW : nullptr, density, alpha, gpuCorrection);
  cfd::gpu::rhieChowMassFluxDevice(plan, deviceVelocity, dPressure, dGx, dGy, dGz, ddU, ddV,
                                   threeD ? &ddW : nullptr, density, alpha, gpuRhieChow);

  Real maxAbs = 0.0, maxRel = 0.0, scaleL = 0.0, scaleC = 0.0, scaleR = 0.0;
  const std::size_t dLinear = compareFaces(cpuLinear, pull(gpuLinear), maxAbs, maxRel, scaleL);
  const std::size_t dCorrection =
      compareFaces(cpuCorrection, pull(gpuCorrection), maxAbs, maxRel, scaleC);
  const std::size_t dRhieChow = compareFaces(cpuRhieChow, pull(gpuRhieChow), maxAbs, maxRel, scaleR);

  ++cases;
  ++coverage.linearCases;
  ++coverage.rhieChowCases;
  ++coverage.correctionCases;
  if (threeD) ++coverage.threeD; else ++coverage.twoD;
  const bool ok = dLinear == 0 && dCorrection == 0 && dRhieChow == 0;
  if (!ok) ++failures;
  if (print || !ok) {
    std::printf("  %s L1 %-14s %-9s %-11s %-11s %-11s rho=%-6.4g a=%-5.3g linear[d=%zu] "
                "corr[d=%zu] rc[d=%zu] maxAbs=%-10.3g scaleL=%-10.4g scaleC=%-10.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), bcName, velocityCase.name,
                pressureCase.name, responseCase.name, density, alpha, dLinear, dCorrection,
                dRhieChow, maxAbs, scaleL, scaleC);
  }
  return cpuRhieChow;
}

// ---------------------------------------------------------------------------
// L3 -- orientation and conservation invariants
// ---------------------------------------------------------------------------

void runInvariants(const std::string& meshName, const Mesh& mesh,
                   const cfd::gpu::DeviceFaceFluxPlan& plan,
                   const BoundaryConditionSet& velocityBoundaries,
                   const BoundaryConditionSet& pressureBoundaries) {
  const Index nc = mesh.numberOfCells();
  const Index nf = mesh.numberOfFaces();
  const bool threeD = mesh.dimension() == 3;
  VectorField velocity(nc), reversed(nc);
  ScalarField pressure(nc), dU(nc), dV(nc), dW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    velocity[c] = nonUniformVelocity(x);
    reversed[c] = reversedVelocity(x);
    pressure[c] = nonUniformPressure(x);
    dU[c] = nonUniformResponse(x);
    dV[c] = nonUniformResponse(x) * 1.13;
    dW[c] = nonUniformResponse(x) * 0.87;
  }
  const FluidProperties fluid(1.2, 1.0e-3);
  const VectorField gradP = cfd::discretization::gradient(
      mesh, pressure, pressureBoundaries, cfd::discretization::GradientScheme::GreenGauss);

  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dGx, dGy, dGz, ddU, ddV, ddW;
  dPressure.uploadFrom(pressure.data(), nc);
  std::vector<Real> gx(nc), gy(nc), gz(nc);
  for (Index c = 0; c < nc; ++c) { gx[c] = gradP[c].x; gy[c] = gradP[c].y; gz[c] = gradP[c].z; }
  dGx.uploadFrom(gx.data(), nc);
  dGy.uploadFrom(gy.data(), nc);
  dGz.uploadFrom(gz.data(), nc);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);
  cfd::gpu::DeviceBuffer<Real> gpuFlux;
  cfd::gpu::rhieChowMassFluxDevice(plan, deviceVelocity, dPressure, dGx, dGy, dGz, ddU, ddV,
                                   threeD ? &ddW : nullptr, 1.2, 0.7, gpuFlux);
  const auto flux = pull(gpuFlux);

  // (a) ONE canonical value per face: the device stores exactly nf entries, so
  //     a duplicated owner/neighbour pair cannot exist by construction. Assert
  //     the size, which is what makes that structural claim checkable.
  const bool oneValuePerFace = flux.size() == static_cast<std::size_t>(nf);

  // (b) Orientation and conservation, checked EXACTLY.
  //
  //     An earlier version of this test summed the signed interior flux over
  //     every cell and demanded the total be exactly 0.0. That is the wrong
  //     invariant: summing ~200 signed doubles leaves ~1e-14 of accumulation
  //     round-off against a magnitude of ~300, so the check failed on correct
  //     code. Relaxing it to "small" would have been weakening a threshold to
  //     pass. The real invariant is PAIRWISE and is exactly representable:
  //
  //       every interior face is visited exactly twice in the per-cell
  //       traversal -- once as owner (+F) and once as neighbour (-F) -- and
  //       (+F) + (-F) is exactly 0.0 for any finite F.
  //
  //     So conservation is verified structurally and exactly, and the global
  //     sum is reported only as a diagnostic.
  std::vector<int> ownerVisits(static_cast<std::size_t>(nf), 0);
  std::vector<int> neighbourVisits(static_cast<std::size_t>(nf), 0);
  Real interiorSum = 0.0;
  Real interiorMagnitude = 0.0;
  for (Index c = 0; c < nc; ++c) {
    for (const Index faceId : mesh.cell(c).faceIds()) {
      const auto& face = mesh.face(faceId);
      if (face.isBoundary()) continue;
      if (face.owner() == c) ++ownerVisits[faceId]; else ++neighbourVisits[faceId];
      const Real signed_ = (face.owner() == c) ? flux[faceId] : -flux[faceId];
      interiorSum += signed_;
      interiorMagnitude += std::abs(signed_);
    }
  }
  bool pairwiseExact = true;
  std::size_t interiorFaces = 0;
  for (Index f = 0; f < nf; ++f) {
    if (mesh.face(f).isBoundary()) {
      // The traversal above skips boundary faces, so both counters must be 0.
      if (ownerVisits[f] != 0 || neighbourVisits[f] != 0) pairwiseExact = false;
      continue;
    }
    ++interiorFaces;
    // exactly one owner visit and one neighbour visit ...
    if (ownerVisits[f] != 1 || neighbourVisits[f] != 1) { pairwiseExact = false; continue; }
    // ... and the two contributions cancel exactly.
    const Real owned = flux[f];
    const Real neighboured = -flux[f];
    if (!(owned + neighboured == 0.0)) pairwiseExact = false;
  }
  const bool cancels = pairwiseExact;

  // (c) Reversing the velocity must change the flux -- otherwise the
  //     orientation tests above would be vacuous.
  auto deviceReversed = uploadVelocity(reversed);
  cfd::gpu::DeviceBuffer<Real> gpuReversed;
  cfd::gpu::rhieChowMassFluxDevice(plan, deviceReversed, dPressure, dGx, dGy, dGz, ddU, ddV,
                                   threeD ? &ddW : nullptr, 1.2, 0.7, gpuReversed);
  const auto reversedFlux = pull(gpuReversed);
  bool changed = false;
  for (std::size_t f = 0; f < flux.size() && !changed; ++f) {
    if (!sameBits(flux[f], reversedFlux[f])) changed = true;
  }

  ++cases;
  ++coverage.orientationChecks;
  ++coverage.conservationChecks;
  const bool ok = oneValuePerFace && cancels && changed;
  if (!ok) ++failures;
  std::printf("  %s L3 %-14s invariants: onePerFace=%s interiorFaces=%zu "
              "pairwiseExact=%s reversalChanged=%s  [diagnostic: signed sum %.3g of "
              "|.|=%.4g]\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), oneValuePerFace ? "yes" : "NO",
              interiorFaces, cancels ? "yes" : "NO", changed ? "yes" : "NO", interiorSum,
              interiorMagnitude);
}

// ---------------------------------------------------------------------------
// L4 -- checkerboard sensitivity
// ---------------------------------------------------------------------------

void runCheckerboard(const std::string& meshName, const Mesh& mesh,
                     const cfd::gpu::DeviceFaceFluxPlan& plan,
                     const BoundaryConditionSet& velocityBoundaries,
                     const BoundaryConditionSet& pressureBoundaries) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
  VectorField velocity(nc);
  ScalarField pressure(nc), dU(nc), dV(nc), dW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    velocity[c] = uniformVelocity(x);
    pressure[c] = checkerboardPressure(x);
    dU[c] = uniformResponse(x);
    dV[c] = uniformResponse(x);
    dW[c] = uniformResponse(x);
  }
  const FluidProperties fluid(1.2, 1.0e-3);
  const VectorField gradP = cfd::discretization::gradient(
      mesh, pressure, pressureBoundaries, cfd::discretization::GradientScheme::GreenGauss);

  const SurfaceField cpuLinear =
      cfd::physics::calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const SurfaceField cpuRhieChow = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, velocity, pressure, gradP, dU, dV, threeD ? &dW : nullptr, fluid, velocityBoundaries,
      0.7);

  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dGx, dGy, dGz, ddU, ddV, ddW;
  dPressure.uploadFrom(pressure.data(), nc);
  std::vector<Real> gx(nc), gy(nc), gz(nc);
  for (Index c = 0; c < nc; ++c) { gx[c] = gradP[c].x; gy[c] = gradP[c].y; gz[c] = gradP[c].z; }
  dGx.uploadFrom(gx.data(), nc);
  dGy.uploadFrom(gy.data(), nc);
  dGz.uploadFrom(gz.data(), nc);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);
  cfd::gpu::DeviceBuffer<Real> gpuRhieChow;
  cfd::gpu::rhieChowMassFluxDevice(plan, deviceVelocity, dPressure, dGx, dGy, dGz, ddU, ddV,
                                   threeD ? &ddW : nullptr, 1.2, 0.7, gpuRhieChow);

  Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
  const std::size_t differing = compareFaces(cpuRhieChow, pull(gpuRhieChow), maxAbs, maxRel, scale);

  // The whole point: the Rhie-Chow term must be LARGE here relative to the
  // plain linear flux, so removing it cannot pass unnoticed.
  Real maxTerm = 0.0, maxLinear = 0.0;
  for (std::size_t f = 0; f < cpuLinear.size(); ++f) {
    maxTerm = std::max(maxTerm, std::abs(cpuRhieChow[f] - cpuLinear[f]));
    maxLinear = std::max(maxLinear, std::abs(cpuLinear[f]));
  }
  const bool termDominates = maxTerm > maxLinear;

  ++cases;
  ++coverage.checkerboard;
  const bool ok = differing == 0 && termDominates;
  if (!ok) ++failures;
  std::printf("  %s L4 %-14s checkerboard: differing=%zu  |RhieChow term|max=%.4g vs "
              "|linear flux|max=%.4g (term dominates: %s)\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), differing, maxTerm, maxLinear,
              termDominates ? "yes" : "NO");
}

// ---------------------------------------------------------------------------
// L2 -- the integrated chain
// ---------------------------------------------------------------------------

void runIntegrated(const std::string& meshName, const Mesh& mesh, Index scheme, Real alpha) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
  const BoundaryConditionSet velocityBoundaries = allFive(mesh);
  const BoundaryConditionSet pressureBoundaries = pressureSet(mesh);

  VectorField velocity(nc);
  ScalarField pressure(nc), viscosity(nc), previousU(nc), previousV(nc), previousW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    velocity[c] = nonUniformVelocity(x);
    pressure[c] = nonUniformPressure(x);
    viscosity[c] = 1.0e-3 + (4.0e-3 * x.x);
    previousU[c] = velocity[c].x - 0.1;
    previousV[c] = velocity[c].y + 0.05;
    previousW[c] = velocity[c].z - 0.02;
  }
  SurfaceField massFlux(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    const Vector3 cc = face.centroid();
    const Vector3 u{-(cc.y - 0.5), cc.x - 0.5, 0.1 * cc.z};
    massFlux[f] = dot(u, face.areaVector());
  }
  const FluidProperties fluid(1.2, 1.0e-3);

  // --- CPU chain ---------------------------------------------------------
  const auto assemble = [&](Index component, const ScalarField& previous) {
    return cfd::pressure_velocity::assembleRelaxedMomentumComponent(
        mesh, velocity, pressure, massFlux, viscosity, velocityBoundaries, pressureBoundaries,
        static_cast<cfd::physics::VelocityComponent>(component), previous, alpha, nullptr, nullptr,
        static_cast<cfd::discretization::ConvectionScheme>(scheme));
  };
  const auto uA = assemble(0, previousU);
  const auto vA = assemble(1, previousV);
  const ScalarField cpuDU =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, uA.diagonal);
  const ScalarField cpuDV =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, vA.diagonal);
  ScalarField cpuDW;
  if (threeD) {
    const auto wA = assemble(2, previousW);
    cpuDW = cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, wA.diagonal);
  }
  const VectorField gradP = cfd::discretization::gradient(
      mesh, pressure, pressureBoundaries, cfd::discretization::GradientScheme::GreenGauss);
  const SurfaceField cpuFlux = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, velocity, pressure, gradP, cpuDU, cpuDV, threeD ? &cpuDW : nullptr, fluid,
      velocityBoundaries, alpha);

  // --- GPU chain, device-resident throughout ------------------------------
  cfd::gpu::DeviceMomentumAssemblyPlan momentumPlan;
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;
  if (!momentumPlan.build(mesh, velocityBoundaries, pressureBoundaries) ||
      !fluxPlan.build(mesh, velocityBoundaries)) {
    ++failures;
    std::printf("  FAIL L2 %-14s plan unusable\n", meshName.c_str());
    return;
  }
  auto deviceVelocity = uploadVelocity(velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dViscosity, dFlux, dPrevU, dPrevV, dPrevW;
  dPressure.uploadFrom(pressure.data(), nc);
  dViscosity.uploadFrom(viscosity.data(), nc);
  dFlux.uploadFrom(massFlux.data(), static_cast<Index>(massFlux.size()));
  dPrevU.uploadFrom(previousU.data(), nc);
  dPrevV.uploadFrom(previousV.data(), nc);
  dPrevW.uploadFrom(previousW.data(), nc);

  const auto deviceResponse = [&](Index component, cfd::gpu::DeviceBuffer<Real>& previous,
                                  cfd::gpu::DeviceBuffer<Real>& out) {
    cfd::gpu::MomentumAssemblyOptions options;
    options.component = component;
    options.convectionScheme = scheme;
    options.relaxationAlpha = alpha;
    cfd::gpu::DeviceMomentumSystem system;
    cfd::gpu::assembleRelaxedMomentumDevice(momentumPlan, deviceVelocity, dPressure, dViscosity,
                                            dFlux, previous, nullptr, options, system);
    cfd::gpu::computeMomentumResponseCoefficientDevice(momentumPlan.convectionPlan().mesh(),
                                                       system.diagonal, out);
  };
  cfd::gpu::DeviceBuffer<Real> gpuDU, gpuDV, gpuDW;
  deviceResponse(0, dPrevU, gpuDU);
  deviceResponse(1, dPrevV, gpuDV);
  if (threeD) deviceResponse(2, dPrevW, gpuDW);

  cfd::gpu::DeviceBuffer<Real> dGx, dGy, dGz;
  std::vector<Real> gx(nc), gy(nc), gz(nc);
  for (Index c = 0; c < nc; ++c) { gx[c] = gradP[c].x; gy[c] = gradP[c].y; gz[c] = gradP[c].z; }
  dGx.uploadFrom(gx.data(), nc);
  dGy.uploadFrom(gy.data(), nc);
  dGz.uploadFrom(gz.data(), nc);
  cfd::gpu::DeviceBuffer<Real> gpuFlux;
  cfd::gpu::rhieChowMassFluxDevice(fluxPlan, deviceVelocity, dPressure, dGx, dGy, dGz, gpuDU,
                                   gpuDV, threeD ? &gpuDW : nullptr, fluid.density(), alpha,
                                   gpuFlux);

  Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
  const std::size_t differing = compareFaces(cpuFlux, pull(gpuFlux), maxAbs, maxRel, scale);
  ++cases;
  ++coverage.integrated;
  if (differing != 0) ++failures;
  std::printf("  %s L2 %-14s scheme=%d a=%-5.3g chain differing=%-4zu maxAbs=%-10.3g "
              "maxRel=%-10.3g scale=%.4g\n",
              differing == 0 ? "PASS" : "FAIL", meshName.c_str(), static_cast<int>(scheme), alpha,
              differing, maxAbs, maxRel, scale);
}

void runMesh(const std::string& name, const Mesh& mesh, bool verbose) {
  const BoundaryConditionSet wallSet = allWall(mesh);
  const BoundaryConditionSet fiveSet = allFive(mesh);
  const BoundaryConditionSet pSet = pressureSet(mesh);

  cfd::gpu::DeviceFaceFluxPlan planWall, planFive;
  if (!planWall.build(mesh, wallSet) || !planFive.build(mesh, fiveSet)) {
    ++failures;
    std::printf("  FAIL %-14s face-flux plan unusable\n", name.c_str());
    return;
  }
  if (planFive.axisAlignedFaces() > 0) ++coverage.axisAlignedPlans;
  if (planFive.generalFaces() > 0) ++coverage.generalPlans;
  std::printf("       [%s faces=%zu axisAligned=%zu general=%zu resident=%zu B]\n", name.c_str(),
              mesh.numberOfFaces(), static_cast<std::size_t>(planFive.axisAlignedFaces()),
              static_cast<std::size_t>(planFive.generalFaces()), planFive.residentBytes());

  const std::vector<VelocityCase> velocities = {{"zero", zeroVelocity},
                                                {"uniform", uniformVelocity},
                                                {"nonuniform", nonUniformVelocity},
                                                {"reversed", reversedVelocity}};
  const std::vector<PressureCase> pressures = {{"uniform", uniformPressure},
                                               {"linear", linearPressure},
                                               {"nonuniform", nonUniformPressure}};
  const std::vector<ResponseCase> responses = {{"uniform", uniformResponse},
                                               {"nonuniform", nonUniformResponse}};
  const std::vector<Real> densities = {1.0, 1.2, 998.2};
  const std::vector<Real> alphas = {1.0, 0.7, 0.3};
  coverage.densities = static_cast<int>(densities.size());
  coverage.alphas = static_cast<int>(alphas.size());

  for (const auto& v : velocities) {
    for (const auto& p : pressures) {
      for (const auto& r : responses) {
        for (const Real density : densities) {
          for (const Real alpha : alphas) {
            runDirect(name, mesh, planWall, wallSet, pSet, "wall", v, p, r, density, alpha,
                      verbose);
            runDirect(name, mesh, planFive, fiveSet, pSet, "all-five", v, p, r, density, alpha,
                      verbose);
          }
        }
      }
    }
  }

  runInvariants(name, mesh, planFive, fiveSet, pSet);
  runCheckerboard(name, mesh, planFive, fiveSet, pSet);
  for (const Index scheme : {0, 3}) {
    for (const Real alpha : {1.0, 0.7}) runIntegrated(name, mesh, scheme, alpha);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001H: CUDA predicted face flux / Rhie-Chow vs CPU (bitwise) ===\n");
  std::printf("CPU refs: physics::calculateMassFlux, pressure_velocity::rhieChow{FaceCorrection,"
              "MassFlux}\n\n");

  if (quick) {
    runMesh("distorted q16", q16(), false);
    runMesh("warped 3d 3", warped3D(3), false);
  } else {
    runMesh("cartesian2d 8", MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0), false);
    runMesh("cartesian2d 16", MeshGeometry::createCartesian2D(16, 16, 1.0, 1.0), false);
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
  std::printf("  Linear-path cases                 %d\n", coverage.linearCases);
  std::printf("  Rhie-Chow-path cases              %d\n", coverage.rhieChowCases);
  std::printf("  correction-only cases             %d\n", coverage.correctionCases);
  std::printf("  2D / 3D direct cases              %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  integrated chain cases            %d\n", coverage.integrated);
  std::printf("  meshes with axis-aligned coupling %d\n", coverage.axisAlignedPlans);
  std::printf("  meshes with general coupling      %d\n", coverage.generalPlans);
  std::printf("  densities / relaxation factors    %d / %d\n", coverage.densities,
              coverage.alphas);
  std::printf("  orientation / conservation checks %d / %d\n", coverage.orientationChecks,
              coverage.conservationChecks);
  std::printf("  checkerboard-sensitive cases      %d\n", coverage.checkerboard);
  std::printf("  values compared                   %zu\n", valuesCompared);
  std::printf("  bitwise-identical values          %zu\n", bitwiseValues);
  std::printf("  max absolute discrepancy          %.3g\n", globalMaxAbs);
  std::printf("  max relative discrepancy          %.3g\n", globalMaxRel);

  if (coverage.twoD == 0 || coverage.threeD == 0) {
    ++failures;
    std::printf("  FAIL 2D and 3D were not both exercised\n");
  }
  // Only the full run includes a Cartesian mesh; --quick deliberately uses two
  // distorted meshes, which have no axis-aligned faces at all, so this coverage
  // requirement applies to the full run only.
  if (!quick && (coverage.axisAlignedPlans == 0 || coverage.generalPlans == 0)) {
    ++failures;
    std::printf("  FAIL both coupling branches were not exercised\n");
  }
  if (coverage.checkerboard == 0 || coverage.integrated == 0) {
    ++failures;
    std::printf("  FAIL the checkerboard or integrated layer never ran\n");
  }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "FACE FLUX EQUIVALENCE: PASS (bitwise)"
                                    : "FACE FLUX EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
