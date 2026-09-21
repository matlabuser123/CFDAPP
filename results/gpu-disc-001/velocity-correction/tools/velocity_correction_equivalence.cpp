// GPU-DISC-001J gate -- CUDA cell-velocity correction vs the CPU.
//
// CPU references:
//   pressure_velocity::correctVelocity            (the correction itself)
//   discretization::leastSquaresGradient          (the second gradient operator
//                                                  correctVelocity can forward to)
//
// Layers:
//   L1 direct      correctVelocity against the CPU, over meshes, p' fields,
//                  response fields, pressure BC sets and BOTH gradient schemes.
//                  The p' gradient, the per-component increment and the final
//                  U/V/W are compared INDEPENDENTLY, so a sign error localises.
//   L2 gradient    leastSquaresGradient on its own -- a new operator on the
//                  device, qualified in isolation and not only through L1.
//   L3 invariants  zero/uniform p', zero response, orthogonal-component
//                  isolation, the 2D W contract, and correction direction --
//                  each stated only where the CPU makes it exactly true.
//   L4a controlled one real solved p' fed to BOTH correction paths (bitwise).
//   L4b chain      CPU assembly -> CPU solve -> CPU correct against
//                  CUDA assembly -> GPU solve -> CUDA correct (solver-limited).
//
// usage: velocity_correction_equivalence [--quick]

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
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
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/Field.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceLeastSquaresGradient.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/gpu/DevicePressureCorrection.hpp"
#include "cfd/gpu/DeviceVelocityCorrection.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshMotion.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/PressureCorrectionEquation.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/RhieChow.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::GradientScheme;
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
  int twoD = 0, threeD = 0;
  int greenGauss = 0, leastSquares = 0;
  int lsGradientCases = 0;
  int lsFallbackCells = 0;
  int lsObliqueEntries = 0;
  int lsThreeDCells = 0;
  int pinnedReference = 0;
  int invariants = 0;
  int controlled = 0;
  int chain = 0;
  int nonZeroPredictorZIn2D = 0;
  int closureExactCells = 0;
  int twoDWithWResponse = 0;
  int i2Exact = 0;
  int signChecks = 0;
  int orthogonalClaims = 0;
  int reuseSteps = 0;
};
Coverage coverage;

bool sameBits(Real a, Real b) { return std::memcmp(&a, &b, sizeof(Real)) == 0; }

std::vector<Real> pull(const cfd::gpu::DeviceBuffer<Real>& b) {
  std::vector<Real> h(static_cast<std::size_t>(b.size()));
  if (!h.empty()) b.downloadTo(h.data(), b.size());
  return h;
}

void track(Real want, Real got, Real& maxAbs, Real& maxRel, Real& scale) {
  ++valuesCompared;
  if (sameBits(want, got)) ++bitwiseValues;
  const Real diff = std::abs(want - got);
  maxAbs = std::max(maxAbs, diff);
  scale = std::max(scale, std::abs(want));
  if (std::abs(want) > 0.0) maxRel = std::max(maxRel, diff / std::abs(want));
}

cfd::gpu::DeviceVelocity uploadVelocity(const VectorField& v) {
  cfd::gpu::DeviceVelocity d;
  const Index n = static_cast<Index>(v.size());
  std::vector<Real> x(n), y(n), z(n);
  for (Index c = 0; c < n; ++c) { x[c] = v[c].x; y[c] = v[c].y; z[c] = v[c].z; }
  d.x.uploadFrom(x.data(), n);
  d.y.uploadFrom(y.data(), n);
  d.z.uploadFrom(z.data(), n);
  return d;
}

// --- meshes ---------------------------------------------------------------
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

// --- pressure-correction fields -------------------------------------------
struct PPrimeCase { const char* name; Real (*value)(const Vector3&); };
Real pZero(const Vector3&) { return 0.0; }
Real pUniform(const Vector3&) { return 7.25; }
Real pLinearX(const Vector3& x) { return 3.5 * x.x; }
Real pLinearY(const Vector3& x) { return -2.75 * x.y; }
Real pLinearZ(const Vector3& x) { return 1.875 * x.z; }
Real pNonuniform(const Vector3& x) {
  const Real pi = cfd::constants::pi;
  return (12.0 * std::sin(pi * x.x) * std::cos(pi * x.y)) + (4.0 * x.z * x.z) - (0.5 * x.x * x.y);
}
Real pNegative(const Vector3& x) { return -pNonuniform(x); }
// Near the top of the double range. Exercises the whole operator at an extreme
// scale -- the normal equations, the determinant and the quotient all carry
// ~1e305 magnitudes -- which is where a difference in operand order or grouping
// would show first. It does NOT overflow: the weighted products stay finite on
// these meshes, so the solve's isfinite backstop is not reached (see summary.md
// on the fallback branch's reachability).
Real pExtremeScale(const Vector3& x) { return 1.0e305 * (1.0 + x.x + (2.0 * x.y) + (3.0 * x.z)); }

// --- response coefficients -------------------------------------------------
struct ResponseCase { const char* name; Real (*value)(const Vector3&, int); };
Real respZero(const Vector3&, int) { return 0.0; }
Real respUniform(const Vector3&, int) { return 0.02; }
Real respNonuniform(const Vector3& x, int component) {
  const Real base = 0.004 + (0.03 * x.x) + (0.012 * x.y * x.y) + (0.008 * x.z);
  return component == 0 ? base : (component == 1 ? base * 0.63 : base * 1.41);
}

// --- pressure boundary sets -----------------------------------------------
BoundaryConditionSet allNeumannPressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  for (const auto& p : mesh.boundaryPatches())
    s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  return s;
}
BoundaryConditionSet mixedPressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    if (i % 2 == 0) s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedValue>(101325.0));
    else s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    ++i;
  }
  return s;
}
BoundaryConditionSet allFixedValuePressure(const Mesh& mesh) {
  BoundaryConditionSet s;
  for (const auto& p : mesh.boundaryPatches())
    s.set(mesh, p.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
  return s;
}
struct PressureBcCase { const char* name; BoundaryConditionSet (*build)(const Mesh&); };

VectorField makePredictor(const Mesh& mesh) {
  const Index nc = mesh.numberOfCells();
  VectorField v(nc);
  const Real pi = cfd::constants::pi;
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    // A deliberately NON-ZERO z even on a 2D mesh: the CPU discards it
    // (Vector2{x,y} zeroes z) and a port that preserves it is invisible
    // against a predictor whose z is already zero.
    v[c] = Vector3{(std::sin(pi * x.x) * std::cos(pi * x.y)) + 0.5,
                   (std::cos(pi * x.x) * std::sin(pi * x.y)) - 0.25,
                   0.375 + (0.2 * x.x) - (0.1 * x.y) + (0.3 * std::sin(pi * x.z))};
  }
  return v;
}

