// P18.8 amendment A2 -- THE PRE-FREEZE DRY-RUN, and afterwards the standing non-vacuity controls.
//
// CLAUDE.md 5.3 requires an amendment to be frozen "as a separate file, with its own dry-run and
// non-vacuity check". This file is that dry-run. It ran BEFORE acceptance_gate_A2.md was frozen,
// and its numbers are what the frozen thresholds were CHECKED against -- never what they were
// derived from. The derivation is in libm_divergence.md section 7 and comes from the two libraries'
// documented accuracy and the operation count.
//
// WHAT THE FIRST PASS OF THIS DRY-RUN FOUND, all before anything was frozen:
//
//  1. ulpDistance() wrapped to 0 on OPPOSITE-SIGN operands. Reinterpreting two doubles as signed
//     integers and subtracting overflows when the signs differ, and the wrapped value read as 0 --
//     so control N6, which flips the sign of S_E, was reported as "worst ULP 0" while its relative
//     difference was 2. A sign flip is the largest error the suite can inject and the instrument
//     scored it as a perfect match. Fixed below with a monotonic key, which fails CLOSED.
//  2. N1 and N2 were pointed at the wrong criterion. They permute or collapse the species index in
//     the MIXTURE PROPERTY functions; deviceConcentration and deviceSpeciesSources do not consult
//     them at all. Run against the reaction sources they changed nothing, which looks exactly like
//     a dead control. Each control now runs against the criterion it actually controls.
//  3. FMA does not reach the omega / S_E accumulation at all (0 of 12288). The hypothesis -- that
//     the coefficients M_i * nu_i times q are EXACT for this mechanism, leaving no rounding for a
//     fusion to remove -- is tested directly below with the host's own std::fma rather than
//     asserted. So the non-vacuity control for the accumulation clause is N5/N8, not FMA.
//  4. The FMA effect on q was reported in raw ULP (44) while the bound is stated in units of
//     eps * (sum of term magnitudes). Comparing the two would have been comparing unlike things.
//     It is now measured in the bound's OWN units, which is the only way the question "could this
//     bound hide contraction?" has a meaning.
//
// THE QUESTION THAT DECIDES WHETHER A2 IS WELL-FORMED. A2 relaxes BITWISE to a derived ULP bound
// for the transcendental-dependent quantities. If that bound were loose enough to also pass with
// FMA contraction present, A2 would have silently abandoned G9's condition (4) while appearing to
// keep it. DRYRUN_2 answers it with a number, in the bound's own units.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include "SpeciesEquivalenceCompare.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceSpeciesFmadControl.hpp"
#include "cfd/gpu/DeviceSpeciesLaunch.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::gpu::DeviceArrhenius;
using cfd::gpu::DeviceBuffer;
using cfd::gpu::DeviceMechanism;
using cfd::gpu::DeviceSpeciesControls;
using cfd::gpu::DeviceSpeciesStatus;

