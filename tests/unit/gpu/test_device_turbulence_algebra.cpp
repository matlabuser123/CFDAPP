// P16-GPU-TURB -- the strain rate -> P_k -> mu_t chain, CPU against GPU.
//
// THE FROZEN GATE, written down before any number was measured
// ------------------------------------------------------------
//   G1  BITWISE (0 ulp) against the PRODUCTION CPU functions, on every cell.
//       Not a tolerance. These three steps are cell-local algebra with no
//       reduction and no association-order freedom, so unlike the P14 assembly
//       (where diagonals differ by <= 2 ulp because the host and device
//       accumulate face contributions in different orders) there is no reason
//       for a single bit to differ. A tolerance here would only hide a
//       transcription error.
//
//   G2  EXECUTION EVIDENCE. Exactly StrainRate, Production and
//       TurbulentViscosity report Gpu. The other five operations must still
//       report NotAttempted -- this stage ports three of eight, and a trace
//       that claimed more would be the first quiet step toward a vacuous
//       equivalence claim.
//
//   G3  THE FLOORS MUST ACTIVATE. At least one state per floor where the floor
//       changes the answer, proven by comparing against the unfloored formula.
//       A sweep that never trips a floor tests the nominal expression twice and
//       reports it as coverage.
//
// NON-VACUITY CONTROLS
//   NV1  The forced-fallback injector is DETECTED for each of the three
//        operations.
//   NV2  Perturbing one input cell changes the output -- so the comparison is
//        reading the data it claims to.
//   NV3  The floored result DIFFERS from the unfloored formula, so G3 is not
//        satisfied by a floor that never binds.
//
// WHAT IS DELIBERATELY NOT HERE
//   The wall treatment's replacement of production in wall cells. That is a
//   separate traced operation and a later step; applying it here would claim
//   coverage this stage does not have.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceTurbulence.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/TurbulenceProduction.hpp"

using cfd::Index;
using cfd::Real;
using cfd::discretization::VelocityGradientField;
using cfd::fields::ScalarField;
using cfd::gpu::ExecutionBackend;
using cfd::gpu::TurbulenceOperation;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

// The k-epsilon defaults (KEpsilonConfig / KEpsilonCoefficients).
constexpr Real kDensity = 1.225;
constexpr Real kCMu = 0.09;
constexpr Real kKFloor = 1e-10;
constexpr Real kEpsilonFloor = 1e-10;

// Deliberately NONUNIFORM, and deliberately not smooth: a field that varies
// slowly makes neighbouring cells nearly equal, so a kernel that read the
// wrong index would still look right.
VelocityGradientField varyingGradient(Index n) {
  VelocityGradientField gradient;
  gradient.gradU = cfd::fields::VectorField(n);
  gradient.gradV = cfd::fields::VectorField(n);
  for (Index i = 0; i < n; ++i) {
    const Real t = static_cast<Real>(i);
    gradient.gradU[i] = {0.31 + (0.017 * t) + (0.9 * static_cast<Real>(i % 5)),
                         -0.44 + (0.023 * t) - (0.7 * static_cast<Real>(i % 3)), 0.0};
    gradient.gradV[i] = {0.19 - (0.011 * t) + (0.5 * static_cast<Real>(i % 7)),
                         0.52 + (0.029 * t) - (1.1 * static_cast<Real>(i % 4)), 0.0};
  }
  return gradient;
}

ScalarField varying(Index n, Real base, Real slope, Index modulus) {
  ScalarField field(n, 0.0);
  for (Index i = 0; i < n; ++i) {
    field[i] = base + (slope * static_cast<Real>(i)) + (0.37 * static_cast<Real>(i % modulus));
  }
  return field;
}

long long ulpDistance(Real a, Real b) {
  if (a == b) return 0;
  if (!std::isfinite(a) || !std::isfinite(b)) return -1;
  long long ia = 0;
  long long ib = 0;
  std::memcpy(&ia, &a, sizeof(ia));
  std::memcpy(&ib, &b, sizeof(ib));
  if (ia < 0) ia = static_cast<long long>(0x8000000000000000ULL) - ia;
  if (ib < 0) ib = static_cast<long long>(0x8000000000000000ULL) - ib;
  return std::llabs(ia - ib);
}

