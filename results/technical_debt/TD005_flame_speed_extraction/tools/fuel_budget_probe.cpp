// TD-005 C3 / defect 2 -- WHICH TERM OF THE DISCRETE FUEL BUDGET FAILS TO CLOSE?
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// WHERE THIS STANDS. logs/19 showed that at dt/4 (cell diffusion number D = 0.833) the march
// CONVERGES -- four march lengths over a 1.67x range agree to 2 % -- and leaves behind a steady
// fuel-conservation residual of 8.48e-02, which fails C3 (< 0.05) by 1.7x. The limit cycle is
// suppressed; this residual is a separate defect and is what C3 now rests on.
//
// WHAT THE REPORTED RESIDUAL ACTUALLY IS. Read from PremixedFlame.cpp:1630-1653 rather than assumed:
//
//     fuelInflux   = gamma_inlet * (Y_F[0] - Y_F,u) / halfCell        [kg/(m^2 s)]
//     convected    = mdot * (Y_F[last] - Y_F,u)
//     fuelIntegral = integral omega_F dx
//     fuelThrough  = mdot * Y_F,u                                      (the normalisation)
//
//     fuelBalanceResidual = |convected - fuelIntegral - fuelInflux| / fuelThrough
//
// So it is a THREE-TERM budget: convection, reaction, and the INLET diffusive flux. Integrating
//
//     mdot dY_F/dx = d/dx(rho D dY_F/dx) + omega_F
//
// over the domain gives FOUR terms, so the reported balance OMITS two things, and both are
// measurable here:
//
//   THE OUTLET DIFFUSIVE FLUX. The budget includes the inlet face and not the outlet one. If the
//     outlet condition is zero-gradient this is correct and the term is zero; the one-sided
//     gradient from the last two cells says whether it actually is.
//
//   THE ACCUMULATION TERM d/dt(integral rho Y_F dx). The balance is a STEADY identity. A state
//     that is converged to 2 % in the balance may still be drifting slowly, and that drift appears
//     as an apparent flux imbalance. It cannot be measured from one snapshot -- but it CAN be
//     measured from TWO, at different march lengths, because the inventory difference divided by
//     the elapsed physical time IS the accumulation rate. That is why this probe runs two solves.
//
// THE TWO OUTCOMES
//   ACCUMULATION EXPLAINS IT   the converged state is still drifting and the balance is correctly
//                              reporting unsteadiness. Then C3's threshold is being applied to a
//                              quantity that includes a transient, and the defect is the stopping
//                              criterion, not the discretisation.
//   IT DOES NOT                the discrepancy is a genuine flux imbalance in the discretisation,
//                              and the term-by-term table says which term carries it.
//
// NO PREDICTION IS REGISTERED for which term. The arithmetic below is a budget, not a hypothesis,
// and this item has already had four readings wrong. What IS registered: the probe must reproduce
// the solver's own reported residual from the reported fields. If it cannot, the probe is wrong
// about the definition and nothing else it prints can be trusted -- so that is checked first and
// the probe says so loudly if it fails.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/PremixedFlame.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using namespace cfd::species;