namespace {

constexpr Real kR = 8.31446261815324;

// The instrument comes from SpeciesEquivalenceCompare.hpp, which DRYRUN_1B below self-tests. It is
// deliberately NOT redefined here: the code under self-test and the code the gate trusts must be
// the same code.
using cfd::species_equivalence::compare;
using cfd::species_equivalence::Divergence;
using cfd::species_equivalence::kDerivedUlpBound;
using cfd::species_equivalence::kEps;
using cfd::species_equivalence::progressRateTermScale;
using cfd::species_equivalence::report;
using cfd::species_equivalence::ulpDistance;

#define REQUIRE_GPU()                                               \
  if (!cfd::gpu::speciesDeviceAvailable()) {                        \
    GTEST_SKIP() << "no CUDA device present; nothing was compared"; \
  }

// The same three-species, two-reaction mechanism the equivalence suite uses, with deliberately
// distinct weights, specific heats, formation enthalpies and kinetics.
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
  m.reactions[0].reactant[0] = 1.0;
  m.reactions[0].reactant[1] = 1.0;
  m.reactions[0].product[2] = 1.0;
  for (Index i = 0; i < 3; ++i) {
    m.reactions[0].net[i] = m.reactions[0].product[i] - m.reactions[0].reactant[i];
  }
  m.forward[0] = DeviceArrhenius{3.0, 0.5, 4.0e4};
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

struct HostState {
  Index cells{};
  std::vector<Real> composition;  // SPECIES-MAJOR
  std::vector<Real> density;
  std::vector<Real> temperature;
};

HostState makeState(Index cells, Index speciesCount) {
  HostState s;
  s.cells = cells;
  s.composition.resize(static_cast<std::size_t>(cells * speciesCount));
  s.density.resize(static_cast<std::size_t>(cells));
  s.temperature.resize(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    const Real t = static_cast<Real>(c) / static_cast<Real>(cells);
    const Real a = 0.2 + (0.5 * t);
    const Real b = 0.3 - (0.1 * t);
    s.composition[(0 * cells) + c] = a;
    s.composition[(1 * cells) + c] = b;
    s.composition[(2 * cells) + c] = 1.0 - a - b;
    s.density[c] = 0.7 + (0.01 * static_cast<Real>(c % 17));
    s.temperature[c] = 900.0 + (7.0 * static_cast<Real>(c % 101));
  }
  return s;
}

const DeviceSpeciesControls kProduction{};

enum class Flavour { Production, FmadControl };

struct MixtureFields {
  std::vector<Real> molecularWeight;
  std::vector<Real> gasConstant;
  std::vector<Real> specificHeatP;
  std::vector<Real> specificHeatV;
  std::vector<Real> specificHeatRatio;
};

MixtureFields runMixture(const DeviceMechanism& mechanism, const HostState& host,
                         const DeviceSpeciesControls& controls, Flavour flavour) {
  const Index cells = host.cells;
  const Index species = mechanism.speciesCount;
  DeviceBuffer<Real> composition(cells * species);
  composition.uploadFrom(host.composition.data(), cells * species);
  DeviceBuffer<Real> m(cells);
  DeviceBuffer<Real> r(cells);
  DeviceBuffer<Real> cp(cells);
  DeviceBuffer<Real> cv(cells);
  DeviceBuffer<Real> gamma(cells);
  DeviceBuffer<int> status(cells);
  if (flavour == Flavour::Production) {
    cfd::gpu::computeMixturePropertiesDevice(mechanism, composition, cells, controls, m, r, cp, cv,
                                             gamma, status);
  } else {
    cfd::gpu::computeMixturePropertiesFmadControlDevice(mechanism, composition, cells, controls, m,
                                                        r, cp, cv, gamma, status);
  }
  MixtureFields out;
  out.molecularWeight.resize(static_cast<std::size_t>(cells));
  out.gasConstant.resize(static_cast<std::size_t>(cells));
  out.specificHeatP.resize(static_cast<std::size_t>(cells));
  out.specificHeatV.resize(static_cast<std::size_t>(cells));
  out.specificHeatRatio.resize(static_cast<std::size_t>(cells));
  m.downloadTo(out.molecularWeight.data(), cells);
  r.downloadTo(out.gasConstant.data(), cells);
  cp.downloadTo(out.specificHeatP.data(), cells);
  cv.downloadTo(out.specificHeatV.data(), cells);
  gamma.downloadTo(out.specificHeatRatio.data(), cells);
  return out;
}

struct SourceFields {
  std::vector<Real> concentration;
  std::vector<Real> progressRate;
  std::vector<Real> omega;
  std::vector<Real> omegaSum;
  std::vector<Real> heatRelease;
  std::vector<Real> heatReleaseFromSources;
  std::vector<int> status;
};

SourceFields runSources(const DeviceMechanism& mechanism, const HostState& host,
                        const DeviceSpeciesControls& controls, Flavour flavour) {
  const Index cells = host.cells;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;

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

  if (flavour == Flavour::Production) {
    cfd::gpu::computeReactionSourcesDevice(mechanism, composition, density, temperature, cells,
                                           controls, concentration, progressRate, omega, omegaSum,
                                           heatRelease, heatReleaseFromSources, status);
  } else {
    cfd::gpu::computeReactionSourcesFmadControlDevice(
        mechanism, composition, density, temperature, cells, controls, concentration, progressRate,
        omega, omegaSum, heatRelease, heatReleaseFromSources, status);
  }

  SourceFields out;
  out.concentration.resize(static_cast<std::size_t>(cells * species));
  out.progressRate.resize(static_cast<std::size_t>(cells * reactions));
  out.omega.resize(static_cast<std::size_t>(cells * species));
  out.omegaSum.resize(static_cast<std::size_t>(cells));
  out.heatRelease.resize(static_cast<std::size_t>(cells));
  out.heatReleaseFromSources.resize(static_cast<std::size_t>(cells));
  out.status.resize(static_cast<std::size_t>(cells));
  concentration.downloadTo(out.concentration.data(), cells * species);
  progressRate.downloadTo(out.progressRate.data(), cells * reactions);
  omega.downloadTo(out.omega.data(), cells * species);
  omegaSum.downloadTo(out.omegaSum.data(), cells);
  heatRelease.downloadTo(out.heatRelease.data(), cells);
  heatReleaseFromSources.downloadTo(out.heatReleaseFromSources.data(), cells);
  status.downloadTo(out.status.data(), cells);
  return out;
}

// The worst |da - db| / (eps * scale) over every reaction and cell -- the bound's own units.
Real worstProgressRateRatio(const DeviceMechanism& mechanism, const HostState& host,
                            const SourceFields& a, const SourceFields& b, Index* exercised) {
  const Index cells = host.cells;
  const Index reactions = mechanism.reactionCount;
  Real worst = 0.0;
  Index count = 0;
  for (Index r = 0; r < reactions; ++r) {
    for (Index c = 0; c < cells; ++c) {
      Real concentrations[8];
      for (Index i = 0; i < mechanism.speciesCount; ++i) {
        concentrations[i] = b.concentration[(i * cells) + c];
      }
      const Real scale = progressRateTermScale(mechanism, r, concentrations, host.temperature[c]);
      if (!(scale > 0.0)) {
        continue;
      }
      ++count;
      const Index slot = (r * cells) + c;
      worst =
          std::max(worst, std::abs(a.progressRate[slot] - b.progressRate[slot]) / (kEps * scale));
    }
  }
  if (exercised != nullptr) {
    *exercised = count;
  }
  return worst;
}

}  // namespace

