// P18.8 -- E11 (invalid states on device) and the LIVE CONTROLS for E13 (CUDA diagnostics).
//
// WHY THE CONTROLS EXIST. A clean compute-sanitizer log is indistinguishable from a sanitizer that
// never instrumented anything: both print zero errors. This project has been bitten by exactly that
// shape of probe before. So E13 is reported only alongside a control that the tool MUST flag, run
// against the same binary in the same environment -- and, for the strongest form, in the SAME
// invocation as the real kernels, so "the tool was live while my kernels ran" is a measurement
// rather than an assumption.
//
// The controls are DISABLED_ so an ordinary ctest run never executes them -- a deliberate
// out-of-bounds access must not become a routine test failure -- and are invoked explicitly with
// --gtest_also_run_disabled_tests under the sanitizer.
//
// NOTE ON SCOPE, stated here because the evidence must not overclaim: these kernels use NO shared
// memory and NO __syncthreads, so racecheck and synccheck have nothing in their problem domain to
// find. A clean racecheck confirms the absence of shared-memory hazards, which follows from the
// absence of shared memory; it is not evidence about cross-thread interference generally. That is
// covered structurally (one thread per cell, no atomics, no cross-thread reads) and empirically by
// E10's block-size independence.
#include <gtest/gtest.h>

#include <cuda_runtime.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceSpeciesLaunch.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::gpu::DeviceArrhenius;
using cfd::gpu::DeviceBuffer;
using cfd::gpu::DeviceMechanism;
using cfd::gpu::DeviceReactorStep;
using cfd::gpu::DeviceSpeciesControls;
using cfd::gpu::DeviceSpeciesStatus;

namespace {

constexpr Real kR = 8.31446261815324;

#define REQUIRE_GPU()                                               \
  if (!cfd::gpu::speciesDeviceAvailable()) {                        \
    GTEST_SKIP() << "no CUDA device present; nothing was compared"; \
  }

DeviceMechanism twoSpeciesMechanism() {
  DeviceMechanism m{};
  m.speciesCount = 2;
  m.reactionCount = 1;
  const Real weights[2] = {0.0625, 0.0625};
  const Real formation[2] = {0.0, -2000000.0};
  for (Index i = 0; i < 2; ++i) {
    m.species[i].molecularWeight = weights[i];
    m.species[i].gasConstant = kR / weights[i];
    m.species[i].specificHeat = 1000.0;
    m.species[i].formationEnthalpy = formation[i];
  }
  m.reactions[0].reactant[0] = 1.0;
  m.reactions[0].product[1] = 1.0;
  m.reactions[0].net[0] = -1.0;
  m.reactions[0].net[1] = 1.0;
  m.forward[0] = DeviceArrhenius{5.0, 0.0, 3.0e4};
  return m;
}

const DeviceSpeciesControls kProduction{};

}  // namespace

// =================================================================================================
// E11 -- the device refuses what the CPU refuses, and names the SAME first offender
// =================================================================================================

