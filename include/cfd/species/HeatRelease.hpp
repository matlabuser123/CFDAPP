#pragma once

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"

// P18.6 -- heat release and the chemistry/energy coupling.
//
// THE CONVENTION BELOW WAS FROZEN BEFORE THIS FILE WAS WRITTEN, at
// results/p18/heat_release/energy_accounting.md. Nothing here was determined experimentally.
//
// ===================================================================
// WHAT CFDApp TRANSPORTS, which is the fact everything else follows from:
//
//     u_sens = rho * cp(T,Y) * T          [J/m^3]
//
// EnergyEquation.hpp:347 says so in those words, and its transient term carries both rho AND cp at
// two time levels. The scale's zero is at T = 0. It contains NO formation energy -- nothing in
// CFDApp did, before this file.
//
// SO AN EXPLICIT CHEMICAL SOURCE IS REQUIRED (authorization section 8, possibility B), and
// DOUBLE COUNTING IS STRUCTURALLY IMPOSSIBLE: the source adds a quantity the transported variable
// does not contain.
//
// ===================================================================
// THE CONSERVED TOTAL, and the source derived from it:
//
//     E = rho cp(T,Y) T  +  rho sum_i Y_i h_f,i
//         \_____________/    \_________________/
//          SENSIBLE            CHEMICAL
//
//     dE/dt = 0  in a closed adiabatic system  =>   S_E = - sum_i h_f,i omega_i      [W/m^3]
//
// and substituting P18.4's omega_i = sum_r M_i nu_{i,r} q_r gives the ALGEBRAICALLY IDENTICAL
//
//     S_E = - sum_r q_r Delta H_r^0,     Delta H_r^0 = sum_i nu_{i,r} M_i h_f,i      [J/mol]
//
// Both forms are implemented, because an implementation can still get one of them wrong. They
// differ only in summation order, so disagreement beyond round-off is a defect.
//
// SIGN, FROZEN: with P18.4's net nu_i = nu''_i - nu'_i,
//     Delta H_r^0 < 0  is EXOTHERMIC
//     q_r > 0          is forward progress
//     => S_E > 0, and the thermal equation's rhs[P] += Q_P V_P ADDS energy and raises T.
//
// ===================================================================
// WHY THE SOURCE USES FORMATION ENTHALPY ONLY, never the total h_i(T) = h_f,i + dh_i(T):
//
// the extra term -sum_i dh_i(T) omega_i is the SENSIBLE enthalpy of the species converted, and
// whether it belongs in the source depends on what is transported:
//     transported rho*integral(cp dT)   -> it IS needed
//     transported rho*cp*T              -> it is NOT needed, and adding it DOUBLE COUNTS
// CFDApp is the second. At 1500 K the spurious term is ~9% of the real one -- big enough to
// matter, small enough to look like discretization error. Controls N5 and N7 attack it.
//
// Delta H_r(T), the temperature-dependent THERMODYNAMIC reaction enthalpy, is also provided and
// Kirchhoff-verified, because it is a real quantity worth having. IT IS NOT THE SOURCE TERM.
// The names are deliberately different and the distinction is not an implementation detail.
//
// ===================================================================
// UNITS, one system throughout -- mol, never kmol:
//     h_f,i [J/kg]  omega_i [kg/(m^3 s)]  M_i [kg/mol]  q_r [mol/(m^3 s)]
//     Delta H_r [J/mol]   S_E [W/m^3]
//
// ===================================================================
// WHERE S_E GOES, and it is an EXISTING assembler, not a new one:
//
//     cfd::thermal::assembleThermalSourceContribution(mesh, const ScalarField& Q, rhs)
//     cfd::thermal::assembleEnergyEquation(..., const ScalarField& volumetricHeatSource, ...)
//
// present since P12-NUM-006. P18.6 produces the W/m^3 field and hands it over; it adds NO energy
// equation and NO assembler of its own.
//
// ===================================================================
// NOT IN P18.6: P17's compressible TotalEnergyState, which REFUSES a temperature-dependent cp and
// whose refusal is not lifted here (results/p18/heat_release/audit.md section 1). Heat release is
// coupled to the P15 THERMAL equation only. No reacting-shock claim is made.
namespace cfd::species {

// h_f,i -- the MASS-SPECIFIC standard enthalpy of formation, [J/kg], one per species, bound to the
// authoritative P18.1 registry.
//
// WHY THIS IS A SEPARATE TYPE AND NOT A FIELD ON P18.3's SpeciesThermodynamics:
//
//   (a) h_f IS NOT A SENSIBLE QUANTITY. P15's SensibleEnthalpy carries a `referenceEnthalpy`,
//       which is a sensible OFFSET -- it shifts one substance's zero point and cancels out of
//       every physical result. A heat of formation does not cancel; it IS the heat release.
//       Conflating them is a category error, and G7 makes it a NEGATIVE CONTROL BY NAME:
//       "h_f,i replaced by the SENSIBLE reference enthalpy must FAIL". Keeping h_f in a
//       differently-named type makes the confusion hard to write rather than merely detectable.
//   (b) P18.3's files stay BITWISE UNTOUCHED, so its preservation claim needs no re-derivation.
//
// Throws InvalidArgumentError for a count mismatch or a non-finite value. A formation enthalpy may
// be any sign and may be zero -- zero is correct for an element in its reference state, which is
// why no positivity check exists.
class FormationEnthalpies {
 public:
  FormationEnthalpies(SpeciesRegistry registry, std::vector<Real> massSpecific);