// =================================================================================================
// DRYRUN 1 -- the control is really contracting
// =================================================================================================

TEST(P188A2DryRun, DRYRUN_1_TheFmadControlIsReallyContracting) {
  REQUIRE_GPU();
  // Without this, every "no difference" result below could mean the control translation unit lost
  // its --fmad=true property rather than that contraction does not matter there -- a probe that
  // prints exactly what success looks like.
  EXPECT_TRUE(cfd::gpu::fmadControlIsReallyContracting())
      << "the --fmad=true control translation unit is NOT contracting; every comparison in this "
         "file would then be vacuous";
}

// =================================================================================================
// DRYRUN 1b -- the instrument itself fails CLOSED
// =================================================================================================

TEST(P188A2DryRun, DRYRUN_1B_TheUlpInstrumentFailsClosed) {
  // The defect this replaces scored a SIGN FLIP as a perfect match, so the instrument is now
  // self-tested on planted disagreements before it is trusted with a verdict.
  EXPECT_EQ(ulpDistance(1.0, 1.0), 0U) << "identical values must be 0 apart";
  EXPECT_EQ(ulpDistance(1.0, std::nextafter(1.0, 2.0)), 1U) << "adjacent doubles are 1 apart";
  EXPECT_EQ(ulpDistance(-0.0, 0.0), 0U) << "the two zeros are adjacent in the key space";
  EXPECT_EQ(ulpDistance(-std::nextafter(0.0, 1.0), std::nextafter(0.0, 1.0)), 2U)
      << "the distance must be correct ACROSS zero";
  // the case that was silently wrong: opposite signs
  const Real x = 1.0e7;
  EXPECT_GT(ulpDistance(x, -x), 1000000000000000ULL)
      << "a sign flip is an enormous disagreement and must not wrap to a small number";
  // a planted non-finite pair must poison the worst-case figures rather than be skipped
  std::vector<Real> clean{1.0, 2.0};
  std::vector<Real> poisoned{1.0, std::numeric_limits<Real>::infinity()};
  const auto d = compare(poisoned, clean);
  EXPECT_EQ(d.nonFinite, 1);
  EXPECT_EQ(d.worstUlp, std::numeric_limits<std::uint64_t>::max());
}

