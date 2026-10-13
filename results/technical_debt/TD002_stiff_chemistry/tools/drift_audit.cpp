// TD-002 AUDIT PROBE -- where does the sum(Y) = 1 budget actually bind, and does sub-stepping
// aggravate it?
//
// INVESTIGATION ONLY. This file changes no production code and asserts nothing; it measures.
//
// The register records "sum Y = 1 error grows linearly at ~0.17 eps/step and crosses 4*eps*N at
// ~72 steps for 3 species", from results/p18/heat_release/logs/04_drift_probe.log. But P18.6's
// own update comment says the additive mass-fraction form was chosen precisely so that
// "the sum-to-one error does not accumulate", and the in-flight ChemistryIntegrator marches 284
// sub-steps through MassFractionState -- which validates 4*eps*N on EVERY sub-step -- without
// throwing. Both cannot be true of the same quantity. This probe decides which.
//
// Four measurements:
//   M1  the 2-species P18.7 problem through the real integrateChemistry: drift vs sub-steps
//   M2  the SAME physical time reached with different sub-step counts: does subdividing aggravate?
//   M3  a 3-species mixture with an INERT CARRIER at O(1) -- the shape TD-001's production case
//       actually uses (A, B, N2) and the shape the drift probe modelled
//   M4  the arithmetic in isolation: for an increment pair that cancels EXACTLY, how much does
//       sum(Y) move per update, and does carrying the lost low-order bits remove it?
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
constexpr Real kSpecificHeat = 1000.0;
constexpr Real kFormationB = -2.0e6;
constexpr Real kPreExponential = 5.0;
constexpr Real kActivationEnergy = 30000.0;
constexpr Real kDensity = 1.0;
constexpr Real kT0 = 1000.0;

SpeciesThermodynamics makeSpecies(Real weight, const std::string& name) {
  return SpeciesThermodynamics(
      weight, cfd::thermal::SensibleEnthalpy(
                  cfd::thermal::TemperatureDependentProperty::constant(kSpecificHeat, name),
                  298.15, 0.0));
}

struct Problem {
  SpeciesRegistry registry;
  MixtureThermodynamics mixture;
  FormationEnthalpies formation;
  ReactionMechanism mechanism;
  ReactionKinetics kinetics;
};

// `inert` adds a third, non-reacting species N2 carrying `inertFraction` of the mass.
Problem makeProblem(bool inert) {
  std::vector<std::string> names{"A", "B"};
  if (inert) names.push_back("N2");
  SpeciesRegistry registry(names);

  std::vector<SpeciesThermodynamics> thermo;
  thermo.push_back(makeSpecies(kWeight, "cp_A"));
  thermo.push_back(makeSpecies(kWeight, "cp_B"));
  if (inert) thermo.push_back(makeSpecies(kWeight, "cp_N2"));
  MixtureThermodynamics mixture(registry, std::move(thermo));

  std::vector<Real> molar{0.0, kFormationB * kWeight};
  if (inert) molar.push_back(0.0);
  FormationEnthalpies formation = FormationEnthalpies::fromMolar(mixture, molar);

  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("A->B", registry, {{"A", 1.0}}, {{"B", 1.0}}));
  ReactionMechanism mechanism(registry, std::move(reactions), mixture);

  std::vector<ReactionRateParameters> parameters;
  parameters.push_back(ReactionRateParameters{
      "A->B", ArrheniusParameters(kPreExponential, 0.0, kActivationEnergy), std::nullopt});
  ReactionKinetics kinetics(mechanism, std::move(parameters));

  return Problem{registry, std::move(mixture), std::move(formation), std::move(mechanism),
                 std::move(kinetics)};
}

// 0.75 / 0.25 and 0.5 are all exactly representable, so the INITIAL state carries no round-off of
// its own and every eps measured below is produced by the march.
ReactorState initialState(const SpeciesRegistry& registry, bool inert) {
  std::vector<Real> Y = inert ? std::vector<Real>{0.25, 0.0, 0.75} : std::vector<Real>{1.0, 0.0};
  return ReactorState{MassFractionState(registry, 1, Y), cfd::fields::ScalarField(1, kT0),
                      cfd::fields::ScalarField(1, kDensity)};
}

Real sumDeviation(const ReactorState& s) {
  Real sum = 0.0;
  for (Index i = 0; i < s.composition.speciesCount(); ++i) sum += s.composition.massFraction(i, 0);
  return sum - 1.0;
}

void header(const std::string& title) {
  std::cout << "\n" << std::string(92, '=') << "\n" << title << "\n"
            << std::string(92, '=') << "\n";
}

}  // namespace