// ---------------------------------------------------------------------------
// L1 -- direct
// ---------------------------------------------------------------------------

void runDirect(const std::string& meshName, const Mesh& mesh,
               const cfd::gpu::DeviceVelocityCorrectionPlan& plan, const PressureBcCase& bcCase,
               const BoundaryConditionSet& pressureBoundaries, const PPrimeCase& pCase,
               const ResponseCase& responseCase, GradientScheme scheme, bool passWOn2D,
               bool print) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
  // The CPU branches on the W-response POINTER, not on mesh.dimension(): a 2D
  // mesh handed a W response legally takes the 3D branch and keeps a corrected
  // z. `passWOn2D` exercises exactly that.
  const bool passW = threeD || passWOn2D;

  const VectorField predictor = makePredictor(mesh);
  ScalarField pPrime(nc), dU(nc), dV(nc), dW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    pPrime[c] = pCase.value(x);
    dU[c] = responseCase.value(x, 0);
    dV[c] = responseCase.value(x, 1);
    dW[c] = responseCase.value(x, 2);
  }
  if (!threeD) {
    bool anyZ = false;
    for (Index c = 0; c < nc && !anyZ; ++c) anyZ = predictor[c].z != 0.0;
    if (anyZ) ++coverage.nonZeroPredictorZIn2D;
  }

  const ScalarField* cpuW = passW ? &dW : nullptr;
  const VectorField cpuCorrected = cfd::pressure_velocity::correctVelocity(
      mesh, predictor, dU, dV, pPrime, pressureBoundaries, scheme, cpuW);
  // The p' gradient the CPU used, recomputed with the same boundary set so it
  // can be compared on its own rather than inferred from the velocity.
  BoundaryConditionSet pPrimeBoundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (pressureBoundaries.get(patch.name()).type() ==
        cfd::boundary::BoundaryConditionType::FixedValue) {
      pPrimeBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
    } else {
      pPrimeBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }
  const VectorField cpuGradient =
      cfd::discretization::gradient(mesh, pPrime, pPrimeBoundaries, scheme);

  auto devicePredictor = uploadVelocity(predictor);
  cfd::gpu::DeviceBuffer<Real> dPPrime, ddU, ddV, ddW;
  dPPrime.uploadFrom(pPrime.data(), nc);
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);
  cfd::gpu::DeviceBuffer<Real> gx, gy, gz;
  cfd::gpu::DeviceVelocity corrected;
  cfd::gpu::correctVelocityDevice(
      plan, devicePredictor, ddU, ddV, passW ? &ddW : nullptr, dPPrime,
      scheme == GradientScheme::LeastSquares ? cfd::gpu::kGradientSchemeLeastSquares
                                             : cfd::gpu::kGradientSchemeGreenGauss,
      gx, gy, gz, corrected);

  const auto hgx = pull(gx), hgy = pull(gy), hgz = pull(gz);
  const auto hx = pull(corrected.x), hy = pull(corrected.y), hz = pull(corrected.z);

  Real maxAbs = 0.0, maxRel = 0.0, scaleG = 0.0, scaleU = 0.0, scaleI = 0.0;
  std::size_t dGrad = 0, dIncrement = 0, dU_ = 0, dV_ = 0, dW_ = 0;
  for (Index c = 0; c < nc; ++c) {
    if (!sameBits(cpuGradient[c].x, hgx[c])) ++dGrad;
    if (!sameBits(cpuGradient[c].y, hgy[c])) ++dGrad;
    if (!sameBits(cpuGradient[c].z, hgz[c])) ++dGrad;
    track(cpuGradient[c].x, hgx[c], maxAbs, maxRel, scaleG);
    track(cpuGradient[c].y, hgy[c], maxAbs, maxRel, scaleG);
    track(cpuGradient[c].z, hgz[c], maxAbs, maxRel, scaleG);

    // Per-component increment, formed the same way on both sides so it is
    // exactly comparable and localises a sign flip to the correction rather
    // than to the gradient.
    const Real incCpuX = predictor[c].x - cpuCorrected[c].x;
    const Real incGpuX = predictor[c].x - hx[c];
    const Real incCpuY = predictor[c].y - cpuCorrected[c].y;
    const Real incGpuY = predictor[c].y - hy[c];
    const Real incCpuZ = predictor[c].z - cpuCorrected[c].z;
    const Real incGpuZ = predictor[c].z - hz[c];
    if (!sameBits(incCpuX, incGpuX)) ++dIncrement;
    if (!sameBits(incCpuY, incGpuY)) ++dIncrement;
    if (!sameBits(incCpuZ, incGpuZ)) ++dIncrement;
    track(incCpuX, incGpuX, maxAbs, maxRel, scaleI);
    track(incCpuY, incGpuY, maxAbs, maxRel, scaleI);
    track(incCpuZ, incGpuZ, maxAbs, maxRel, scaleI);

    if (!sameBits(cpuCorrected[c].x, hx[c])) ++dU_;
    if (!sameBits(cpuCorrected[c].y, hy[c])) ++dV_;
    if (!sameBits(cpuCorrected[c].z, hz[c])) ++dW_;
    track(cpuCorrected[c].x, hx[c], maxAbs, maxRel, scaleU);
    track(cpuCorrected[c].y, hy[c], maxAbs, maxRel, scaleU);
    track(cpuCorrected[c].z, hz[c], maxAbs, maxRel, scaleU);
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);

  if (threeD) ++coverage.threeD; else ++coverage.twoD;
  if (scheme == GradientScheme::LeastSquares) ++coverage.leastSquares;
  else ++coverage.greenGauss;

  ++cases;
  const bool ok = dGrad == 0 && dIncrement == 0 && dU_ == 0 && dV_ == 0 && dW_ == 0;
  if (!ok) ++failures;
  if (print || !ok) {
    std::printf("  %s %-14s %-14s %-11s %-11s %-12s grad[d=%zu] inc[d=%zu] u[d=%zu] v[d=%zu] "
                "w[d=%zu] maxAbs=%-10.3g scaleG=%-10.4g scaleU=%-10.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), bcCase.name, pCase.name,
                responseCase.name,
                scheme == GradientScheme::LeastSquares ? "least_squares" : "green_gauss", dGrad,
                dIncrement, dU_, dV_, dW_, maxAbs, scaleG, scaleU);
  }
}

// ---------------------------------------------------------------------------
// L1b -- REUSED output buffers
//
// A real caller (SIMPLE's outer loop) hands the same gradient and velocity
// buffers to every iteration. Every other layer here allocates fresh ones, so a
// port that reused a stale gradient instead of recomputing it would agree with
// the CPU everywhere and still be wrong in the solver. This layer calls the
// device twice with ONE set of buffers and two different p' fields, and
// requires the second result to match the CPU for the second field.
// ---------------------------------------------------------------------------