void uploadGradient(const VelocityGradientField& gradient, Index n,
                    cfd::gpu::DeviceVelocityGradient& device) {
  std::vector<Real> ux(n), uy(n), vx(n), vy(n);
  for (Index i = 0; i < n; ++i) {
    ux[i] = gradient.gradU[i].x;
    uy[i] = gradient.gradU[i].y;
    vx[i] = gradient.gradV[i].x;
    vy[i] = gradient.gradV[i].y;
  }
  device.gradUx.uploadFrom(ux.data(), n);
  device.gradUy.uploadFrom(uy.data(), n);
  device.gradVx.uploadFrom(vx.data(), n);
  device.gradVy.uploadFrom(vy.data(), n);
}

std::vector<Real> download(const cfd::gpu::DeviceBuffer<Real>& buffer, Index n) {
  std::vector<Real> host(static_cast<std::size_t>(n), 0.0);
  buffer.downloadTo(host.data(), n);
  return host;
}

// Bitwise comparison with a message that names the first offender, because a
// count of mismatches is not something anyone can debug from.
void expectBitwise(const ScalarField& host, const std::vector<Real>& device, const char* what) {
  ASSERT_EQ(static_cast<std::size_t>(host.size()), device.size()) << what;
  Index mismatches = 0;
  long long maxUlps = 0;
  std::string first;
  for (Index i = 0; i < host.size(); ++i) {
    const Real h = host[i];
    const Real d = device[static_cast<std::size_t>(i)];
    if (d == h) continue;
    ++mismatches;
    maxUlps = std::max(maxUlps, ulpDistance(d, h));
    if (first.empty()) {
      first = "cell " + std::to_string(i) + ": host " + std::to_string(h) + " device " +
              std::to_string(d) + " (" + std::to_string(ulpDistance(d, h)) + " ulp)";
    }
  }
  EXPECT_EQ(mismatches, 0u) << what << ": " << mismatches << "/" << host.size()
                            << " cells differ, max " << maxUlps << " ulp. First: " << first;
}

}  // namespace

// =============================================================================
// G1 -- bitwise against the production CPU functions.
// =============================================================================

TEST(DeviceTurbulenceAlgebra, StrainRateProductionAndViscosityAreBitwise) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  for (const Index n : {Index{1}, Index{7}, Index{108}, Index{1000}}) {
    const Mesh mesh = MeshGeometry::createCartesian2D(n, 1, 1.0, 1.0);
    ASSERT_EQ(mesh.numberOfCells(), n);

    const VelocityGradientField gradient = varyingGradient(n);
    const ScalarField k = varying(n, 0.42, 0.013, 5);
    const ScalarField epsilon = varying(n, 0.27, 0.009, 3);

    cfd::gpu::clearForcedCpuFallbacksForTesting();
    cfd::gpu::turbulenceBackendTrace().reset();

    cfd::gpu::DeviceVelocityGradient deviceGradient;
    uploadGradient(gradient, n, deviceGradient);
    cfd::gpu::DeviceBuffer<Real> deviceK;
    deviceK.uploadFrom(k.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
    deviceEpsilon.uploadFrom(epsilon.data(), n);

    cfd::gpu::DeviceBuffer<Real> deviceS2;
    cfd::gpu::computeStrainRateMagnitudeSquaredDevice(deviceGradient, n, deviceS2);

    cfd::gpu::DeviceBuffer<Real> deviceMuT;
    cfd::gpu::computeTurbulentViscosityDevice(kDensity, kCMu, deviceK, deviceEpsilon, kKFloor,
                                              kEpsilonFloor, deviceMuT);

    cfd::gpu::DeviceBuffer<Real> deviceProduction;
    cfd::gpu::computeTurbulentProductionDevice(deviceMuT, deviceS2, deviceProduction);

    // The PRODUCTION host functions, not a re-typed formula.
    const ScalarField hostS2 = cfd::turbulence::computeStrainRateMagnitudeSquared(mesh, gradient);
    const ScalarField hostMuT = cfd::turbulence::computeTurbulentViscosityField(
        mesh, kDensity, kCMu, k, epsilon, kKFloor, kEpsilonFloor);
    const ScalarField hostProduction =
        cfd::turbulence::computeTurbulentProduction(mesh, hostMuT, gradient);

    const std::string label = "n=" + std::to_string(n);
    expectBitwise(hostS2, download(deviceS2, n), ("S^2 " + label).c_str());
    expectBitwise(hostMuT, download(deviceMuT, n), ("mu_t " + label).c_str());
    expectBitwise(hostProduction, download(deviceProduction, n), ("P_k " + label).c_str());
  }
}