// =================================================================================================
// DRYRUN 2 -- THE DECISIVE MEASUREMENT: how big is the FMA effect, in the bound's own units?
// =================================================================================================

TEST(P188A2DryRun, DRYRUN_2_HowLargeIsTheFmadEffectOnEachQuantity) {
  REQUIRE_GPU();
  ASSERT_TRUE(cfd::gpu::fmadControlIsReallyContracting());
  const auto mechanism = chainMechanism();
  const Index cells = 4096;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto host = makeState(cells, species);

  // ---- mixture properties: the self-validating signature
  const auto productionMixture = runMixture(mechanism, host, kProduction, Flavour::Production);
  const auto fmadMixture = runMixture(mechanism, host, kProduction, Flavour::FmadControl);

  std::cout << "  --fmad=true vs --fmad=false, MIXTURE PROPERTIES (" << cells << " cells):\n";
  const auto mDivergence = compare(fmadMixture.molecularWeight, productionMixture.molecularWeight);
  const auto rDivergence = compare(fmadMixture.gasConstant, productionMixture.gasConstant);
  const auto cpDivergence = compare(fmadMixture.specificHeatP, productionMixture.specificHeatP);
  report("M_mix", mDivergence);
  report("R_mix", rDivergence);
  report("cp_mix", cpDivergence);

  // THE SIGNATURE. deviceMolecularWeight accumulates `reciprocal += composition[i] / weight` -- a
  // divide then an add, which is not an a*b+c pattern, so FMA CANNOT reach it. R_mix and cp_mix
  // both accumulate `total += composition[i] * coefficient`, which it can. A control that broke
  // all three, or none of them, would be measuring something other than contraction.
  EXPECT_EQ(mDivergence.differing, 0)
      << "M_mix is divide-then-add; FMA cannot reach it, so a difference here means this control "
         "is not isolating contraction";
  EXPECT_GT(rDivergence.differing, 0) << "R_mix accumulates a*b+c and must be affected";
  EXPECT_GT(cpDivergence.differing, 0) << "cp_mix accumulates a*b+c and must be affected";

  // ---- reaction sources
  const auto productionSources = runSources(mechanism, host, kProduction, Flavour::Production);
  const auto fmadSources = runSources(mechanism, host, kProduction, Flavour::FmadControl);

  std::cout << "  --fmad=true vs --fmad=false, REACTION SOURCES:\n";
  const auto cDivergence = compare(fmadSources.concentration, productionSources.concentration);
  report("C_i", cDivergence);
  report("q", compare(fmadSources.progressRate, productionSources.progressRate));
  report("omega_i", compare(fmadSources.omega, productionSources.omega));
  report("S_E", compare(fmadSources.heatRelease, productionSources.heatRelease));

  // C_i is (rho*Y)/M -- one multiply, one divide, no accumulation. FMA cannot reach it either.
  EXPECT_EQ(cDivergence.differing, 0)
      << "C_i has no a*b+c site; a difference here means the control is not isolating contraction";

  // ---- THE DECIDING NUMBER, in the bound's OWN units
  Index exercised = 0;
  const Real fmadQRatio =
      worstProgressRateRatio(mechanism, host, fmadSources, productionSources, &exercised);
  std::cout << "\n  FMA's effect on q, IN THE BOUND'S UNITS: worst |dq| / (eps * sum of term\n"
            << "  magnitudes) = " << fmadQRatio << ",  against A2's derived bound of "
            << kDerivedUlpBound << "  (" << exercised << " of " << (cells * reactions)
            << " comparisons exercised)\n";

  EXPECT_GT(exercised, 0) << "no comparison had a nonzero term scale, so this measures nothing";

  // WHERE G9 CONDITION (4) IS ACTUALLY POLICED, and why it cannot be policed here.
  //
  // The first draft of A2 asserted that contraction must move q by MORE than the bound, so that a
  // bounded criterion could still reject it. This dry-run measured the opposite: contraction moves
  // q by ~0.84 bound-units while the libm difference the bound exists to tolerate is ~3.14. The
  // contraction effect is SMALLER than the effect the bound must admit, so no threshold that lets
  // the correct implementation pass can reject contraction on q. That is arithmetic, not a choice.
  //
  // A2 therefore polices condition (4) with the BITWISE mixture-property criterion asserted above,
  // where contraction IS visible. That is airtight rather than a consolation prize: R_mix, cp_mix
  // and every rate quantity are compiled from ONE translation unit carrying ONE --fmad=false
  // property, so the flag cannot be lost for the rate path alone without the zero-tolerance
  // criterion failing.
  SourceFields hostSide = productionSources;
  for (Index r = 0; r < reactions; ++r) {
    for (Index c = 0; c < cells; ++c) {
      Real concentrations[8];
      for (Index i = 0; i < species; ++i) {
        concentrations[i] = productionSources.concentration[(i * cells) + c];
      }
      DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
      hostSide.progressRate[(r * cells) + c] = cfd::gpu::deviceProgressRate(
          mechanism, r, concentrations, host.temperature[c], kProduction, &st);
    }
  }
  Index libmExercised = 0;
  const Real libmQRatio =
      worstProgressRateRatio(mechanism, host, productionSources, hostSide, &libmExercised);
  std::cout << "  the libm difference the bound must ADMIT, same units: " << libmQRatio << "\n"
            << "  => contraction (" << fmadQRatio << ") is SMALLER than libm (" << libmQRatio
            << "), so no passing\n"
            << "     threshold on q can reject contraction. Condition (4) is policed by the\n"
            << "     BITWISE mixture-property criterion above, not by this bound.\n";

  // The fact that forces that allocation. If a future toolchain made contraction the LARGER of the
  // two, a bounded criterion on q could police condition (4) after all -- and this assertion is
  // what would say so instead of leaving the allocation unexamined.
  EXPECT_LT(fmadQRatio, libmQRatio)
      << "contraction now moves q MORE than libm does (" << fmadQRatio << " vs " << libmQRatio
      << "); a bounded criterion on q could then police condition (4), and A2's allocation of that "
         "duty to the mixture-property criterion should be reconsidered";
  // and the criterion that DOES police it must be live
  EXPECT_GT(rDivergence.differing, 0)
      << "the bitwise mixture-property criterion is the only thing policing condition (4); if "
         "contraction does not move R_mix, nothing in this suite detects a lost --fmad=false";

  // ---- the accumulation ISOLATED: recompute omega and S_E on the host from the CONTROL's own q
  std::vector<Real> hostOmegaFromControlQ(static_cast<std::size_t>(cells * species));
  std::vector<Real> hostHeatFromControlQ(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    Real q[8];
    for (Index r = 0; r < reactions; ++r) {
      q[r] = fmadSources.progressRate[(r * cells) + c];
    }
    Real omega[8];
    cfd::gpu::deviceSpeciesSources(mechanism, q, omega, kProduction);
    for (Index i = 0; i < species; ++i) {
      hostOmegaFromControlQ[(i * cells) + c] = omega[i];
    }
    DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
    hostHeatFromControlQ[c] = cfd::gpu::deviceHeatRelease(mechanism, q, kProduction, &st);
  }
  std::cout << "  the ACCUMULATION isolated (host --fmad=false vs control --fmad=true, SAME q):\n";
  const auto accumulationOmega = compare(fmadSources.omega, hostOmegaFromControlQ);
  const auto accumulationHeat = compare(fmadSources.heatRelease, hostHeatFromControlQ);
  report("omega_i", accumulationOmega);
  report("S_E", accumulationHeat);

  // ---- WHY the accumulation is unaffected, tested rather than asserted.
  // Hypothesis: the coefficients M_i * nu_i times q are EXACTLY representable for this mechanism,
  // so there is no rounding in the multiply for a fusion to remove. The host's own std::fma
  // settles it: if mul-then-add and fma agree on this data, no contraction COULD change the
  // result, and the accumulation clause's control is N5/N8 rather than FMA.
  Index fmaWouldChange = 0;
  for (Index c = 0; c < cells; ++c) {
    for (Index i = 0; i < species; ++i) {
      Real viaMultiply = 0.0;
      Real viaFma = 0.0;
      for (Index r = 0; r < reactions; ++r) {
        const Real coefficient =
            mechanism.species[i].molecularWeight * mechanism.reactions[r].net[i];
        const Real rate = productionSources.progressRate[(r * cells) + c];
        viaMultiply = viaMultiply + (coefficient * rate);
        viaFma = std::fma(coefficient, rate, viaFma);
      }
      if (viaMultiply != viaFma) {
        ++fmaWouldChange;
      }
    }
  }
  std::cout << "  host std::fma vs host multiply-then-add on the SAME omega accumulation: "
            << fmaWouldChange << " / " << (cells * species) << " would change\n";
  if (fmaWouldChange == 0) {
    std::cout << "  => the products M_i*nu_i*q are exact for this mechanism, so NO contraction\n"
              << "     could alter omega. The accumulation clause's control is N5/N8, not FMA.\n";
  } else {
    std::cout << "  => contraction COULD alter omega, so the device control TU is not contracting\n"
              << "     that site and the FMA control is weaker there than it appears.\n";
  }
  // Whichever way it went, the two must agree: if the host says FMA cannot change it, the device
  // control must also have found no change, and vice versa. Disagreement means one of the two is
  // not measuring what it claims.
  EXPECT_EQ(fmaWouldChange == 0, accumulationOmega.differing == 0)
      << "the host fma probe and the device control disagree about whether contraction can reach "
         "the omega accumulation; one of them is not measuring contraction";
}