namespace {

constexpr Real kWeight = 0.029;
constexpr Real kSpecificHeat = 1200.0;
constexpr Real kFuelFraction = 0.05;
constexpr Real kUnburntT = 300.0;
constexpr Real kPressure = 101325.0;
constexpr Real kConductivity = 0.05;
constexpr Real kActivation = 1.2e5;
constexpr Real kPreExponential = 3.0e7;
constexpr Real kMolarFormationP = -1.044e6;
constexpr Real kDomain = 0.010;
constexpr Real kAnchorFraction = 0.35;
constexpr Index kCells = 500;
constexpr Real kDtQuarter = 9.4243e-06;
constexpr Real kLewis = 1.0;

SpeciesThermodynamics makeSpecies(const std::string& name) {
  return SpeciesThermodynamics(
      kWeight, cfd::thermal::SensibleEnthalpy(
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

Problem makeProblem() {
  SpeciesRegistry registry({"F", "P"});
  std::vector<SpeciesThermodynamics> thermo;
  thermo.push_back(makeSpecies("cp_F"));
  thermo.push_back(makeSpecies("cp_P"));
  MixtureThermodynamics mixture(registry, std::move(thermo));
  FormationEnthalpies formation = FormationEnthalpies::fromMolar(mixture, {0.0, kMolarFormationP});
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("F->P", registry, {{"F", 1.0}}, {{"P", 1.0}}));
  ReactionMechanism mechanism(registry, std::move(reactions), mixture);
  std::vector<ReactionRateParameters> parameters;
  parameters.push_back(ReactionRateParameters{
      "F->P", ArrheniusParameters(kPreExponential, 0.0, kActivation), std::nullopt});
  ReactionKinetics kinetics(mechanism, std::move(parameters));
  return Problem{registry, std::move(mixture), std::move(formation), std::move(mechanism),
                 std::move(kinetics)};
}

struct Budget {
  Real relaxT{};
  Index marchSteps{};
  Real massFlux{};
  Real yInlet{};     // Y_F in cell 0
  Real yOutlet{};    // Y_F in cell nc-1
  Real yOutletPrev{} ;
  Real convected{};
  Real reaction{};
  Real inletDiffusion{};
  Real outletDiffusion{};  // OMITTED by the solver's balance
  Real fuelThrough{};
  Real residualRecomputed{};
  Real residualReported{};
  Real inventory{};  // integral rho Y_F dx  [kg/m^2]
  Real consumptionSpeed{};
  Real flameSpeed{};
  Real fuelBalance{};
};

Budget measure(const PremixedFlameChemistry& chem, const PremixedFlameConditions& cond, Real relaxT) {
  PremixedFlameSettings s;
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = 1.0e-3;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.driftWindow = 32;
  s.timeStep = kDtQuarter;
  s.finalMarchRelaxationTimes = relaxT;
  s.maximumStepsPerTrial = 80000;

  const PremixedFlameResult f = solvePremixedFlame(chem, cond, s);
  const Index nc = static_cast<Index>(f.temperature.size());
  const Index last = nc - 1;
  const Real dx = kDomain / static_cast<Real>(kCells);
  const Real halfCell = 0.5 * dx;
  const std::vector<Real>& yF = f.massFractions[0];

  Budget b;
  b.relaxT = relaxT;
  b.marchSteps = f.finalMarchSteps;
  b.massFlux = f.massFlux;
  b.yInlet = yF[0];
  b.yOutlet = yF[static_cast<std::size_t>(last)];
  b.yOutletPrev = yF[static_cast<std::size_t>(last - 1)];
  b.consumptionSpeed = f.consumptionSpeed;
  b.flameSpeed = f.flameSpeed;
  b.fuelBalance = f.fuelBalanceResidual;

  // gamma = lambda / (cp * Le), with the inlet temperature, exactly as the solver does it
  const Real gammaInlet = kConductivity / (kSpecificHeat * kLewis);

  b.fuelThrough = f.massFlux * kFuelFraction;
  b.convected = f.massFlux * (b.yOutlet - kFuelFraction);
  // fuelIntegral is recovered EXACTLY from the reported consumption speed, by its definition
  // S_c = -fuelIntegral / (rho_u * Y_F,u); no approximation is involved.
  b.reaction = -f.consumptionSpeed * f.unburntDensity * kFuelFraction;
  b.inletDiffusion = gammaInlet * (b.yInlet - kFuelFraction) / halfCell;
  // the term the solver's balance OMITS: the outlet face, from the one-sided gradient
  b.outletDiffusion = gammaInlet * (b.yOutlet - b.yOutletPrev) / dx;

  b.residualRecomputed =
      std::abs(b.convected - b.reaction - b.inletDiffusion) / b.fuelThrough;
  b.residualReported = f.fuelBalanceResidual;

  // the fuel inventory, for the accumulation term
  Real inv = 0.0;
  for (Index c = 0; c < nc; ++c) {
    inv += f.density[static_cast<std::size_t>(c)] * yF[static_cast<std::size_t>(c)] * dx;
  }
  b.inventory = inv;
  return b;
}

void report(const Budget& b) {
  std::printf("\n--- relaxT %.0f, %lld march steps\n", b.relaxT,
              static_cast<long long>(b.marchSteps));
  std::printf("    S_L %.5f cm/s   S_c %.5f cm/s   mismatch %.4f   reported balance %.4e\n",
              b.flameSpeed * 100.0, b.consumptionSpeed * 100.0,
              std::abs(b.consumptionSpeed - b.flameSpeed) / b.flameSpeed, b.fuelBalance);
  std::printf("    Y_F[0] %.9e   Y_F[last] %.4e   inventory %.6e kg/m2\n", b.yInlet, b.yOutlet,
              b.inventory);
  std::printf("\n    TERM                      kg/(m^2 s)     as %% of fuelThrough\n");
  std::printf("    fuelThrough (normaliser) %14.6e   %10.3f %%\n", b.fuelThrough, 100.0);
  std::printf("    convection               %14.6e   %10.3f %%\n", b.convected,
              b.convected / b.fuelThrough * 100.0);
  std::printf("    reaction                 %14.6e   %10.3f %%\n", b.reaction,
              b.reaction / b.fuelThrough * 100.0);
  std::printf("    inlet diffusion          %14.6e   %10.3f %%\n", b.inletDiffusion,
              b.inletDiffusion / b.fuelThrough * 100.0);
  std::printf("    ---- in the solver's balance above this line ----\n");
  std::printf("    outlet diffusion OMITTED %14.6e   %10.3f %%\n", b.outletDiffusion,
              b.outletDiffusion / b.fuelThrough * 100.0);
  const Real gap = b.convected - b.reaction - b.inletDiffusion;
  std::printf("\n    gap = convection - reaction - inletDiff = %14.6e  (%.3f %%)\n", gap,
              gap / b.fuelThrough * 100.0);
  std::printf("    recomputed residual %.6e   reported %.6e   agree: %s\n", b.residualRecomputed,
              b.residualReported,
              (std::abs(b.residualRecomputed - b.residualReported) < 1.0e-6) ? "YES" : "*** NO ***");
  std::fflush(stdout);
}

}  // namespace

int main() {
  const Problem problem = makeProblem();
  const cfd::physics::ConstantProperty conductivity(kConductivity);
  const PremixedFlameChemistry chemistry{&problem.mechanism, &problem.mixture, &problem.formation,
                                         &problem.kinetics};
  PremixedFlameConditions conditions;
  conditions.pressure = kPressure;
  conditions.unburntTemperature = kUnburntT;
  conditions.unburntComposition = {kFuelFraction, 1.0 - kFuelFraction};
  conditions.conductivity = &conductivity;
  conditions.specificHeat = kSpecificHeat;
  conditions.lewisNumber = kLewis;
  conditions.fuelIndex = 0;

  std::printf("TD-005 -- TERM-BY-TERM FUEL BUDGET on the CONVERGED dt/4 state\n\n");
  std::printf("  500 cells, dt = %.4e s (D = 0.833). logs/19: the march converges by relaxT 18\n",
              kDtQuarter);
  std::printf("  and the steady residual is 8.48e-02, failing C3 (< 0.05) by 1.7x.\n");
  std::printf("  The solver's balance (PremixedFlame.cpp:1650) is a THREE-term budget:\n");
  std::printf("    |convection - reaction - inletDiffusion| / fuelThrough\n");
  std::printf("  so it omits the OUTLET diffusive flux and the ACCUMULATION term. Both are\n");
  std::printf("  measured here -- accumulation from the inventory difference between two marches.\n");

  const Budget b1 = measure(chemistry, conditions, 22.0);
  report(b1);
  const Budget b2 = measure(chemistry, conditions, 26.0);
  report(b2);

  // ---------------------------------------------------------------- the accumulation term
  std::printf("\n=== THE ACCUMULATION TERM, from the two snapshots\n");
  const Real dSteps = static_cast<Real>(b2.marchSteps) - static_cast<Real>(b1.marchSteps);
  const Real dTime = dSteps * kDtQuarter;
  const Real dInv = b2.inventory - b1.inventory;
  const Real accumulation = (dTime != 0.0) ? dInv / dTime : 0.0;
  std::printf("    inventory  %.9e -> %.9e kg/m2      change %+.6e\n", b1.inventory,
              b2.inventory, dInv);
  std::printf("    elapsed    %.0f steps = %.6e s\n", dSteps, dTime);
  std::printf("    d/dt(inventory) = %+.6e kg/(m^2 s)   = %+.4f %% of fuelThrough\n",
              accumulation, accumulation / b1.fuelThrough * 100.0);

  const Real gap = b1.convected - b1.reaction - b1.inletDiffusion;
  std::printf("\n    the gap to explain          %+.6e  (%.3f %%)\n", gap,
              gap / b1.fuelThrough * 100.0);
  std::printf("    accumulation                %+.6e  (%.3f %%)\n", accumulation,
              accumulation / b1.fuelThrough * 100.0);
  std::printf("    outlet diffusion (omitted)  %+.6e  (%.3f %%)\n", b1.outletDiffusion,
              b1.outletDiffusion / b1.fuelThrough * 100.0);
  const Real unexplained = gap - accumulation - b1.outletDiffusion;
  std::printf("    UNEXPLAINED REMAINDER       %+.6e  (%.3f %%)\n", unexplained,
              unexplained / b1.fuelThrough * 100.0);

  std::printf("\n=== READING\n");
  if (std::abs(accumulation) > 0.5 * std::abs(gap)) {
    std::printf("    ACCUMULATION explains most of the gap. The converged state is still DRIFTING,\n");
    std::printf("    and the balance -- a STEADY identity -- is correctly reporting unsteadiness.\n");
    std::printf("    C3's threshold is then being applied to a quantity containing a transient,\n");
    std::printf("    and the defect is the stopping criterion rather than the discretisation.\n");
  } else if (std::abs(b1.outletDiffusion) > 0.5 * std::abs(gap)) {
    std::printf("    THE OMITTED OUTLET DIFFUSIVE FLUX explains most of the gap. The balance is\n");
    std::printf("    missing a term it should contain, and the discretisation may be conservative\n");
    std::printf("    after all -- the INSTRUMENT is incomplete, not the solver.\n");
  } else {
    std::printf("    NEITHER omitted term explains the gap: the remainder is %.3f %% of\n",
                unexplained / b1.fuelThrough * 100.0);
    std::printf("    fuelThrough. That is a genuine flux imbalance among convection, reaction and\n");
    std::printf("    the inlet flux, and the table above says which of them carries it.\n");
  }
  return 0;
}