// =============================================================================
// G2 -- execution evidence, and the honest partial state.
// =============================================================================

TEST(DeviceTurbulenceAlgebra, ExactlyThreeOperationsReportGpu) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Index n = 64;
  const VelocityGradientField gradient = varyingGradient(n);
  const ScalarField k = varying(n, 0.42, 0.013, 5);
  const ScalarField epsilon = varying(n, 0.27, 0.009, 3);

  cfd::gpu::clearForcedCpuFallbacksForTesting();
  auto& trace = cfd::gpu::turbulenceBackendTrace();
  trace.reset();

  cfd::gpu::DeviceVelocityGradient deviceGradient;
  uploadGradient(gradient, n, deviceGradient);
  cfd::gpu::DeviceBuffer<Real> deviceK;
  deviceK.uploadFrom(k.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
  deviceEpsilon.uploadFrom(epsilon.data(), n);

  cfd::gpu::DeviceBuffer<Real> deviceS2;
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  cfd::gpu::DeviceBuffer<Real> deviceProduction;
  cfd::gpu::computeStrainRateMagnitudeSquaredDevice(deviceGradient, n, deviceS2);
  cfd::gpu::computeTurbulentViscosityDevice(kDensity, kCMu, deviceK, deviceEpsilon, kKFloor,
                                            kEpsilonFloor, deviceMuT);
  cfd::gpu::computeTurbulentProductionDevice(deviceMuT, deviceS2, deviceProduction);

  EXPECT_EQ(trace.backendFor(TurbulenceOperation::StrainRate), ExecutionBackend::Gpu);
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::Production), ExecutionBackend::Gpu);
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::TurbulentViscosity), ExecutionBackend::Gpu);

  // The five that are NOT ported yet. This assertion is the honest half of the
  // gate: it fails the moment someone claims coverage this stage has not built.
  for (const auto pending :
       {TurbulenceOperation::KAssembly, TurbulenceOperation::KSolve,
        TurbulenceOperation::EpsilonAssembly, TurbulenceOperation::EpsilonSolve,
        TurbulenceOperation::WallTreatment}) {
    EXPECT_EQ(trace.backendFor(pending), ExecutionBackend::NotAttempted)
        << cfd::gpu::turbulenceOperationName(pending)
        << " reports a backend, but nothing in this stage ports it:\n"
        << trace.describe();
  }
}

// =============================================================================
// G3 + NV3 -- the floors must actually bind.
// =============================================================================