void runBufferReuse(const std::string& meshName, const Mesh& mesh,
                    const cfd::gpu::DeviceVelocityCorrectionPlan& plan,
                    const BoundaryConditionSet& pressureBoundaries, GradientScheme scheme) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
  const VectorField predictor = makePredictor(mesh);
  auto devicePredictor = uploadVelocity(predictor);
  ScalarField dU(nc), dV(nc), dW(nc);
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    dU[c] = respNonuniform(x, 0);
    dV[c] = respNonuniform(x, 1);
    dW[c] = respNonuniform(x, 2);
  }
  cfd::gpu::DeviceBuffer<Real> ddU, ddV, ddW;
  ddU.uploadFrom(dU.data(), nc);
  ddV.uploadFrom(dV.data(), nc);
  ddW.uploadFrom(dW.data(), nc);

  // ONE set of outputs, reused across both calls.
  cfd::gpu::DeviceBuffer<Real> gx, gy, gz;
  cfd::gpu::DeviceVelocity corrected;
  const Index deviceScheme = scheme == GradientScheme::LeastSquares
                                 ? cfd::gpu::kGradientSchemeLeastSquares
                                 : cfd::gpu::kGradientSchemeGreenGauss;

  std::size_t differing = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
  const PPrimeCase sequence[] = {{"linear x", pLinearX}, {"nonuniform", pNonuniform},
                                 {"negative", pNegative}, {"zero", pZero}};
  for (const auto& field : sequence) {
    ScalarField pPrime(nc);
    for (Index c = 0; c < nc; ++c) pPrime[c] = field.value(mesh.cell(c).centroid());
    const VectorField want = cfd::pressure_velocity::correctVelocity(
        mesh, predictor, dU, dV, pPrime, pressureBoundaries, scheme, threeD ? &dW : nullptr);
    cfd::gpu::DeviceBuffer<Real> dP;
    dP.uploadFrom(pPrime.data(), nc);
    cfd::gpu::correctVelocityDevice(plan, devicePredictor, ddU, ddV, threeD ? &ddW : nullptr, dP,
                                    deviceScheme, gx, gy, gz, corrected);
    const auto hx = pull(corrected.x), hy = pull(corrected.y), hz = pull(corrected.z);
    for (Index c = 0; c < nc; ++c) {
      if (!sameBits(want[c].x, hx[c])) ++differing;
      if (!sameBits(want[c].y, hy[c])) ++differing;
      if (!sameBits(want[c].z, hz[c])) ++differing;
      track(want[c].x, hx[c], maxAbs, maxRel, scale);
      track(want[c].y, hy[c], maxAbs, maxRel, scale);
      track(want[c].z, hz[c], maxAbs, maxRel, scale);
    }
    ++coverage.reuseSteps;
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);
  ++cases;
  const bool ok = differing == 0;
  if (!ok) ++failures;
  std::printf("  %s L1b %-14s %-12s reused buffers, 4 successive p' differing=%zu maxAbs=%-10.3g "
              "scale=%.4g\n",
              ok ? "PASS" : "FAIL", meshName.c_str(),
              scheme == GradientScheme::LeastSquares ? "least_squares" : "green_gauss", differing,
              maxAbs, scale);
}

// ---------------------------------------------------------------------------
// L2 -- the least-squares gradient on its own
// ---------------------------------------------------------------------------

void runLeastSquaresGradient(const std::string& meshName, const Mesh& mesh, const char* bcName,
                             const BoundaryConditionSet& boundaries) {
  cfd::gpu::DeviceLeastSquaresGradientPlan plan;
  if (!plan.build(mesh, boundaries)) {
    ++failures;
    std::printf("  FAIL L2 %-14s %-14s plan unusable: %s\n", meshName.c_str(), bcName,
                plan.unsupportedReason().c_str());
    return;
  }
  coverage.lsFallbackCells += static_cast<int>(plan.illConditionedCells());
  coverage.lsObliqueEntries += static_cast<int>(plan.obliqueEntries());
  coverage.lsThreeDCells += static_cast<int>(plan.threeDimensionalCells());

  const Index nc = mesh.numberOfCells();
  // The last field runs the operator at ~1e305, where any difference in
  // operand order or grouping inside the normal equations would show first.
  const PPrimeCase fields[] = {{"zero", pZero},         {"uniform", pUniform},
                               {"linear x", pLinearX},  {"linear y", pLinearY},
                               {"linear z", pLinearZ},  {"nonuniform", pNonuniform},
                               {"negative", pNegative}, {"extreme scale", pExtremeScale}};
  std::size_t differing = 0;
  Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
  for (const auto& field : fields) {
    ScalarField phi(nc);
    for (Index c = 0; c < nc; ++c) phi[c] = field.value(mesh.cell(c).centroid());
    const VectorField want =
        cfd::discretization::gradient(mesh, phi, boundaries, GradientScheme::LeastSquares);
    cfd::gpu::DeviceBuffer<Real> dPhi, gx, gy, gz;
    dPhi.uploadFrom(phi.data(), nc);
    cfd::gpu::leastSquaresGradientDevice(plan, dPhi, gx, gy, gz);
    const auto hgx = pull(gx), hgy = pull(gy), hgz = pull(gz);
    for (Index c = 0; c < nc; ++c) {
      if (!sameBits(want[c].x, hgx[c])) ++differing;
      if (!sameBits(want[c].y, hgy[c])) ++differing;
      if (!sameBits(want[c].z, hgz[c])) ++differing;
      track(want[c].x, hgx[c], maxAbs, maxRel, scale);
      track(want[c].y, hgy[c], maxAbs, maxRel, scale);
      track(want[c].z, hgz[c], maxAbs, maxRel, scale);
    }
    ++coverage.lsGradientCases;
  }
  globalMaxAbs = std::max(globalMaxAbs, maxAbs);
  globalMaxRel = std::max(globalMaxRel, maxRel);
  ++cases;
  const bool ok = differing == 0;
  if (!ok) ++failures;
  std::printf("  %s L2 %-14s %-14s least-squares gradient differing=%zu fallbackCells=%zu "
              "obliqueEntries=%zu 3dCells=%zu skipped=%zu maxAbs=%-10.3g scale=%.4g\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), bcName, differing,
              static_cast<std::size_t>(plan.illConditionedCells()),
              static_cast<std::size_t>(plan.obliqueEntries()),
              static_cast<std::size_t>(plan.threeDimensionalCells()),
              static_cast<std::size_t>(plan.skippedEntries()), maxAbs, scale);
}

// ---------------------------------------------------------------------------
// L3 -- invariants, each stated only where the CPU makes it EXACTLY true
// ---------------------------------------------------------------------------

