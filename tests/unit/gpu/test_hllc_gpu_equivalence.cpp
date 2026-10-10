// P17.7 sections 29 and 30 -- CPU/GPU equivalence and determinism for the HLLC flux.
//
// The CPU path is the reference (CLAUDE.md section 6). The target is BITWISE, not a tolerance,
// because CPU and GPU run THE SAME SOURCE: include/cfd/compressible/HllcFlux.hpp is marked
// CFD_HLLC_HOST_DEVICE and compiled by gcc for the host and nvcc for the device. P17_GATES.md G8:
// "where the operation order is identical, BITWISE is the correct requirement and anything looser
// is a concession that must be argued for, not assumed."
//
// Section 29 insists the comparison cover more than the final flux, because CPU and GPU could pick
// DIFFERENT BRANCHES and still land close. So every intermediate the algorithm exposes is compared:
// the three wave speeds, the contact denominator, both star densities, both star internal energies,
// the branch, and the validity code.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/gpu/DeviceHllcFlux.hpp"
#include "hllc_reference_data.hpp"

namespace {

using cfd::Real;
using cfd::Vector3;
using cfd::compressible::hllcFlux;
using cfd::compressible::HllcResult;
using cfd::compressible::hllFluxForControlOnly;
using cfd::compressible::PrimitiveState;
using cfd::compressible::WaveSpeedEstimator;
using cfd::gpu::evaluateHllcOnDevice;
using cfd::gpu::evaluateHllOnDeviceForControlOnly;
using cfd::gpu::hllcDeviceAvailable;
using cfd::gpu::HllcDeviceInput;
using cfd::gpu::HllcDeviceOutput;

namespace ref = cfd_test::hllc_ref;

PrimitiveState toState(const ref::Side& s) {
  PrimitiveState p{};
  p.density = s.density;
  p.velocity = Vector3{s.u, s.v, s.w};
  p.pressure = s.pressure;
  p.temperature = s.temperature;
  p.speedOfSound = s.speedOfSound;
  p.totalEnergyDensity = s.totalEnergyDensity;
  return p;
}

// THE SECTION 29 STATE MATRIX. Every row the authorization names, taken from the frozen reference
// table, plus the oblique normal and an explicitly constructed right-star case (the reference table
// has none, so without it that branch would go uncompared on device).
struct Entry {
  std::string name;
  HllcDeviceInput in;
};

std::vector<Entry> buildMatrix() {
  std::vector<Entry> out;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    HllcDeviceInput in{};
    in.left = toState(c.left);
    in.right = toState(c.right);
    in.normal = Vector3{c.nx, c.ny, c.nz};
    in.gamma = ref::kGamma;
    in.estimator = 0;
    out.push_back({c.name, in});
  }
  // a right-star case: both states moving left fast enough that S* < 0 < S_R
  {
    const ref::Case& c = ref::kCases[0];
    HllcDeviceInput in{};
    in.left = toState(c.left);
    in.right = toState(c.right);
    in.left.velocity = Vector3{-200.0, 0.0, 0.0};
    in.right.velocity = Vector3{-200.0, 0.0, 0.0};
    in.left.density = 0.5;
    in.normal = Vector3{1.0, 0.0, 0.0};
    in.gamma = ref::kGamma;
    out.push_back({"constructed_right_star", in});
  }
  // the Davis estimator on device too, so the control path is not CPU-only
  {
    const ref::Case& c = ref::kCases[2];  // sod_literature
    HllcDeviceInput in{};
    in.left = toState(c.left);
    in.right = toState(c.right);
    in.normal = Vector3{c.nx, c.ny, c.nz};
    in.gamma = ref::kGamma;
    in.estimator = 1;
    out.push_back({"sod_davis_estimator", in});
  }
  return out;
}

HllcResult cpuOf(const HllcDeviceInput& in) {
  const WaveSpeedEstimator est =
      in.estimator == 1 ? WaveSpeedEstimator::Davis : WaveSpeedEstimator::ToroPressureBased;
  return hllcFlux(in.left, in.right, in.normal, in.gamma, est);
}

void cpuFluxArray(const HllcResult& r, Real out[5]) {
  out[0] = r.flux.mass;
  out[1] = r.flux.momentum.x;
  out[2] = r.flux.momentum.y;
  out[3] = r.flux.momentum.z;
  out[4] = r.flux.energy;
}

}  // namespace