// =================================================================================================
// DRYRUN 3 -- the derived bounds against the CORRECT implementation, and the margin
// =================================================================================================

TEST(P188A2DryRun, DRYRUN_3_TheDerivedBoundsHoldWithMargin) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 4096;
  const Index reactions = mechanism.reactionCount;
  const auto host = makeState(cells, mechanism.speciesCount);

  DeviceBuffer<Real> temperature(cells);
  temperature.uploadFrom(host.temperature.data(), cells);
  DeviceBuffer<Real> rateCoefficient(cells * reactions);
  DeviceBuffer<int> branch(cells * reactions);
  DeviceBuffer<int> status(cells);
  cfd::gpu::computeRateCoefficientsDevice(mechanism, temperature, cells, kProduction,
                                          rateCoefficient, branch, status);
  std::vector<Real> gpuK(static_cast<std::size_t>(cells * reactions));
  std::vector<int> gpuBranch(static_cast<std::size_t>(cells * reactions));
  rateCoefficient.downloadTo(gpuK.data(), cells * reactions);
  branch.downloadTo(gpuBranch.data(), cells * reactions);

  std::uint64_t worstKUlp = 0;
  Index branchDisagreements = 0;
  for (Index r = 0; r < reactions; ++r) {
    for (Index c = 0; c < cells; ++c) {
      DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
      int taken = -1;
      const Real hostK = cfd::gpu::deviceRateCoefficient(mechanism.forward[r], host.temperature[c],
                                                         kProduction, &st, &taken);
      const Index slot = (r * cells) + c;
      ASSERT_TRUE(std::isfinite(hostK) && std::isfinite(gpuK[slot]))
          << "a non-finite k at reaction " << r << " cell " << c
          << " would leave the worst-case figure meaningless";
      worstKUlp = std::max(worstKUlp, ulpDistance(gpuK[slot], hostK));
      if (gpuBranch[slot] != taken) {
        ++branchDisagreements;
      }
    }
  }

  const auto sources = runSources(mechanism, host, kProduction, Flavour::Production);
  SourceFields hostSide = sources;  // same concentrations; progress rates recomputed on the host
  for (Index r = 0; r < reactions; ++r) {
    for (Index c = 0; c < cells; ++c) {
      Real concentrations[8];
      for (Index i = 0; i < mechanism.speciesCount; ++i) {
        concentrations[i] = sources.concentration[(i * cells) + c];
      }
      DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
      hostSide.progressRate[(r * cells) + c] = cfd::gpu::deviceProgressRate(
          mechanism, r, concentrations, host.temperature[c], kProduction, &st);
    }
  }
  Index exercised = 0;
  const Real worstQRatio = worstProgressRateRatio(mechanism, host, sources, hostSide, &exercised);

  std::cout << "  CORRECT implementation against the DERIVED bounds:\n"
            << "    k:  worst ULP distance " << worstKUlp << "  against the derived bound "
            << kDerivedUlpBound << "\n"
            << "    q:  worst |dq| / (eps * sum of term magnitudes) = " << worstQRatio
            << "  against the derived bound " << kDerivedUlpBound << "\n"
            << "    branch disagreements: " << branchDisagreements << " / " << (cells * reactions)
            << "\n";
  EXPECT_GT(exercised, 0) << "no cell had a nonzero term scale, so the q bound was not exercised";
  EXPECT_LE(worstKUlp, kDerivedUlpBound);
  EXPECT_LE(worstQRatio, static_cast<Real>(kDerivedUlpBound));
  EXPECT_EQ(branchDisagreements, 0);
}