void runInvariants(const std::string& meshName, const Mesh& mesh,
                   const cfd::gpu::DeviceVelocityCorrectionPlan& plan, GradientScheme scheme) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
  const bool cartesian = meshName.rfind("cartesian", 0) == 0;
  const bool leastSquares = scheme == GradientScheme::LeastSquares;
  const VectorField predictor = makePredictor(mesh);
  auto devicePredictor = uploadVelocity(predictor);
  const Index deviceScheme = leastSquares ? cfd::gpu::kGradientSchemeLeastSquares
                                          : cfd::gpu::kGradientSchemeGreenGauss;

  const auto correct = [&](const ScalarField& pPrime, const ScalarField& du,
                           const ScalarField& dv, const ScalarField& dw,
                           std::vector<Real>& ox, std::vector<Real>& oy, std::vector<Real>& oz,
                           std::vector<Real>& ggx, std::vector<Real>& ggy, std::vector<Real>& ggz) {
    cfd::gpu::DeviceBuffer<Real> dP, ddu, ddv, ddw, gx, gy, gz;
    dP.uploadFrom(pPrime.data(), nc);
    ddu.uploadFrom(du.data(), nc);
    ddv.uploadFrom(dv.data(), nc);
    ddw.uploadFrom(dw.data(), nc);
    cfd::gpu::DeviceVelocity corrected;
    cfd::gpu::correctVelocityDevice(plan, devicePredictor, ddu, ddv, threeD ? &ddw : nullptr, dP,
                                    deviceScheme, gx, gy, gz, corrected);
    ox = pull(corrected.x); oy = pull(corrected.y); oz = pull(corrected.z);
    ggx = pull(gx); ggy = pull(gy); ggz = pull(gz);
  };

  ScalarField uniformResponse(nc), zeroResponse(nc);
  for (Index c = 0; c < nc; ++c) { uniformResponse[c] = 0.02; zeroResponse[c] = 0.0; }

  std::size_t e1 = 0, e2 = 0, e3 = 0, e4 = 0, e5 = 0, e6 = 0;
  const char* i2Applicability = "n/a";
  std::vector<Real> ox, oy, oz, ggx, ggy, ggz;

  // I1 -- p' == 0. Every face value is 0 whichever p' condition applies
  // (FixedValue(0) gives 0; FixedGradient(0) gives phi_P + 0*d = 0), so the
  // gradient is exactly zero and x/y are untouched bitwise. Unconditional.
  {
    ScalarField zero(nc);
    correct(zero, uniformResponse, uniformResponse, uniformResponse, ox, oy, oz, ggx, ggy, ggz);
    for (Index c = 0; c < nc; ++c) {
      if (!sameBits(ggx[c], 0.0) || !sameBits(ggy[c], 0.0) || !sameBits(ggz[c], 0.0)) ++e1;
      if (!sameBits(ox[c], predictor[c].x) || !sameBits(oy[c], predictor[c].y)) ++e1;
      if (threeD) { if (!sameBits(oz[c], predictor[c].z)) ++e1; }
      else if (!sameBits(oz[c], 0.0)) ++e1;
    }
  }

  // I2 -- a UNIFORM p'. This is EXACTLY zero only under stated conditions, and
  // claiming it elsewhere would be claiming something the CPU does not do:
  //
  //   * Any FixedValue pressure patch becomes FixedValue(0.0) for p'
  //     (makeGradientBoundaries). A uniform p' = 7.25 then has a genuine JUMP
  //     to 0 at that boundary, and a non-zero gradient there is CORRECT. So
  //     the invariant only applies when no patch is FixedValue.
  //   * With that condition, least squares gives exactly zero everywhere:
  //     every value difference is exactly zero, so b is zero and g = 0/det.
  //   * Green-Gauss sums c * sum(+/-Sf) / V, exactly zero only where the area
  //     vectors close exactly AND no skew/oblique/boundary-transfer sweep runs
  //     (a sweep reintroduces the previous gradient, which is only ~0). Both
  //     are proven properties here -- plan.greenGauss().sweepsNeeded() and the
  //     per-cell closure -- not assumptions.
  if (plan.fixedValuePressurePatches() == 0) {
    ScalarField uniform(nc);
    for (Index c = 0; c < nc; ++c) uniform[c] = 7.25;
    correct(uniform, uniformResponse, uniformResponse, uniformResponse, ox, oy, oz, ggx, ggy, ggz);
    const bool sweeps = plan.greenGauss().sweepsNeeded();
    Index claimed = 0;
    for (Index c = 0; c < nc; ++c) {
      Vector3 closure{0.0, 0.0, 0.0};
      for (const Index faceId : mesh.cell(c).faceIds()) {
        const Vector3 sf = mesh.face(faceId).areaVector();
        if (mesh.face(faceId).owner() == c) closure += sf; else closure -= sf;
      }
      const bool closureExact = closure == Vector3{};
      if (closureExact) ++coverage.closureExactCells;
      const bool applies = leastSquares || (!sweeps && closureExact);
      if (!applies) continue;
      ++claimed;
      if (!sameBits(ggx[c], 0.0) || !sameBits(ggy[c], 0.0)) ++e2;
      if (!sameBits(ox[c], predictor[c].x) || !sameBits(oy[c], predictor[c].y)) ++e2;
    }
    if (claimed > 0) {
      i2Applicability = "exact";
      coverage.i2Exact += static_cast<int>(claimed);
    } else {
      i2Applicability = leastSquares ? "none" : "sweeps/closure";
    }
  } else {
    i2Applicability = "fixedvalue p'";
  }

  // I6 -- a zero response coefficient produces exactly no correction, whatever
  // the gradient is. u - (0 * g) == u for every finite g.
  {
    ScalarField pPrime(nc);
    for (Index c = 0; c < nc; ++c) pPrime[c] = pNonuniform(mesh.cell(c).centroid());
    correct(pPrime, zeroResponse, zeroResponse, zeroResponse, ox, oy, oz, ggx, ggy, ggz);
    bool gradientNonZero = false;
    for (Index c = 0; c < nc; ++c) {
      if (ggx[c] != 0.0 || ggy[c] != 0.0) gradientNonZero = true;
      if (!sameBits(ox[c], predictor[c].x) || !sameBits(oy[c], predictor[c].y)) ++e6;
      if (threeD && !sameBits(oz[c], predictor[c].z)) ++e6;
    }
    if (!gradientNonZero) ++e6;  // non-vacuity: the gradient must be live
  }

  // I3 -- orthogonal-component isolation. On a CARTESIAN mesh a p' = a*x field
  // gives exactly zero y (and z) gradient: with a uniformly-Neumann p' set,
  // every y-face -- boundary (value = phi_P + 0*d = phi_P) and interior
  // (interpolated between two cells at the same x) -- carries the same value,
  // and the two opposite ones have exactly opposite area components.
  //
  // The uniformly-Neumann condition is NOT incidental. A FixedValue pressure
  // patch becomes FixedValue(0.0) for p', so a y-normal patch then holds 0
  // while the interior holds 3.5x: the boundary cells acquire a REAL y
  // gradient, and claiming zero there would be claiming something false. Gated
  // on the same proven property as I2.
  //
  // I5 -- direction. The correction is u - d*(dp'/dx) with d > 0, so wherever
  // the increment is non-zero its sign must MATCH the gradient's. Stated on
  // the sign of the increment rather than on `corrected < predictor`, because
  // a small enough positive gradient makes d*g underflow to exactly zero and
  // leaves the velocity untouched -- which is correct, not a sign error.
  {
    ScalarField pPrime(nc);
    for (Index c = 0; c < nc; ++c) pPrime[c] = 3.5 * mesh.cell(c).centroid().x;
    correct(pPrime, uniformResponse, uniformResponse, uniformResponse, ox, oy, oz, ggx, ggy, ggz);
    Index signChecked = 0;
    bool uMoved = false;
    const bool orthogonalClaimApplies = cartesian && plan.fixedValuePressurePatches() == 0;
    for (Index c = 0; c < nc; ++c) {
      if (orthogonalClaimApplies) {
        if (!sameBits(ggy[c], 0.0)) ++e3;
        if (!sameBits(oy[c], predictor[c].y)) ++e3;
        if (threeD && !sameBits(ggz[c], 0.0)) ++e3;
        if (threeD && !sameBits(oz[c], predictor[c].z)) ++e3;
      }
      const Real increment = predictor[c].x - ox[c];
      if (increment != 0.0) {
        uMoved = true;
        ++signChecked;
        if ((increment > 0.0) != (ggx[c] > 0.0)) ++e5;
      }
    }
    if (!uMoved) ++e5;  // non-vacuity
    coverage.signChecks += static_cast<int>(signChecked);
    if (orthogonalClaimApplies) coverage.orthogonalClaims += static_cast<int>(nc);
  }

  // I4 -- the 2D W contract: corrected z is exactly +0.0 and the predictor's z
  // is discarded. Non-vacuous only if the predictor actually carried a z.
  if (!threeD) {
    ScalarField pPrime(nc);
    for (Index c = 0; c < nc; ++c) pPrime[c] = pNonuniform(mesh.cell(c).centroid());
    correct(pPrime, uniformResponse, uniformResponse, uniformResponse, ox, oy, oz, ggx, ggy, ggz);
    bool predictorHadZ = false;
    for (Index c = 0; c < nc; ++c) {
      if (predictor[c].z != 0.0) predictorHadZ = true;
      if (!sameBits(oz[c], 0.0)) ++e4;
    }
    if (!predictorHadZ) ++e4;  // non-vacuity: the discard must be observable
  }

  ++cases;
  ++coverage.invariants;
  const std::size_t errors = e1 + e2 + e3 + e4 + e5 + e6;
  if (errors != 0) ++failures;
  std::printf("  %s L3 %-14s %-12s I1[%zu] I2[%zu,%s] I3[%zu] I4[%zu] I5[%zu] I6[%zu]\n",
              errors == 0 ? "PASS" : "FAIL", meshName.c_str(),
              leastSquares ? "least_squares" : "green_gauss", e1, e2, i2Applicability, e3, e4, e5,
              e6);
}

}  // namespace

