// G8 closure, step 1 -- REACTION ORDERS INDEPENDENT OF STOICHIOMETRY, and the unit conversion of
// the frozen mechanism.
//
// WHY THIS EXISTS. G8's frozen mechanism is Westbrook & Dryer (1981) Table II Set 3 for methane:
//
//     CH4 + 2 O2 -> CO2 + 2 H2O      rate = A exp(-Ea/RT) [CH4]^0.2 [O2]^1.3
//
// The ORDERS (0.2, 1.3) are not the STOICHIOMETRY (1, 2). Before this change the progress rate read
// nu'_i as the order, so the mechanism was inexpressible; and setting the reactant coefficients to
// the orders instead would have destroyed sum_i M_i nu_i = 0, which is G3's and G6's own criterion.
// So the two are now stored separately.
//
// THE MOST IMPORTANT TEST IN THIS FILE is the unit round-trip in P18G8UnitConversionTest. The paper
// gives A in cm-sec-mole-kcal-Kelvins; the code works in m-s-mol-J-K. A's dimensions depend on the
// reaction orders, so the conversion is DERIVED, not looked up, and a wrong factor of 1e3 would
// move the flame speed by about 10^1.5 -- straight out of G8's 35-37 cm/s band. That would fail G8
// for an arithmetic slip in a unit conversion while looking like a solver defect.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::species::Reaction;
using cfd::species::ReactionDirection;
using cfd::species::ReactionTerm;
using cfd::species::SpeciesRegistry;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// The four species of the frozen G8 mechanism, in a fixed registry order.
SpeciesRegistry methaneAirRegistry() { return SpeciesRegistry({"CH4", "O2", "CO2", "H2O"}); }

// A constant-cp sensible enthalpy, the same shape the P18.4/P18.5 suites use.
cfd::species::SpeciesThermodynamics species(Real molecularWeight, Real specificHeat,
                                            const std::string& name) {
  return cfd::species::SpeciesThermodynamics(
      molecularWeight,
      cfd::thermal::SensibleEnthalpy(
          cfd::thermal::TemperatureDependentProperty::constant(specificHeat, name), 298.15, 0.0));
}

// CH4 + 2 O2 -> CO2 + 2 H2O
std::vector<ReactionTerm> wdReactants() { return {{"CH4", 1.0}, {"O2", 2.0}}; }
std::vector<ReactionTerm> wdProducts() { return {{"CO2", 1.0}, {"H2O", 2.0}}; }

// Westbrook & Dryer (1981) Table II Set 3: a = 0.2 on the fuel, b = 1.3 on the oxidiser.
std::vector<ReactionTerm> wdOrders() { return {{"CH4", 0.2}, {"O2", 1.3}}; }

}  // namespace

// =================================================================================================
// The default must be bitwise what every pre-existing caller already got
// =================================================================================================

TEST(P18G8ReactionOrderTest, OmittingOrdersLeavesThemEqualToTheReactantStoichiometry) {
  const SpeciesRegistry registry = methaneAirRegistry();
  const Reaction massAction("wd-default", registry, wdReactants(), wdProducts());

  EXPECT_FALSE(massAction.hasExplicitOrders());
  for (Index i = 0; i < registry.size(); ++i) {
    // BITWISE, not close: the progress rate reads reactantOrder() unconditionally now, so if these
    // differ by even one ulp every existing mechanism's rate changes.
    EXPECT_EQ(massAction.reactantOrder(i), massAction.reactantCoefficient(i))
        << "species index " << i;
  }
  EXPECT_EQ(massAction.reactantOrder(0), 1.0);
  EXPECT_EQ(massAction.reactantOrder(1), 2.0);
  EXPECT_EQ(massAction.reactantOrder(2), 0.0);
  EXPECT_EQ(massAction.reactantOrder(3), 0.0);
}