// =================================================================================================
// DRYRUN 4 -- every negative control, against THE CRITERION IT ACTUALLY CONTROLS
// =================================================================================================

TEST(P188A2DryRun, DRYRUN_4_EachControlViolatesTheCriterionItControls) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 1024;
  const auto host = makeState(cells, mechanism.speciesCount);

  // N1 and N2 perturb the species index inside the MIXTURE PROPERTY functions only.
  // deviceConcentration and deviceSpeciesSources never consult them, so running them against the
  // reaction sources -- as the first pass of this dry-run did -- reports a dead control.
  const auto referenceMixture = runMixture(mechanism, host, kProduction, Flavour::Production);
  struct MixtureCase {
    const char* name;
    DeviceSpeciesControls controls;
  };
  std::vector<MixtureCase> mixtureCases;
  {
    DeviceSpeciesControls c{};
    c.permuteSpeciesIndex = true;
    mixtureCases.push_back({"N1 permute species index", c});
  }
  {
    DeviceSpeciesControls c{};
    c.oneWeightForAllSpecies = true;
    mixtureCases.push_back({"N2 one weight for all species", c});
  }

  std::cout << "  controls on the MIXTURE PROPERTY criterion (BITWISE, zero tolerance):\n";
  for (const auto& one : mixtureCases) {
    const auto perturbed = runMixture(mechanism, host, one.controls, Flavour::Production);
    const auto m = compare(perturbed.molecularWeight, referenceMixture.molecularWeight);
    const auto r = compare(perturbed.gasConstant, referenceMixture.gasConstant);
    const auto cp = compare(perturbed.specificHeatP, referenceMixture.specificHeatP);
    const Index differing = m.differing + r.differing + cp.differing;
    const std::uint64_t worst = std::max({m.worstUlp, r.worstUlp, cp.worstUlp});
    std::cout << "    " << one.name << ":  " << differing << " values differ,  worst ULP " << worst
              << ",  worst relative "
              << std::max({m.worstRelative, r.worstRelative, cp.worstRelative}) << "\n";
    EXPECT_GT(differing, 0) << one.name << " changed nothing, so it controls nothing";
  }

  // N4, N5, N6 and N8 perturb the kinetics, the sources and the heat release.
  const auto referenceSources = runSources(mechanism, host, kProduction, Flavour::Production);
  struct SourceCase {
    const char* name;
    DeviceSpeciesControls controls;
    bool boundedCriterion;  // true: must exceed the ULP bound.  false: a BITWISE clause.
  };
  std::vector<SourceCase> sourceCases;
  {
    DeviceSpeciesControls c{};
    c.flipArrheniusExponentSign = true;
    sourceCases.push_back({"N4 flip Arrhenius exponent sign", c, true});
  }
  {
    DeviceSpeciesControls c{};
    c.omitMolecularWeightInOmega = true;
    sourceCases.push_back({"N5 omit M_i in omega", c, false});
  }
  {
    DeviceSpeciesControls c{};
    c.flipHeatReleaseSign = true;
    sourceCases.push_back({"N6 flip the heat-release sign", c, false});
  }
  {
    DeviceSpeciesControls c{};
    c.omitLastReaction = true;
    // N8 drops the last reaction from the omega and S_E ACCUMULATIONS and leaves the progress
    // rates untouched -- measured at 0 in the q bound's units. It controls the BITWISE
    // accumulation clause, not the bounded one. Classifying it as bounded made it look dead.
    sourceCases.push_back({"N8 omit the last reaction", c, false});
  }

  std::cout << "  controls on the REACTION SOURCE criteria:\n";
  for (const auto& one : sourceCases) {
    const auto perturbed = runSources(mechanism, host, one.controls, Flavour::Production);
    const auto q = compare(perturbed.progressRate, referenceSources.progressRate);
    const auto omega = compare(perturbed.omega, referenceSources.omega);
    const auto heat = compare(perturbed.heatRelease, referenceSources.heatRelease);
    const Index differing = q.differing + omega.differing + heat.differing;
    const std::uint64_t worst = std::max({q.worstUlp, omega.worstUlp, heat.worstUlp});
    const Real worstRelative = std::max({q.worstRelative, omega.worstRelative, heat.worstRelative});
    std::cout << "    " << one.name << ":  " << differing << " values differ,  worst ULP " << worst
              << ",  worst relative " << worstRelative << "\n";

    EXPECT_GT(differing, 0) << one.name << " changed nothing, so it controls nothing";
    if (one.boundedCriterion) {
      Index exercised = 0;
      const Real ratio =
          worstProgressRateRatio(mechanism, host, perturbed, referenceSources, &exercised);
      std::cout << "        in the q bound's units: " << ratio << "  against " << kDerivedUlpBound
                << "\n";
      EXPECT_GT(ratio, static_cast<Real>(kDerivedUlpBound))
          << one.name << " stayed inside the q bound, so that bound cannot detect it";
    } else {
      // A BITWISE clause needs only a nonzero difference, which is asserted above. The ULP figure
      // is reported so a sign flip cannot be mistaken for a small perturbation.
      EXPECT_GT(worst, 0U) << one.name
                           << " differs but the ULP instrument scored it 0 -- the instrument, not "
                              "the control, is wrong";
    }
  }
}

