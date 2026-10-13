// P18.8 -- LINK TWO OF TWO: nvcc's DEVICE code against the host-compiled shared header.
//
// tests/unit/species/test_device_species_equivalence.cpp compares PRODUCTION against the shared
// header compiled for the HOST, and runs in the CPU-only configuration. This file compares the
// same header compiled for the DEVICE against that host compilation, by actually launching the
// kernels. Together the two links chain production -> host restatement -> device.
//
// WHY TWO FILES AND NOT ONE. A single end-to-end comparison would conflate "the restatement is
// wrong" with "nvcc's device codegen differs", and on a failure would name neither. P17.10 split it
// for that reason and P18.8 follows.
//
// TARGET: BITWISE, which G9 requires. The thermochemistry is CELL-LOCAL -- one thread per cell, no
// atomics, no cross-thread reduction -- so of G9's five conditions only (1) one shared source and
// (4) --fmad=false are doing any work here. The kernel is registered with --fmad=false in
// cuda/CMakeLists.txt.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "SpeciesEquivalenceCompare.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceSpeciesLaunch.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

using cfd::Index;
using cfd::Real;
using cfd::gpu::DeviceArrhenius;
using cfd::gpu::DeviceBuffer;
using cfd::gpu::DeviceMechanism;
using cfd::gpu::DeviceReaction;
using cfd::gpu::DeviceReactorStep;
using cfd::gpu::DeviceSpecies;
using cfd::gpu::DeviceSpeciesControls;
using cfd::gpu::DeviceSpeciesStatus;

namespace {

constexpr Real kR = 8.31446261815324;

#define REQUIRE_GPU()                                               \
  if (!cfd::gpu::speciesDeviceAvailable()) {                        \
    GTEST_SKIP() << "no CUDA device present; nothing was compared"; \
  }

// The three-species, two-reaction mechanism with DELIBERATELY DISTINCT weights, specific heats,
// formation enthalpies and kinetics -- section 34, so a species permutation cannot pass unnoticed.
DeviceMechanism chainMechanism() {
  DeviceMechanism m{};
  m.speciesCount = 3;
  m.reactionCount = 2;
  const Real weights[3] = {0.0625, 0.125, 0.1875};
  const Real heats[3] = {900.0, 1300.0, 1700.0};
  const Real formation[3] = {0.0, -1500000.0, 2500000.0};
  for (Index i = 0; i < 3; ++i) {
    m.species[i].molecularWeight = weights[i];
    m.species[i].gasConstant = kR / weights[i];
    m.species[i].specificHeat = heats[i];
    m.species[i].formationEnthalpy = formation[i];
  }
  // A + B -> C
  m.reactions[0].reactant[0] = 1.0;
  m.reactions[0].reactant[1] = 1.0;
  m.reactions[0].product[2] = 1.0;
  for (Index i = 0; i < 3; ++i) {
    m.reactions[0].net[i] = m.reactions[0].product[i] - m.reactions[0].reactant[i];
  }
  m.forward[0] = DeviceArrhenius{3.0, 0.5, 4.0e4};
  // C <-> A + B
  m.reactions[1].reactant[2] = 1.0;
  m.reactions[1].product[0] = 1.0;
  m.reactions[1].product[1] = 1.0;
  for (Index i = 0; i < 3; ++i) {
    m.reactions[1].net[i] = m.reactions[1].product[i] - m.reactions[1].reactant[i];
  }
  m.reactions[1].reversible = true;
  m.forward[1] = DeviceArrhenius{1.5, -0.25, 6.0e4};
  m.reverse[1] = DeviceArrhenius{0.75, -0.25, 6.0e4};
  return m;
}

// A spatially varying state, so a kernel that computed one cell and broadcast it could not pass.
struct HostState {
  Index cells{};
  std::vector<Real> composition;  // SPECIES-MAJOR
  std::vector<Real> density;
  std::vector<Real> temperature;
  std::vector<Real> pressure;
};

HostState makeState(Index cells, Index speciesCount) {
  HostState s;
  s.cells = cells;
  s.composition.resize(static_cast<std::size_t>(cells * speciesCount));
  s.density.resize(static_cast<std::size_t>(cells));
  s.temperature.resize(static_cast<std::size_t>(cells));
  s.pressure.resize(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    const Real t = static_cast<Real>(c) / static_cast<Real>(cells);
    const Real a = 0.2 + (0.5 * t);
    const Real b = 0.3 - (0.1 * t);
    s.composition[(0 * cells) + c] = a;
    s.composition[(1 * cells) + c] = b;
    s.composition[(2 * cells) + c] = 1.0 - a - b;  // the exact remainder
    s.density[c] = 0.7 + (0.01 * static_cast<Real>(c % 17));
    s.temperature[c] = 900.0 + (7.0 * static_cast<Real>(c % 101));
    s.pressure[c] = 90000.0 + (500.0 * static_cast<Real>(c % 53));
  }
  return s;
}

// One cell's composition, gathered out of the species-major layout exactly as the kernel does.
std::vector<Real> cellComposition(const HostState& s, Index speciesCount, Index cell) {
  std::vector<Real> out(static_cast<std::size_t>(speciesCount));
  for (Index i = 0; i < speciesCount; ++i) {
    out[i] = s.composition[(i * s.cells) + cell];
  }
  return out;
}

const DeviceSpeciesControls kProduction{};

// A2's instrument and its frozen bound, from the shared header the dry-run self-tests.
using cfd::species_equivalence::compare;
using cfd::species_equivalence::kDerivedUlpBound;
using cfd::species_equivalence::kEps;
using cfd::species_equivalence::progressRateTermScale;
using cfd::species_equivalence::report;
using cfd::species_equivalence::ulpDistance;

}  // namespace

// =================================================================================================
// E1, E2 -- mixture properties and EOS, on DEVICE
// =================================================================================================