// ---------------------------------------------------------------------------
// L4 -- the integrated pressure path
// ---------------------------------------------------------------------------

namespace {

struct ChainInputs {
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  VectorField velocity;
  ScalarField pressure, viscosity, prevU, prevV, prevW;
  SurfaceField seedFlux;
};

ChainInputs makeChainInputs(const Mesh& mesh) {
  ChainInputs in;
  std::size_t i = 0;
  for (const auto& p : mesh.boundaryPatches()) {
    switch (i % 5) {
      case 0: in.velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Wall>()); break;
      case 1: in.velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::MovingWall>(Vector3{1.5, -0.25, 0.1})); break;
      case 2: in.velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Inlet>(Vector3{0.9, 0.4, -0.2})); break;
      case 3: in.velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Outlet>()); break;
      default: in.velocityBoundaries.set(mesh, p.name(), std::make_unique<cfd::boundary::Symmetry>()); break;
    }
    ++i;
  }
  in.pressureBoundaries = mixedPressure(mesh);
  const Index nc = mesh.numberOfCells();
  in.velocity = makePredictor(mesh);
  in.pressure = ScalarField(nc);
  in.viscosity = ScalarField(nc);
  in.prevU = ScalarField(nc);
  in.prevV = ScalarField(nc);
  in.prevW = ScalarField(nc);
  const Real pi = cfd::constants::pi;
  for (Index c = 0; c < nc; ++c) {
    const Vector3 x = mesh.cell(c).centroid();
    in.pressure[c] = 101325.0 + (500.0 * std::sin(pi * x.x) * std::cos(pi * x.y));
    in.viscosity[c] = 1.0e-3 + (4.0e-3 * x.x);
    in.prevU[c] = in.velocity[c].x - 0.1;
    in.prevV[c] = in.velocity[c].y + 0.05;
    in.prevW[c] = in.velocity[c].z - 0.02;
  }
  in.seedFlux = SurfaceField(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const auto& face = mesh.face(f);
    const Vector3 cc = face.centroid();
    in.seedFlux[f] = dot(Vector3{-(cc.y - 0.5), cc.x - 0.5, 0.1 * cc.z}, face.areaVector());
  }
  return in;
}

cfd::algebra::LinearSolverSettings solverSettings() {
  cfd::algebra::LinearSolverSettings s;
  s.absoluteTolerance = 1e-12;
  s.relativeTolerance = 1e-12;
  s.maxIterations = 5000;
  return s;
}

