#include "cfd/species/ArrheniusKinetics.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::species {
namespace {

std::string exact(Real value) {
  std::ostringstream out;
  out.precision(std::numeric_limits<Real>::max_digits10);
  out << value;
  return out.str();
}

// Checked BEFORE any division or exp(), never inferred from their output (section 4).
void requireValidTemperature(Real temperature, const std::string& where) {
  if (!std::isfinite(temperature)) {
    throw InvalidArgumentError(where + ": temperature is " + exact(temperature) +
                               "; must be finite");
  }
  if (!(temperature > 0.0)) {
    throw InvalidArgumentError(where + ": temperature is " + exact(temperature) +
                               "; must be > 0 K. Arrhenius kinetics are undefined at or below "
                               "absolute zero, and no temperature floor is applied.");
  }
}

}  // namespace

// ===================================================================
// ArrheniusParameters
// ===================================================================

ArrheniusParameters::ArrheniusParameters(Real preExponential, Real temperatureExponent,
                                         Real activationEnergy)
    : preExponential_(preExponential),
      exponent_(temperatureExponent),
      activationEnergy_(activationEnergy) {
  if (!std::isfinite(preExponential_) || preExponential_ < 0.0) {
    throw InvalidArgumentError("ArrheniusParameters: pre-exponential factor A is " +
                               exact(preExponential_) +
                               "; must be finite and >= 0. A = 0 is allowed and gives k = 0.");
  }
  if (!std::isfinite(exponent_)) {
    throw InvalidArgumentError("ArrheniusParameters: temperature exponent n is " +
                               exact(exponent_) +
                               "; must be finite. A negative n is allowed and is ordinary.");
  }
  if (!std::isfinite(activationEnergy_) || activationEnergy_ < 0.0) {
    throw InvalidArgumentError(
        "ArrheniusParameters: activation energy Ea is " + exact(activationEnergy_) +
        " J/mol; must be finite and >= 0. A negative Ea is physical for some elementary steps but "
        "is outside the adopted P18 model -- nothing in the frozen gate judges it. Nothing is "
        "clamped.");
  }
}

Real ArrheniusParameters::exponentialArgument(Real temperature) const {
  requireValidTemperature(temperature, "ArrheniusParameters::exponentialArgument");
  // x = -Ea/(R_u T). With Ea >= 0 and T > 0 this is always <= 0; a positive value would mean a
  // sign error, which is what exponentDiagnostics() reports and control N1 attacks.
  return -activationEnergy_ / (kUniversalGasConstant * temperature);
}

Real ArrheniusParameters::logRateCoefficient(Real temperature) const {
  requireValidTemperature(temperature, "ArrheniusParameters::logRateCoefficient");
  if (preExponential_ == 0.0) {
    // EXACT branch, not a clamp: ln(0) = -inf, and k = 0 for every valid T.
    return -std::numeric_limits<Real>::infinity();
  }
  // ln k = ln A + n ln T - Ea/(R_u T), summed in a fixed order.
  return std::log(preExponential_) + (exponent_ * std::log(temperature)) +
         exponentialArgument(temperature);
}