// E1's FROZEN CASE LIST, on device: "single-species, binary, ternary, near-pure (Y -> 1-eps),
// spatially varying, x 5 temperatures" (acceptance_gate.md E1). The spatially-varying ternary case
// is the test below this one; this one covers the four compositions that case does not reach.
//
// Added after mapping each criterion to the test that measures it: the device suite had only the
// ternary case, so four fifths of E1's own list was unmeasured on device even though link one
// covered binary and ternary on the host.
//
// ON THE 5 TEMPERATURES: these mixtures use CONSTANT cp per species, which is all DeviceSpecies
// carries, so M_mix, R_mix and cp_mix cannot depend on T at all. The temperature loop below is run
// because the frozen list says so, and it is recorded here as INERT FOR THIS MECHANISM FAMILY
// rather than presented as coverage. The real limitation behind that -- the device restatement
// supports constant cp only, while the CPU production path supports ranged cp -- is a capability
// gap recorded in kernel_inventory.md, not something this test can close.
TEST(P188GpuEquivalence, E1_EveryCompositionCaseInTheFrozenListIsBitwiseOnDevice) {
  REQUIRE_GPU();

  struct Case {
    const char* name;
    Index speciesCount;
    Real composition[3];
  };
  const Real nearPure = 1.0 - std::numeric_limits<Real>::epsilon();
  const Case cases[] = {
      {"single-species", 1, {1.0, 0.0, 0.0}},
      {"binary", 2, {0.375, 0.625, 0.0}},
      {"ternary", 3, {0.25, 0.3125, 0.4375}},
      {"near-pure (Y -> 1-eps)", 3, {nearPure, std::numeric_limits<Real>::epsilon(), 0.0}},
  };
  const Real temperatures[5] = {300.0, 700.0, 1200.0, 1800.0, 2400.0};

  Index comparisons = 0;
  for (const Case& one : cases) {
    // the chain mechanism truncated to this case's species count, so the weights, heats and
    // formation enthalpies stay the deliberately distinct ones
    DeviceMechanism mechanism = chainMechanism();
    mechanism.speciesCount = one.speciesCount;
    mechanism.reactionCount = 0;

    for (const Real temperature : temperatures) {
      const Index cells = 64;
      std::vector<Real> composition(static_cast<std::size_t>(cells * one.speciesCount));
      for (Index i = 0; i < one.speciesCount; ++i) {
        for (Index c = 0; c < cells; ++c) {
          composition[(i * cells) + c] = one.composition[i];
        }
      }
      DeviceBuffer<Real> deviceComposition(cells * one.speciesCount);
      deviceComposition.uploadFrom(composition.data(), cells * one.speciesCount);
      DeviceBuffer<Real> molecularWeight(cells);
      DeviceBuffer<Real> gasConstant(cells);
      DeviceBuffer<Real> specificHeatP(cells);
      DeviceBuffer<Real> specificHeatV(cells);
      DeviceBuffer<Real> specificHeatRatio(cells);
      DeviceBuffer<int> status(cells);
      cfd::gpu::computeMixturePropertiesDevice(mechanism, deviceComposition, cells, kProduction,
                                               molecularWeight, gasConstant, specificHeatP,
                                               specificHeatV, specificHeatRatio, status);

      std::vector<Real> gpuM(static_cast<std::size_t>(cells));
      std::vector<Real> gpuR(static_cast<std::size_t>(cells));
      std::vector<Real> gpuCp(static_cast<std::size_t>(cells));
      std::vector<Real> gpuCv(static_cast<std::size_t>(cells));
      std::vector<Real> gpuGamma(static_cast<std::size_t>(cells));
      molecularWeight.downloadTo(gpuM.data(), cells);
      gasConstant.downloadTo(gpuR.data(), cells);
      specificHeatP.downloadTo(gpuCp.data(), cells);
      specificHeatV.downloadTo(gpuCv.data(), cells);
      specificHeatRatio.downloadTo(gpuGamma.data(), cells);

      DeviceSpeciesStatus hostStatus = DeviceSpeciesStatus::Ok;
      const Real m =
          cfd::gpu::deviceMolecularWeight(mechanism, one.composition, kProduction, &hostStatus);
      const Real r = cfd::gpu::deviceGasConstant(mechanism, one.composition, kProduction);
      const Real cp = cfd::gpu::deviceSpecificHeatPressure(mechanism, one.composition, kProduction);
      const Real cv = cfd::gpu::deviceSpecificHeatVolume(cp, r);
      const Real gamma = cfd::gpu::deviceSpecificHeatRatio(cp, cv);

      for (Index c = 0; c < cells; ++c) {
        EXPECT_EQ(gpuM[c], m) << one.name << " at T = " << temperature << ", cell " << c;
        EXPECT_EQ(gpuR[c], r) << one.name << " at T = " << temperature << ", cell " << c;
        EXPECT_EQ(gpuCp[c], cp) << one.name << " at T = " << temperature << ", cell " << c;
        EXPECT_EQ(gpuCv[c], cv) << one.name << " at T = " << temperature << ", cell " << c;
        EXPECT_EQ(gpuGamma[c], gamma) << one.name << " at T = " << temperature << ", cell " << c;
        ++comparisons;
      }
      // the single-species case must give exactly that species' own properties, which is an
      // independent check on the mixture rule rather than a comparison of two implementations
      if (one.speciesCount == 1) {
        EXPECT_EQ(m, mechanism.species[0].molecularWeight)
            << "a single-species mixture must have exactly that species' molecular weight";
        EXPECT_EQ(cp, mechanism.species[0].specificHeat)
            << "a single-species mixture must have exactly that species' cp";
      }
    }
  }
  std::cout << "    E1 case list on device: " << comparisons
            << " comparisons over 4 compositions x 5 temperatures, all bitwise\n"
            << "    (the 5 temperatures are INERT here: DeviceSpecies carries constant cp only)\n";
  EXPECT_EQ(comparisons, 4 * 5 * 64);
}

TEST(P188GpuEquivalence, MixturePropertiesAreBitwiseOnDevice) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 1024;
  const auto host = makeState(cells, mechanism.speciesCount);

  DeviceBuffer<Real> composition(cells * mechanism.speciesCount);
  composition.uploadFrom(host.composition.data(), cells * mechanism.speciesCount);
  DeviceBuffer<Real> molecularWeight(cells);
  DeviceBuffer<Real> gasConstant(cells);
  DeviceBuffer<Real> specificHeatP(cells);
  DeviceBuffer<Real> specificHeatV(cells);
  DeviceBuffer<Real> specificHeatRatio(cells);
  DeviceBuffer<int> status(cells);

  cfd::gpu::computeMixturePropertiesDevice(mechanism, composition, cells, kProduction,
                                           molecularWeight, gasConstant, specificHeatP,
                                           specificHeatV, specificHeatRatio, status);

  std::vector<Real> gpuM(cells);
  std::vector<Real> gpuR(cells);
  std::vector<Real> gpuCp(cells);
  std::vector<Real> gpuCv(cells);
  std::vector<Real> gpuGamma(cells);
  std::vector<int> gpuStatus(cells);
  molecularWeight.downloadTo(gpuM.data(), cells);
  gasConstant.downloadTo(gpuR.data(), cells);
  specificHeatP.downloadTo(gpuCp.data(), cells);
  specificHeatV.downloadTo(gpuCv.data(), cells);
  specificHeatRatio.downloadTo(gpuGamma.data(), cells);
  status.downloadTo(gpuStatus.data(), cells);

  Index bitwise = 0;
  for (Index c = 0; c < cells; ++c) {
    const auto local = cellComposition(host, mechanism.speciesCount, c);
    DeviceSpeciesStatus hostStatus = DeviceSpeciesStatus::Ok;
    const Real m =
        cfd::gpu::deviceMolecularWeight(mechanism, local.data(), kProduction, &hostStatus);
    const Real r = cfd::gpu::deviceGasConstant(mechanism, local.data(), kProduction);
    const Real cp = cfd::gpu::deviceSpecificHeatPressure(mechanism, local.data(), kProduction);
    const Real cv = cfd::gpu::deviceSpecificHeatVolume(cp, r);
    const Real gamma = cfd::gpu::deviceSpecificHeatRatio(cp, cv);
    // mixturePropertiesKernel's own extra check, restated here so the status comparison below
    // compares the SAME predicate rather than happening to agree
    if (!(cv > 0.0) && hostStatus == DeviceSpeciesStatus::Ok) {
      hostStatus = DeviceSpeciesStatus::NonPositiveSpecificHeatVolume;
    }

    EXPECT_EQ(gpuM[c], m) << "M_mix at cell " << c;
    EXPECT_EQ(gpuR[c], r) << "R_mix at cell " << c;
    EXPECT_EQ(gpuCp[c], cp) << "cp_mix at cell " << c;
    EXPECT_EQ(gpuCv[c], cv) << "cv_mix at cell " << c;
    EXPECT_EQ(gpuGamma[c], gamma) << "gamma at cell " << c;
    EXPECT_EQ(gpuStatus[c], static_cast<int>(hostStatus)) << "status at cell " << c;
    if (gpuM[c] == m && gpuR[c] == r && gpuCp[c] == cp) {
      ++bitwise;
    }
  }
  EXPECT_EQ(bitwise, cells) << bitwise << " of " << cells << " cells bitwise";
  // the state really did vary, so a broadcast could not have passed
  EXPECT_NE(gpuM[0], gpuM[cells - 1]);
}

TEST(P188GpuEquivalence, EquationOfStateIsBitwiseOnDevice) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 512;
  const auto host = makeState(cells, mechanism.speciesCount);

  std::vector<Real> hostR(cells);
  for (Index c = 0; c < cells; ++c) {
    const auto local = cellComposition(host, mechanism.speciesCount, c);
    hostR[c] = cfd::gpu::deviceGasConstant(mechanism, local.data(), kProduction);
  }

  DeviceBuffer<Real> gasConstant(cells);
  gasConstant.uploadFrom(hostR.data(), cells);
  DeviceBuffer<Real> pressure(cells);
  pressure.uploadFrom(host.pressure.data(), cells);
  DeviceBuffer<Real> temperature(cells);
  temperature.uploadFrom(host.temperature.data(), cells);
  DeviceBuffer<Real> density(cells);
  density.uploadFrom(host.density.data(), cells);
  DeviceBuffer<Real> densityOut(cells);
  DeviceBuffer<Real> pressureOut(cells);
  DeviceBuffer<Real> temperatureOut(cells);

  cfd::gpu::computeEquationOfStateDevice(gasConstant, pressure, temperature, density, cells,
                                         densityOut, pressureOut, temperatureOut);

  std::vector<Real> gpuRho(cells);
  std::vector<Real> gpuP(cells);
  std::vector<Real> gpuT(cells);
  densityOut.downloadTo(gpuRho.data(), cells);
  pressureOut.downloadTo(gpuP.data(), cells);
  temperatureOut.downloadTo(gpuT.data(), cells);

  for (Index c = 0; c < cells; ++c) {
    EXPECT_EQ(gpuRho[c],
              cfd::gpu::deviceDensityFromPressure(host.pressure[c], hostR[c], host.temperature[c]))
        << "rho at cell " << c;
    EXPECT_EQ(gpuP[c], cfd::gpu::devicePressure(host.density[c], hostR[c], host.temperature[c]))
        << "p at cell " << c;
    EXPECT_EQ(gpuT[c],
              cfd::gpu::deviceTemperatureFromPressure(host.pressure[c], host.density[c], hostR[c]))
        << "T at cell " << c;
  }
}