  // Built from MOLAR values [J/mol], the form thermochemical tables are published in, divided by
  // the molecular weights P18.3 already owns. No molecular weight is stored here.
  [[nodiscard]] static FormationEnthalpies fromMolar(const MixtureThermodynamics& mixture,
                                                     const std::vector<Real>& molar);

  [[nodiscard]] const SpeciesRegistry& registry() const noexcept { return registry_; }
  [[nodiscard]] Index speciesCount() const noexcept { return registry_.size(); }

  // h_f,i [J/kg].
  [[nodiscard]] Real massSpecific(Index speciesIndex) const;

  // hbar_f,i = M_i h_f,i [J/mol]. M_i is READ from P18.3, never stored here.
  [[nodiscard]] Real molar(const MixtureThermodynamics& mixture, Index speciesIndex) const;

 private:
  SpeciesRegistry registry_;
  std::vector<Real> massSpecific_;
};

// ===================================================================
// Reaction enthalpy
// ===================================================================

// Delta H_r^0 = sum_i nu_i M_i h_f,i   [J/mol].  THE QUANTITY THE SOURCE USES.
//
// Temperature-INDEPENDENT by construction, because it is built from formation enthalpies alone.
// Negative is exothermic.
//
// REFERENCE-STATE INVARIANCE, and it is EXACT: shifting every h_f,i by one common constant c
// [J/kg] changes this by c * sum_i M_i nu_i, which P18.4 enforces to be ZERO per reaction, on the
// definition. Shifting the MOLAR values by a common constant does NOT cancel -- sum_i nu_i is not
// zero, because moles are not conserved. Both are asserted.
[[nodiscard]] Real standardReactionEnthalpy(const Reaction& reaction,
                                            const MixtureThermodynamics& mixture,
                                            const FormationEnthalpies& formation);

// Delta H_r(T) = sum_i nu_i M_i [ h_f,i + h_i(T) ]   [J/mol], with h_i(T) P18.3's integrated
// SENSIBLE enthalpy (P15's, never h_i = cp_i T).
//
// THIS IS NOT THE SOURCE TERM. It is the thermodynamic reaction enthalpy at temperature T, and it
// is provided because it is a real quantity with a strong consistency test (Kirchhoff, below).
// Using it as the source would double-count the sensible enthalpy the thermal equation already
// evolves -- see the header note.
[[nodiscard]] Real reactionEnthalpy(const Reaction& reaction, const MixtureThermodynamics& mixture,
                                    const FormationEnthalpies& formation, Real temperature);

// Delta Cp_r(T) = sum_i nu_i M_i cp_i(T)   [J/(mol K)].
//
// KIRCHHOFF'S RELATION: d(Delta H_r)/dT = Delta Cp_r. Verified against a central finite difference
// of reactionEnthalpy() -- two independent routes to the same number, and a strong thermodynamic
// consistency check on the enthalpy integration itself.
[[nodiscard]] Real reactionHeatCapacity(const Reaction& reaction,
                                        const MixtureThermodynamics& mixture, Real temperature);

// ===================================================================
// The energy source
// ===================================================================

// S_E = - sum_r q_r Delta H_r^0   [W/m^3], one value per cell.  THE MOLAR ROUTE.
//
// Takes the progress rates as an ARGUMENT rather than recomputing them, so the q_r that produced
// P18.4's species sources is structurally the same q_r that produces this energy source
// (authorization section 30). Evaluating kinetics twice could yield two different numbers for one
// step, and no amount of care makes that safe if the API permits it.
//
// Throws, naming the reaction and the cell, for a non-finite rate, reaction enthalpy or result.
[[nodiscard]] cfd::fields::ScalarField reactionHeatRelease(const ReactionMechanism& mechanism,
                                                           const MixtureThermodynamics& mixture,
                                                           const FormationEnthalpies& formation,
                                                           const ReactionProgressRates& rates,
                                                           Index cellCount);

// S_E = - sum_i h_f,i omega_i   [W/m^3].  THE MASS ROUTE -- G7's own formula, verbatim.
//
// Algebraically identical to the molar route; implemented independently so the two can be
// compared. They share no arithmetic beyond h_f,i, and a disagreement beyond round-off means one
// of them is wrong. Consumes P18.4's SpeciesSources directly.
[[nodiscard]] cfd::fields::ScalarField reactionHeatReleaseFromSpeciesSources(
    const FormationEnthalpies& formation, const SpeciesSources& sources, Index cellCount);

// ===================================================================
// Energy accounting
// ===================================================================

// The conserved total energy density of one cell, split into the two pools it moves between.
// Reporting only `sensible` would be wrong: exothermic chemistry is SUPPOSED to raise it while
// `total` stays fixed.
struct EnergyDensity {
  Real sensible{};  // rho cp(T,Y) T          [J/m^3]
  Real chemical{};  // rho sum_i Y_i h_f,i    [J/m^3]
  Real total{};     // the conserved quantity [J/m^3]
};

[[nodiscard]] EnergyDensity energyDensity(const MixtureThermodynamics& mixture,
                                          const FormationEnthalpies& formation,
                                          const std::vector<Real>& composition, Real density,
                                          Real temperature);

// Volume-weighted sum over a field, which is the quantity the closed-reactor test conserves:
//     E = sum_cells V_c [ rho cp T + rho sum_i Y_i h_f,i ]        [J]
struct TotalEnergy {
  Real sensible{};
  Real chemical{};
  Real total{};
  Index cellCount{};
};

[[nodiscard]] TotalEnergy totalEnergy(const MixtureThermodynamics& mixture,
                                      const FormationEnthalpies& formation,
                                      const MassFractionState& state,
                                      const cfd::fields::ScalarField& density,
                                      const cfd::fields::ScalarField& temperature,
                                      const cfd::fields::ScalarField& cellVolume);

// T from the transported sensible energy density, at a GIVEN composition:
//     solve   rho cp(T,Y) T = u_sens   for T
//
// A ROOT FIND, not a division: cp depends on T, so u_sens is not linear in T. P15's
// SensibleEnthalpy::temperature() inverts the same shape for ONE substance by bisection; the
// bracket-and-bisect logic is reused in spirit, the integrand is not (it is sum_i Y_i cp_i here).
//
// THE COMPOSITION ARGUMENT MUST BE THE UPDATED ONE (authorization section 17). Recovering T from
// stale Y is control N4. This function cannot enforce that -- it is the caller's ordering -- so
// the coupled stepper below owns it and the control tests the stepper.
//
// Throws NumericalError if the root is not bracketed in (0, T_max] or does not converge, and
// InvalidArgumentError for a non-finite or non-positive u_sens or density. NOTHING IS CLAMPED and
// no temperature floor is applied.
[[nodiscard]] Real temperatureFromSensibleEnergy(const MixtureThermodynamics& mixture,
                                                 const std::vector<Real>& composition, Real density,
                                                 Real sensibleEnergyDensity);

// ===================================================================
// The coupled step -- EXPLICIT operator splitting (sections 18, 19)
// ===================================================================

// THE ORDER IS FROZEN at energy_accounting.md section 10 and this struct's fields follow it:
//
//   1. state at n
//   2. ONE evaluation of q_r at (T^n, Y^n, rho^n)              P18.5, done by the CALLER
//   3. omega_i from that q_r                                   P18.4
//   4. S_E from THE SAME q_r                                   P18.6
//   5. advance species                                         explicit Euler here
//   6. advance the sensible energy                             u_sens += dt S_E
//   7. recover T^{n+1} from the UPDATED Y^{n+1}                section 17
//   8. the next step's rates are evaluated at T^{n+1}          sections 18, 20
//
// EXPLICIT, AND NOT CLAIMED TO BE ANYTHING ELSE. There is no sub-stepping, no operator-split
// iteration and no implicit treatment (authorization section 18 forbids implying otherwise, and
// section 41 forbids inventing a stiff integrator outside scope).
struct ReactorState {
  MassFractionState composition;
  cfd::fields::ScalarField temperature;
  cfd::fields::ScalarField density;
};

// One explicit step of the closed, homogeneous, adiabatic, source-only reactor of section 23:
// U = 0, no boundary heat flux, no boundary mass flux, constant volume. The only evolution is
// species conversion and the temperature that follows from it.
//
// REFUSES rather than clamps (sections 40, 41, 42):
//   - a step that would drive any rho Y_i negative      -- there is no chemistry integrator in
//     P18.6 scope, so an invalid explicit step is an error, not something to normalise away
//   - a non-finite S_E, energy or recovered T
//   - a recovered T <= 0
// The diagnostic names the quantity, the cell and the value.

// TD-002: the per-species ROUND-OFF CARRY of a multi-step march, [cell][species] in species-index
// order. It exists because of a measured defect and its shape is dictated by a measured constraint.
//
// THE DEFECT. `Y_i + dt omega_i / rho` is applied once per species, and each of those N additions
// rounds. The errors do not cancel and are sign-biased, so over a march `sum_i Y_i` walks away from
// 1 LINEARLY -- measured 0.006 eps/step on a reversible mechanism, crossing P18.1's 4*eps*N at
// ~1333 steps, at which point MassFractionState refuses the state and the march STOPS. Tightening a
// chemistry integrator's accuracy control makes it throw, which is reproducer.md V1. Note that the
// increments themselves sum to exactly zero: this is not a closure defect in omega, it is the
// rounding of the stores.
//
// WHAT THE CARRY IS. The low-order bits each addition could not represent. Carried into the next
// increment instead of being discarded, so nothing is lost and `sum_i Y_i = 1` follows from
// `sum_i omega_i = 0` rather than being imposed. It is NOT clipping, NOT normalisation and NOT a
// loosened tolerance -- all three are forbidden by TD-002's authorization, and all three would
// operate ON the sum, which this never touches. Measured: 0.00 eps after 100000 steps.
//
// WHY IT IS A PARAMETER HERE AND NOT STATE INSIDE THIS FUNCTION. Two reasons, both binding:
//
//   * `include/cfd/gpu/DeviceSpeciesThermochemistry.hpp` RESTATES this update for the device, and
//     P18.8 gates that restatement BITWISE against this host code. Changing the arithmetic here
//     would break a closed gate.
//   * with the carry absent -- or present and zero -- the arithmetic below is unchanged:
//     `y = d - 0` is exactly `d`, so the store is exactly `fl(Y + d)`. A SINGLE step is therefore
//     bitwise what it always was, and only steps AFTER the first differ. That is what lets P18.6's
//     qualified step, P18.8's equivalence and TD-001's single-step production dispatch all stay
//     bitwise while a long march becomes possible.
//
// So `nullptr` is the default and is exactly the pre-TD-002 code path. A caller that marches passes
// a carry sized [cells][species]; `integrateChemistry` is that caller. Resized and zeroed if it
// does not already match, so a fresh vector works and a carried one is respected.
[[nodiscard]] ReactorState advanceClosedReactor(
    const ReactionMechanism& mechanism, const MixtureThermodynamics& mixture,
    const FormationEnthalpies& formation, const ReactionProgressRates& rates,
    const ReactorState& state, Real dt, std::vector<std::vector<Real>>* roundOffCarry = nullptr);

}  // namespace cfd::species