TEST(HllcGpuEquivalenceTest, G2_CpuAndGpuAgreeBitwiseOverTheStateMatrix) {
  if (!hllcDeviceAvailable()) {
    GTEST_SKIP() << "no CUDA device available -- CPU/GPU equivalence is NOT claimed, not passed";
  }

  const std::vector<Entry> matrix = buildMatrix();
  std::vector<HllcDeviceInput> inputs;
  inputs.reserve(matrix.size());
  for (const Entry& e : matrix) inputs.push_back(e.in);

  std::vector<HllcDeviceOutput> gpu(matrix.size());
  std::string message;
  ASSERT_TRUE(
      evaluateHllcOnDevice(inputs.data(), gpu.data(), static_cast<int>(inputs.size()), &message))
      << message;

  int branchDifferences = 0;
  int bitwiseRows = 0;
  Real worstFlux = 0.0;
  Real worstSpeed = 0.0;
  const char* worstName = "";

  std::printf("\n  %-26s %-10s %-12s %-12s\n", "case", "branch", "max|dF|", "max|dS|");
  for (size_t i = 0; i < matrix.size(); ++i) {
    const HllcResult cpu = cpuOf(matrix[i].in);
    const HllcDeviceOutput& g = gpu[i];
    const std::string& nm = matrix[i].name;

    EXPECT_EQ(g.valid, cpu.valid ? 1 : 0) << nm;
    EXPECT_EQ(g.validity, static_cast<int>(cpu.validity)) << nm;
    if (g.branch != static_cast<int>(cpu.branch)) ++branchDifferences;
    EXPECT_EQ(g.branch, static_cast<int>(cpu.branch))
        << nm << ": CPU and GPU selected DIFFERENT BRANCHES";

    Real cf[5];
    cpuFluxArray(cpu, cf);
    Real dF = 0.0;
    for (int k = 0; k < 5; ++k) {
      EXPECT_EQ(g.flux[k], cf[k]) << nm << " flux component " << k << " is not BITWISE equal";
      dF = std::max(dF, std::abs(g.flux[k] - cf[k]));
    }

    EXPECT_EQ(g.sLeft, cpu.speeds.left) << nm << " S_L";
    EXPECT_EQ(g.sContact, cpu.speeds.contact) << nm << " S*";
    EXPECT_EQ(g.sRight, cpu.speeds.right) << nm << " S_R";
    EXPECT_EQ(g.denominator, cpu.speeds.denominator) << nm << " denominator";
    EXPECT_EQ(g.starDensityLeft, cpu.starDensityLeft) << nm << " rho*_L";
    EXPECT_EQ(g.starDensityRight, cpu.starDensityRight) << nm << " rho*_R";
    EXPECT_EQ(g.starInternalEnergyLeft, cpu.starInternalEnergyLeft) << nm << " e*_L";
    EXPECT_EQ(g.starInternalEnergyRight, cpu.starInternalEnergyRight) << nm << " e*_R";

    const Real dS = std::max(
        std::max(std::abs(g.sLeft - cpu.speeds.left), std::abs(g.sContact - cpu.speeds.contact)),
        std::abs(g.sRight - cpu.speeds.right));
    if (dF > worstFlux) {
      worstFlux = dF;
      worstName = nm.c_str();
    }
    worstSpeed = std::max(worstSpeed, dS);
    if (dF == 0.0 && dS == 0.0) ++bitwiseRows;

    std::printf("  %-26s %-10d %-12.3e %-12.3e\n", nm.c_str(), g.branch, dF, dS);
  }

  std::printf("\n  rows compared              : %zu\n", matrix.size());
  std::printf("  rows BITWISE identical     : %d\n", bitwiseRows);
  std::printf("  branch-selection differences: %d   (must be 0)\n", branchDifferences);
  std::printf("  max |flux difference|      : %.17g\n", worstFlux);
  std::printf("  max |wave-speed difference|: %.17g\n", worstSpeed);
  if (worstFlux != 0.0) std::printf("  worst row: %s\n", worstName);
  std::printf("  built with --fmad=false, so the arithmetic is unfused on BOTH sides\n");

  EXPECT_EQ(branchDifferences, 0);
  EXPECT_EQ(worstFlux, 0.0) << "CPU/GPU flux is not bitwise identical";
  EXPECT_EQ(worstSpeed, 0.0) << "CPU/GPU wave speeds are not bitwise identical";
  EXPECT_EQ(bitwiseRows, static_cast<int>(matrix.size()));
}

// Section 29 also asks for the fallback decision to be compared. There is no fallback in this
// layer -- the denominator is provably non-degenerate -- so what is compared instead is the
// VALIDITY code on deliberately inadmissible inputs: the device must refuse exactly what the host
// refuses, with the same reason, rather than producing a non-finite flux.
TEST(HllcGpuEquivalenceTest, G2_InadmissibleInputsAreRefusedIdenticallyOnDevice) {
  if (!hllcDeviceAvailable()) {
    GTEST_SKIP() << "no CUDA device available";
  }
  const ref::Case& c = ref::kCases[0];
  const Real nan = std::numeric_limits<Real>::quiet_NaN();

  std::vector<HllcDeviceInput> bad;
  auto push = [&](auto mutate) {
    HllcDeviceInput in{};
    in.left = toState(c.left);
    in.right = toState(c.right);
    in.normal = Vector3{1.0, 0.0, 0.0};
    in.gamma = ref::kGamma;
    mutate(in);
    bad.push_back(in);
  };
  push([](HllcDeviceInput& in) { in.left.density = 0.0; });
  push([](HllcDeviceInput& in) { in.left.density = -1.0; });
  push([&](HllcDeviceInput& in) { in.left.pressure = nan; });
  push([](HllcDeviceInput& in) { in.right.pressure = -5.0; });
  push([&](HllcDeviceInput& in) { in.right.velocity.y = nan; });
  push([](HllcDeviceInput& in) { in.normal = Vector3{2.0, 0.0, 0.0}; });
  push([](HllcDeviceInput& in) { in.normal = Vector3{0.0, 0.0, 0.0}; });
  push([](HllcDeviceInput& in) { in.gamma = 1.0; });

  std::vector<HllcDeviceOutput> gpu(bad.size());
  std::string message;
  ASSERT_TRUE(evaluateHllcOnDevice(bad.data(), gpu.data(), static_cast<int>(bad.size()), &message))
      << message;

  for (size_t i = 0; i < bad.size(); ++i) {
    const HllcResult cpu = cpuOf(bad[i]);
    EXPECT_FALSE(cpu.valid) << "row " << i << " should have been refused on the host";
    EXPECT_EQ(gpu[i].valid, 0) << "row " << i << " was ACCEPTED on device but refused on host";
    EXPECT_EQ(gpu[i].validity, static_cast<int>(cpu.validity))
        << "row " << i << " gave a different reason on device";
  }
  std::printf(
      "\n  %zu inadmissible inputs refused identically on host and device, same reason code\n",
      bad.size());
}