// =================================================================================================
// DIAGNOSTIC 2 -- can the two libm implementations take DIFFERENT BRANCHES?
//
// Amendment A1 selects the direct form k = (A T^n) exp(arg) when A*T^n is finite and a log-space
// form otherwise. That predicate reads pow()'s OUTPUT. If host pow() returns DBL_MAX where device
// pow() returns inf for the same T, the two backends run DIFFERENT ALGORITHMS, and their results
// then differ by far more than a last-bit libm disagreement.
//
// This probe walks T across consecutive doubles around the T where A*T^n crosses DBL_MAX, with Ea
// large enough that the true k is finite -- which is exactly the regime A1 was written for -- and
// reports whether any T produced a branch disagreement, and what the value difference was there.
//
// It asserts nothing about magnitude. It answers one yes/no question, because the answer changes
// what a correct replacement criterion has to say.
TEST(P188GpuEquivalence, DIAGNOSTIC_CanHostAndDeviceTakeDifferentArrheniusBranches) {
  REQUIRE_GPU();

  const Real preExponential = 3.0;
  const Real exponent = 100.0;
  // Ea chosen so exp(-Ea/(R_u T)) is about 1e-20 near the crossing: the true k is then finite
  // while A*T^n is at the edge of representability, which is the only regime where the two
  // branches of A1 can disagree and still both produce a number.
  const Real activationEnergy = 457000.0;

  const Real critical = std::pow(std::numeric_limits<Real>::max() / preExponential, 1.0 / exponent);
  // 4096 consecutive doubles centred on the crossing
  const Index cells = 4096;
  std::vector<Real> temperature(static_cast<std::size_t>(cells));
  Real walk = critical;
  for (Index i = 0; i < cells / 2; ++i) {
    walk = std::nextafter(walk, 0.0);
  }
  for (Index c = 0; c < cells; ++c) {
    temperature[c] = walk;
    walk = std::nextafter(walk, std::numeric_limits<Real>::infinity());
  }

  DeviceMechanism m{};
  m.speciesCount = 1;
  m.reactionCount = 1;
  m.species[0].molecularWeight = 0.0625;
  m.species[0].gasConstant = kR / 0.0625;
  m.species[0].specificHeat = 1000.0;
  m.forward[0] = DeviceArrhenius{preExponential, exponent, activationEnergy};

  DeviceBuffer<Real> deviceTemperature(cells);
  deviceTemperature.uploadFrom(temperature.data(), cells);
  DeviceBuffer<Real> rateCoefficient(cells);
  DeviceBuffer<int> branch(cells);
  DeviceBuffer<int> status(cells);
  cfd::gpu::computeRateCoefficientsDevice(m, deviceTemperature, cells, kProduction, rateCoefficient,
                                          branch, status);
  std::vector<Real> gpuK(static_cast<std::size_t>(cells));
  std::vector<int> gpuBranch(static_cast<std::size_t>(cells));
  rateCoefficient.downloadTo(gpuK.data(), cells);
  branch.downloadTo(gpuBranch.data(), cells);

  Index branchDisagreements = 0;
  Real worstRelativeAtDisagreement = 0.0;
  std::int64_t worstUlpWithSameBranch = 0;
  int firstHostBranch = -99;
  int lastHostBranch = -99;
  for (Index c = 0; c < cells; ++c) {
    DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
    int hostBranch = -1;
    const Real hostK = cfd::gpu::deviceRateCoefficient(m.forward[0], temperature[c], kProduction,
                                                       &st, &hostBranch);
    if (c == 0) {
      firstHostBranch = hostBranch;
    }
    lastHostBranch = hostBranch;
    if (gpuBranch[c] != hostBranch) {
      ++branchDisagreements;
      if (std::isfinite(hostK) && std::isfinite(gpuK[c]) && hostK != 0.0) {
        worstRelativeAtDisagreement =
            std::max(worstRelativeAtDisagreement, std::abs(gpuK[c] - hostK) / std::abs(hostK));
      }
      continue;
    }
    if (std::isfinite(hostK) && std::isfinite(gpuK[c]) && gpuK[c] != hostK) {
      std::int64_t a = 0;
      std::int64_t b = 0;
      std::memcpy(&a, &gpuK[c], sizeof(a));
      std::memcpy(&b, &hostK, sizeof(b));
      worstUlpWithSameBranch = std::max(worstUlpWithSameBranch, a > b ? (a - b) : (b - a));
    }
  }

  std::cout << "  T swept across " << cells << " consecutive doubles around " << critical << "\n"
            << "  host branch at the low end " << firstHostBranch << ", at the high end "
            << lastHostBranch << "  (1 = direct, 2 = log-space fallback)\n"
            << "  BRANCH DISAGREEMENTS host vs device: " << branchDisagreements << " / " << cells
            << "\n"
            << "  worst relative difference where the branches DISAGREED: "
            << worstRelativeAtDisagreement << "\n"
            << "  worst ULP distance where the branches AGREED: " << worstUlpWithSameBranch << "\n";

  // The probe is only informative if the sweep actually straddled the crossing.
  EXPECT_NE(firstHostBranch, lastHostBranch)
      << "the sweep never crossed A1's predicate, so it proves nothing about branch agreement; "
         "both ends took branch "
      << firstHostBranch;
}

// =================================================================================================
// E3, E4, E5, E6 -- kinetics, sources and heat release, on DEVICE
// =================================================================================================

// E3' (amendment A2). k carries the libm difference, so it is bounded rather than bitwise -- but
// the BRANCH is not, and a disagreement there would mean the two backends ran different algorithms.
TEST(P188GpuEquivalence, E3_RateCoefficientsWithinTheDerivedBoundAndBranchesIdentical) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 1024;
  const auto host = makeState(cells, mechanism.speciesCount);

  DeviceBuffer<Real> temperature(cells);
  temperature.uploadFrom(host.temperature.data(), cells);
  DeviceBuffer<Real> rateCoefficient(cells * mechanism.reactionCount);
  DeviceBuffer<int> branch(cells * mechanism.reactionCount);
  DeviceBuffer<int> status(cells);

  cfd::gpu::computeRateCoefficientsDevice(mechanism, temperature, cells, kProduction,
                                          rateCoefficient, branch, status);

  std::vector<Real> gpuK(cells * mechanism.reactionCount);
  std::vector<int> gpuBranch(cells * mechanism.reactionCount);
  rateCoefficient.downloadTo(gpuK.data(), cells * mechanism.reactionCount);
  branch.downloadTo(gpuBranch.data(), cells * mechanism.reactionCount);

  std::uint64_t worstUlp = 0;
  Index bitwise = 0;
  Index compared = 0;
  for (Index r = 0; r < mechanism.reactionCount; ++r) {
    for (Index c = 0; c < cells; ++c) {
      DeviceSpeciesStatus hostStatus = DeviceSpeciesStatus::Ok;
      int hostBranch = -1;
      const Real k = cfd::gpu::deviceRateCoefficient(mechanism.forward[r], host.temperature[c],
                                                     kProduction, &hostStatus, &hostBranch);
      const Index slot = (r * cells) + c;
      ASSERT_TRUE(std::isfinite(k) && std::isfinite(gpuK[slot]))
          << "a non-finite k at reaction " << r << " cell " << c
          << " would make the bound meaningless";
      const std::uint64_t ulp = ulpDistance(gpuK[slot], k);
      worstUlp = std::max(worstUlp, ulp);
      ++compared;
      if (ulp == 0) {
        ++bitwise;
      }
      EXPECT_LE(ulp, kDerivedUlpBound) << "k for reaction " << r << " at cell " << c << ": " << ulp
                                       << " ULP apart, bound " << kDerivedUlpBound;
      // THE BRANCH IS NOT BOUNDED. A1's A == 0 / direct / log-space decisions must agree exactly.
      EXPECT_EQ(gpuBranch[slot], hostBranch) << "branch for reaction " << r << " at cell " << c;
    }
  }
  std::cout << "    k: worst " << worstUlp << " ULP against the bound " << kDerivedUlpBound << ", "
            << bitwise << " of " << compared << " exactly bitwise\n";
  EXPECT_GT(compared, 0) << "nothing was compared";
}