void runChain(const std::string& meshName, const Mesh& mesh, Index scheme, GradientScheme gradScheme,
              bool quick) {
  const Index nc = mesh.numberOfCells();
  const bool threeD = mesh.dimension() == 3;
  const ChainInputs in = makeChainInputs(mesh);
  const FluidProperties fluid(1.2, 1.0e-3);
  const Index referenceCell = 0;
  const Real alpha = 0.7;

  // --- CPU chain ---------------------------------------------------------
  const auto assemble = [&](Index component, const ScalarField& previous) {
    return cfd::pressure_velocity::assembleRelaxedMomentumComponent(
        mesh, in.velocity, in.pressure, in.seedFlux, in.viscosity, in.velocityBoundaries,
        in.pressureBoundaries, static_cast<cfd::physics::VelocityComponent>(component), previous,
        alpha, nullptr, nullptr, static_cast<cfd::discretization::ConvectionScheme>(scheme));
  };
  const ScalarField cpuDU =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, assemble(0, in.prevU).diagonal);
  const ScalarField cpuDV =
      cfd::pressure_velocity::computeMomentumResponseCoefficient(mesh, assemble(1, in.prevV).diagonal);
  ScalarField cpuDW;
  if (threeD)
    cpuDW = cfd::pressure_velocity::computeMomentumResponseCoefficient(
        mesh, assemble(2, in.prevW).diagonal);
  const VectorField gradP = cfd::discretization::gradient(
      mesh, in.pressure, in.pressureBoundaries, GradientScheme::GreenGauss);
  const SurfaceField cpuFlux = cfd::pressure_velocity::rhieChowMassFlux(
      mesh, in.velocity, in.pressure, gradP, cpuDU, cpuDV, threeD ? &cpuDW : nullptr, fluid,
      in.velocityBoundaries, alpha);
  const auto cpuAssembly = cfd::pressure_velocity::assemblePressureCorrection(
      mesh, cpuFlux, cpuDU, cpuDV, fluid.density(), referenceCell, in.pressureBoundaries, {},
      threeD ? &cpuDW : nullptr);

  auto cpuSolver = std::make_unique<cfd::algebra::BiCGSTAB>(solverSettings());
  const auto cpuSolve = cpuSolver->solve(cpuAssembly.system);
  ScalarField cpuPPrime(nc);
  for (Index c = 0; c < nc; ++c) cpuPPrime[c] = cpuSolve.solution[c];
  const VectorField cpuCorrected = cfd::pressure_velocity::correctVelocity(
      mesh, in.velocity, cpuDU, cpuDV, cpuPPrime, in.pressureBoundaries, gradScheme,
      threeD ? &cpuDW : nullptr);

  // --- GPU chain ---------------------------------------------------------
  cfd::gpu::DeviceMomentumAssemblyPlan momentumPlan;
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;
  cfd::gpu::DevicePressureCorrectionPlan pcorrPlan;
  cfd::gpu::DeviceVelocityCorrectionPlan correctPlan;
  if (!momentumPlan.build(mesh, in.velocityBoundaries, in.pressureBoundaries) ||
      !fluxPlan.build(mesh, in.velocityBoundaries) ||
      !pcorrPlan.build(mesh, in.pressureBoundaries) ||
      !correctPlan.build(mesh, in.pressureBoundaries)) {
    ++failures;
    std::printf("  FAIL L4 %-14s a plan is unusable\n", meshName.c_str());
    return;
  }
  auto deviceVelocity = uploadVelocity(in.velocity);
  cfd::gpu::DeviceBuffer<Real> dPressure, dViscosity, dSeed, dPrevU, dPrevV, dPrevW;
  dPressure.uploadFrom(in.pressure.data(), nc);
  dViscosity.uploadFrom(in.viscosity.data(), nc);
  dSeed.uploadFrom(in.seedFlux.data(), static_cast<Index>(in.seedFlux.size()));
  dPrevU.uploadFrom(in.prevU.data(), nc);
  dPrevV.uploadFrom(in.prevV.data(), nc);
  dPrevW.uploadFrom(in.prevW.data(), nc);
  const auto response = [&](Index component, cfd::gpu::DeviceBuffer<Real>& previous,
                            cfd::gpu::DeviceBuffer<Real>& out) {
    cfd::gpu::MomentumAssemblyOptions options;
    options.component = component;
    options.convectionScheme = scheme;
    options.relaxationAlpha = alpha;
    cfd::gpu::DeviceMomentumSystem system;
    cfd::gpu::assembleRelaxedMomentumDevice(momentumPlan, deviceVelocity, dPressure, dViscosity,
                                            dSeed, previous, nullptr, options, system);
    cfd::gpu::computeMomentumResponseCoefficientDevice(momentumPlan.convectionPlan().mesh(),
                                                       system.diagonal, out);
  };
  cfd::gpu::DeviceBuffer<Real> gpuDU, gpuDV, gpuDW;
  response(0, dPrevU, gpuDU);
  response(1, dPrevV, gpuDV);
  if (threeD) response(2, dPrevW, gpuDW);
  cfd::gpu::DeviceBuffer<Real> dGx, dGy, dGz;
  {
    std::vector<Real> a(nc), b(nc), c(nc);
    for (Index i = 0; i < nc; ++i) { a[i] = gradP[i].x; b[i] = gradP[i].y; c[i] = gradP[i].z; }
    dGx.uploadFrom(a.data(), nc);
    dGy.uploadFrom(b.data(), nc);
    dGz.uploadFrom(c.data(), nc);
  }
  cfd::gpu::DeviceBuffer<Real> gpuFlux;
  cfd::gpu::rhieChowMassFluxDevice(fluxPlan, deviceVelocity, dPressure, dGx, dGy, dGz, gpuDU, gpuDV,
                                   threeD ? &gpuDW : nullptr, fluid.density(), alpha, gpuFlux);
  cfd::gpu::PressureCorrectionOptionsDevice pcorrOptions;
  pcorrOptions.referenceCell = referenceCell;
  pcorrOptions.density = fluid.density();
  cfd::gpu::DevicePressureCorrectionSystem gpuSystem;
  cfd::gpu::assemblePressureCorrectionDevice(pcorrPlan, gpuFlux, gpuDU, gpuDV,
                                             threeD ? &gpuDW : nullptr, nullptr, pcorrOptions,
                                             gpuSystem);

  const Index deviceGradScheme = gradScheme == GradientScheme::LeastSquares
                                     ? cfd::gpu::kGradientSchemeLeastSquares
                                     : cfd::gpu::kGradientSchemeGreenGauss;

  // --- L4a: the CONTROLLED comparison ------------------------------------
  // Both correction paths consume the SAME p' -- the CPU's solved field. This
  // qualifies the correction operator independently of any solver difference,
  // and it is bitwise.
  {
    cfd::gpu::DeviceBuffer<Real> dPPrime, gx, gy, gz;
    dPPrime.uploadFrom(cpuPPrime.data(), nc);
    cfd::gpu::DeviceVelocity corrected;
    cfd::gpu::correctVelocityDevice(correctPlan, deviceVelocity, gpuDU, gpuDV,
                                    threeD ? &gpuDW : nullptr, dPPrime, deviceGradScheme, gx, gy,
                                    gz, corrected);
    const auto hx = pull(corrected.x), hy = pull(corrected.y), hz = pull(corrected.z);
    std::size_t differing = 0;
    Real maxAbs = 0.0, maxRel = 0.0, scale = 0.0;
    for (Index c = 0; c < nc; ++c) {
      if (!sameBits(cpuCorrected[c].x, hx[c])) ++differing;
      if (!sameBits(cpuCorrected[c].y, hy[c])) ++differing;
      if (!sameBits(cpuCorrected[c].z, hz[c])) ++differing;
      track(cpuCorrected[c].x, hx[c], maxAbs, maxRel, scale);
      track(cpuCorrected[c].y, hy[c], maxAbs, maxRel, scale);
      track(cpuCorrected[c].z, hz[c], maxAbs, maxRel, scale);
    }
    globalMaxAbs = std::max(globalMaxAbs, maxAbs);
    globalMaxRel = std::max(globalMaxRel, maxRel);
    ++cases;
    ++coverage.controlled;
    const bool ok = differing == 0;
    if (!ok) ++failures;
    std::printf("  %s L4a %-14s scheme=%d %-12s controlled p' differing=%zu maxAbs=%-10.3g "
                "scale=%.4g\n",
                ok ? "PASS" : "FAIL", meshName.c_str(), static_cast<int>(scheme),
                gradScheme == GradientScheme::LeastSquares ? "least_squares" : "green_gauss",
                differing, maxAbs, scale);
  }

  if (quick) return;

  // --- L4b: the full chain, GPU solve included ---------------------------
  auto gpuSolver = cfd::gpu::makeGpuBiCGSTAB(solverSettings());
  if (gpuSolver == nullptr) {
    std::printf("       (no GPU solver available -- L4b skipped on %s)\n", meshName.c_str());
    return;
  }
  cfd::algebra::SparseMatrix gpuMatrix = [&] {
    const auto rows = [&] {
      std::vector<Index> h(static_cast<std::size_t>(gpuSystem.rowOffsets.size()));
      gpuSystem.rowOffsets.downloadTo(h.data(), gpuSystem.rowOffsets.size());
      return h;
    }();
    const auto cols = [&] {
      std::vector<Index> h(static_cast<std::size_t>(gpuSystem.columnIndices.size()));
      gpuSystem.columnIndices.downloadTo(h.data(), gpuSystem.columnIndices.size());
      return h;
    }();
    const auto vals = pull(gpuSystem.values);
    cfd::algebra::SparseMatrixBuilder builder(nc, nc);
    for (Index r = 0; r < nc; ++r)
      for (Index k = rows[r]; k < rows[r + 1]; ++k)
        if (vals[k] != 0.0) builder.add(r, cols[k], vals[k]);
    return builder.build();
  }();
  const auto gpuRhsHost = pull(gpuSystem.rhs);
  cfd::algebra::Vector gpuRhs(static_cast<std::size_t>(nc));
  for (Index c = 0; c < nc; ++c) gpuRhs[c] = gpuRhsHost[c];
  const cfd::algebra::LinearSystem gpuLinearSystem(gpuMatrix, gpuRhs);
  const auto gpuSolve = gpuSolver->solve(gpuLinearSystem);

  ScalarField gpuPPrime(nc);
  for (Index c = 0; c < nc; ++c) gpuPPrime[c] = gpuSolve.solution[c];
  cfd::gpu::DeviceBuffer<Real> dGpuPPrime, gx, gy, gz;
  dGpuPPrime.uploadFrom(gpuPPrime.data(), nc);
  cfd::gpu::DeviceVelocity corrected;
  cfd::gpu::correctVelocityDevice(correctPlan, deviceVelocity, gpuDU, gpuDV,
                                  threeD ? &gpuDW : nullptr, dGpuPPrime, deviceGradScheme, gx, gy,
                                  gz, corrected);
  const auto hx = pull(corrected.x), hy = pull(corrected.y), hz = pull(corrected.z);

  Real maxAbs = 0.0, scale = 0.0, maxPPrimeDiff = 0.0, pPrimeScale = 0.0;
  for (Index c = 0; c < nc; ++c) {
    maxAbs = std::max({maxAbs, std::abs(cpuCorrected[c].x - hx[c]),
                       std::abs(cpuCorrected[c].y - hy[c]), std::abs(cpuCorrected[c].z - hz[c])});
    scale = std::max({scale, std::abs(cpuCorrected[c].x), std::abs(cpuCorrected[c].y),
                      std::abs(cpuCorrected[c].z)});
    maxPPrimeDiff = std::max(maxPPrimeDiff, std::abs(cpuPPrime[c] - gpuPPrime[c]));
    pPrimeScale = std::max(pPrimeScale, std::abs(cpuPPrime[c]));
  }
  // The two solvers converge to the same solution only to their own tolerance,
  // so this layer is solver-limited by construction and its criterion is
  // derived from the solve, not chosen: the corrected velocity may differ by at
  // most the response coefficient times the gradient of the p' difference. The
  // p' difference itself is bounded by the two solves' residual levels. Rather
  // than model the gradient operator's amplification, the criterion is stated
  // on the quantity that is actually solver-limited -- p' -- with the velocity
  // difference reported for the record. L4a is the bitwise statement about the
  // correction operator; this layer is about composition.
  const Real pPrimeBound = 1e-6 * std::max(pPrimeScale, 1.0);
  ++cases;
  ++coverage.chain;
  const bool bothConverged = cpuSolve.converged() && gpuSolve.converged();
  const bool ok = bothConverged && maxPPrimeDiff <= pPrimeBound;
  if (!ok) ++failures;
  std::printf("  %s L4b %-14s scheme=%d %-12s cpu[it=%zu r=%.3g restarts=%zu] "
              "gpu[it=%zu r=%.3g restarts=%zu] |dp'|=%.3g (<=%.3g) |du|=%.3g scale=%.4g\n",
              ok ? "PASS" : "FAIL", meshName.c_str(), static_cast<int>(scheme),
              gradScheme == GradientScheme::LeastSquares ? "least_squares" : "green_gauss",
              static_cast<std::size_t>(cpuSolve.iterations), cpuSolve.finalResidual,
              static_cast<std::size_t>(cpuSolve.restarts),
              static_cast<std::size_t>(gpuSolve.iterations), gpuSolve.finalResidual,
              static_cast<std::size_t>(gpuSolve.restarts), maxPPrimeDiff, pPrimeBound, maxAbs,
              scale);
  if (!ok && !bothConverged) {
    std::printf("       cpu status=%d gpu status=%d -- classify against the recorded GPU BiCGSTAB "
                "restart asymmetry before treating this as a velocity-correction regression\n",
                static_cast<int>(cpuSolve.status), static_cast<int>(gpuSolve.status));
  }
}