TEST(P18G8ReactionOrderTest, ExplicitOrdersAreStoredAndDoNotDisturbTheStoichiometry) {
  const SpeciesRegistry registry = methaneAirRegistry();
  const Reaction global("wd-set3", registry, wdReactants(), wdProducts(),
                        ReactionDirection::Irreversible, wdOrders());

  EXPECT_TRUE(global.hasExplicitOrders());
  EXPECT_EQ(global.reactantOrder(0), 0.2);
  EXPECT_EQ(global.reactantOrder(1), 1.3);

  // The stoichiometry is UNTOUCHED -- this is the whole point of separating them.
  EXPECT_EQ(global.reactantCoefficient(0), 1.0);
  EXPECT_EQ(global.reactantCoefficient(1), 2.0);
  EXPECT_EQ(global.netStoichiometry(0), -1.0);  // CH4 consumed
  EXPECT_EQ(global.netStoichiometry(1), -2.0);  // O2 consumed
  EXPECT_EQ(global.netStoichiometry(2), 1.0);   // CO2 produced
  EXPECT_EQ(global.netStoichiometry(3), 2.0);   // H2O produced
}

// =================================================================================================
// Mass conservation -- G3/G6's criterion -- must survive, and this is why orders are separate
// =================================================================================================

// =================================================================================================
// Mass conservation -- G3/G6's criterion -- and the molecular weights the mechanism REQUIRES
// =================================================================================================

TEST(P18G8MolecularWeightTest, TabulatedWeightsCannotCloseTheMassBalanceButElementDerivedOnesDo) {
  // FOUND BY THIS TEST FAILING, and kept as a permanent control rather than quietly fixed.
  //
  // G6 requires sum_i M_i nu_i = 0 to 4*eps*max|M_i nu_i| for every reaction. For
  // CH4 + 2 O2 -> CO2 + 2 H2O that is a statement about the WEIGHTS, not about the code: it holds
  // only if the four weights are mutually consistent. Rounded table values are not.
  const SpeciesRegistry registry = methaneAirRegistry();

  // (a) STANDARD TABULATED weights, each rounded independently to 5 decimal places in kg/mol.
  const Real tabulatedCH4 = 0.016042;
  const Real tabulatedO2 = 0.031998;
  const Real tabulatedCO2 = 0.044009;
  const Real tabulatedH2O = 0.018015;
  const Real tabulatedResidual =
      -tabulatedCH4 - 2.0 * tabulatedO2 + tabulatedCO2 + 2.0 * tabulatedH2O;

  // (b) ELEMENT-DERIVED weights, built from one atomic mass per element.
  const Real massC = 0.012011;  // kg/mol
  const Real massH = 0.001008;
  const Real massO = 0.015999;
  const Real derivedCH4 = massC + 4.0 * massH;
  const Real derivedO2 = 2.0 * massO;
  const Real derivedCO2 = massC + 2.0 * massO;
  const Real derivedH2O = 2.0 * massH + massO;
  const Real derivedResidual = -derivedCH4 - 2.0 * derivedO2 + derivedCO2 + 2.0 * derivedH2O;

  const Real scale = std::max({derivedCH4, 2.0 * derivedO2, derivedCO2, 2.0 * derivedH2O});
  const Real bound = cfd::species::massClosureTolerance(scale);

  std::cout << "    tabulated residual      : " << tabulatedResidual << "\n"
            << "    element-derived residual: " << derivedResidual << "\n"
            << "    G6 bound (4*eps*scale)  : " << bound << "\n";

  // The tabulated weights MISS by about 1e-6 kg/mol -- six orders of magnitude outside the bound.
  EXPECT_GT(std::abs(tabulatedResidual), bound)
      << "if independently rounded table weights closed the balance, this control would be "
         "vacuous and the element-derived requirement unjustified";

  // The element-derived weights close it by CONSTRUCTION: both sides are
  // m_C + 4 m_H + 4 m_O identically, so only floating-point summation round-off remains.
  EXPECT_LE(std::abs(derivedResidual), bound)
      << "element-derived residual " << derivedResidual << " against bound " << bound;
}