TEST(P188DeviceInvalidState, E11_TheDeviceRefusesWhatTheHostRefusesAndNamesTheSameReason) {
  REQUIRE_GPU();
  const auto mechanism = twoSpeciesMechanism();

  // Each row is a state the thermochemistry must refuse, paired with the reason the CPU gives.
  // FIRST FAILURE WINS: the reason must be the ROOT CAUSE, not a downstream consequence. The
  // host-side version of this is link one's N11 control, which is what caught the device status
  // being last-write-wins (logs/02_first_failure_PRESERVED.log).
  struct Row {
    const char* name;
    Real density;
    Real temperature;
    Real massFractionA;
    DeviceSpeciesStatus expected;
  };
  const Row rows[] = {
      {"T = -5 K", 0.8, -5.0, 0.5, DeviceSpeciesStatus::NonPositiveTemperature},
      {"T = 0 K", 0.8, 0.0, 0.5, DeviceSpeciesStatus::NonPositiveTemperature},
      {"T = NaN", 0.8, std::numeric_limits<Real>::quiet_NaN(), 0.5,
       DeviceSpeciesStatus::NonFiniteTemperature},
      {"T = +inf", 0.8, std::numeric_limits<Real>::infinity(), 0.5,
       DeviceSpeciesStatus::NonFiniteTemperature},
      {"rho = 0", 0.0, 1200.0, 0.5, DeviceSpeciesStatus::NonPositiveDensity},
      {"rho = -1", -1.0, 1200.0, 0.5, DeviceSpeciesStatus::NonPositiveDensity},
      {"a valid state", 0.8, 1200.0, 0.5, DeviceSpeciesStatus::Ok},
  };
  const Index cells = static_cast<Index>(sizeof(rows) / sizeof(rows[0]));

  std::vector<Real> composition(static_cast<std::size_t>(cells * 2));
  std::vector<Real> density(static_cast<std::size_t>(cells));
  std::vector<Real> temperature(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    composition[(0 * cells) + c] = rows[c].massFractionA;
    composition[(1 * cells) + c] = 1.0 - rows[c].massFractionA;
    density[c] = rows[c].density;
    temperature[c] = rows[c].temperature;
  }

  DeviceBuffer<Real> deviceComposition(cells * 2);
  deviceComposition.uploadFrom(composition.data(), cells * 2);
  DeviceBuffer<Real> deviceDensity(cells);
  deviceDensity.uploadFrom(density.data(), cells);
  DeviceBuffer<Real> deviceTemperature(cells);
  deviceTemperature.uploadFrom(temperature.data(), cells);

  DeviceBuffer<Real> compositionOut(cells * 2);
  DeviceBuffer<Real> temperatureOut(cells);
  DeviceBuffer<Real> progressRateOut(cells);
  DeviceBuffer<Real> rateCoefficientOut(cells);
  DeviceBuffer<Real> omegaOut(cells * 2);
  DeviceBuffer<Real> omegaSumOut(cells);
  DeviceBuffer<Real> heatReleaseOut(cells);
  DeviceBuffer<Real> sensibleBeforeOut(cells);
  DeviceBuffer<Real> sensibleAfterOut(cells);
  DeviceBuffer<Real> chemicalBeforeOut(cells);
  DeviceBuffer<Real> chemicalAfterOut(cells);
  DeviceBuffer<int> branchOut(cells);
  DeviceBuffer<int> statusOut(cells);

  cfd::gpu::advanceReactorDevice(
      mechanism, deviceComposition, deviceDensity, deviceTemperature, cells, 1.0e-6, 1.0e-6, 1.0e5,
      kProduction, false, false, 0.0, compositionOut, temperatureOut, progressRateOut,
      rateCoefficientOut, omegaOut, omegaSumOut, heatReleaseOut, sensibleBeforeOut,
      sensibleAfterOut, chemicalBeforeOut, chemicalAfterOut, branchOut, statusOut);

  std::vector<int> gpuStatus(static_cast<std::size_t>(cells));
  statusOut.downloadTo(gpuStatus.data(), cells);

  Index refused = 0;
  for (Index c = 0; c < cells; ++c) {
    // the host, from the same shared source
    Real local[2] = {composition[(0 * cells) + c], composition[(1 * cells) + c]};
    DeviceReactorStep expected;
    cfd::gpu::deviceAdvanceReactorCell(mechanism, local, density[c], temperature[c], 1.0e-6, 1.0e-6,
                                       1.0e5, kProduction, false, false, 0.0, &expected);

    EXPECT_EQ(static_cast<int>(expected.status), static_cast<int>(rows[c].expected))
        << rows[c].name << ": the HOST reason changed; the expectation table is stale";
    EXPECT_EQ(gpuStatus[c], static_cast<int>(rows[c].expected))
        << rows[c].name << ": device reported " << gpuStatus[c] << ", expected "
        << static_cast<int>(rows[c].expected);
    EXPECT_EQ(gpuStatus[c], static_cast<int>(expected.status))
        << rows[c].name << ": host and device disagree on the reason";
    if (rows[c].expected != DeviceSpeciesStatus::Ok) {
      ++refused;
    }
  }
  // NON-VACUITY: the table must contain states that are actually refused AND one that is not, or
  // "host and device agree" would be a statement about nothing.
  EXPECT_EQ(refused, cells - 1) << "every row but the last must be refused";
  EXPECT_EQ(gpuStatus[cells - 1], static_cast<int>(DeviceSpeciesStatus::Ok))
      << "the valid row must be ACCEPTED, or the suite only ever tests rejection";
}

