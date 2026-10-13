// P18.4 -- INSTRUMENTATION, not production and not a test.
//
// Section 50 asks for measured values rather than PASS/FAIL. This reports every
// quantity that section names, from the built library.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using namespace cfd;
using namespace cfd::species;
using cfd::fields::ScalarField;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {
constexpr Real kEps = std::numeric_limits<Real>::epsilon();
SensibleEnthalpy cpOf(Real v, const std::string& n) {
  return SensibleEnthalpy(TemperatureDependentProperty::constant(v, n), 298.15, 0.0);
}
}  // namespace

int main() {
  const Real mA = 0.0625, mB = 0.03125, mC = 0.125, mInert = 0.25;
  const SpeciesRegistry registry({"A", "B", "C", "Inert"});
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(mA, cpOf(1000.0, "cp_A")));
  species.push_back(SpeciesThermodynamics(mB, cpOf(1200.0, "cp_B")));
  species.push_back(SpeciesThermodynamics(mC, cpOf(900.0, "cp_C")));
  species.push_back(SpeciesThermodynamics(mInert, cpOf(800.0, "cp_Inert")));
  const MixtureThermodynamics mixture(registry, std::move(species));

  std::printf("=== P18.4 quantitative evidence (section 50) ===\n");
  std::printf("eps = %.17g\n\n", kEps);

  // ---- per-reaction mass residual ----
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("A+2B->C", registry, {{"A", 1.0}, {"B", 2.0}}, {{"C", 1.0}},
                               ReactionDirection::Reversible));
  reactions.push_back(Reaction("C->A+2B", registry, {{"C", 1.0}}, {{"A", 1.0}, {"B", 2.0}},
                               ReactionDirection::Irreversible));
  const ReactionMechanism mechanism(registry, std::move(reactions), mixture);
  const auto balance = mechanism.massBalance(mixture);
  std::printf("max per-reaction |sum_i M_i nu_i|      %.17g   (reaction %zu)\n",
              balance.worstResidual, static_cast<std::size_t>(balance.worstReaction));
  std::printf("  relative to max|M_i nu_i|            %.17g\n", balance.worstRelative);
  std::printf("  G6 tolerance 4 eps * max|M nu|       %.17g\n",
              massClosureTolerance(mechanism.reaction(0).massScale(mixture)));
  std::printf("\n");

  // ---- per-cell source conservation, spatially varying and signed ----
  const Index cells = 256;
  ReactionProgressRates rates = zeroProgressRates(2, cells);
  for (Index c = 0; c < cells; ++c) {
    rates.perReaction[0][c] = -8.0 + (0.0625 * static_cast<Real>(c));  // spans negative to positive
    rates.perReaction[1][c] = 0.5 + (0.01 * static_cast<Real>(c));     // irreversible, >= 0
  }
  const auto sources = reactionSpeciesSources(mechanism, mixture, rates, cells);
  const auto conservation = sourceConservation(sources, cells);
  std::printf("max per-cell |sum_i omega_i|           %.17g   (cell %zu)\n",
              conservation.maxAbsoluteSum, static_cast<std::size_t>(conservation.worstCell));
  std::printf("  max |omega_i| for scale              %.17g\n", conservation.maxAbsoluteSource);
  std::printf("  relative                             %.17g\n",
              conservation.maxAbsoluteSum / conservation.maxAbsoluteSource);
  std::printf("\n");

  // ---- source ratio: omega_i/(M_i nu_i) must recover q ----
  ReactionProgressRates single = zeroProgressRates(2, 1);
  single.perReaction[0].fill(2.75);
  const auto oneRx = reactionSpeciesSources(mechanism, mixture, single, 1);
  Real worstRatio = 0.0;
  for (Index i = 0; i < 4; ++i) {
    const Real nu = mechanism.reaction(0).netStoichiometry(i);
    if (nu == 0.0) continue;
    const Real recovered = oneRx.perSpecies[i].Su[0] / (mixture.species(i).molecularWeight() * nu);
    worstRatio = std::max(worstRatio, std::abs(recovered - 2.75) / 2.75);
  }
  std::printf("source ratio error (recovers q)        %.17g\n", worstRatio);

  // ---- zero-rate source magnitude ----
  const auto zeroSrc = reactionSpeciesSources(mechanism, mixture, zeroProgressRates(2, 64), 64);
  Real zeroMagnitude = 0.0;
  for (Index i = 0; i < 4; ++i) {
    for (Index c = 0; c < 64; ++c) {
      zeroMagnitude = std::max(zeroMagnitude, std::abs(zeroSrc.perSpecies[i].Su[c]));
    }
  }
  std::printf("zero-rate source magnitude             %.17g   (must be exactly 0)\n",
              zeroMagnitude);

  // ---- reversible zero-net ----
  ReactionProgressRates net0 = zeroProgressRates(2, 1);
  net0.perReaction[0].fill(4.25 - 4.25);
  const auto equilibrium = reactionSpeciesSources(mechanism, mixture, net0, 1);
  Real zeroNet = 0.0;
  for (Index i = 0; i < 4; ++i) {
    zeroNet = std::max(zeroNet, std::abs(equilibrium.perSpecies[i].Su[0]));
  }
  std::printf("reversible zero-net error              %.17g   (must be exactly 0)\n", zeroNet);

  // ---- multi-reaction accumulation: both vs each alone ----
  ReactionProgressRates only0 = zeroProgressRates(2, 1);
  only0.perReaction[0].fill(2.0);
  ReactionProgressRates only1 = zeroProgressRates(2, 1);
  only1.perReaction[1].fill(0.5);
  ReactionProgressRates both = zeroProgressRates(2, 1);
  both.perReaction[0].fill(2.0);
  both.perReaction[1].fill(0.5);
  const auto s0 = reactionSpeciesSources(mechanism, mixture, only0, 1);
  const auto s1 = reactionSpeciesSources(mechanism, mixture, only1, 1);
  const auto sBoth = reactionSpeciesSources(mechanism, mixture, both, 1);
  Real accumulation = 0.0;
  for (Index i = 0; i < 4; ++i) {
    const Real expected = s0.perSpecies[i].Su[0] + s1.perSpecies[i].Su[0];
    const Real scale = std::max(std::abs(expected), 1.0e-300);
    accumulation = std::max(accumulation, std::abs(sBoth.perSpecies[i].Su[0] - expected) / scale);
  }
  std::printf("multi-reaction accumulation error      %.17g\n", accumulation);
  std::printf("\n");

  // ---- determinism ----
  int differences = 0;
  const auto first = reactionSpeciesSources(mechanism, mixture, rates, cells);
  for (int repeat = 0; repeat < 200; ++repeat) {
    const auto again = reactionSpeciesSources(mechanism, mixture, rates, cells);
    for (Index i = 0; i < 4; ++i) {
      for (Index c = 0; c < cells; ++c) {
        if (again.perSpecies[i].Su[c] != first.perSpecies[i].Su[c]) ++differences;
        if (again.perSpecies[i].Sp[c] != first.perSpecies[i].Sp[c]) ++differences;
      }
    }
  }
  std::printf("determinism: 200 repeats x %zu species x %zu cells, bitwise differences : %d\n",
              static_cast<std::size_t>(4), static_cast<std::size_t>(cells), differences);
  std::printf("\n");

  // ---- control margins ----
  std::printf("=== control margins ===\n");
  const Real correctA = oneRx.perSpecies[0].Su[0];
  std::printf("  N2 omitting M_A       omega %.6e -> %.6e   ratio %.1fx\n", correctA,
              mechanism.reaction(0).netStoichiometry(0) * 2.75,
              std::abs((mechanism.reaction(0).netStoichiometry(0) * 2.75) / correctA));
  std::printf("  N9 kmol instead of mol                          ratio %.1fx\n", 1000.0);
  // N1: perturb a stoichiometric coefficient
  const Reaction perturbed("p", registry, {{"A", 1.0}, {"B", 2.0 + 1.0e-9}}, {{"C", 1.0}});
  std::printf("  N1 coefficient perturbed by 1e-9   residual %.6e vs tolerance %.6e   %.0fx over\n",
              std::abs(perturbed.massResidual(mixture)),
              massClosureTolerance(perturbed.massScale(mixture)),
              std::abs(perturbed.massResidual(mixture)) /
                  massClosureTolerance(perturbed.massScale(mixture)));

  // ---- A MECHANISM THAT BALANCES ONLY TO ROUND-OFF, so the tolerance is
  // ---- EXERCISED rather than bypassed by exactly-representable weights.
  std::printf("\n=== non-dyadic weights: the balance is ROUND-OFF, not exact ===\n");
  std::printf("    (the dyadic mechanism above gives exactly 0 everywhere, which is sharp but\n"
              "     never exercises the tolerance. These weights were found by search outside\n"
              "     CFDApp so that M_C - M_A - 2 M_B is non-zero yet within 4 eps * scale.)\n");
  const Real nA = 0.05552842445740003;
  const Real nB = 0.028473354135886624;
  const Real nC = 0.11247513272917328;  // = nA + 2 nB in double, with a 0.03 eps residual
  std::vector<SpeciesThermodynamics> odd;
  odd.push_back(SpeciesThermodynamics(nA, cpOf(1000.0, "cp_A")));
  odd.push_back(SpeciesThermodynamics(nB, cpOf(1200.0, "cp_B")));
  odd.push_back(SpeciesThermodynamics(nC, cpOf(900.0, "cp_C")));
  odd.push_back(SpeciesThermodynamics(mInert, cpOf(800.0, "cp_Inert")));
  const MixtureThermodynamics oddMixture(registry, std::move(odd));
  std::vector<Reaction> oddReactions;
  oddReactions.push_back(Reaction("A+2B->C", registry, {{"A", 1.0}, {"B", 2.0}}, {{"C", 1.0}},
                                  ReactionDirection::Reversible));
  const ReactionMechanism oddMechanism(registry, std::move(oddReactions), oddMixture);
  const auto oddBalance = oddMechanism.massBalance(oddMixture);
  std::printf("  per-reaction |sum M nu|              %.17g   (%.2f eps)\n",
              oddBalance.worstResidual, oddBalance.worstResidual / kEps);
  std::printf("  tolerance 4 eps * max|M nu|          %.17g\n",
              massClosureTolerance(oddMechanism.reaction(0).massScale(oddMixture)));
  std::printf("  ACCEPTED by the mechanism            yes -- it is within tolerance\n");

  ReactionProgressRates oddRates = zeroProgressRates(1, cells);
  for (Index c = 0; c < cells; ++c) {
    oddRates.perReaction[0][c] = -8.0 + (0.0625 * static_cast<Real>(c));
  }
  const auto oddSources = reactionSpeciesSources(oddMechanism, oddMixture, oddRates, cells);
  const auto oddConservation = sourceConservation(oddSources, cells);
  std::printf("  max per-cell |sum omega_i|           %.17g\n", oddConservation.maxAbsoluteSum);
  std::printf("  max |omega_i| for scale              %.17g\n", oddConservation.maxAbsoluteSource);
  std::printf("  relative                             %.17g   (allowance 8 eps = %.6e)\n",
              oddConservation.maxAbsoluteSum / oddConservation.maxAbsoluteSource, 8.0 * kEps);
  return 0;
}