TEST(P18G8ReactionOrderTest, MassClosureIsUnaffectedByTheOrders) {
  const SpeciesRegistry registry = methaneAirRegistry();

  // Element-derived weights, for the reason the test above measures.
  const Real massC = 0.012011;
  const Real massH = 0.001008;
  const Real massO = 0.015999;
  std::vector<cfd::species::SpeciesThermodynamics> thermo;
  thermo.push_back(species(massC + 4.0 * massH, 2200.0, "cp_CH4"));
  thermo.push_back(species(2.0 * massO, 1000.0, "cp_O2"));
  thermo.push_back(species(massC + 2.0 * massO, 1100.0, "cp_CO2"));
  thermo.push_back(species(2.0 * massH + massO, 2000.0, "cp_H2O"));
  const cfd::species::MixtureThermodynamics mixture(registry, std::move(thermo));

  const Reaction global("wd-set3", registry, wdReactants(), wdProducts(),
                        ReactionDirection::Irreversible, wdOrders());
  const Reaction massAction("wd-default", registry, wdReactants(), wdProducts());

  // Identical stoichiometry => identical mass residual, BITWISE. The orders cannot reach it, which
  // is the whole reason they are stored separately.
  EXPECT_EQ(global.massResidual(mixture), massAction.massResidual(mixture));
  EXPECT_LE(std::abs(global.massResidual(mixture)),
            cfd::species::massClosureTolerance(global.massScale(mixture)))
      << "residual " << global.massResidual(mixture) << " against scale "
      << global.massScale(mixture);

  // THE COUNTERFACTUAL that justifies the design: had the orders been written into the
  // coefficients, CH4 + 2 O2 would have become 0.2 CH4 + 1.3 O2 and the balance would be destroyed.
  // Measured, not asserted in a comment.
  const Reaction ordersAsCoefficients("orders-as-coefficients", registry, wdOrders(), wdProducts());
  EXPECT_GT(std::abs(ordersAsCoefficients.massResidual(mixture)),
            cfd::species::massClosureTolerance(ordersAsCoefficients.massScale(mixture)))
      << "writing the orders into the coefficients must BREAK the closure, or separating them "
         "would be pointless; residual "
      << ordersAsCoefficients.massResidual(mixture);
}

// =================================================================================================
// Refusals -- each one a defect the API must not let through
// =================================================================================================

TEST(P18G8ReactionOrderTest,
     ANegativeOrderIsRefusedBecauseTheRateWouldDivergeAndClippingIsForbidden) {
  const SpeciesRegistry registry = methaneAirRegistry();
  // Westbrook & Dryer's own best-fit methane set, Table I / Table II Set 2, has a = -0.3. They
  // write that it makes "the rate of methane consumption increase without limit as the methane
  // concentration approaches zero", with a concentration floor as one remedy. This phase forbids
  // clipping, so the API refuses the mechanism rather than silently clipping later.
  EXPECT_THROW(
      static_cast<void>(Reaction("wd-set2", registry, wdReactants(), wdProducts(),
                                 ReactionDirection::Irreversible, {{"CH4", -0.3}, {"O2", 1.3}})),
      cfd::InvalidArgumentError);
}

TEST(P18G8ReactionOrderTest, NonFiniteOrdersAreRefused) {
  const SpeciesRegistry registry = methaneAirRegistry();
  for (const Real bad :
       {std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity(),
        -std::numeric_limits<Real>::infinity()}) {
    EXPECT_THROW(
        static_cast<void>(Reaction("bad-order", registry, wdReactants(), wdProducts(),
                                   ReactionDirection::Irreversible, {{"CH4", bad}, {"O2", 1.3}})),
        cfd::InvalidArgumentError);
  }
}

TEST(P18G8ReactionOrderTest, AnOrderOnANonReactantIsRefused) {
  const SpeciesRegistry registry = methaneAirRegistry();
  // CO2 is a product, not a reactant. An order on it is a third-body or inhibitor dependence, which
  // this class does not model -- so it is refused rather than silently applied to the forward rate.
  EXPECT_THROW(static_cast<void>(Reaction("order-on-product", registry, wdReactants(), wdProducts(),
                                          ReactionDirection::Irreversible,
                                          {{"CH4", 0.2}, {"O2", 1.3}, {"CO2", 0.5}})),
               cfd::InvalidArgumentError);
}