// =================================================================================================
// E13 LIVE CONTROLS -- DISABLED_, run explicitly under compute-sanitizer
// =================================================================================================

TEST(P188SanitizerControl, DISABLED_SANITIZER_CONTROL_OutOfBoundsDeviceAccess) {
  REQUIRE_GPU();
  // A deliberate read past the end of a device allocation. compute-sanitizer --tool memcheck MUST
  // report this. If it does not, memcheck is not instrumenting this binary and every clean
  // memcheck result in the evidence is worthless.
  //
  // Done with a raw cudaMemcpy rather than a kernel so no production source needs a defect in it,
  // and so the control cannot drift from the kernels it vouches for.
  const Index elements = 256;
  DeviceBuffer<Real> buffer(elements);
  std::vector<Real> hostSide(static_cast<std::size_t>(elements), 1.0);
  buffer.uploadFrom(hostSide.data(), elements);

  std::vector<Real> target(static_cast<std::size_t>(elements * 4), 0.0);
  // 4x the allocation: reads 3/4 of the way past the end
  const cudaError_t status =
      cudaMemcpy(target.data(), buffer.data(),
                 static_cast<std::size_t>(elements * 4) * sizeof(Real), cudaMemcpyDeviceToHost);
  std::cout << "  deliberate out-of-bounds cudaMemcpy returned: " << cudaGetErrorName(status)
            << "\n  compute-sanitizer --tool memcheck MUST have reported an error above.\n";
  // The call's own return code is NOT the control. cudaMemcpy may well return success for a read
  // inside the same CUDA allocation granule; it is the SANITIZER's report that is being tested, and
  // that is checked by the harness in tools/, which greps the sanitizer output.
  static_cast<void>(status);
  // clear any sticky error so the process exits cleanly and the harness reads the sanitizer's
  // verdict rather than a crash
  static_cast<void>(cudaGetLastError());
}

TEST(P188SanitizerControl, DISABLED_SANITIZER_CONTROL_UninitialisedDeviceRead) {
  REQUIRE_GPU();
  // A PRODUCTION kernel launched over a buffer that was allocated and never written, so the kernel
  // reads uninitialised device memory. compute-sanitizer --tool initcheck MUST report this.
  //
  // This uses the real equationOfStateKernel through its real launcher -- the control exercises the
  // same code path the clean runs do, with one input deliberately left unwritten, so it proves
  // initcheck is live ON THESE KERNELS rather than on a toy.
  const Index cells = 1024;
  DeviceBuffer<Real> gasConstant(cells);  // DELIBERATELY never uploaded
  DeviceBuffer<Real> pressure(cells);
  DeviceBuffer<Real> temperature(cells);
  DeviceBuffer<Real> density(cells);
  std::vector<Real> ones(static_cast<std::size_t>(cells), 1.0);
  pressure.uploadFrom(ones.data(), cells);
  temperature.uploadFrom(ones.data(), cells);
  density.uploadFrom(ones.data(), cells);
  DeviceBuffer<Real> densityOut(cells);
  DeviceBuffer<Real> pressureOut(cells);
  DeviceBuffer<Real> temperatureOut(cells);

  cfd::gpu::computeEquationOfStateDevice(gasConstant, pressure, temperature, density, cells,
                                         densityOut, pressureOut, temperatureOut);
  std::vector<Real> out(static_cast<std::size_t>(cells));
  densityOut.downloadTo(out.data(), cells);
  std::cout << "  a production kernel read an UNINITIALISED device buffer.\n"
            << "  compute-sanitizer --tool initcheck MUST have reported errors above.\n";
}