void runMesh(const std::string& name, const Mesh& mesh, bool quick) {
  const std::vector<PressureBcCase> bcs = {{"all-Neumann", allNeumannPressure},
                                           {"mixed", mixedPressure},
                                           {"all-FixedValue", allFixedValuePressure}};
  const std::vector<PPrimeCase> fields = {
      {"zero", pZero},         {"uniform", pUniform},   {"linear x", pLinearX},
      {"linear y", pLinearY},  {"linear z", pLinearZ},  {"nonuniform", pNonuniform},
      {"negative", pNegative}};
  const std::vector<ResponseCase> responses = {
      {"zero", respZero}, {"uniform", respUniform}, {"nonuniform", respNonuniform}};

  for (const auto& bcCase : bcs) {
    const BoundaryConditionSet pressureBoundaries = bcCase.build(mesh);
    cfd::gpu::DeviceVelocityCorrectionPlan plan;
    if (!plan.build(mesh, pressureBoundaries)) {
      ++failures;
      std::printf("  FAIL %-14s %-14s plan unusable: %s\n", name.c_str(), bcCase.name,
                  plan.unsupportedReason().c_str());
      continue;
    }
    std::printf("       [%s/%s fixedValuePatches=%zu lsFallbackCells=%zu lsOblique=%zu "
                "resident=%zu B]\n",
                name.c_str(), bcCase.name,
                static_cast<std::size_t>(plan.fixedValuePressurePatches()),
                static_cast<std::size_t>(plan.leastSquares().illConditionedCells()),
                static_cast<std::size_t>(plan.leastSquares().obliqueEntries()),
                plan.residentBytes());

    for (const auto& field : fields)
      for (const auto& response : responses)
        for (const GradientScheme scheme : {GradientScheme::GreenGauss,
                                            GradientScheme::LeastSquares})
          runDirect(name, mesh, plan, bcCase, pressureBoundaries, field, response, scheme, false,
                    false);

    // The pointer-not-dimension branch: a 2D mesh handed a W response takes the
    // 3D branch and keeps a corrected z, which no other case here exercises.
    if (mesh.dimension() != 3) {
      for (const GradientScheme scheme : {GradientScheme::GreenGauss,
                                          GradientScheme::LeastSquares}) {
        runDirect(name, mesh, plan, bcCase, pressureBoundaries, fields[5], responses[2], scheme,
                  true, false);
        ++coverage.twoDWithWResponse;
      }
    }

    for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares})
      runBufferReuse(name, mesh, plan, pressureBoundaries, scheme);

    runLeastSquaresGradient(name, mesh, bcCase.name, pressureBoundaries);
    for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares})
      runInvariants(name, mesh, plan, scheme);
  }
  for (const GradientScheme gradScheme : {GradientScheme::GreenGauss,
                                          GradientScheme::LeastSquares})
    runChain(name, mesh, 0, gradScheme, quick);
}

}  // namespace