Real ArrheniusParameters::rateCoefficient(Real temperature) const {
  if (preExponential_ == 0.0) {
    requireValidTemperature(temperature, "ArrheniusParameters::rateCoefficient");
    return 0.0;  // exactly, for every valid T (section 8)
  }
  const Real logK = logRateCoefficient(temperature);

  // AMENDMENT A1 (results/p18/arrhenius_chemistry/formulation_A1.md): the DIRECT form where it is
  // safe, log space only as the overflow fallback.
  //
  // WHY: the direct form carries one fewer transcendental rounding, and it is EXACT in the reduced
  // cases -- std::pow(T, 0.0) and std::exp(0.0) are both exactly 1.0, so n = 0 with Ea = 0 gives
  // A * 1.0 * 1.0 == A BITWISE. Under log-space-always it gave 150000000.00000015 against
  // A = 150000000, a 4.5 eps error in the case section 11 calls the strongest simple regression
  // test. That failure is preserved at logs/01_first_failures_PRESERVED.log.
  //
  // THE PREDICATE IS THE ACTUAL CONDITION, not a parameter-range guess: A T^n is computed and
  // tested for finiteness. And multiplying by exp(-Ea/(R_u T)) can never overflow afterwards,
  // because that factor is <= 1 for Ea >= 0 and T > 0 -- which is why Ea >= 0 being ENFORCED
  // matters here and not only physically.
  const Real powerLaw = preExponential_ * std::pow(temperature, exponent_);
  if (std::isfinite(powerLaw)) {
    const Real direct = powerLaw * std::exp(exponentialArgument(temperature));
    if (!std::isfinite(direct)) {
      throw NumericalError(
          "ArrheniusParameters::rateCoefficient: k is not finite at T = " + exact(temperature) +
          " (A T^n = " + exact(powerLaw) + ", A = " + exact(preExponential_) +
          ", n = " + exact(exponent_) + ", Ea = " + exact(activationEnergy_) + ")");
    }
    return direct;
  }

  // A T^n overflowed while k itself may be representable. Log space combines the terms before
  // exponentiating, so this is the branch the original formulation existed for.
  const Real k = std::exp(logK);
  if (!std::isfinite(k)) {
    // An OVERFLOW is refused by name rather than returned as inf (section 31). An underflow to
    // exactly 0 is finite and is accepted -- see the header.
    throw NumericalError(
        "ArrheniusParameters::rateCoefficient: k is not finite at T = " + exact(temperature) +
        " (ln k = " + exact(logK) + ", A = " + exact(preExponential_) +
        ", n = " + exact(exponent_) + ", Ea = " + exact(activationEnergy_) + ")");
  }
  return k;
}

Real ArrheniusParameters::rateCoefficientDerivative(Real temperature) const {
  const Real k = rateCoefficient(temperature);
  // dk/dT = k [ n/T + Ea/(R_u T^2) ], from d(ln k)/dT. Closed form, verified in the tests against
  // a central finite difference of rateCoefficient().
  const Real dLogK = (exponent_ / temperature) +
                     (activationEnergy_ / (kUniversalGasConstant * temperature * temperature));
  return k * dLogK;
}

// ===================================================================
// ReactionKinetics
// ===================================================================

ReactionKinetics::ReactionKinetics(const ReactionMechanism& mechanism,
                                   std::vector<ReactionRateParameters> parameters) {
  const Index nr = mechanism.reactionCount();
  if (parameters.size() != nr) {
    throw InvalidArgumentError("ReactionKinetics: " + std::to_string(parameters.size()) +
                               " parameter sets for " + std::to_string(nr) +
                               " reactions; exactly one per reaction is required");
  }

  // BOUND BY NAME, not by position. The supplied order is irrelevant; the stored order is the
  // mechanism's. An index binding would silently misattach every parameter set if the mechanism's
  // reaction order changed -- control N8.
  parameters_.reserve(nr);
  std::unordered_set<std::string> seen;
  for (Index r = 0; r < nr; ++r) {
    const Reaction& reaction = mechanism.reaction(r);
    const ReactionRateParameters* match = nullptr;
    for (const auto& candidate : parameters) {
      if (candidate.reaction == reaction.name()) {
        if (match != nullptr) {
          throw InvalidArgumentError("ReactionKinetics: two parameter sets name reaction \"" +
                                     reaction.name() + "\"");
        }
        match = &candidate;
      }
    }
    if (match == nullptr) {
      throw InvalidArgumentError("ReactionKinetics: no kinetic parameters for reaction \"" +
                                 reaction.name() +
                                 "\". Every reaction in the mechanism needs its own.");
    }
    seen.insert(match->reaction);

    const bool reversible = reaction.direction() == ReactionDirection::Reversible;
    if (reversible && !match->reverse.has_value()) {
      throw InvalidArgumentError(
          "ReactionKinetics: reaction \"" + reaction.name() +
          "\" is REVERSIBLE but has no reverse Arrhenius parameters. Reverse kinetics are supplied "
          "independently in P18.5 -- they are NOT derived from an equilibrium constant.");
    }
    if (!reversible && match->reverse.has_value()) {
      throw InvalidArgumentError("ReactionKinetics: reaction \"" + reaction.name() +
                                 "\" is IRREVERSIBLE but reverse Arrhenius parameters were "
                                 "supplied; an irreversible reaction has no reverse rate");
    }
    parameters_.push_back(*match);
  }

  // Every supplied set must have matched something, or a set naming a reaction the mechanism does
  // not hold would be silently ignored.
  for (const auto& supplied : parameters) {
    if (seen.find(supplied.reaction) == seen.end()) {
      throw InvalidArgumentError("ReactionKinetics: parameter set names reaction \"" +
                                 supplied.reaction + "\", which the mechanism does not hold");
    }
  }
}