// =================================================================================================
// DRYRUN 5 -- A2's replacement clause: omega and S_E BITWISE from the device's own q
// =================================================================================================

TEST(P188A2DryRun, DRYRUN_5_OmegaAndHeatReleaseAreBitwiseFromTheDeviceOwnProgressRates) {
  REQUIRE_GPU();
  const auto mechanism = chainMechanism();
  const Index cells = 4096;
  const Index species = mechanism.speciesCount;
  const Index reactions = mechanism.reactionCount;
  const auto host = makeState(cells, species);
  const auto sources = runSources(mechanism, host, kProduction, Flavour::Production);

  // This is A2's structure. The libm difference lives in k and therefore in q. Feeding the
  // device's OWN q back through the host accumulation removes it as a confound, and what remains
  // is the accumulation itself -- order, parenthesisation, coefficients -- under ZERO tolerance.
  std::vector<Real> hostOmega(static_cast<std::size_t>(cells * species));
  std::vector<Real> hostHeat(static_cast<std::size_t>(cells));
  std::vector<Real> hostHeatMass(static_cast<std::size_t>(cells));
  for (Index c = 0; c < cells; ++c) {
    Real q[8];
    for (Index r = 0; r < reactions; ++r) {
      q[r] = sources.progressRate[(r * cells) + c];
    }
    Real omega[8];
    cfd::gpu::deviceSpeciesSources(mechanism, q, omega, kProduction);
    for (Index i = 0; i < species; ++i) {
      hostOmega[(i * cells) + c] = omega[i];
    }
    DeviceSpeciesStatus st = DeviceSpeciesStatus::Ok;
    hostHeat[c] = cfd::gpu::deviceHeatRelease(mechanism, q, kProduction, &st);
    hostHeatMass[c] = cfd::gpu::deviceHeatReleaseFromSources(mechanism, omega);
  }

  const auto omegaDivergence = compare(sources.omega, hostOmega);
  const auto heatDivergence = compare(sources.heatRelease, hostHeat);
  const auto heatMassDivergence = compare(sources.heatReleaseFromSources, hostHeatMass);
  std::cout << "  accumulations from the DEVICE's own q, under zero tolerance:\n";
  report("omega_i", omegaDivergence);
  report("S_E", heatDivergence);
  report("S_E (mass route)", heatMassDivergence);

  EXPECT_EQ(omegaDivergence.differing, 0) << "omega from the device's own q must be BITWISE";
  EXPECT_EQ(heatDivergence.differing, 0) << "S_E from the device's own q must be BITWISE";
  EXPECT_EQ(heatMassDivergence.differing, 0)
      << "S_E by the mass route from the device's own omega must be BITWISE";
}