TEST(HllcGpuEquivalenceTest, G4_DeviceEvaluationIsDeterministic) {
  if (!hllcDeviceAvailable()) {
    GTEST_SKIP() << "no CUDA device available";
  }
  const std::vector<Entry> matrix = buildMatrix();
  std::vector<HllcDeviceInput> inputs;
  for (const Entry& e : matrix) inputs.push_back(e.in);

  std::vector<HllcDeviceOutput> first(matrix.size());
  std::vector<HllcDeviceOutput> second(matrix.size());
  std::string message;
  ASSERT_TRUE(
      evaluateHllcOnDevice(inputs.data(), first.data(), static_cast<int>(inputs.size()), &message))
      << message;
  ASSERT_TRUE(
      evaluateHllcOnDevice(inputs.data(), second.data(), static_cast<int>(inputs.size()), &message))
      << message;

  for (size_t i = 0; i < matrix.size(); ++i) {
    for (int k = 0; k < 5; ++k) {
      EXPECT_EQ(first[i].flux[k], second[i].flux[k]) << matrix[i].name << " component " << k;
    }
    EXPECT_EQ(first[i].sLeft, second[i].sLeft) << matrix[i].name;
    EXPECT_EQ(first[i].sContact, second[i].sContact) << matrix[i].name;
    EXPECT_EQ(first[i].sRight, second[i].sRight) << matrix[i].name;
    EXPECT_EQ(first[i].branch, second[i].branch) << matrix[i].name;
    EXPECT_EQ(first[i].starDensityLeft, second[i].starDensityLeft) << matrix[i].name;
  }
  std::printf("\n  G4 GPU determinism: %zu rows repeated, flux/speeds/branch BITWISE identical\n",
              matrix.size());
}

// The V2 control on device: deleting the contact wave must break the contact THERE TOO. Without
// this, a GPU path could silently be a different scheme and the equivalence test above would still
// pass, because it only compares GPU against CPU -- not against physics.
TEST(HllcGpuEquivalenceTest, V2_ContactWaveDeletionBreaksTheContactOnDeviceToo) {
  if (!hllcDeviceAvailable()) {
    GTEST_SKIP() << "no CUDA device available";
  }
  // find the stationary-contact row
  int idx = -1;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    if (std::string(ref::kCases[i].name) == "stationary_contact") idx = i;
  }
  ASSERT_GE(idx, 0);
  const ref::Case& c = ref::kCases[idx];

  HllcDeviceInput in{};
  in.left = toState(c.left);
  in.right = toState(c.right);
  in.normal = Vector3{c.nx, c.ny, c.nz};
  in.gamma = ref::kGamma;

  HllcDeviceOutput hllc{};
  HllcDeviceOutput hll{};
  std::string message;
  ASSERT_TRUE(evaluateHllcOnDevice(&in, &hllc, 1, &message)) << message;
  ASSERT_TRUE(evaluateHllOnDeviceForControlOnly(&in, &hll, 1, &message)) << message;

  // HLLC resolves it exactly, on device as on host
  EXPECT_EQ(hllc.flux[0], 0.0) << "the device HLLC must carry NO mass across a stationary contact";
  // HLL does not, and by the amount the 60-digit reference predicts
  EXPECT_GT(std::abs(hll.flux[0]), 100.0);
  const Real expected = std::abs(c.hllFlux[0]);
  EXPECT_LE(std::abs(std::abs(hll.flux[0]) - expected) / expected, 32.0 * 2.2204460492503131e-16);

  // and it must match the HOST's HLL bitwise, so the control is the same defect on both sides
  const HllcResult hostHll = hllFluxForControlOnly(in.left, in.right, in.normal, in.gamma);
  EXPECT_EQ(hll.flux[0], hostHll.flux.mass);
  EXPECT_EQ(hll.flux[4], hostHll.flux.energy);

  std::printf("\n  V2 on device: HLLC mass flux %.17g, HLL mass flux %.10f (reference %.10f)\n",
              hllc.flux[0], std::abs(hll.flux[0]), expected);
}