int main(int argc, char** argv) {
  const bool quick = argc > 1 && std::string(argv[1]) == "--quick";
  std::printf("=== GPU-DISC-001J: CUDA cell-velocity correction vs CPU (bitwise) ===\n");
  std::printf("CPU references: pressure_velocity::correctVelocity, "
              "discretization::leastSquaresGradient\n\n");

  if (quick) {
    runMesh("distorted q16", q16(), true);
    runMesh("warped 3d 3", warped3D(3), true);
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
  std::printf("  2D / 3D direct cases              %d / %d\n", coverage.twoD, coverage.threeD);
  std::printf("  green_gauss / least_squares cases %d / %d\n", coverage.greenGauss,
              coverage.leastSquares);
  std::printf("  isolated LS gradient cases        %d\n", coverage.lsGradientCases);
  std::printf("  LS cells taking the GG fallback   %d\n", coverage.lsFallbackCells);
  std::printf("  LS oblique-Neumann entries        %d\n", coverage.lsObliqueEntries);
  std::printf("  LS three-dimensional cells        %d\n", coverage.lsThreeDCells);
  std::printf("  invariant cases                   %d\n", coverage.invariants);
  std::printf("  closure-exact cells (I2 support)  %d\n", coverage.closureExactCells);
  std::printf("  cells where I2 is exactly claimed  %d\n", coverage.i2Exact);
  std::printf("  cells where I3 is exactly claimed  %d\n", coverage.orthogonalClaims);
  std::printf("  correction-direction sign checks  %d\n", coverage.signChecks);
  std::printf("  reused-buffer successive calls    %d\n", coverage.reuseSteps);
  std::printf("  2D cases with a non-zero predictor z  %d\n", coverage.nonZeroPredictorZIn2D);
  std::printf("  2D cases taking the 3D branch     %d\n", coverage.twoDWithWResponse);
  std::printf("  controlled p' chain cases         %d\n", coverage.controlled);
  std::printf("  full chain cases (GPU solve)      %d\n", coverage.chain);
  std::printf("  values compared                   %zu\n", valuesCompared);
  std::printf("  bitwise-identical                 %zu\n", bitwiseValues);
  std::printf("  max absolute discrepancy          %.3g\n", globalMaxAbs);
  std::printf("  max relative discrepancy          %.3g\n", globalMaxRel);

  if (coverage.twoD == 0 || coverage.threeD == 0) { ++failures; std::printf("  FAIL 2D and 3D not both exercised\n"); }
  if (coverage.greenGauss == 0 || coverage.leastSquares == 0) { ++failures; std::printf("  FAIL both gradient schemes not exercised\n"); }
  if (coverage.nonZeroPredictorZIn2D == 0) { ++failures; std::printf("  FAIL the 2D W discard was never observable\n"); }
  if (coverage.lsObliqueEntries == 0) { ++failures; std::printf("  FAIL the least-squares oblique-Neumann path never ran\n"); }
  if (coverage.controlled == 0) { ++failures; std::printf("  FAIL the controlled p' comparison never ran\n"); }
  if (coverage.twoDWithWResponse == 0) { ++failures; std::printf("  FAIL the pointer-not-dimension branch was never exercised\n"); }
  if (coverage.i2Exact == 0) { ++failures; std::printf("  FAIL the uniform-p' invariant was never claimed on any cell\n"); }
  // I3 is provable only on a Cartesian mesh, and --quick deliberately runs only
  // distorted ones, so this requirement belongs to the full suite. Gated on the
  // mode, not relaxed: the full run still has to satisfy it.
  if (!quick && coverage.orthogonalClaims == 0) { ++failures; std::printf("  FAIL the orthogonal-component invariant was never claimed\n"); }
  if (coverage.signChecks == 0) { ++failures; std::printf("  FAIL the correction direction was never checked\n"); }
  if (coverage.reuseSteps == 0) { ++failures; std::printf("  FAIL output buffers were never reused across calls\n"); }

  std::printf("\ncases=%d failures=%d\n", cases, failures);
  std::printf("%s\n", failures == 0 ? "VELOCITY CORRECTION EQUIVALENCE: PASS (bitwise)"
                                    : "VELOCITY CORRECTION EQUIVALENCE: FAIL");
  return failures == 0 ? 0 : 1;
}