// =================================================================================================
// DIAGNOSTIC for the preserved failure in logs/03_second_failure_PRESERVED.log.
//
// It changes no production code and asserts no new acceptance threshold. Its only job is to say
// WHICH call diverges, by isolating the three factors of k = (A * T^n) * exp(-Ea/(R_u T)):
//
//   case A   n = 0, Ea = 0     pow(T,0) and exp(0) are EXACTLY 1 by IEEE-754, so k == A exactly
//   case B   n = 0, Ea != 0    only exp() can differ
//   case C   n != 0, Ea = 0    only pow() can differ
//   case D   n != 0, Ea != 0   both
//
// If case A is bitwise and B/C are not, the divergence is confined to the libm calls and is not the
// arithmetic, the operand order, the parenthesisation or FMA contraction.
TEST(P188GpuEquivalence, DIAGNOSTIC_WhichCallDivergesAndByHowManyUlp) {
  REQUIRE_GPU();

  struct Probe {
    const char* name;
    Real exponent;
    Real activationEnergy;
    const char* isolates;
  };
  const Probe probes[4] = {
      {"A", 0.0, 0.0, "nothing -- pow(T,0) and exp(0) are exactly 1"},
      {"B", 0.0, 4.0e4, "exp() only"},
      {"C", 0.5, 0.0, "pow() only"},
      {"D", 0.5, 4.0e4, "exp() and pow()"},
  };

  const Index cells = 4096;
  std::vector<Real> temperature(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    temperature[c] = 300.0 + (0.5 * static_cast<Real>(c));  // 300 K to 2347.5 K
  }
  DeviceBuffer<Real> deviceTemperature(cells);
  deviceTemperature.uploadFrom(temperature.data(), cells);

  for (const Probe& probe : probes) {
    DeviceMechanism m{};
    m.speciesCount = 1;
    m.reactionCount = 1;
    m.species[0].molecularWeight = 0.0625;
    m.species[0].gasConstant = kR / 0.0625;
    m.species[0].specificHeat = 1000.0;
    m.forward[0] = DeviceArrhenius{3.0, probe.exponent, probe.activationEnergy};

    DeviceBuffer<Real> rateCoefficient(cells);
    DeviceBuffer<int> branch(cells);
    DeviceBuffer<int> status(cells);
    cfd::gpu::computeRateCoefficientsDevice(m, deviceTemperature, cells, kProduction,
                                            rateCoefficient, branch, status);
    std::vector<Real> gpuK(static_cast<std::size_t>(cells));
    rateCoefficient.downloadTo(gpuK.data(), cells);

    Index differing = 0;
    std::int64_t worstUlp = 0;
    Real worstRelative = 0.0;
    Index worstCell = 0;
    bool sawWorst = false;
    for (Index c = 0; c < cells; ++c) {
      DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
      int taken = -1;
      const Real hostK =
          cfd::gpu::deviceRateCoefficient(m.forward[0], temperature[c], kProduction, &st, &taken);
      if (gpuK[c] == hostK) {
        continue;
      }
      ++differing;
      std::int64_t a = 0;
      std::int64_t b = 0;
      std::memcpy(&a, &gpuK[c], sizeof(a));
      std::memcpy(&b, &hostK, sizeof(b));
      const std::int64_t ulp = a > b ? (a - b) : (b - a);
      const Real relative = std::abs(gpuK[c] - hostK) / std::abs(hostK);
      if (!sawWorst || ulp > worstUlp) {
        worstUlp = ulp;
        worstCell = c;
        sawWorst = true;
      }
      worstRelative = std::max(worstRelative, relative);
    }
    std::cout << "  case " << probe.name << "  n=" << probe.exponent
              << " Ea=" << probe.activationEnergy << "  isolates: " << probe.isolates << "\n"
              << "          differing cells " << differing << " / " << cells
              << ",  worst ULP distance " << worstUlp << " (cell "
              << (sawWorst ? std::to_string(worstCell) : std::string("none"))
              << "),  worst relative " << worstRelative << "\n";

    if (probe.exponent == 0.0 && probe.activationEnergy == 0.0) {
      // THE ONE ASSERTION. Case A has no inexact libm call at all, so if the arithmetic, the
      // operand order, the parenthesisation or FMA contraction were wrong it would fail here.
      EXPECT_EQ(differing, 0) << "case A is pure arithmetic and MUST be bitwise; " << differing
                              << " cells differ, so the cause is NOT confined to libm";
    } else {
      // Not a threshold -- a record that the divergence is real and that the instrument saw it.
      EXPECT_GT(differing, 0) << "case " << probe.name
                              << " was expected to exercise an inexact libm call, but every cell "
                                 "agreed; the isolation is then unproven";
    }
  }
}

