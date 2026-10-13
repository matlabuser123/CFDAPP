#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"

// P18.5 -- Arrhenius kinetics.
//
//     k(T) = A T^n exp( -Ea / (R_u T) )
//
// and elementary mass action,
//
//     q_f = k_f(T) Pi_i C_i^(nu'_i)      with  C_i = rho Y_i / M_i
//
// WHAT THIS IS NOT: P18.4's source algebra. P18.5 produces PROGRESS RATES and nothing else. The
// conversion q -> omega_i = sum_r M_i nu_{i,r} q_r stays entirely in
// reactionSpeciesSources(), and the mass-conservation formula is not duplicated here.
//
//     P18.5                     P18.4                      P18.2
//     T, rho, Y_i, A, n, Ea
//       -> k(T)
//       -> C_i = rho Y_i / M_i
//       -> q_r
//                               -> omega_i                 -> conservative transport
//
// ===================================================================
// UNITS, one system throughout -- mol, never kmol:
//     Ea  [J/mol]      paired with R_u [J/(mol K)], REUSED from P18.3, so Ea/(R_u T) is
//                      dimensionless by construction rather than by convention
//     T   [K]          C_i [mol/m^3]      q [mol/(m^3 s)]
//     A   carries k's units times K^-n, and k's units DEPEND ON REACTION ORDER:
//                      [k] = (m^3/mol)^(m-1) / s   for total reactant order m
//                      so no single dimensional form is claimed for A, and no dimension can be
//                      validated in code -- see results/p18/arrhenius_chemistry/units.md
// ===================================================================
//
// EVALUATION, as AMENDED BY A1 (results/p18/arrhenius_chemistry/formulation_A1.md). The original
// formulation froze log-space-always; A1 makes the direct form primary and keeps log space as the
// overflow fallback. The MODEL is unchanged and no threshold moved.
//
//     k = 0                                   if A == 0        exact branch, ln(0) = -inf
//     k = (A T^n) exp(-Ea/(R_u T))            if A T^n FINITE  DIRECT
//     k = exp( ln A + n ln T - Ea/(R_u T) )   otherwise        FALLBACK
//
// WHY DIRECT WHERE IT IS SAFE: one fewer transcendental rounding, and it is EXACT in the reduced
// cases -- std::pow(T, 0.0) and std::exp(0.0) are both exactly 1.0, so n = 0 with Ea = 0 gives
// k == A BITWISE. Log space gave 150000000.00000015 against A = 150000000.
//
// WHY LOG SPACE IS KEPT: A T^n can overflow while k itself is representable, and inf * 0 is NaN.
// The branch is chosen by TESTING isfinite(A T^n) -- the actual condition, not a parameter-range
// guess -- and the fallback is proven live by a test that drives A T^n to overflow.
//
// NO HEAT RELEASE. P18.5 READS temperature and never writes it. The loop
// reaction -> heat -> T -> reaction is P18.6's; the dependency here is strictly one-way.
namespace cfd::species {

// Arrhenius parameters for one direction of one reaction.
//
// Ea < 0 is REFUSED: negative activation energies are physical for some elementary steps but are
// outside the adopted P18 model, and nothing in the frozen gate judges them. Negative n IS
// allowed -- it is ordinary, and T^n is well defined for T > 0 and any finite n.
class ArrheniusParameters {
 public:
  // Throws InvalidArgumentError if A < 0 or non-finite, n non-finite, or Ea < 0 or non-finite.
  // Nothing is clamped.
  ArrheniusParameters(Real preExponential, Real temperatureExponent, Real activationEnergy);

  [[nodiscard]] Real preExponential() const noexcept { return preExponential_; }      // A
  [[nodiscard]] Real temperatureExponent() const noexcept { return exponent_; }       // n
  [[nodiscard]] Real activationEnergy() const noexcept { return activationEnergy_; }  // Ea

  // ln k = ln A + n ln T - Ea/(R_u T).  Throws for T <= 0 or non-finite -- checked BEFORE any
  // division or exp(), never inferred from their output. Returns -infinity when A == 0.
  [[nodiscard]] Real logRateCoefficient(Real temperature) const;