TEST(P18G8ReactionOrderTest, ExplicitOrdersOnAReversibleReactionAreRefusedNotHalfApplied) {
  const SpeciesRegistry registry = methaneAirRegistry();
  // The REVERSE order is not modelled. Accepting this would silently use product stoichiometry on
  // the reverse side while the forward side used 0.2 and 1.3 -- a half-applied feature.
  EXPECT_THROW(static_cast<void>(Reaction("reversible-with-orders", registry, wdReactants(),
                                          wdProducts(), ReactionDirection::Reversible, wdOrders())),
               cfd::InvalidArgumentError);
  // and the same reaction without orders is accepted, so the refusal is about the ORDERS and not
  // about reversibility
  static_cast<void>(Reaction("reversible-plain", registry, wdReactants(), wdProducts(),
                             ReactionDirection::Reversible));
}

TEST(P18G8ReactionOrderTest, AZeroOrderIsAcceptedAndMeansTheSpeciesDropsOutOfTheRate) {
  const SpeciesRegistry registry = methaneAirRegistry();
  // A zero order is legitimate: the rate is independent of that species' concentration. It must not
  // be confused with "not supplied".
  const Reaction zeroOrder("zero-order-o2", registry, wdReactants(), wdProducts(),
                           ReactionDirection::Irreversible, {{"CH4", 1.0}, {"O2", 0.0}});
  EXPECT_TRUE(zeroOrder.hasExplicitOrders());
  EXPECT_EQ(zeroOrder.reactantOrder(1), 0.0);
  EXPECT_EQ(zeroOrder.reactantCoefficient(1), 2.0) << "the stoichiometry still consumes 2 O2";
}

// =================================================================================================
// THE UNIT CONVERSION of the frozen mechanism -- the one slip that could invalidate G8 silently
// =================================================================================================

TEST(P18G8UnitConversionTest,
     TheFrozenMechanismsPreExponentialConvertsFromCgsToSiExactlyAsDerived) {
  // Westbrook & Dryer (1981), Table II Set 3, Table I caption: units cm-sec-mole-kcal-Kelvins.
  const Real aCgs = 6.7e12;         // (mole/cm^3)^(-0.5) / s
  const Real eaKcalPerMole = 48.4;  // kcal/mol
  const Real orderFuel = 0.2;
  const Real orderOxidiser = 1.3;

  // The derivation, recomputed here rather than quoted:
  //   w_SI = 1e6 * w_cgs,  [X]_cgs = 1e-6 [X]_SI
  //   w_SI = 1e6 * A_cgs * (1e-6)^(a+b) * [CH4]^a [O2]^b
  //   A_SI = A_cgs * 1e6 * 1e(-6*(a+b))
  const Real orderSum = orderFuel + orderOxidiser;
  const Real aSiDerived = aCgs * std::pow(10.0, 6.0 - 6.0 * orderSum);

  EXPECT_DOUBLE_EQ(orderSum, 1.5);
  EXPECT_NEAR(aSiDerived, 6.7e9, 1.0e-3 * 6.7e9)
      << "A_SI must be 6.7e9 (mol/m^3)^-0.5/s; got " << aSiDerived;

  // Ea: 1 kcal = 4184 J exactly (thermochemical calorie).
  const Real eaSi = eaKcalPerMole * 4184.0;
  EXPECT_DOUBLE_EQ(eaSi, 202505.6);

  // THE ROUND TRIP. Pick a physical state, evaluate the rate in both unit systems, and require
  // w_SI == 1e6 * w_cgs to round-off. This is what fails if the 1e-3 factor or either exponent is
  // wrong -- a mistake a hand-check of the algebra can easily miss.
  const Real temperature = 1800.0;
  const Real concentrationFuelSi = 2.5;       // mol/m^3
  const Real concentrationOxidiserSi = 12.0;  // mol/m^3

  const Real gasConstantSi = 8.31446261815324;         // J/(mol K)
  const Real gasConstantCgs = gasConstantSi / 4184.0;  // kcal/(mol K)

  const Real rateSi = aSiDerived * std::exp(-eaSi / (gasConstantSi * temperature)) *
                      std::pow(concentrationFuelSi, orderFuel) *
                      std::pow(concentrationOxidiserSi, orderOxidiser);

  const Real concentrationFuelCgs = concentrationFuelSi * 1.0e-6;
  const Real concentrationOxidiserCgs = concentrationOxidiserSi * 1.0e-6;
  const Real rateCgs = aCgs * std::exp(-eaKcalPerMole / (gasConstantCgs * temperature)) *
                       std::pow(concentrationFuelCgs, orderFuel) *
                       std::pow(concentrationOxidiserCgs, orderOxidiser);

  ASSERT_GT(rateSi, 0.0);
  const Real relative = std::abs(rateSi - 1.0e6 * rateCgs) / (1.0e6 * rateCgs);
  EXPECT_LT(relative, 1.0e-12) << "SI rate " << rateSi << " against 1e6 x cgs rate "
                               << (1.0e6 * rateCgs) << ", relative difference " << relative;

  // NON-VACUITY: the round trip must be able to FAIL. A deliberately wrong conversion factor must
  // not pass, or the check above proves nothing.
  const Real aSiWrong = aCgs * 1.0e-6;  // the plausible slip: forgetting the 1e6 on the rate
  const Real rateWrong = aSiWrong * std::exp(-eaSi / (gasConstantSi * temperature)) *
                         std::pow(concentrationFuelSi, orderFuel) *
                         std::pow(concentrationOxidiserSi, orderOxidiser);
  const Real relativeWrong = std::abs(rateWrong - 1.0e6 * rateCgs) / (1.0e6 * rateCgs);
  EXPECT_GT(relativeWrong, 0.99)
      << "a 1e-6 instead of 1e-3 conversion must be detected by this round trip";
}