const ArrheniusParameters& ReactionKinetics::forward(Index reactionIndex) const {
  if (reactionIndex >= parameters_.size()) {
    throw InvalidArgumentError("ReactionKinetics: reaction index " + std::to_string(reactionIndex) +
                               " is out of range; holding " + std::to_string(parameters_.size()));
  }
  return parameters_[reactionIndex].forward;
}

const std::optional<ArrheniusParameters>& ReactionKinetics::reverse(Index reactionIndex) const {
  if (reactionIndex >= parameters_.size()) {
    throw InvalidArgumentError("ReactionKinetics: reaction index " + std::to_string(reactionIndex) +
                               " is out of range; holding " + std::to_string(parameters_.size()));
  }
  return parameters_[reactionIndex].reverse;
}

// ===================================================================
// Concentrations and progress rates
// ===================================================================

std::vector<cfd::fields::ScalarField> molarConcentrations(const MixtureThermodynamics& mixture,
                                                          const MassFractionState& state,
                                                          const cfd::fields::ScalarField& density) {
  const Index ns = state.speciesCount();
  if (mixture.speciesCount() != ns) {
    throw InvalidArgumentError("molarConcentrations: the mixture holds " +
                               std::to_string(mixture.speciesCount()) +
                               " species; the state holds " + std::to_string(ns));
  }
  if (mixture.registry() != state.registry()) {
    throw InvalidArgumentError(
        "molarConcentrations: the mixture's species registry differs from the state's; molecular "
        "weights would be read against the wrong species ordering");
  }
  if (density.size() != state.cellCount()) {
    throw InvalidArgumentError("molarConcentrations: the density field holds " +
                               std::to_string(density.size()) + " cells; the state holds " +
                               std::to_string(state.cellCount()));
  }

  std::vector<cfd::fields::ScalarField> concentrations(ns,
                                                       cfd::fields::ScalarField(state.cellCount()));
  for (Index cell = 0; cell < state.cellCount(); ++cell) {
    const Real rho = density[cell];
    if (!std::isfinite(rho) || rho < 0.0) {
      throw InvalidArgumentError("molarConcentrations: density at cell " + std::to_string(cell) +
                                 " is " + exact(rho) + "; must be finite and >= 0");
    }
    for (Index i = 0; i < ns; ++i) {
      // C_i = rho Y_i / M_i. Y_i is already finite and in [0,1] (P18.1 enforced it at
      // construction), and M_i > 0 (P18.3 enforced it), so with a finite non-negative rho the
      // result cannot be negative or non-finite. The guard above is therefore on the one input
      // P18.5 is handed unvalidated.
      concentrations[i][cell] =
          (rho * state.massFraction(i, cell)) / mixture.species(i).molecularWeight();
    }
  }
  return concentrations;
}