int main() {
  std::cout.precision(17);
  std::cout << "eps = " << kEps << "\n"
            << "4*eps*2 (2 species) = " << massFractionSumTolerance(2) << "  (= 8 eps)\n"
            << "4*eps*3 (3 species) = " << massFractionSumTolerance(3) << "  (= 12 eps)\n";

  // ----------------------------------------------------------------------------------------- M1
  header("M1 -- the 2-species P18.7 problem through the real integrateChemistry");
  std::cout << "  Each sub-step constructs a ReactorState, so MassFractionState validates\n"
               "  4*eps*N on every one. Reaching N sub-steps without throwing IS the evidence\n"
               "  that the drift stayed inside the invariant for all N.\n\n";
  std::cout << "     dt       sub-steps   sum(Y)-1              in eps    Y_A\n";
  {
    Problem p = makeProblem(false);
    for (Real dt : {1.0, 5.0, 20.0, 100.0, 500.0}) {
      try {
        const auto r = integrateChemistry(p.mechanism, p.mixture, p.formation, p.kinetics,
                                          initialState(p.registry, false), dt);
        const Real d = sumDeviation(r.state);
        std::printf("  %7.1f   %9zu   %+.17e  %8.2f   %.17g\n", dt, r.subSteps, d, d / kEps,
                    r.state.composition.massFraction(0, 0));
      } catch (const std::exception& e) {
        std::printf("  %7.1f   THREW: %s\n", dt, e.what());
      }
    }
  }

  // ----------------------------------------------------------------------------------------- M2
  header("M2 -- the SAME physical time at different sub-step counts: does subdividing aggravate?");
  std::cout << "  maximumFractionalChange is the knob. A tenth of the value means roughly ten\n"
               "  times the sub-steps for the same dt. If the drift is per-STEP, it must rise\n"
               "  with the sub-step count; if it does not accumulate, it must not.\n\n";
  std::cout << "    control    sub-steps   sum(Y)-1              in eps\n";
  {
    Problem p = makeProblem(false);
    for (Real control : {0.4, 0.2, 0.1, 0.05, 0.0125, 0.003125}) {
      ChemistryIntegratorSettings s;
      s.maximumFractionalChange = control;
      s.maximumSubSteps = 2000000;
      try {
        const auto r = integrateChemistry(p.mechanism, p.mixture, p.formation, p.kinetics,
                                          initialState(p.registry, false), 20.0, s);
        const Real d = sumDeviation(r.state);
        std::printf("  %9.6f  %9zu   %+.17e  %8.2f\n", control, r.subSteps, d, d / kEps);
      } catch (const std::exception& e) {
        std::printf("  %9.6f  THREW: %s\n", control, e.what());
      }
    }
  }

  // ----------------------------------------------------------------------------------------- M3
  header("M3 -- 3 species with an INERT CARRIER at O(1), the shape the production case uses");
  std::cout << "  A = 0.25, B = 0, N2 = 0.75. N2 never reacts, so its increment is exactly 0 and\n"
               "  its stored value should never move. This is the shape 04_drift_probe.log\n"
               "  modelled when it reported ~0.17 eps/step and a ~72-step budget.\n\n";
  std::cout << "     dt       sub-steps   sum(Y)-1              in eps    Y_A          Y_N2\n";
  {
    Problem p = makeProblem(true);
    for (Real dt : {1.0, 5.0, 20.0, 100.0, 500.0, 2000.0}) {
      try {
        const auto r = integrateChemistry(p.mechanism, p.mixture, p.formation, p.kinetics,
                                          initialState(p.registry, true), dt);
        const Real d = sumDeviation(r.state);
        std::printf("  %7.1f   %9zu   %+.17e  %8.2f   %-12.8g %.17g\n", dt, r.subSteps, d, d / kEps,
                    r.state.composition.massFraction(0, 0),
                    r.state.composition.massFraction(2, 0));
      } catch (const std::exception& e) {
        std::printf("  %7.1f   THREW: %s\n", dt, e.what());
      }
    }
  }

  // ----------------------------------------------------------------------------------------- M4
  header("M4 -- the arithmetic in isolation, no CFDApp code");
  std::cout << "  The question P18.6's comment answers for ONE step and leaves open for many:\n"
               "  when the increments cancel EXACTLY, does sum(Y) move at all?\n\n";
  {
    // An increment that is NOT representable at Y_A's exponent is the interesting case: adding it
    // to a number of order 1 rounds, while adding it to a small number does not.
    const Real delta = 1.0 / 3.0 * 1e-6;  // deliberately not dyadic
    std::cout << "  delta = " << delta << " (not representable as a short dyadic)\n\n";

    struct Variant {
      const char* name;
      bool compensated;
      int species;
    };
    const Variant variants[] = {
        {"2 species, plain", false, 2},
        {"2 species, compensated (Kahan per species)", true, 2},
        {"3 species + inert carrier, plain", false, 3},
        {"3 species + inert carrier, compensated", true, 3},
    };

    for (const Variant& v : variants) {
      std::vector<Real> Y = (v.species == 2) ? std::vector<Real>{1.0, 0.0}
                                             : std::vector<Real>{0.25, 0.0, 0.75};
      std::vector<Real> comp(Y.size(), 0.0);  // the carried low-order bits
      std::cout << "  " << v.name << "\n";
      std::cout << "        steps     sum(Y)-1              in eps\n";
      for (std::size_t step = 1; step <= 100000; ++step) {
        std::vector<Real> d(Y.size(), 0.0);
        d[0] = -delta;
        d[1] = +delta;  // sum of increments is EXACTLY zero
        for (std::size_t i = 0; i < Y.size(); ++i) {
          if (v.compensated) {
            // Kahan: the increment carries forward whatever the previous add could not represent.
            const Real y = d[i] - comp[i];
            const Real t = Y[i] + y;
            comp[i] = (t - Y[i]) - y;
            Y[i] = t;
          } else {
            Y[i] = Y[i] + d[i];
          }
        }
        if (step == 72 || step == 100 || step == 1000 || step == 10000 || step == 100000) {
          Real sum = 0.0;
          for (Real y : Y) sum += y;
          std::printf("     %8zu     %+.17e  %10.2f\n", step, sum - 1.0, (sum - 1.0) / kEps);
        }
      }
      std::cout << "\n";
    }
  }

  std::cout << "\n(probe complete -- measurements only, no production code touched)\n";
  return 0;
}