TEST(P18G8UnitConversionTest, TheFrozenMechanismIsExpressibleAndItsOrdersReachTheRate) {
  const SpeciesRegistry registry = methaneAirRegistry();
  const Reaction global("wd-set3", registry, wdReactants(), wdProducts(),
                        ReactionDirection::Irreversible, wdOrders());

  // The orders the progress rate will read are the PAPER's, not the stoichiometry's. If this ever
  // regresses to 1.0 and 2.0 the mechanism silently becomes mass-action and the flame speed moves.
  EXPECT_EQ(global.reactantOrder(registry.index("CH4")), 0.2);
  EXPECT_EQ(global.reactantOrder(registry.index("O2")), 1.3);
  EXPECT_NE(global.reactantOrder(registry.index("CH4")),
            global.reactantCoefficient(registry.index("CH4")));
  EXPECT_NE(global.reactantOrder(registry.index("O2")),
            global.reactantCoefficient(registry.index("O2")));

  // And the frozen Arrhenius parameters are accepted by P18.5's own validator.
  const cfd::species::ArrheniusParameters parameters(6.7e9, 0.0, 202505.6);
  EXPECT_EQ(parameters.preExponential(), 6.7e9);
  EXPECT_EQ(parameters.temperatureExponent(), 0.0);
  EXPECT_EQ(parameters.activationEnergy(), 202505.6);
  // k(T) must be finite and positive at flame temperatures, or the mechanism cannot be marched.
  for (const Real temperature : {300.0, 1000.0, 1800.0, 2200.0}) {
    const Real k = parameters.rateCoefficient(temperature);
    EXPECT_TRUE(std::isfinite(k)) << "k(" << temperature << ") is not finite";
    EXPECT_GT(k, 0.0) << "k(" << temperature << ") = " << k;
  }
  // and k must RISE steeply with temperature -- an Arrhenius rate that did not would not sustain a
  // flame front
  EXPECT_GT(parameters.rateCoefficient(2200.0), parameters.rateCoefficient(300.0));
  static_cast<void>(kEps);
}