namespace {

// Pi_i C_i^(order_i), over ASCENDING SPECIES INDEX. A zero concentration with a positive order
// short-circuits to EXACTLY zero, so a zero never reaches pow().
//
// 0^0 cannot arise: an order of 0 means the species is not on that side and is skipped.
// A negative order cannot arise from either route: P18.4 refuses a negative stoichiometric
// coefficient at construction, AND Reaction refuses a negative explicit reaction order for the
// same reason -- C^(negative) diverges as C -> 0 and the remedy would be a clip this phase forbids.
// The original wording below covered only the first route; both are needed now that orders can be
// supplied independently.
// P18.4 refuses a negative stoichiometric coefficient at
// construction. Both are structural, not checked here.
Real concentrationProduct(const std::vector<cfd::fields::ScalarField>& concentrations,
                          const Reaction& reaction, Index cell, bool forwardSide,
                          const SpeciesRegistry& registry, const std::string& where) {
  Real product = 1.0;
  for (Index i = 0; i < concentrations.size(); ++i) {
    // FORWARD: the reaction ORDER, which equals nu'_i for a mass-action reaction and differs for
    // a fitted global one. REVERSE: the product stoichiometry -- a reversible reaction may not
    // carry explicit orders (Reaction's constructor refuses that combination), so there is no
    // reverse order to read.
    const Real order = forwardSide ? reaction.reactantOrder(i) : reaction.productCoefficient(i);
    if (order == 0.0) {
      continue;  // not on this side
    }
    const Real c = concentrations[i][cell];
    if (!std::isfinite(c) || c < 0.0) {
      throw InvalidArgumentError(where + ": concentration of species \"" + registry.name(i) +
                                 "\" at cell " + std::to_string(cell) + " is " + exact(c) +
                                 "; must be finite and >= 0. A fractional power of a negative "
                                 "concentration is not evaluated.");
    }
    if (c == 0.0) {
      return 0.0;  // EXACTLY, and pow() is never called (section 23)
    }
    product *= std::pow(c, order);
  }
  return product;
}

}  // namespace

ReactionProgressRates arrheniusProgressRates(const ReactionMechanism& mechanism,
                                             const ReactionKinetics& kinetics,
                                             const MixtureThermodynamics& mixture,
                                             const MassFractionState& state,
                                             const cfd::fields::ScalarField& density,
                                             const cfd::fields::ScalarField& temperature) {
  const Index nr = mechanism.reactionCount();
  const Index cells = state.cellCount();
  if (kinetics.reactionCount() != nr) {
    throw InvalidArgumentError("arrheniusProgressRates: kinetics hold " +
                               std::to_string(kinetics.reactionCount()) +
                               " reactions; the mechanism holds " + std::to_string(nr));
  }
  if (temperature.size() != cells) {
    throw InvalidArgumentError("arrheniusProgressRates: the temperature field holds " +
                               std::to_string(temperature.size()) + " cells; the state holds " +
                               std::to_string(cells));
  }
  if (mechanism.registry() != state.registry()) {
    throw InvalidArgumentError(
        "arrheniusProgressRates: the mechanism's species registry differs from the state's");
  }

  const auto concentrations = molarConcentrations(mixture, state, density);
  ReactionProgressRates rates = zeroProgressRates(nr, cells);

  // ASCENDING REACTION INDEX, then ASCENDING CELL. Each cell uses ITS OWN temperature -- no global
  // rate coefficient is cached for a spatially varying field (control N6).
  for (Index r = 0; r < nr; ++r) {
    const Reaction& reaction = mechanism.reaction(r);
    const auto& forward = kinetics.forward(r);
    const auto& reverse = kinetics.reverse(r);
    for (Index cell = 0; cell < cells; ++cell) {
      const Real t = temperature[cell];
      requireValidTemperature(t, "arrheniusProgressRates: reaction \"" + reaction.name() +
                                     "\" at cell " + std::to_string(cell));

      const Real kForward = forward.rateCoefficient(t);
      const Real qForward =
          kForward * concentrationProduct(
                         concentrations, reaction, cell, true, mechanism.registry(),
                         "arrheniusProgressRates: reaction \"" + reaction.name() + "\" forward");
      Real q = qForward;
      if (reverse.has_value()) {
        const Real kReverse = reverse->rateCoefficient(t);
        const Real qReverse =
            kReverse * concentrationProduct(
                           concentrations, reaction, cell, false, mechanism.registry(),
                           "arrheniusProgressRates: reaction \"" + reaction.name() + "\" reverse");
        q = qForward - qReverse;
      }
      if (!std::isfinite(q)) {
        throw NumericalError("arrheniusProgressRates: progress rate for reaction \"" +
                             reaction.name() + "\" at cell " + std::to_string(cell) + " is " +
                             exact(q) + " (T = " + exact(t) + ")");
      }
      rates.perReaction[r][cell] = q;
    }
  }
  return rates;
}

