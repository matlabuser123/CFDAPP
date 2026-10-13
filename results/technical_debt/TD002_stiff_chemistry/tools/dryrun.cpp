// TD-002 GATE DRY-RUN -- every criterion run against the UNCHANGED baseline, before the freeze.
//
// CLAUDE.md section 5.2. The questions this must answer, for each criterion:
//   * does the baseline FAIL it (so it is a real instrument), or PASS it (so it measures nothing)?
//   * can a correct post-change implementation satisfy it at all?
//   * is the threshold reachable -- in particular, is C3's 30 000 sub-steps reachable, and does the
//     uncompensated update really miss the bound by the 100x the derivation claims?
//
// INVESTIGATION ONLY. No production file is changed. The compensated variant below is a LOCAL
// reimplementation of the march used only to answer "can the fix satisfy C1/C2/C3", so that the
// threshold is validated before it is frozen rather than after.
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/ChemistryIntegrator.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using namespace cfd::species;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
constexpr Real kWeight = 0.0625;
constexpr Real kCp = 1000.0;
constexpr Real kFormationB = -2.0e6;
constexpr Real kPre = 5.0;
constexpr Real kEa = 30000.0;
constexpr Real kRho = 1.0;
constexpr Real kT0 = 1000.0;

SpeciesThermodynamics sp(Real w, const std::string& n) {
  return SpeciesThermodynamics(
      w, cfd::thermal::SensibleEnthalpy(
             cfd::thermal::TemperatureDependentProperty::constant(kCp, n), 298.15, 0.0));
}

struct Problem {
  SpeciesRegistry registry;
  MixtureThermodynamics mixture;
  FormationEnthalpies formation;
  ReactionMechanism mechanism;
  ReactionKinetics kinetics;
};

// `reversible` is what keeps the composition O(1) for an unbounded number of steps: without it the
// fuel decays to ~1e-323 and every addition becomes exact, which audit section 2.1 showed makes the
// problem DEGENERATE for round-off accumulation. A gate written on the non-reversible problem alone
// would be vacuous, so this dry-run has to establish that a drift-exercising problem exists.
Problem makeProblem(bool reversible, Real pre = kPre, Real formationB = kFormationB) {
  SpeciesRegistry registry({"A", "B"});
  std::vector<SpeciesThermodynamics> thermo;
  thermo.push_back(sp(kWeight, "cp_A"));
  thermo.push_back(sp(kWeight, "cp_B"));
  MixtureThermodynamics mixture(registry, std::move(thermo));
  FormationEnthalpies formation =
      FormationEnthalpies::fromMolar(mixture, {0.0, formationB * kWeight});

  std::vector<Reaction> reactions;
  // The DIRECTION has to be declared too: ReactionKinetics refuses reverse parameters on a
  // reaction marked irreversible ("an irreversible reaction has no reverse rate"). Found by this
  // dry-run aborting -- which is the API refusing to be used half-way, correctly.
  reactions.push_back(Reaction("A->B", registry, {{"A", 1.0}}, {{"B", 1.0}},
                               reversible ? ReactionDirection::Reversible
                                          : ReactionDirection::Irreversible));
  ReactionMechanism mechanism(registry, std::move(reactions), mixture);

  std::vector<ReactionRateParameters> parameters;
  std::optional<ArrheniusParameters> reverse =
      reversible ? std::optional<ArrheniusParameters>(ArrheniusParameters(pre, 0.0, kEa))
                 : std::nullopt;
  parameters.push_back(
      ReactionRateParameters{"A->B", ArrheniusParameters(pre, 0.0, kEa), reverse});
  ReactionKinetics kinetics(mechanism, std::move(parameters));

  return Problem{registry, std::move(mixture), std::move(formation), std::move(mechanism),
                 std::move(kinetics)};
}

ReactorState start(const SpeciesRegistry& r) {
  return ReactorState{MassFractionState(r, 1, {1.0, 0.0}), cfd::fields::ScalarField(1, kT0),
                      cfd::fields::ScalarField(1, kRho)};
}

Real dev(const ReactorState& s) {
  Real sum = 0.0;
  for (Index i = 0; i < s.composition.speciesCount(); ++i) sum += s.composition.massFraction(i, 0);
  return std::abs(sum - 1.0);
}

void head(const std::string& t) {
  std::cout << "\n" << std::string(94, '=') << "\n" << t << "\n" << std::string(94, '=') << "\n";
}

// A LOCAL compensated march, used ONLY to answer "can the fix meet C1/C2/C3?". It is not the
// implementation; it reuses advanceClosedReactor one step at a time and carries the per-species
// low-order bits across steps, which is the architecture section 2 of the gate describes.
struct LocalMarch {
  std::size_t steps = 0;
  Real worstDeviation = 0.0;
  Real minMassFraction = 2.0;
  bool threw = false;
  std::string message;
};