TEST(DeviceTurbulenceAlgebra, BothFloorsActivateAndMatchTheHost) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  // Four states per cell group: neither floor binds, only k binds, only
  // epsilon binds, both bind. Values straddle the floor rather than sitting
  // far below it, so an off-by-one in the comparison direction shows up.
  const std::vector<Real> kValues = {0.5, 1e-14, 0.5, 1e-20, kKFloor, 0.0, -3.0};
  const std::vector<Real> epsilonValues = {0.3, 0.3, 1e-15, 1e-30, 0.3, kEpsilonFloor, 0.0};
  const Index n = static_cast<Index>(kValues.size());
  const Mesh mesh = MeshGeometry::createCartesian2D(n, 1, 1.0, 1.0);

  ScalarField k(n, 0.0);
  ScalarField epsilon(n, 0.0);
  for (Index i = 0; i < n; ++i) {
    k[i] = kValues[static_cast<std::size_t>(i)];
    epsilon[i] = epsilonValues[static_cast<std::size_t>(i)];
  }

  cfd::gpu::clearForcedCpuFallbacksForTesting();
  cfd::gpu::turbulenceBackendTrace().reset();

  cfd::gpu::DeviceBuffer<Real> deviceK;
  deviceK.uploadFrom(k.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
  deviceEpsilon.uploadFrom(epsilon.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceMuT;
  cfd::gpu::computeTurbulentViscosityDevice(kDensity, kCMu, deviceK, deviceEpsilon, kKFloor,
                                            kEpsilonFloor, deviceMuT);

  const ScalarField hostMuT = cfd::turbulence::computeTurbulentViscosityField(
      mesh, kDensity, kCMu, k, epsilon, kKFloor, kEpsilonFloor);
  expectBitwise(hostMuT, download(deviceMuT, n), "mu_t across floor states");

  // NV3: the floor must CHANGE the answer somewhere, otherwise "the floors are
  // covered" is satisfied by a sweep in which they never bind.
  Index kFloorBound = 0;
  Index epsilonFloorBound = 0;
  for (Index i = 0; i < n; ++i) {
    if (k[i] < kKFloor) ++kFloorBound;
    if (epsilon[i] < kEpsilonFloor) ++epsilonFloorBound;
  }
  EXPECT_GT(kFloorBound, 0u) << "no cell trips the k floor";
  EXPECT_GT(epsilonFloorBound, 0u) << "no cell trips the epsilon floor";

  // And the floored answer must differ from the unfloored formula, on at least
  // one cell. Without this the floors could be dead code that happens to agree.
  Index differsFromUnfloored = 0;
  for (Index i = 0; i < n; ++i) {
    const Real unfloored = kDensity * kCMu * k[i] * k[i] / epsilon[i];
    if (!std::isfinite(unfloored) || unfloored != hostMuT[i]) ++differsFromUnfloored;
  }
  EXPECT_GT(differsFromUnfloored, 0u)
      << "the floors never changed the result, so this test would pass with them removed";

  // Every floored result stays finite -- the reason the floors exist.
  const std::vector<Real> deviceValues = download(deviceMuT, n);
  for (Index i = 0; i < n; ++i) {
    EXPECT_TRUE(std::isfinite(deviceValues[static_cast<std::size_t>(i)]))
        << "cell " << i << " is not finite despite the floors";
  }
}

// =============================================================================
// NV1 -- the forced-fallback injector is detected per operation.
// =============================================================================

TEST(DeviceTurbulenceAlgebra, AForcedFallbackIsDetectedForEachOperation) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Index n = 32;
  const Mesh mesh = MeshGeometry::createCartesian2D(n, 1, 1.0, 1.0);
  const VelocityGradientField gradient = varyingGradient(n);
  const ScalarField k = varying(n, 0.42, 0.013, 5);
  const ScalarField epsilon = varying(n, 0.27, 0.009, 3);

  const std::vector<TurbulenceOperation> ported = {TurbulenceOperation::StrainRate,
                                                   TurbulenceOperation::TurbulentViscosity,
                                                   TurbulenceOperation::Production};

  for (const auto forced : ported) {
    cfd::gpu::clearForcedCpuFallbacksForTesting();
    cfd::gpu::forceCpuFallbackForTesting(forced, true);
    auto& trace = cfd::gpu::turbulenceBackendTrace();
    trace.reset();

    cfd::gpu::DeviceVelocityGradient deviceGradient;
    uploadGradient(gradient, n, deviceGradient);
    cfd::gpu::DeviceBuffer<Real> deviceK;
    deviceK.uploadFrom(k.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
    deviceEpsilon.uploadFrom(epsilon.data(), n);

    // The CALLER owns the host path -- see the fallback contract in
    // DeviceTurbulence.hpp. This loop is what the k-epsilon integration will
    // do: attempt the device, and where it declined, run the PRODUCTION CPU
    // function and upload the result so the chain continues. A fallback that
    // still produces the right numbers is the realistic case, and it is
    // exactly the case a naive gate would wave through -- which is why the
    // trace, not the numbers, has to be the discriminator here.
    cfd::gpu::DeviceBuffer<Real> deviceS2;
    cfd::gpu::computeStrainRateMagnitudeSquaredDevice(deviceGradient, n, deviceS2);
    const ScalarField hostS2 = cfd::turbulence::computeStrainRateMagnitudeSquared(mesh, gradient);
    if (deviceS2.size() != n) deviceS2.uploadFrom(hostS2.data(), n);

    cfd::gpu::DeviceBuffer<Real> deviceMuT;
    cfd::gpu::computeTurbulentViscosityDevice(kDensity, kCMu, deviceK, deviceEpsilon, kKFloor,
                                              kEpsilonFloor, deviceMuT);
    const ScalarField hostMuT = cfd::turbulence::computeTurbulentViscosityField(
        mesh, kDensity, kCMu, k, epsilon, kKFloor, kEpsilonFloor);
    if (deviceMuT.size() != n) deviceMuT.uploadFrom(hostMuT.data(), n);

    cfd::gpu::DeviceBuffer<Real> deviceProduction;
    cfd::gpu::computeTurbulentProductionDevice(deviceMuT, deviceS2, deviceProduction);
    const ScalarField hostProduction =
        cfd::turbulence::computeTurbulentProduction(mesh, hostMuT, gradient);
    if (deviceProduction.size() != n) deviceProduction.uploadFrom(hostProduction.data(), n);

    // The numbers are RIGHT on both arms. That is the point.
    expectBitwise(hostProduction, download(deviceProduction, n), "P_k under forced fallback");

    // And the gate must still fail, because one operation did not run on the
    // device. If this passed, every downstream equivalence claim in P16 could
    // be satisfied by a solve that never touched CUDA.
    EXPECT_EQ(trace.backendFor(forced), ExecutionBackend::Cpu)
        << "forcing " << cfd::gpu::turbulenceOperationName(forced)
        << " onto the host was not recorded:\n"
        << trace.describe();

    const TurbulenceOperation required[] = {TurbulenceOperation::StrainRate,
                                            TurbulenceOperation::Production,
                                            TurbulenceOperation::TurbulentViscosity};
    EXPECT_FALSE(trace.allOnGpu(required, 3))
        << "the execution gate did not notice the fallback, even though the numbers agree:\n"
        << trace.describe();

    // The other two DID run on the device -- a fallback of one operation must
    // not be reported as a fallback of the whole chain.
    for (const auto other : ported) {
      if (other == forced) continue;
      EXPECT_EQ(trace.backendFor(other), ExecutionBackend::Gpu)
          << cfd::gpu::turbulenceOperationName(other) << " was downgraded by an unrelated forced "
          << "fallback:\n"
          << trace.describe();
    }
  }
  cfd::gpu::clearForcedCpuFallbacksForTesting();
}

// =============================================================================
// NV2 -- the comparison reads the data it claims to.
// =============================================================================

TEST(DeviceTurbulenceAlgebra, PerturbingOneInputCellChangesOnlyThatOutput) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Index n = 48;
  const Index target = 29;  // interior, not the first or last thread
  VelocityGradientField gradient = varyingGradient(n);

  cfd::gpu::clearForcedCpuFallbacksForTesting();
  cfd::gpu::turbulenceBackendTrace().reset();

  cfd::gpu::DeviceVelocityGradient deviceGradient;
  uploadGradient(gradient, n, deviceGradient);
  cfd::gpu::DeviceBuffer<Real> baseline;
  cfd::gpu::computeStrainRateMagnitudeSquaredDevice(deviceGradient, n, baseline);
  const std::vector<Real> before = download(baseline, n);

  gradient.gradU[target].y += 0.75;
  uploadGradient(gradient, n, deviceGradient);
  cfd::gpu::DeviceBuffer<Real> perturbed;
  cfd::gpu::computeStrainRateMagnitudeSquaredDevice(deviceGradient, n, perturbed);
  const std::vector<Real> after = download(perturbed, n);

  for (Index i = 0; i < n; ++i) {
    const std::size_t idx = static_cast<std::size_t>(i);
    if (i == target) {
      EXPECT_NE(after[idx], before[idx]) << "the perturbed cell did not change";
    } else {
      EXPECT_EQ(after[idx], before[idx]) << "cell " << i << " changed but was not perturbed";
    }
  }

  // And the host agrees on the perturbed field, so the change is the physics
  // and not the upload.
  const Mesh mesh = MeshGeometry::createCartesian2D(n, 1, 1.0, 1.0);
  expectBitwise(cfd::turbulence::computeStrainRateMagnitudeSquared(mesh, gradient), after,
                "S^2 after perturbation");
}