// ===================================================================================
// THE ITERATE PATH -- G8. See ArrheniusKinetics.hpp for why this is a separate path rather than a
// refactor of the validated one above.
// ===================================================================================

namespace {

// Pi_i C_i^(order_i) for a RAW field set, over ASCENDING SPECIES INDEX -- the same order and the
// same expression as concentrationProduct above, with one added branch for the declared policy.
//
// `negativeFound` is an OUT parameter rather than a thrown error under the permissive policy,
// because the caller must be able to report the magnitude it supplied.
Real concentrationProductRaw(const std::vector<cfd::fields::ScalarField>& concentrations,
                             const Reaction& reaction, Index cell, bool forwardSide,
                             const SpeciesRegistry& registry, const std::string& where,
                             NegativeConcentrationPolicy policy, bool& negativeFound) {
  Real product = 1.0;
  for (Index i = 0; i < concentrations.size(); ++i) {
    const Real order = forwardSide ? reaction.reactantOrder(i) : reaction.productCoefficient(i);
    if (order == 0.0) {
      continue;  // not on this side
    }
    const Real c = concentrations[i][cell];
    if (!std::isfinite(c)) {
      throw InvalidArgumentError(where + ": concentration of species \"" + registry.name(i) +
                                 "\" at cell " + std::to_string(cell) + " is " + exact(c) +
                                 "; must be finite");
    }
    if (c < 0.0) {
      if (policy == NegativeConcentrationPolicy::Refuse) {
        throw InvalidArgumentError(where + ": concentration of species \"" + registry.name(i) +
                                   "\" at cell " + std::to_string(cell) + " is " + exact(c) +
                                   "; must be finite and >= 0. A fractional power of a negative "
                                   "concentration is not evaluated.");
      }
      // The continuous extension: every order here is > 0, so C^order -> 0 as C -> 0+.
      negativeFound = true;
      return 0.0;
    }
    if (c == 0.0) {
      return 0.0;  // EXACTLY, and pow() is never called -- as in the validated path
    }
    product *= std::pow(c, order);
  }
  return product;
}

}  // namespace

std::vector<cfd::fields::ScalarField> molarConcentrationsFromFields(
    const MixtureThermodynamics& mixture, const SpeciesRegistry& registry,
    const std::vector<cfd::fields::ScalarField>& fields, const cfd::fields::ScalarField& density) {
  const Index ns = registry.size();
  if (mixture.speciesCount() != ns) {
    throw InvalidArgumentError("molarConcentrationsFromFields: the mixture holds " +
                               std::to_string(mixture.speciesCount()) +
                               " species; the registry holds " + std::to_string(ns));
  }
  if (mixture.registry() != registry) {
    throw InvalidArgumentError(
        "molarConcentrationsFromFields: the mixture's species registry differs from the one "
        "supplied; molecular weights would be read against the wrong species ordering");
  }
  if (fields.size() != ns) {
    throw InvalidArgumentError("molarConcentrationsFromFields: " + std::to_string(fields.size()) +
                               " fields were supplied for " + std::to_string(ns) + " species");
  }
  const Index cells = density.size();
  for (Index i = 0; i < ns; ++i) {
    if (fields[i].size() != cells) {
      throw InvalidArgumentError("molarConcentrationsFromFields: the field for species \"" +
                                 registry.name(i) + "\" holds " + std::to_string(fields[i].size()) +
                                 " cells; the density field holds " + std::to_string(cells));
    }
  }

  std::vector<cfd::fields::ScalarField> concentrations(ns, cfd::fields::ScalarField(cells));
  for (Index cell = 0; cell < cells; ++cell) {
    const Real rho = density[cell];
    if (!std::isfinite(rho) || rho < 0.0) {
      throw InvalidArgumentError("molarConcentrationsFromFields: density at cell " +
                                 std::to_string(cell) + " is " + exact(rho) +
                                 "; must be finite and >= 0");
    }
    for (Index i = 0; i < ns; ++i) {
      // The SAME expression, in the SAME order, as molarConcentrations above -- which is what
      // makes the bitwise-agreement test meaningful rather than approximate.
      concentrations[i][cell] = (rho * fields[i][cell]) / mixture.species(i).molecularWeight();
    }
  }
  return concentrations;
}