TEST(P188SanitizerControl, DISABLED_SANITIZER_SCOPE_EveryKernelIsLaunched) {
  REQUIRE_GPU();
  // Exercises all five production kernels in one process, so a sanitizer run over this filter has
  // demonstrably had every kernel in its scope. Paired in the harness with the controls above,
  // which fire in the same invocation and prove the tool was live while these ran.
  DeviceMechanism mechanism = twoSpeciesMechanism();
  const Index cells = 512;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;

  std::vector<Real> composition(static_cast<std::size_t>(cells * species));
  std::vector<Real> density(static_cast<std::size_t>(cells));
  std::vector<Real> temperature(static_cast<std::size_t>(cells));
  std::vector<Real> pressure(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    const Real a = 0.3 + (0.4 * static_cast<Real>(c) / static_cast<Real>(cells));
    composition[(0 * cells) + c] = a;
    composition[(1 * cells) + c] = 1.0 - a;
    density[c] = 0.8;
    temperature[c] = 1100.0 + static_cast<Real>(c % 23);
    pressure[c] = 101325.0;
  }

  DeviceBuffer<Real> deviceComposition(cells * species);
  deviceComposition.uploadFrom(composition.data(), cells * species);
  DeviceBuffer<Real> deviceDensity(cells);
  deviceDensity.uploadFrom(density.data(), cells);
  DeviceBuffer<Real> deviceTemperature(cells);
  deviceTemperature.uploadFrom(temperature.data(), cells);
  DeviceBuffer<Real> devicePressure(cells);
  devicePressure.uploadFrom(pressure.data(), cells);

  DeviceBuffer<Real> molecularWeight(cells);
  DeviceBuffer<Real> gasConstant(cells);
  DeviceBuffer<Real> specificHeatP(cells);
  DeviceBuffer<Real> specificHeatV(cells);
  DeviceBuffer<Real> specificHeatRatio(cells);
  DeviceBuffer<int> status(cells);
  cfd::gpu::computeMixturePropertiesDevice(mechanism, deviceComposition, cells, kProduction,
                                           molecularWeight, gasConstant, specificHeatP,
                                           specificHeatV, specificHeatRatio, status);

  DeviceBuffer<Real> densityOut(cells);
  DeviceBuffer<Real> pressureOut(cells);
  DeviceBuffer<Real> temperatureOut(cells);
  cfd::gpu::computeEquationOfStateDevice(gasConstant, devicePressure, deviceTemperature,
                                         deviceDensity, cells, densityOut, pressureOut,
                                         temperatureOut);

  DeviceBuffer<Real> rateCoefficient(cells * reactions);
  DeviceBuffer<int> branch(cells * reactions);
  cfd::gpu::computeRateCoefficientsDevice(mechanism, deviceTemperature, cells, kProduction,
                                          rateCoefficient, branch, status);

  DeviceBuffer<Real> concentration(cells * species);
  DeviceBuffer<Real> progressRate(cells * reactions);
  DeviceBuffer<Real> omega(cells * species);
  DeviceBuffer<Real> omegaSum(cells);
  DeviceBuffer<Real> heatRelease(cells);
  DeviceBuffer<Real> heatReleaseFromSources(cells);
  cfd::gpu::computeReactionSourcesDevice(
      mechanism, deviceComposition, deviceDensity, deviceTemperature, cells, kProduction,
      concentration, progressRate, omega, omegaSum, heatRelease, heatReleaseFromSources, status);

  DeviceBuffer<Real> compositionOut(cells * species);
  DeviceBuffer<Real> stepTemperatureOut(cells);
  DeviceBuffer<Real> sensibleBeforeOut(cells);
  DeviceBuffer<Real> sensibleAfterOut(cells);
  DeviceBuffer<Real> chemicalBeforeOut(cells);
  DeviceBuffer<Real> chemicalAfterOut(cells);
  cfd::gpu::advanceReactorDevice(mechanism, deviceComposition, deviceDensity, deviceTemperature,
                                 cells, 1.0e-6, 1.0e-6, 1.0e5, kProduction, false, false, 0.0,
                                 compositionOut, stepTemperatureOut, progressRate, rateCoefficient,
                                 omega, omegaSum, heatRelease, sensibleBeforeOut, sensibleAfterOut,
                                 chemicalBeforeOut, chemicalAfterOut, branch, status);

  std::cout << "  all five production kernels launched over " << cells << " cells:\n"
            << "    mixturePropertiesKernel, equationOfStateKernel, rateCoefficientKernel,\n"
            << "    reactionSourcesKernel, advanceReactorKernel\n";
  std::vector<Real> probe(static_cast<std::size_t>(cells));
  stepTemperatureOut.downloadTo(probe.data(), cells);
  EXPECT_GT(probe[0], 0.0) << "the kernels must have produced a usable result";
}