LocalMarch localMarch(const Problem& p, Real dt, std::size_t stepCount, bool compensated) {
  LocalMarch out;
  std::vector<Real> Y{1.0, 0.0};
  std::vector<Real> carry{0.0, 0.0};
  Real T = kT0;
  const Real h = dt / static_cast<Real>(stepCount);

  for (std::size_t n = 0; n < stepCount; ++n) {
    // The rates come from the production kinetics, on the CURRENT state -- never reused.
    MassFractionState state(p.registry, 1, Y);
    cfd::fields::ScalarField Tf(1, T), rhof(1, kRho);
    const auto rates =
        arrheniusProgressRates(p.mechanism, p.kinetics, p.mixture, state, rhof, Tf);
    const auto sources = reactionSpeciesSources(p.mechanism, p.mixture, rates, 1);

    for (std::size_t i = 0; i < Y.size(); ++i) {
      const Real d = (h * sources.perSpecies[i].Su[0]) / kRho;
      if (compensated) {
        const Real y = d - carry[i];
        const Real t = Y[i] + y;
        carry[i] = (t - Y[i]) - y;
        Y[i] = t;
      } else {
        Y[i] = Y[i] + d;
      }
      out.minMassFraction = std::min(out.minMassFraction, Y[i]);
    }
    // Energy: the same recovery advanceClosedReactor performs, via the reactor itself on the
    // pre-update state, so the temperature trajectory is the production one.
    try {
      const auto after =
          advanceClosedReactor(p.mechanism, p.mixture, p.formation, rates,
                               ReactorState{state, Tf, rhof}, h);
      T = after.temperature[0];
    } catch (const std::exception& e) {
      out.threw = true;
      out.message = e.what();
      return out;
    }
    Real sum = 0.0;
    for (Real v : Y) sum += v;
    out.worstDeviation = std::max(out.worstDeviation, std::abs(sum - 1.0));
    out.steps = n + 1;
  }
  return out;
}

}  // namespace