// E4' / E5 / E6' (amendment A2). The libm difference originates in k and reaches omega and S_E
// only through q. So q is BOUNDED, the concentrations stay BITWISE, and the ACCUMULATIONS are
// compared from the device's OWN q under zero tolerance -- which keeps their order,
// parenthesisation and coefficients under a criterion with no tolerance to hide behind.
TEST(P188GpuEquivalence, E4_E5_E6_ProgressRatesBoundedAndAccumulationsBitwise) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 1024;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto host = makeState(cells, species);

  DeviceBuffer<Real> composition(cells * species);
  composition.uploadFrom(host.composition.data(), cells * species);
  DeviceBuffer<Real> density(cells);
  density.uploadFrom(host.density.data(), cells);
  DeviceBuffer<Real> temperature(cells);
  temperature.uploadFrom(host.temperature.data(), cells);
  DeviceBuffer<Real> concentration(cells * species);
  DeviceBuffer<Real> progressRate(cells * reactions);
  DeviceBuffer<Real> omega(cells * species);
  DeviceBuffer<Real> omegaSum(cells);
  DeviceBuffer<Real> heatRelease(cells);
  DeviceBuffer<Real> heatReleaseFromSources(cells);
  DeviceBuffer<int> status(cells);

  cfd::gpu::computeReactionSourcesDevice(mechanism, composition, density, temperature, cells,
                                         kProduction, concentration, progressRate, omega, omegaSum,
                                         heatRelease, heatReleaseFromSources, status);

  std::vector<Real> gpuC(cells * species);
  std::vector<Real> gpuQ(cells * reactions);
  std::vector<Real> gpuOmega(cells * species);
  std::vector<Real> gpuOmegaSum(cells);
  std::vector<Real> gpuHeat(cells);
  std::vector<Real> gpuHeat2(cells);
  concentration.downloadTo(gpuC.data(), cells * species);
  progressRate.downloadTo(gpuQ.data(), cells * reactions);
  omega.downloadTo(gpuOmega.data(), cells * species);
  omegaSum.downloadTo(gpuOmegaSum.data(), cells);
  heatRelease.downloadTo(gpuHeat.data(), cells);
  heatReleaseFromSources.downloadTo(gpuHeat2.data(), cells);

  // ---- E5: concentrations are BITWISE. (rho*Y)/M -- no transcendental, no accumulation.
  Index concentrationDiffering = 0;
  for (Index c = 0; c < cells; ++c) {
    const auto local = cellComposition(host, species, c);
    for (Index i = 0; i < species; ++i) {
      const Real expected = cfd::gpu::deviceConcentration(mechanism, i, host.density[c], local[i]);
      EXPECT_EQ(gpuC[(i * cells) + c], expected) << "C_" << i << " at cell " << c;
      if (gpuC[(i * cells) + c] != expected) {
        ++concentrationDiffering;
      }
    }
  }
  EXPECT_EQ(concentrationDiffering, 0);

  // ---- E4': q within 8 * eps * (sum of term magnitudes), and the branch identical
  Real worstQRatio = 0.0;
  Index qExercised = 0;
  for (Index r = 0; r < reactions; ++r) {
    for (Index c = 0; c < cells; ++c) {
      Real concentrations[8];
      for (Index i = 0; i < species; ++i) {
        concentrations[i] = gpuC[(i * cells) + c];
      }
      DeviceSpeciesStatus hostStatus = DeviceSpeciesStatus::Ok;
      const Real hostQ = cfd::gpu::deviceProgressRate(
          mechanism, r, concentrations, host.temperature[c], kProduction, &hostStatus);
      const Real scale = progressRateTermScale(mechanism, r, concentrations, host.temperature[c]);
      const Real deviceQ = gpuQ[(r * cells) + c];
      ASSERT_TRUE(std::isfinite(hostQ) && std::isfinite(deviceQ))
          << "a non-finite q at reaction " << r << " cell " << c;
      if (!(scale > 0.0)) {
        // nothing to bound against: then the two must agree exactly
        EXPECT_EQ(deviceQ, hostQ) << "q_" << r << " at cell " << c << " with a zero term scale";
        continue;
      }
      ++qExercised;
      const Real ratio = std::abs(deviceQ - hostQ) / (kEps * scale);
      worstQRatio = std::max(worstQRatio, ratio);
      EXPECT_LE(ratio, static_cast<Real>(kDerivedUlpBound))
          << "q_" << r << " at cell " << c << ": " << ratio << " against the bound "
          << kDerivedUlpBound;
    }
  }
  EXPECT_GT(qExercised, 0) << "the q bound was never exercised";

  // ---- E6': the ACCUMULATIONS, from the device's OWN q, BITWISE
  std::vector<Real> hostOmega(static_cast<std::size_t>(cells * species));
  std::vector<Real> hostHeat(static_cast<std::size_t>(cells));
  std::vector<Real> hostHeatMass(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    Real q[8];
    for (Index r = 0; r < reactions; ++r) {
      q[r] = gpuQ[(r * cells) + c];
    }
    Real localOmega[8];
    cfd::gpu::deviceSpeciesSources(mechanism, q, localOmega, kProduction);
    for (Index i = 0; i < species; ++i) {
      hostOmega[(i * cells) + c] = localOmega[i];
    }
    DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
    hostHeat[c] = cfd::gpu::deviceHeatRelease(mechanism, q, kProduction, &st);
    hostHeatMass[c] = cfd::gpu::deviceHeatReleaseFromSources(mechanism, localOmega);
  }
  const auto omegaDivergence = compare(gpuOmega, hostOmega);
  const auto heatDivergence = compare(gpuHeat, hostHeat);
  const auto heatMassDivergence = compare(gpuHeat2, hostHeatMass);
  std::cout << "    q: worst " << worstQRatio << " against the bound " << kDerivedUlpBound << " ("
            << qExercised << " exercised)\n"
            << "    accumulations from the device's own q, zero tolerance:\n";
  report("omega_i", omegaDivergence);
  report("S_E", heatDivergence);
  report("S_E (mass route)", heatMassDivergence);
  EXPECT_EQ(omegaDivergence.differing, 0) << "omega from the device's own q must be BITWISE";
  EXPECT_EQ(heatDivergence.differing, 0) << "S_E from the device's own q must be BITWISE";
  EXPECT_EQ(heatMassDivergence.differing, 0)
      << "S_E by the mass route from the device's own omega must be BITWISE";

  // ---- the propagated bound end to end, so a divergence entering through q is still bounded
  Real worstOmegaRatio = 0.0;
  Index omegaExercised = 0;
  for (Index c = 0; c < cells; ++c) {
    Real concentrations[8];
    for (Index i = 0; i < species; ++i) {
      concentrations[i] = gpuC[(i * cells) + c];
    }
    Real hostQ[8];
    for (Index r = 0; r < reactions; ++r) {
      DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
      hostQ[r] = cfd::gpu::deviceProgressRate(mechanism, r, concentrations, host.temperature[c],
                                              kProduction, &st);
    }
    Real endToEndOmega[8];
    cfd::gpu::deviceSpeciesSources(mechanism, hostQ, endToEndOmega, kProduction);
    for (Index i = 0; i < species; ++i) {
      Real scale = 0.0;
      for (Index r = 0; r < reactions; ++r) {
        scale += std::abs(mechanism.species[i].molecularWeight * mechanism.reactions[r].net[i] *
                          hostQ[r]);
      }
      if (!(scale > 0.0)) {
        continue;
      }
      ++omegaExercised;
      worstOmegaRatio = std::max(
          worstOmegaRatio, std::abs(gpuOmega[(i * cells) + c] - endToEndOmega[i]) / (kEps * scale));
    }
  }
  std::cout << "    omega end to end: worst " << worstOmegaRatio << " against " << kDerivedUlpBound
            << " (" << omegaExercised << " exercised)\n";
  EXPECT_GT(omegaExercised, 0) << "the propagated omega bound was never exercised";
  EXPECT_LE(worstOmegaRatio, static_cast<Real>(kDerivedUlpBound));

  // ---- SECTION 14: mass conservation holds ON THE DEVICE independently. sum_i M_i nu_i is
  // EXACTLY zero for both reactions -- 0.0625, 0.125 and 0.1875 are dyadic and sum to zero against
  // nu = (-1,-1,+1) -- so the residual is pure accumulation round-off.
  Real worstRatio = 0.0;
  Index worstCell = 0;
  bool anyCellExercised = false;
  Real largestScale = 0.0;
  for (Index c = 0; c < cells; ++c) {
    Real cellScale = 0.0;
    for (Index i = 0; i < species; ++i) {
      cellScale = std::max(cellScale, std::abs(gpuOmega[(i * cells) + c]));
    }
    largestScale = std::max(largestScale, cellScale);
    if (cellScale > 0.0) {
      const Real ratio = std::abs(gpuOmegaSum[c]) / cellScale;
      if (!anyCellExercised || ratio > worstRatio) {
        worstRatio = ratio;
        worstCell = c;
        anyCellExercised = true;
      }
    }
  }
  const Real bound = 32.0 * std::numeric_limits<Real>::epsilon();
  EXPECT_LE(worstRatio, bound) << "worst |sum omega| / max_i |omega_i| on DEVICE = " << worstRatio
                               << " at cell " << worstCell << ", against " << bound;
  EXPECT_GT(largestScale, 0.0) << "there must be real source magnitude, or the bound is vacuous";
  EXPECT_TRUE(anyCellExercised) << "no cell had a nonzero source, so the bound was never exercised";
}

// =================================================================================================
// E8, E9 -- the coupled step and the trajectory, on DEVICE
// =================================================================================================