  // k(T). Throws for an invalid T, or if the result is non-finite (an overflow is refused by
  // name rather than returned as inf). An UNDERFLOW to exactly 0 is accepted and documented: a
  // rate that small is physically negligible.
  [[nodiscard]] Real rateCoefficient(Real temperature) const;

  // dk/dT = k [ n/T + Ea/(R_u T^2) ], from the closed form. Verified against a central finite
  // difference of rateCoefficient() -- two independent routes to the same number.
  [[nodiscard]] Real rateCoefficientDerivative(Real temperature) const;

  // The exponential argument x = -Ea/(R_u T), exposed so its sign and magnitude can be asserted
  // directly (section 32) rather than inferred from k. A sign reversal here is control N1.
  [[nodiscard]] Real exponentialArgument(Real temperature) const;

 private:
  Real preExponential_{};
  Real exponent_{};
  Real activationEnergy_{};
};

// Kinetic parameters BOUND TO A REACTION BY NAME.
//
// Not by index: an index binding is silently wrong if the mechanism's reaction order changes --
// the parameters would attach to the wrong reaction and every rate would be plausible and wrong.
// Name binding makes that unrepresentable rather than merely detectable (control N8).
struct ReactionRateParameters {
  std::string reaction;  // must name a reaction the mechanism holds
  ArrheniusParameters forward;
  std::optional<ArrheniusParameters> reverse{};  // REQUIRED for Reversible, FORBIDDEN otherwise
};

// Kinetics for a whole mechanism.
//
// Refuses at construction: an unknown reaction name, two parameter sets for one reaction, a
// reaction with no parameter set, reverse parameters on an irreversible reaction, and missing
// reverse parameters on a reversible one.
class ReactionKinetics {
 public:
  ReactionKinetics(const ReactionMechanism& mechanism,
                   std::vector<ReactionRateParameters> parameters);

  [[nodiscard]] Index reactionCount() const noexcept { return parameters_.size(); }

  // In MECHANISM reaction-index order, whatever order the parameters were supplied in.
  [[nodiscard]] const ArrheniusParameters& forward(Index reactionIndex) const;
  [[nodiscard]] const std::optional<ArrheniusParameters>& reverse(Index reactionIndex) const;