ReactionProgressRates arrheniusProgressRatesFromFields(
    const ReactionMechanism& mechanism, const ReactionKinetics& kinetics,
    const MixtureThermodynamics& mixture, const std::vector<cfd::fields::ScalarField>& fields,
    const cfd::fields::ScalarField& density, const cfd::fields::ScalarField& temperature,
    NegativeConcentrationPolicy policy) {
  const Index nr = mechanism.reactionCount();
  const Index cells = temperature.size();
  if (kinetics.reactionCount() != nr) {
    throw InvalidArgumentError("arrheniusProgressRatesFromFields: kinetics hold " +
                               std::to_string(kinetics.reactionCount()) +
                               " reactions; the mechanism holds " + std::to_string(nr));
  }
  if (density.size() != cells) {
    throw InvalidArgumentError("arrheniusProgressRatesFromFields: the density field holds " +
                               std::to_string(density.size()) +
                               " cells; the temperature field holds " + std::to_string(cells));
  }

  const auto concentrations =
      molarConcentrationsFromFields(mixture, mechanism.registry(), fields, density);
  ReactionProgressRates rates = zeroProgressRates(nr, cells);

  bool negativeFound = false;
  for (Index r = 0; r < nr; ++r) {
    const Reaction& reaction = mechanism.reaction(r);
    const auto& forward = kinetics.forward(r);
    const auto& reverse = kinetics.reverse(r);
    for (Index cell = 0; cell < cells; ++cell) {
      const Real t = temperature[cell];
      requireValidTemperature(t, "arrheniusProgressRatesFromFields: reaction \"" + reaction.name() +
                                     "\" at cell " + std::to_string(cell));

      const Real kForward = forward.rateCoefficient(t);
      const Real qForward =
          kForward *
          concentrationProductRaw(
              concentrations, reaction, cell, true, mechanism.registry(),
              "arrheniusProgressRatesFromFields: reaction \"" + reaction.name() + "\" forward",
              policy, negativeFound);
      Real q = qForward;
      if (reverse.has_value()) {
        const Real kReverse = reverse->rateCoefficient(t);
        const Real qReverse =
            kReverse *
            concentrationProductRaw(
                concentrations, reaction, cell, false, mechanism.registry(),
                "arrheniusProgressRatesFromFields: reaction \"" + reaction.name() + "\" reverse",
                policy, negativeFound);
        q = qForward - qReverse;
      }
      if (!std::isfinite(q)) {
        throw NumericalError("arrheniusProgressRatesFromFields: progress rate for reaction \"" +
                             reaction.name() + "\" at cell " + std::to_string(cell) + " is " +
                             exact(q) + " (T = " + exact(t) + ")");
      }
      rates.perReaction[r][cell] = q;
    }
  }
  return rates;
}

ExponentDiagnostics exponentDiagnostics(const ReactionKinetics& kinetics, Index reactionIndex,
                                        const cfd::fields::ScalarField& temperature) {
  const auto& forward = kinetics.forward(reactionIndex);
  ExponentDiagnostics diagnostics;
  diagnostics.mostNegative = std::numeric_limits<Real>::max();
  diagnostics.mostPositive = -std::numeric_limits<Real>::max();
  for (Index cell = 0; cell < temperature.size(); ++cell) {
    const Real x = forward.exponentialArgument(temperature[cell]);
    if (x < diagnostics.mostNegative) {
      diagnostics.mostNegative = x;
      diagnostics.worstCell = cell;
    }
    diagnostics.mostPositive = std::max(diagnostics.mostPositive, x);
    if (x > 0.0) {
      // Would mean exp(+Ea/RT): a sign error. With Ea >= 0 and T > 0 it cannot happen, so this
      // reports a structural impossibility -- which is the point of asserting it.
      diagnostics.anyPositive = true;
    }
  }
  return diagnostics;
}

}  // namespace cfd::species