// E8' (amendment A2). One coupled step. EVERY quantity here is BITWISE except the heat-release
// intermediate, which is downstream of k and therefore carries the libm difference -- so it gets
// A2's propagated bound, and is ALSO required to be bitwise when recomputed from the device step's
// OWN progress rates, which is the clause that keeps the accumulation under zero tolerance.
//
// The state -- Y, T, and the two energy pools -- is BITWISE, and that is not a concession granted
// by A2: it is measured. A <= 3 ULP difference in omega enters the state as Y + dt*omega/rho, where
// it lands below one ULP of Y, and the bisection-based T recovery returns the same double.
TEST(P188GpuEquivalence, E8_TheCoupledStepStateIsBitwiseAndTheRateIntermediatesAreBounded) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 1024;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto host = makeState(cells, species);
  const Real dt = 1.0e-5;

  DeviceBuffer<Real> composition(cells * species);
  composition.uploadFrom(host.composition.data(), cells * species);
  DeviceBuffer<Real> density(cells);
  density.uploadFrom(host.density.data(), cells);
  DeviceBuffer<Real> temperature(cells);
  temperature.uploadFrom(host.temperature.data(), cells);

  DeviceBuffer<Real> compositionOut(cells * species);
  DeviceBuffer<Real> temperatureOut(cells);
  DeviceBuffer<Real> progressRateOut(cells * reactions);
  DeviceBuffer<Real> rateCoefficientOut(cells * reactions);
  DeviceBuffer<Real> omegaOut(cells * species);
  DeviceBuffer<Real> omegaSumOut(cells);
  DeviceBuffer<Real> heatReleaseOut(cells);
  DeviceBuffer<Real> sensibleBeforeOut(cells);
  DeviceBuffer<Real> sensibleAfterOut(cells);
  DeviceBuffer<Real> chemicalBeforeOut(cells);
  DeviceBuffer<Real> chemicalAfterOut(cells);
  DeviceBuffer<int> branchOut(cells * reactions);
  DeviceBuffer<int> statusOut(cells);

  cfd::gpu::advanceReactorDevice(mechanism, composition, density, temperature, cells, dt, 1.0e-6,
                                 1.0e5, kProduction, false, false, 0.0, compositionOut,
                                 temperatureOut, progressRateOut, rateCoefficientOut, omegaOut,
                                 omegaSumOut, heatReleaseOut, sensibleBeforeOut, sensibleAfterOut,
                                 chemicalBeforeOut, chemicalAfterOut, branchOut, statusOut);

  std::vector<Real> gpuY(cells * species);
  std::vector<Real> gpuT(cells);
  std::vector<Real> gpuQ(cells * reactions);
  std::vector<Real> gpuSensibleAfter(cells);
  std::vector<Real> gpuChemicalAfter(cells);
  std::vector<Real> gpuHeat(cells);
  std::vector<int> gpuBranch(cells * reactions);
  std::vector<int> gpuStatus(cells);
  compositionOut.downloadTo(gpuY.data(), cells * species);
  temperatureOut.downloadTo(gpuT.data(), cells);
  progressRateOut.downloadTo(gpuQ.data(), cells * reactions);
  sensibleAfterOut.downloadTo(gpuSensibleAfter.data(), cells);
  chemicalAfterOut.downloadTo(gpuChemicalAfter.data(), cells);
  heatReleaseOut.downloadTo(gpuHeat.data(), cells);
  branchOut.downloadTo(gpuBranch.data(), cells * reactions);
  statusOut.downloadTo(gpuStatus.data(), cells);

  Index stateBitwise = 0;
  Real worstHeatRatio = 0.0;
  Index heatExercised = 0;
  Index heatFromOwnQDiffering = 0;
  for (Index c = 0; c < cells; ++c) {
    const auto local = cellComposition(host, species, c);
    DeviceReactorStep expected;
    cfd::gpu::deviceAdvanceReactorCell(mechanism, local.data(), host.density[c],
                                       host.temperature[c], dt, 1.0e-6, 1.0e5, kProduction, false,
                                       false, 0.0, &expected);

    // ---- BITWISE: the status and its REASON, which must name the same first offender
    ASSERT_EQ(gpuStatus[c], static_cast<int>(expected.status)) << "status at cell " << c;
    // ---- BITWISE: every branch decision
    for (Index r = 0; r < reactions; ++r) {
      EXPECT_EQ(gpuBranch[(r * cells) + c], expected.branch[r])
          << "branch for reaction " << r << " at cell " << c;
    }
    // ---- BITWISE: the state and the energy pools
    for (Index i = 0; i < species; ++i) {
      EXPECT_EQ(gpuY[(i * cells) + c], expected.compositionAfter[i])
          << "Y_" << i << " at cell " << c;
    }
    EXPECT_EQ(gpuT[c], expected.temperatureAfter) << "T at cell " << c;
    EXPECT_EQ(gpuSensibleAfter[c], expected.sensibleAfter) << "sensible at cell " << c;
    EXPECT_EQ(gpuChemicalAfter[c], expected.chemicalAfter) << "chemical at cell " << c;
    if (gpuT[c] == expected.temperatureAfter &&
        gpuY[(0 * cells) + c] == expected.compositionAfter[0]) {
      ++stateBitwise;
    }

    // ---- E6' on the step: S_E recomputed from the DEVICE step's OWN q must be BITWISE
    Real ownQ[8];
    for (Index r = 0; r < reactions; ++r) {
      ownQ[r] = gpuQ[(r * cells) + c];
    }
    DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
    const Real heatFromOwnQ = cfd::gpu::deviceHeatRelease(mechanism, ownQ, kProduction, &st);
    EXPECT_EQ(gpuHeat[c], heatFromOwnQ)
        << "S_E at cell " << c << " must be bitwise from the step's own q";
    if (gpuHeat[c] != heatFromOwnQ) {
      ++heatFromOwnQDiffering;
    }

    // ---- E4' propagated: end to end, S_E within 8 * eps * sum |q_r dH_r|
    Real scale = 0.0;
    for (Index r = 0; r < reactions; ++r) {
      scale += std::abs(expected.progressRate[r] *
                        cfd::gpu::deviceStandardReactionEnthalpy(mechanism, r));
    }
    if (scale > 0.0) {
      ++heatExercised;
      const Real ratio = std::abs(gpuHeat[c] - expected.heatRelease) / (kEps * scale);
      worstHeatRatio = std::max(worstHeatRatio, ratio);
      EXPECT_LE(ratio, static_cast<Real>(kDerivedUlpBound))
          << "S_E at cell " << c << ": " << ratio << " against the bound " << kDerivedUlpBound;
    }
  }

  std::cout << "    state (Y, T, pools): " << stateBitwise << " of " << cells << " bitwise\n"
            << "    S_E from the step's own q: " << heatFromOwnQDiffering << " of " << cells
            << " differ (must be 0)\n"
            << "    S_E end to end: worst " << worstHeatRatio << " against the bound "
            << kDerivedUlpBound << " (" << heatExercised << " exercised)\n";
  EXPECT_EQ(stateBitwise, cells) << stateBitwise << " of " << cells << " cells bitwise in state";
  EXPECT_EQ(heatFromOwnQDiffering, 0);
  EXPECT_GT(heatExercised, 0) << "the S_E bound was never exercised";
  // the step did something, or none of the above means anything
  EXPECT_NE(gpuT[0], host.temperature[0]);
}