int main() {
  std::cout.precision(17);
  const Real tol2 = massFractionSumTolerance(2);
  std::cout << "eps = " << kEps << "\n4*eps*2 = " << tol2 << "  (= 8 eps)\n";

  // ------------------------------------------------------------------------------------------ C1
  head("C1 DRY-RUN -- the reproducer: dt=20, control=0.0125. The BASELINE must FAIL.");
  {
    Problem p = makeProblem(false);
    ChemistryIntegratorSettings s;
    s.maximumFractionalChange = 0.0125;
    s.maximumSubSteps = 2000000;
    try {
      const auto r = integrateChemistry(p.mechanism, p.mixture, p.formation, p.kinetics,
                                        start(p.registry), 20.0, s);
      std::printf("  baseline COMPLETED: %zu sub-steps, deviation %.6e  -> C1 PASSES on the\n"
                  "  baseline, which would make it VACUOUS\n", r.subSteps, dev(r.state));
    } catch (const std::exception& e) {
      std::printf("  baseline THREW -- C1 is a real instrument\n    %s\n", e.what());
    }
  }

  // ------------------------------------------------------------------------------------------ C2
  head("C2 DRY-RUN -- the control ladder. Which rungs does the baseline reach?");
  std::cout << "    control      sub-steps   deviation        in eps   verdict\n";
  {
    Problem p = makeProblem(false);
    for (Real c : {0.4, 0.2, 0.1, 0.05, 0.0125, 0.003125, 0.00078125}) {
      ChemistryIntegratorSettings s;
      s.maximumFractionalChange = c;
      s.maximumSubSteps = 2000000;
      try {
        const auto r = integrateChemistry(p.mechanism, p.mixture, p.formation, p.kinetics,
                                          start(p.registry), 20.0, s);
        const Real d = dev(r.state);
        std::printf("  %10.8f   %9zu   %.6e   %7.2f   %s\n", c, r.subSteps, d, d / kEps,
                    d <= tol2 ? "ok" : "OVER BOUND");
      } catch (const std::exception&) {
        std::printf("  %10.8f   THREW\n", c);
      }
    }
  }

  // ------------------------------------------------------------------------------------------ C3
  head("C3 DRY-RUN -- is 30 000 steps reachable, and does the uncompensated march miss by 100x?");
  std::cout << "  A reversible reaction keeps both species O(1) indefinitely, which is what makes\n"
               "  the problem exercise round-off at all (audit 2.1: the irreversible one does not).\n\n";
  std::cout << "    variant          steps      worst deviation    in eps    x bound   min Y\n";
  {
    Problem p = makeProblem(true);
    for (bool compensated : {false, true}) {
      for (std::size_t n : {std::size_t{1000}, std::size_t{30000}}) {
        const LocalMarch m = localMarch(p, 2.0, n, compensated);
        if (m.threw) {
          std::printf("  %-14s %8zu      THREW: %s\n", compensated ? "compensated" : "plain",
                      n, m.message.substr(0, 60).c_str());
        } else {
          std::printf("  %-14s %8zu      %.6e   %9.2f  %8.2f   %+.3e\n",
                      compensated ? "compensated" : "plain", m.steps, m.worstDeviation,
                      m.worstDeviation / kEps, m.worstDeviation / tol2, m.minMassFraction);
        }
      }
    }
  }

  // ------------------------------------------------------------------------------------------ C5
  head("C5 DRY-RUN -- refinement direction on the baseline (the level the fix must not worsen)");
  {
    Problem p = makeProblem(false);
    // P18.7's reference at t = 1.0
    const Real refA = 0.78521237887865421, refT = 1429.5752422426916;
    std::cout << "    control     Y_A error        T error\n";
    for (Real c : {0.2, 0.05, 0.0125}) {
      ChemistryIntegratorSettings s;
      s.maximumFractionalChange = c;
      const auto r = integrateChemistry(p.mechanism, p.mixture, p.formation, p.kinetics,
                                        start(p.registry), 1.0, s);
      std::printf("  %9.6f   %.9e   %.9e\n", c,
                  std::abs(r.state.composition.massFraction(0, 0) - refA) / refA,
                  std::abs(r.state.temperature[0] - refT) / refT);
    }
  }

  // ---------------------------------------------------------------------------------------- C6ab
  head("C6 DRY-RUN -- the zero limits on the baseline");
  {
    Problem zeroRate = makeProblem(false, 0.0);
    const auto r = integrateChemistry(zeroRate.mechanism, zeroRate.mixture, zeroRate.formation,
                                      zeroRate.kinetics, start(zeroRate.registry), 20.0);
    std::printf("  C6a pre_exponential=0 : %zu sub-step(s), Y_A = %.17g, T = %.17g\n", r.subSteps,
                r.state.composition.massFraction(0, 0), r.state.temperature[0]);

    Problem zeroHeat = makeProblem(false, kPre, 0.0);
    const auto r2 = integrateChemistry(zeroHeat.mechanism, zeroHeat.mixture, zeroHeat.formation,
                                       zeroHeat.kinetics, start(zeroHeat.registry), 1.0);
    std::printf("  C6b formation=0       : %zu sub-steps, Y_A = %.17g, T = %.17g  (T0 = %.1f)\n",
                r2.subSteps, r2.state.composition.massFraction(0, 0), r2.state.temperature[0], kT0);
  }

  // ------------------------------------------------------------------------------------------ C7
  head("C7 DRY-RUN -- determinism on the baseline");
  {
    Problem p = makeProblem(false);
    Real first = 0.0, firstT = 0.0;
    bool same = true;
    for (int k = 0; k < 3; ++k) {
      const auto r = integrateChemistry(p.mechanism, p.mixture, p.formation, p.kinetics,
                                        start(p.registry), 5.0);
      if (k == 0) {
        first = r.state.composition.massFraction(0, 0);
        firstT = r.state.temperature[0];
      } else if (r.state.composition.massFraction(0, 0) != first ||
                 r.state.temperature[0] != firstT) {
        same = false;
      }
    }
    std::printf("  3 identical calls bitwise identical: %s   (Y_A = %.17g)\n",
                same ? "YES" : "NO", first);
  }

  // ------------------------------------------------------------------------------------------ C9
  head("C9 DRY-RUN -- can the BASELINE accept a transport-closure composition at all?");
  {
    Problem p = makeProblem(false);
    const Real dIn = 5.718197704229766e-08;  // TD-001's measured transport closure
    std::printf("  input deviation to carry: %.9e  (4*eps*2 = %.6e)\n", dIn, tol2);
    try {
      MassFractionState bad(p.registry, 1, {1.0 - dIn, dIn + dIn});  // sums to 1 + dIn
      std::printf("  MassFractionState ACCEPTED it -- unexpected\n");
      static_cast<void>(bad);
    } catch (const std::exception& e) {
      std::printf("  MassFractionState REFUSED it, as expected:\n    %s\n", e.what());
      std::printf("  => there is NO baseline path that accepts a transported composition, so C9\n"
                  "     measures something that does not exist yet. A raw-fields entry point is\n"
                  "     REQUIRED, not optional, for scope (b).\n");
    }
  }

  std::cout << "\n(dry-run complete -- measurements only, no production code touched)\n";
  return 0;
}