 private:
  std::vector<ReactionRateParameters> parameters_;  // reordered to match the mechanism
};

// C_i = rho Y_i / M_i, one field per species. [mol/m^3]
//
// Every input is reused: rho from the caller, Y_i from P18.1's validated state, M_i from P18.3.
// P18.5 stores no composition and no molecular weight, so the basis cannot drift.
//
// Throws for a non-finite or negative density. A negative concentration cannot arise from a valid
// MassFractionState and a positive density, which is why the guard is on the density.
[[nodiscard]] std::vector<cfd::fields::ScalarField> molarConcentrations(
    const MixtureThermodynamics& mixture, const MassFractionState& state,
    const cfd::fields::ScalarField& density);

// Elementary mass-action progress rates, per reaction, per cell, ready for P18.4's
// reactionSpeciesSources().
//
//     q_f = k_f(T) Pi_i C_i^(nu'_i)     over REACTANTS, using nu'_i -- the reaction ORDER is the
//                                       reactant coefficient, not the net one
//     q_r = k_r(T) Pi_i C_i^(nu''_i)    over PRODUCTS, reversible only
//     q   = q_f - q_r
//
// EACH CELL FROM ITS OWN T, rho AND Y_i (sections 27, 28). No global rate coefficient is cached
// for a spatially varying temperature field -- control N6.
//
// Throws, naming the reaction, the species and the cell: an invalid temperature, a negative or
// non-finite concentration, or a non-finite rate.
[[nodiscard]] ReactionProgressRates arrheniusProgressRates(
    const ReactionMechanism& mechanism, const ReactionKinetics& kinetics,
    const MixtureThermodynamics& mixture, const MassFractionState& state,
    const cfd::fields::ScalarField& density, const cfd::fields::ScalarField& temperature);

// ===================================================================================
// THE ITERATE PATH -- G8. Everything above takes a validated MassFractionState; this does not.
// ===================================================================================
//
// WHY A SECOND ENTRY POINT EXISTS. A segregated flame solver evaluates rates on a WORKING VECTOR,
// not on a state. Two of P18.1's invariants are legitimately violated by such a vector:
//
//   * `sum_i Y_i = 1` holds only to the linear solver's tolerance, not to 4*eps*N, because each
//     species is solved from its own system;
//   * `0 <= Y_i` holds for the exact discrete solution (the Patankar split makes the operator an
//     M-matrix) but a BiCGSTAB solve converged to a tolerance can return -1e-14 where the exact
//     value is 0.
//
// MassFractionState correctly refuses both -- G2(b) makes the bound "boolean, no tolerance" -- so
// an iterate cannot be wrapped in one. This is the same distinction P18.2 already drew, for the
// same reason, when speciesDiffusiveFaceFluxes was given a raw-fields entry point: "a deferred-
// correction iterate legitimately violates sum(Y_i) = 1 ... The invariant is enforced at step
// boundaries, where it must hold; an iterate is a working vector" (finding F2).
//
// THESE ARE INDEPENDENT FUNCTIONS, NOT A REFACTOR. The validated path above is not touched, so its
// behaviour is preserved by construction rather than by argument. That the two routes agree
// BITWISE on a state that satisfies both contracts is asserted by test_raw_field_kinetics.cpp --
// which is a stronger statement than a careful refactor could make.
enum class NegativeConcentrationPolicy {
  // THROW, naming the species and the cell. P18.5's contract, and the default.
  Refuse,
  // Treat the progress rate as EXACTLY ZERO at any cell where a reactant concentration is
  // negative.
  //
  // THIS IS THE CONTINUOUS EXTENSION OF THE RATE LAW, NOT A CLIP. Every reaction order in this
  // codebase is non-negative by construction -- Reaction refuses a negative explicit order, and
  // P18.4 refuses a negative stoichiometric coefficient -- so C^order -> 0 as C -> 0+, and the
  // value used is the limit from the physical side. The zero-concentration case already returns
  // exactly 0.0 by the same reasoning, and this extends that by continuity rather than inventing
  // a different rule.
  //
  // NOTHING IN THE COMPOSITION IS MODIFIED. Only the rate at that cell is defined. A caller using
  // this policy is expected to REPORT the most negative value it supplied, so the magnitude of the
  // artefact stays visible instead of being absorbed -- PremixedFlameResult carries exactly that.
  ZeroRateWhereNegative,
};

// C_i = rho Y_i / M_i from RAW per-species fields. Identical arithmetic to the overload above,
// in the identical order.
//
// Throws InvalidArgumentError for a field-count or size mismatch, a registry mismatch, or a
// non-finite/negative density. A negative Y_i is NOT rejected here -- it is the caller's declared
// policy at the rate evaluation that decides, and rejecting it here would make the policy
// unreachable.
[[nodiscard]] std::vector<cfd::fields::ScalarField> molarConcentrationsFromFields(
    const MixtureThermodynamics& mixture, const SpeciesRegistry& registry,
    const std::vector<cfd::fields::ScalarField>& fields, const cfd::fields::ScalarField& density);

// Progress rates from RAW per-species fields, with an explicit negative-concentration policy.
//
// Throws, naming the reaction, the species and the cell: an invalid temperature, a non-finite
// concentration, a non-finite rate, or -- under NegativeConcentrationPolicy::Refuse -- a negative
// concentration. Size and registry mismatches throw InvalidArgumentError.
[[nodiscard]] ReactionProgressRates arrheniusProgressRatesFromFields(
    const ReactionMechanism& mechanism, const ReactionKinetics& kinetics,
    const MixtureThermodynamics& mixture, const std::vector<cfd::fields::ScalarField>& fields,
    const cfd::fields::ScalarField& density, const cfd::fields::ScalarField& temperature,
    NegativeConcentrationPolicy policy = NegativeConcentrationPolicy::Refuse);

// Diagnostics for the exponential argument over a field (section 32), reported rather than only
// thresholded.
struct ExponentDiagnostics {
  Real mostNegative{};
  Real mostPositive{};
  bool anyPositive{false};  // true would mean exp(+Ea/RT) somewhere -- a sign error
  Index worstCell{};
};

[[nodiscard]] ExponentDiagnostics exponentDiagnostics(const ReactionKinetics& kinetics,
                                                      Index reactionIndex,
                                                      const cfd::fields::ScalarField& temperature);

}  // namespace cfd::species