// =============================================================================
// Determinism.
// =============================================================================

TEST(DeviceTurbulenceAlgebra, TheSameInputsProduceTheSameBytesTwice) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Index n = 256;
  const VelocityGradientField gradient = varyingGradient(n);
  const ScalarField k = varying(n, 0.42, 0.013, 5);
  const ScalarField epsilon = varying(n, 0.27, 0.009, 3);

  cfd::gpu::clearForcedCpuFallbacksForTesting();
  std::vector<Real> firstS2, firstMuT, firstProduction;
  for (int pass = 0; pass < 2; ++pass) {
    cfd::gpu::turbulenceBackendTrace().reset();
    cfd::gpu::DeviceVelocityGradient deviceGradient;
    uploadGradient(gradient, n, deviceGradient);
    cfd::gpu::DeviceBuffer<Real> deviceK;
    deviceK.uploadFrom(k.data(), n);
    cfd::gpu::DeviceBuffer<Real> deviceEpsilon;
    deviceEpsilon.uploadFrom(epsilon.data(), n);

    cfd::gpu::DeviceBuffer<Real> deviceS2;
    cfd::gpu::DeviceBuffer<Real> deviceMuT;
    cfd::gpu::DeviceBuffer<Real> deviceProduction;
    cfd::gpu::computeStrainRateMagnitudeSquaredDevice(deviceGradient, n, deviceS2);
    cfd::gpu::computeTurbulentViscosityDevice(kDensity, kCMu, deviceK, deviceEpsilon, kKFloor,
                                              kEpsilonFloor, deviceMuT);
    cfd::gpu::computeTurbulentProductionDevice(deviceMuT, deviceS2, deviceProduction);

    if (pass == 0) {
      firstS2 = download(deviceS2, n);
      firstMuT = download(deviceMuT, n);
      firstProduction = download(deviceProduction, n);
    } else {
      EXPECT_EQ(firstS2, download(deviceS2, n));
      EXPECT_EQ(firstMuT, download(deviceMuT, n));
      EXPECT_EQ(firstProduction, download(deviceProduction, n));
    }
  }
}