// =================================================================================================
// DIAGNOSTIC 3 -- does the libm divergence STAY at a few ULP along a trajectory, or amplify?
//
// A one-step difference of 3 ULP says nothing about a thousand-step march: a reacting system can be
// contracting (the difference is damped toward the equilibrium both backends share) or expanding
// (the difference grows, and no fixed tolerance holds). This walks the same initial state forward
// on the device and on the host, resident on each side, and reports the divergence at a ladder of
// step counts. It asserts nothing about magnitude -- the growth RATE is what a correct replacement
// criterion for E9 has to be derived from, and this is the measurement that supplies it.
TEST(P188GpuEquivalence, DIAGNOSTIC_HowTheDivergenceGrowsAlongATrajectory) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 256;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto initial = makeState(cells, species);
  // The SAME dt the coupled-step criterion uses. The first attempt at this probe used 1e-7 and
  // reported zero divergence at every sample -- because at that dt neither T nor Y moved by one
  // ulp, so the probe printed exactly what success looks like while measuring nothing. The
  // assertions at the bottom now require the state to have MOVED before the ladder means anything.
  const Real dt = 1.0e-5;
  const Index totalSteps = 50;

  DeviceBuffer<Real> composition(cells * species);
  composition.uploadFrom(initial.composition.data(), cells * species);
  DeviceBuffer<Real> density(cells);
  density.uploadFrom(initial.density.data(), cells);
  DeviceBuffer<Real> temperature(cells);
  temperature.uploadFrom(initial.temperature.data(), cells);
  DeviceBuffer<Real> compositionOut(cells * species);
  DeviceBuffer<Real> temperatureOut(cells);
  DeviceBuffer<Real> progressRateOut(cells * reactions);
  DeviceBuffer<Real> rateCoefficientOut(cells * reactions);
  DeviceBuffer<Real> omegaOut(cells * species);
  DeviceBuffer<Real> omegaSumOut(cells);
  DeviceBuffer<Real> heatReleaseOut(cells);
  DeviceBuffer<Real> sensibleBeforeOut(cells);
  DeviceBuffer<Real> sensibleAfterOut(cells);
  DeviceBuffer<Real> chemicalBeforeOut(cells);
  DeviceBuffer<Real> chemicalAfterOut(cells);
  DeviceBuffer<int> branchOut(cells * reactions);
  DeviceBuffer<int> statusOut(cells);

  // the host trajectory, marched independently for every cell
  std::vector<Real> hostY(initial.composition);
  std::vector<Real> hostT(initial.temperature);

  std::cout << "  dt = " << dt << ",  " << cells << " cells,  " << totalSteps << " steps\n"
            << "  step |   max |dT|/T    |   max |dY|      | worst T ULP | status\n";

  const Index samples[4] = {1, 5, 20, 50};
  Index nextSample = 0;
  int worstStatus = 0;
  Real largestTemperatureMove = 0.0;
  Real largestCompositionMove = 0.0;
  for (Index step = 1; step <= totalSteps; ++step) {
    cfd::gpu::advanceReactorDevice(mechanism, composition, density, temperature, cells, dt, 1.0e-6,
                                   1.0e5, kProduction, false, false, 0.0, compositionOut,
                                   temperatureOut, progressRateOut, rateCoefficientOut, omegaOut,
                                   omegaSumOut, heatReleaseOut, sensibleBeforeOut, sensibleAfterOut,
                                   chemicalBeforeOut, chemicalAfterOut, branchOut, statusOut);
    std::swap(composition, compositionOut);
    std::swap(temperature, temperatureOut);

    for (Index c = 0; c < cells; ++c) {
      Real local[8];
      for (Index i = 0; i < species; ++i) {
        local[i] = hostY[(i * cells) + c];
      }
      DeviceReactorStep out;
      cfd::gpu::deviceAdvanceReactorCell(mechanism, local, initial.density[c], hostT[c], dt, 1.0e-6,
                                         1.0e5, kProduction, false, false, 0.0, &out);
      for (Index i = 0; i < species; ++i) {
        hostY[(i * cells) + c] = out.compositionAfter[i];
      }
      hostT[c] = out.temperatureAfter;
      worstStatus = std::max(worstStatus, static_cast<int>(out.status));
    }

    if (nextSample < 4 && step == samples[nextSample]) {
      std::vector<Real> gpuY(static_cast<std::size_t>(cells * species));
      std::vector<Real> gpuT(static_cast<std::size_t>(cells));
      composition.downloadTo(gpuY.data(), cells * species);
      temperature.downloadTo(gpuT.data(), cells);
      Real worstRelativeT = 0.0;
      Real worstAbsoluteY = 0.0;
      std::int64_t worstUlpT = 0;
      for (Index c = 0; c < cells; ++c) {
        if (hostT[c] != 0.0) {
          worstRelativeT =
              std::max(worstRelativeT, std::abs(gpuT[c] - hostT[c]) / std::abs(hostT[c]));
        }
        std::int64_t a = 0;
        std::int64_t b = 0;
        std::memcpy(&a, &gpuT[c], sizeof(a));
        std::memcpy(&b, &hostT[c], sizeof(b));
        worstUlpT = std::max(worstUlpT, a > b ? (a - b) : (b - a));
        for (Index i = 0; i < species; ++i) {
          worstAbsoluteY =
              std::max(worstAbsoluteY, std::abs(gpuY[(i * cells) + c] - hostY[(i * cells) + c]));
        }
      }
      for (Index c = 0; c < cells; ++c) {
        largestTemperatureMove =
            std::max(largestTemperatureMove, std::abs(hostT[c] - initial.temperature[c]));
        for (Index i = 0; i < species; ++i) {
          largestCompositionMove =
              std::max(largestCompositionMove,
                       std::abs(hostY[(i * cells) + c] - initial.composition[(i * cells) + c]));
        }
      }
      std::cout << "  " << step << " | " << worstRelativeT << " | " << worstAbsoluteY << " | "
                << worstUlpT << " | " << worstStatus << "\n";
      ++nextSample;
    }
  }

  std::cout << "  the state actually moved:  max |T - T_0| = " << largestTemperatureMove
            << " K,   max |Y - Y_0| = " << largestCompositionMove << "\n";

  // The march must have been a legal one, or the growth figures describe a refused state rather
  // than a trajectory.
  EXPECT_EQ(worstStatus, 0) << "the host march did not stay valid (worst status " << worstStatus
                            << "), so the divergence ladder above describes nothing";
  // NON-VACUITY. A ladder of zeros is only informative if the trajectory went somewhere; the
  // thresholds are ONE ULP of each quantity at its own scale, which is the smallest move that can
  // be represented at all, not a tolerance.
  EXPECT_GT(largestTemperatureMove, std::numeric_limits<Real>::epsilon() * 2000.0)
      << "T never moved by even one ulp, so a zero divergence ladder proves nothing";
  EXPECT_GT(largestCompositionMove, std::numeric_limits<Real>::epsilon())
      << "Y never moved by even one ulp, so a zero divergence ladder proves nothing";
}

// CONTROL N10 -- "different timestep/control", the one control in the frozen table (E9) that the
// first pass of this suite never implemented. Found by mapping each frozen control to the test that
// exercises it, which is also how N1/N2/N8 turned out to be pointed at the wrong criteria.
//
// What it guards: the trajectory comparison above concludes "host and device agree bitwise". That
// conclusion is only interesting if the trajectory is actually SENSITIVE to the integration -- if a
// different dt produced the same answer, the comparison would be comparing something inert and
// would agree for the wrong reason.
TEST(P188GpuEquivalence, N10_ADifferentTimestepProducesADifferentTrajectory) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 256;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto initial = makeState(cells, species);

  auto march = [&](Real dt, Index steps) {
    DeviceBuffer<Real> composition(cells * species);
    composition.uploadFrom(initial.composition.data(), cells * species);
    DeviceBuffer<Real> density(cells);
    density.uploadFrom(initial.density.data(), cells);
    DeviceBuffer<Real> temperature(cells);
    temperature.uploadFrom(initial.temperature.data(), cells);
    DeviceBuffer<Real> compositionOut(cells * species);
    DeviceBuffer<Real> temperatureOut(cells);
    DeviceBuffer<Real> progressRateOut(cells * reactions);
    DeviceBuffer<Real> rateCoefficientOut(cells * reactions);
    DeviceBuffer<Real> omegaOut(cells * species);
    DeviceBuffer<Real> omegaSumOut(cells);
    DeviceBuffer<Real> heatReleaseOut(cells);
    DeviceBuffer<Real> sensibleBeforeOut(cells);
    DeviceBuffer<Real> sensibleAfterOut(cells);
    DeviceBuffer<Real> chemicalBeforeOut(cells);
    DeviceBuffer<Real> chemicalAfterOut(cells);
    DeviceBuffer<int> branchOut(cells * reactions);
    DeviceBuffer<int> statusOut(cells);
    for (Index step = 0; step < steps; ++step) {
      cfd::gpu::advanceReactorDevice(
          mechanism, composition, density, temperature, cells, dt, 1.0e-6, 1.0e5, kProduction,
          false, false, 0.0, compositionOut, temperatureOut, progressRateOut, rateCoefficientOut,
          omegaOut, omegaSumOut, heatReleaseOut, sensibleBeforeOut, sensibleAfterOut,
          chemicalBeforeOut, chemicalAfterOut, branchOut, statusOut);
      std::swap(composition, compositionOut);
      std::swap(temperature, temperatureOut);
    }
    std::vector<Real> t(static_cast<std::size_t>(cells));
    std::vector<Real> y(static_cast<std::size_t>(cells * species));
    temperature.downloadTo(t.data(), cells);
    composition.downloadTo(y.data(), cells * species);
    return std::make_pair(t, y);
  };

  // The SAME total time by two different step sizes. An explicit first-order march must give a
  // different answer; identical answers would mean the march is not integrating anything.
  const auto coarse = march(1.0e-5, 50);
  const auto fine = march(5.0e-6, 100);

  Real worstTemperatureDifference = 0.0;
  Real worstCompositionDifference = 0.0;
  for (Index c = 0; c < cells; ++c) {
    worstTemperatureDifference =
        std::max(worstTemperatureDifference, std::abs(coarse.first[c] - fine.first[c]));
    for (Index i = 0; i < species; ++i) {
      worstCompositionDifference =
          std::max(worstCompositionDifference,
                   std::abs(coarse.second[(i * cells) + c] - fine.second[(i * cells) + c]));
    }
  }
  std::cout << "    N10: dt = 1e-5 x 50 vs dt = 5e-6 x 100 over the same total time\n"
            << "         max |dT| = " << worstTemperatureDifference
            << " K,  max |dY| = " << worstCompositionDifference << "\n";

  // The threshold is the smallest difference that is representable rather than a tolerance: the
  // point is that the two marches are NOT the same computation.
  EXPECT_GT(worstTemperatureDifference, std::numeric_limits<Real>::epsilon() * 2000.0)
      << "halving dt changed nothing, so the trajectory comparison is not sensitive to the "
         "integration and its bitwise agreement proves nothing";
  EXPECT_GT(worstCompositionDifference, std::numeric_limits<Real>::epsilon())
      << "halving dt left the composition unchanged";
  // and the step-size difference must be a CONVERGENCE difference, not a blow-up
  EXPECT_LT(worstTemperatureDifference, 50.0)
      << "halving dt changed T by " << worstTemperatureDifference
      << " K, which is too large to be a first-order truncation difference on this march";
}

// =================================================================================================
// E10 -- determinism and block-size independence (§45, §46)
// =================================================================================================