// =============================================================================
// The promotion is tied to production.
// =============================================================================

TEST(DeviceTurbulenceAlgebra, PromotedViscosityMatchesTheProductionModel) {
  // computeTurbulentViscosityField was moved out of KEpsilonModel.cpp's
  // anonymous namespace so this file could compare against it. This checks the
  // move preserved the formula, by reproducing the relation the model's own
  // header documents. It runs without a GPU on purpose: the promotion is a CPU
  // change and must be verified even in a CPU-only build.
  const Index n = 16;
  const Mesh mesh = MeshGeometry::createCartesian2D(n, 1, 1.0, 1.0);
  const ScalarField k = varying(n, 0.42, 0.013, 5);
  const ScalarField epsilon = varying(n, 0.27, 0.009, 3);

  const ScalarField muT = cfd::turbulence::computeTurbulentViscosityField(
      mesh, kDensity, kCMu, k, epsilon, kKFloor, kEpsilonFloor);

  for (Index i = 0; i < n; ++i) {
    const Real kSafe = std::max(k[i], kKFloor);
    const Real epsSafe = std::max(epsilon[i], kEpsilonFloor);
    EXPECT_EQ(muT[i], kDensity * kCMu * kSafe * kSafe / epsSafe) << "cell " << i;
  }
}