TEST(P188GpuEquivalence, RepeatsAndBlockSizesAreBitwise) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 1024;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto host = makeState(cells, species);
  const Real dt = 1.0e-5;

  auto run = [&](int threadsPerBlock) {
    DeviceBuffer<Real> composition(cells * species);
    composition.uploadFrom(host.composition.data(), cells * species);
    DeviceBuffer<Real> density(cells);
    density.uploadFrom(host.density.data(), cells);
    DeviceBuffer<Real> temperature(cells);
    temperature.uploadFrom(host.temperature.data(), cells);
    DeviceBuffer<Real> compositionOut(cells * species);
    DeviceBuffer<Real> temperatureOut(cells);
    DeviceBuffer<Real> progressRateOut(cells * reactions);
    DeviceBuffer<Real> rateCoefficientOut(cells * reactions);
    DeviceBuffer<Real> omegaOut(cells * species);
    DeviceBuffer<Real> omegaSumOut(cells);
    DeviceBuffer<Real> heatReleaseOut(cells);
    DeviceBuffer<Real> sensibleBeforeOut(cells);
    DeviceBuffer<Real> sensibleAfterOut(cells);
    DeviceBuffer<Real> chemicalBeforeOut(cells);
    DeviceBuffer<Real> chemicalAfterOut(cells);
    DeviceBuffer<int> branchOut(cells * reactions);
    DeviceBuffer<int> statusOut(cells);
    cfd::gpu::advanceReactorDevice(
        mechanism, composition, density, temperature, cells, dt, 1.0e-6, 1.0e5, kProduction, false,
        false, 0.0, compositionOut, temperatureOut, progressRateOut, rateCoefficientOut, omegaOut,
        omegaSumOut, heatReleaseOut, sensibleBeforeOut, sensibleAfterOut, chemicalBeforeOut,
        chemicalAfterOut, branchOut, statusOut, threadsPerBlock);
    std::vector<Real> y(cells * species);
    std::vector<Real> t(cells);
    std::vector<Real> omega(cells * species);
    compositionOut.downloadTo(y.data(), cells * species);
    temperatureOut.downloadTo(t.data(), cells);
    omegaOut.downloadTo(omega.data(), cells * species);
    return std::make_tuple(y, t, omega);
  };

  const auto reference = run(256);
  // repeats at the SAME block size
  for (int repeat = 0; repeat < 5; ++repeat) {
    const auto again = run(256);
    EXPECT_EQ(std::get<0>(again), std::get<0>(reference)) << "Y, repeat " << repeat;
    EXPECT_EQ(std::get<1>(again), std::get<1>(reference)) << "T, repeat " << repeat;
    EXPECT_EQ(std::get<2>(again), std::get<2>(reference)) << "omega, repeat " << repeat;
  }
  // and at DIFFERENT legal block sizes -- section 46
  for (const int threads : {64, 128, 512}) {
    const auto other = run(threads);
    EXPECT_EQ(std::get<0>(other), std::get<0>(reference)) << "Y at " << threads << " threads";
    EXPECT_EQ(std::get<1>(other), std::get<1>(reference)) << "T at " << threads << " threads";
    EXPECT_EQ(std::get<2>(other), std::get<2>(reference)) << "omega at " << threads << " threads";
  }
}

// =================================================================================================
// E12 -- transfer and residency, MEASURED (§36, §37, §38)
// =================================================================================================

TEST(P188GpuEquivalence, E12_TheResidentLoopPerformsNoFullFieldTransfer) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 4096;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto host = makeState(cells, species);
  const Real dt = 1.0e-6;
  const Index fieldBytes = cells * static_cast<Index>(sizeof(Real));

  // --- INITIALIZATION: upload once
  DeviceBuffer<Real> composition(cells * species);
  DeviceBuffer<Real> density(cells);
  DeviceBuffer<Real> temperature(cells);
  DeviceBuffer<Real> compositionOut(cells * species);
  DeviceBuffer<Real> temperatureOut(cells);
  DeviceBuffer<Real> progressRateOut(cells * reactions);
  DeviceBuffer<Real> rateCoefficientOut(cells * reactions);
  DeviceBuffer<Real> omegaOut(cells * species);
  DeviceBuffer<Real> omegaSumOut(cells);
  DeviceBuffer<Real> heatReleaseOut(cells);
  DeviceBuffer<Real> sensibleBeforeOut(cells);
  DeviceBuffer<Real> sensibleAfterOut(cells);
  DeviceBuffer<Real> chemicalBeforeOut(cells);
  DeviceBuffer<Real> chemicalAfterOut(cells);
  DeviceBuffer<int> branchOut(cells * reactions);
  DeviceBuffer<int> statusOut(cells);

  cfd::gpu::resetGpuExecutionStats();
  composition.uploadFrom(host.composition.data(), cells * species);
  density.uploadFrom(host.density.data(), cells);
  temperature.uploadFrom(host.temperature.data(), cells);
  const auto initialization = cfd::gpu::gpuExecutionStats();

  // --- THE RESIDENT LOOP: ten steps, nothing uploaded or downloaded
  cfd::gpu::resetGpuExecutionStats();
  for (int step = 0; step < 10; ++step) {
    cfd::gpu::advanceReactorDevice(mechanism, composition, density, temperature, cells, dt, 1.0e-6,
                                   1.0e5, kProduction, false, false, 0.0, compositionOut,
                                   temperatureOut, progressRateOut, rateCoefficientOut, omegaOut,
                                   omegaSumOut, heatReleaseOut, sensibleBeforeOut, sensibleAfterOut,
                                   chemicalBeforeOut, chemicalAfterOut, branchOut, statusOut);
    // the device-to-device hand-off: the output becomes the next input. NO HOST ROUND TRIP.
    std::swap(composition, compositionOut);
    std::swap(temperature, temperatureOut);
  }
  const auto loop = cfd::gpu::gpuExecutionStats();

  // --- FINAL OUTPUT: download once
  cfd::gpu::resetGpuExecutionStats();
  std::vector<Real> finalY(cells * species);
  std::vector<Real> finalT(cells);
  composition.downloadTo(finalY.data(), cells * species);
  temperature.downloadTo(finalT.data(), cells);
  const auto output = cfd::gpu::gpuExecutionStats();

  // THE CLAIM, MEASURED RATHER THAN INSPECTED (section 36):
  EXPECT_EQ(loop.hostToDeviceCalls, 0u)
      << "the resident loop uploaded " << loop.hostToDeviceCalls << " times";
  EXPECT_EQ(loop.hostToDeviceBytes, 0u);
  EXPECT_EQ(loop.deviceToHostCalls, 0u)
      << "the resident loop downloaded " << loop.deviceToHostCalls << " times";
  EXPECT_EQ(loop.deviceToHostBytes, 0u);
  // and no transfer of FIELD SIZE occurred in the loop -- the quantity section 38 actually needs
  EXPECT_LT(loop.largestDeviceToHostBytes, static_cast<std::uint64_t>(fieldBytes))
      << "a field-sized download happened inside the resident loop";

  // the bookends are real and are NOT charged against residency (section 39)
  EXPECT_GT(initialization.hostToDeviceCalls, 0u) << "initialization must actually upload";
  EXPECT_GT(output.deviceToHostCalls, 0u) << "the final output must actually download";

  // and the loop did real work
  EXPECT_NE(finalT[0], host.temperature[0]);
}

TEST(P188GpuEquivalence, N12_AFullFieldTransferInTheResidentLoopIsDetected) {
  // CONTROL N12. The instrument must be able to SEE the defect it is meant to exclude, or the
  // clean measurement above means nothing.
  REQUIRE_GPU();
  const Index cells = 4096;
  const Index fieldBytes = cells * static_cast<Index>(sizeof(Real));
  DeviceBuffer<Real> field(cells);
  std::vector<Real> hostField(static_cast<std::size_t>(cells), 1.0);

  cfd::gpu::resetGpuExecutionStats();
  // no transfer: the clean case
  const auto clean = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(clean.largestDeviceToHostBytes, 0u);

  cfd::gpu::resetGpuExecutionStats();
  field.uploadFrom(hostField.data(), cells);
  field.downloadTo(hostField.data(), cells);  // THE DEFECT: a full-field round trip
  const auto dirty = cfd::gpu::gpuExecutionStats();

  EXPECT_GT(dirty.deviceToHostCalls, 0u);
  EXPECT_GE(dirty.largestDeviceToHostBytes, static_cast<std::uint64_t>(fieldBytes))
      << "the instrument must register a field-sized download";
  // so the assertion used in E12 would FAIL here, which is what makes E12 meaningful
  EXPECT_FALSE(dirty.largestDeviceToHostBytes < static_cast<std::uint64_t>(fieldBytes));
}
