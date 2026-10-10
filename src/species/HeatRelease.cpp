#include "cfd/species/HeatRelease.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
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

}  // namespace

// ===================================================================
// FormationEnthalpies
// ===================================================================

FormationEnthalpies::FormationEnthalpies(SpeciesRegistry registry, std::vector<Real> massSpecific)
    : registry_(std::move(registry)), massSpecific_(std::move(massSpecific)) {
  if (massSpecific_.size() != registry_.size()) {
    throw InvalidArgumentError("FormationEnthalpies: " + std::to_string(massSpecific_.size()) +
                               " formation enthalpies for " + std::to_string(registry_.size()) +
                               " species; one per species is required");
  }
  for (Index i = 0; i < static_cast<Index>(massSpecific_.size()); ++i) {
    if (!std::isfinite(massSpecific_[i])) {
      throw InvalidArgumentError("FormationEnthalpies: h_f for species \"" + registry_.name(i) +
                                 "\" is " + exact(massSpecific_[i]) + "; must be finite");
    }
  }
  // NO SIGN OR MAGNITUDE CHECK. h_f = 0 is correct for an element in its reference state, and both
  // signs are ordinary. Nothing here is clamped.
}

FormationEnthalpies FormationEnthalpies::fromMolar(const MixtureThermodynamics& mixture,
                                                   const std::vector<Real>& molar) {
  const Index n = mixture.speciesCount();
  if (molar.size() != static_cast<std::size_t>(n)) {
    throw InvalidArgumentError("FormationEnthalpies::fromMolar: " + std::to_string(molar.size()) +
                               " molar values for " + std::to_string(n) + " species");
  }
  std::vector<Real> massSpecific(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) {
    if (!std::isfinite(molar[i])) {
      throw InvalidArgumentError("FormationEnthalpies::fromMolar: hbar_f for species \"" +
                                 mixture.registry().name(i) + "\" is " + exact(molar[i]) +
                                 "; must be finite");
    }
    // M_i READ from P18.3, never stored here.
    massSpecific[i] = molar[i] / mixture.species(i).molecularWeight();
  }
  return FormationEnthalpies(mixture.registry(), std::move(massSpecific));
}

Real FormationEnthalpies::massSpecific(Index speciesIndex) const {
  if (speciesIndex < 0 || speciesIndex >= static_cast<Index>(massSpecific_.size())) {
    throw InvalidArgumentError("FormationEnthalpies: species index " +
                               std::to_string(speciesIndex) + " is out of range for " +
                               std::to_string(massSpecific_.size()) + " species");
  }
  return massSpecific_[speciesIndex];
}

Real FormationEnthalpies::molar(const MixtureThermodynamics& mixture, Index speciesIndex) const {
  if (mixture.speciesCount() != speciesCount()) {
    throw InvalidArgumentError(
        "FormationEnthalpies::molar: the mixture holds " + std::to_string(mixture.speciesCount()) +
        " species but these formation enthalpies hold " + std::to_string(speciesCount()));
  }
  return massSpecific(speciesIndex) * mixture.species(speciesIndex).molecularWeight();
}

// ===================================================================
// Reaction enthalpy
// ===================================================================

namespace {

void requireMatchingRegistries(const ReactionMechanism& mechanism,
                               const MixtureThermodynamics& mixture,
                               const FormationEnthalpies& formation, const std::string& where) {
  if (mechanism.speciesCount() != mixture.speciesCount() ||
      mechanism.speciesCount() != formation.speciesCount()) {
    throw InvalidArgumentError(where + ": species counts disagree -- mechanism " +
                               std::to_string(mechanism.speciesCount()) + ", mixture " +
                               std::to_string(mixture.speciesCount()) + ", formation enthalpies " +
                               std::to_string(formation.speciesCount()));
  }
}

}  // namespace

Real standardReactionEnthalpy(const Reaction& reaction, const MixtureThermodynamics& mixture,
                              const FormationEnthalpies& formation) {
  if (reaction.speciesCount() != mixture.speciesCount() ||
      reaction.speciesCount() != formation.speciesCount()) {
    throw InvalidArgumentError("standardReactionEnthalpy: species counts disagree for reaction \"" +
                               reaction.name() + "\"");
  }
  // Delta H_r^0 = sum_i nu_i M_i h_f,i, accumulated in ASCENDING SPECIES INDEX so repeated
  // evaluation is bitwise identical.
  Real total = 0.0;
  for (Index i = 0; i < reaction.speciesCount(); ++i) {
    const Real nu = reaction.netStoichiometry(i);
    if (nu == 0.0) {
      continue;  // an uninvolved species or a catalyst contributes exactly nothing
    }
    total += nu * formation.molar(mixture, i);
  }
  if (!std::isfinite(total)) {
    throw NumericalError("standardReactionEnthalpy: Delta H_r for reaction \"" + reaction.name() +
                         "\" is " + exact(total) + "; must be finite");
  }
  return total;
}

Real reactionEnthalpy(const Reaction& reaction, const MixtureThermodynamics& mixture,
                      const FormationEnthalpies& formation, Real temperature) {
  // Delta H_r(T) = sum_i nu_i M_i [ h_f,i + h_i(T) ].
  //
  // NOT THE SOURCE TERM -- see the header. h_i(T) is P18.3's integrated SENSIBLE enthalpy, which is
  // P15's; it is never evaluated as cp_i * T.
  if (!std::isfinite(temperature)) {
    throw InvalidArgumentError("reactionEnthalpy: temperature is " + exact(temperature) +
                               "; must be finite");
  }
  Real total = 0.0;
  for (Index i = 0; i < reaction.speciesCount(); ++i) {
    const Real nu = reaction.netStoichiometry(i);
    if (nu == 0.0) {
      continue;
    }
    const auto& species = mixture.species(i);
    const Real molarTotal =
        species.molecularWeight() * (formation.massSpecific(i) + species.enthalpy(temperature));
    total += nu * molarTotal;
  }
  if (!std::isfinite(total)) {
    throw NumericalError("reactionEnthalpy: Delta H_r for reaction \"" + reaction.name() +
                         "\" at T = " + exact(temperature) + " is " + exact(total));
  }
  return total;
}

Real reactionHeatCapacity(const Reaction& reaction, const MixtureThermodynamics& mixture,
                          Real temperature) {
  // Delta Cp_r(T) = sum_i nu_i M_i cp_i(T). Kirchhoff's d(Delta H_r)/dT.
  if (!std::isfinite(temperature)) {
    throw InvalidArgumentError("reactionHeatCapacity: temperature is " + exact(temperature) +
                               "; must be finite");
  }
  Real total = 0.0;
  for (Index i = 0; i < reaction.speciesCount(); ++i) {
    const Real nu = reaction.netStoichiometry(i);
    if (nu == 0.0) {
      continue;
    }
    const auto& species = mixture.species(i);
    total += nu * species.molecularWeight() * species.specificHeatPressure(temperature);
  }
  if (!std::isfinite(total)) {
    throw NumericalError("reactionHeatCapacity: Delta Cp_r for reaction \"" + reaction.name() +
                         "\" at T = " + exact(temperature) + " is " + exact(total));
  }
  return total;
}

// ===================================================================
// The energy source
// ===================================================================

cfd::fields::ScalarField reactionHeatRelease(const ReactionMechanism& mechanism,
                                             const MixtureThermodynamics& mixture,
                                             const FormationEnthalpies& formation,
                                             const ReactionProgressRates& rates, Index cellCount) {
  requireMatchingRegistries(mechanism, mixture, formation, "reactionHeatRelease");
  if (rates.perReaction.size() != static_cast<std::size_t>(mechanism.reactionCount())) {
    throw InvalidArgumentError("reactionHeatRelease: " + std::to_string(rates.perReaction.size()) +
                               " progress-rate fields for " +
                               std::to_string(mechanism.reactionCount()) + " reactions");
  }

  cfd::fields::ScalarField source(cellCount, 0.0);

  // S_E = - sum_r q_r Delta H_r^0, accumulated in ASCENDING REACTION INDEX then ascending cell, so
  // repeated evaluation is bitwise identical and multi-reaction superposition is exact addition.
  for (Index r = 0; r < mechanism.reactionCount(); ++r) {
    const Reaction& reaction = mechanism.reaction(r);
    if (rates.perReaction[r].size() != cellCount) {
      throw InvalidArgumentError("reactionHeatRelease: the progress-rate field for reaction \"" +
                                 reaction.name() + "\" holds " +
                                 std::to_string(rates.perReaction[r].size()) + " cells, expected " +
                                 std::to_string(cellCount));
    }
    // ONE Delta H_r^0 per reaction -- temperature-independent, so it is hoisted out of the cell
    // loop rather than recomputed per cell. That is not an optimisation shortcut: the quantity
    // genuinely does not depend on the cell.
    const Real deltaH = standardReactionEnthalpy(reaction, mixture, formation);
    for (Index cell = 0; cell < cellCount; ++cell) {
      const Real q = rates.perReaction[r][cell];
      if (!std::isfinite(q)) {
        throw InvalidArgumentError("reactionHeatRelease: progress rate for reaction \"" +
                                   reaction.name() + "\" at cell " + std::to_string(cell) + " is " +
                                   exact(q) + "; must be finite");
      }
      source[cell] -= q * deltaH;
    }
  }

  for (Index cell = 0; cell < cellCount; ++cell) {
    if (!std::isfinite(source[cell])) {
      throw NumericalError("reactionHeatRelease: S_E at cell " + std::to_string(cell) + " is " +
                           exact(source[cell]) + " W/m^3; must be finite");
    }
  }
  return source;
}

cfd::fields::ScalarField reactionHeatReleaseFromSpeciesSources(const FormationEnthalpies& formation,
                                                               const SpeciesSources& sources,
                                                               Index cellCount) {
  // S_E = - sum_i h_f,i omega_i -- G7's own formula, verbatim, and an INDEPENDENT route to the
  // same number as reactionHeatRelease(). The two share no arithmetic beyond h_f,i.
  if (sources.perSpecies.size() != static_cast<std::size_t>(formation.speciesCount())) {
    throw InvalidArgumentError(
        "reactionHeatReleaseFromSpeciesSources: " + std::to_string(sources.perSpecies.size()) +
        " species source fields for " + std::to_string(formation.speciesCount()) + " species");
  }

  cfd::fields::ScalarField source(cellCount, 0.0);
  for (Index i = 0; i < formation.speciesCount(); ++i) {
    const auto& su = sources.perSpecies[i].Su;
    if (su.size() != cellCount) {
      throw InvalidArgumentError("reactionHeatReleaseFromSpeciesSources: the source field for \"" +
                                 formation.registry().name(i) + "\" holds " +
                                 std::to_string(su.size()) + " cells, expected " +
                                 std::to_string(cellCount));
    }
    const Real hf = formation.massSpecific(i);
    for (Index cell = 0; cell < cellCount; ++cell) {
      if (!std::isfinite(su[cell])) {
        throw InvalidArgumentError("reactionHeatReleaseFromSpeciesSources: omega for \"" +
                                   formation.registry().name(i) + "\" at cell " +
                                   std::to_string(cell) + " is " + exact(su[cell]));
      }
      source[cell] -= hf * su[cell];
    }
  }
  for (Index cell = 0; cell < cellCount; ++cell) {
    if (!std::isfinite(source[cell])) {
      throw NumericalError("reactionHeatReleaseFromSpeciesSources: S_E at cell " +
                           std::to_string(cell) + " is " + exact(source[cell]));
    }
  }
  return source;
}

// ===================================================================
// Energy accounting
// ===================================================================

EnergyDensity energyDensity(const MixtureThermodynamics& mixture,
                            const FormationEnthalpies& formation,
                            const std::vector<Real>& composition, Real density, Real temperature) {
  if (!std::isfinite(density) || density <= 0.0) {
    throw InvalidArgumentError("energyDensity: density is " + exact(density) +
                               "; must be finite and > 0");
  }
  if (!std::isfinite(temperature) || temperature <= 0.0) {
    throw InvalidArgumentError("energyDensity: temperature is " + exact(temperature) +
                               "; must be finite and > 0 K. No temperature floor is applied.");
  }
  if (composition.size() != static_cast<std::size_t>(formation.speciesCount())) {
    throw InvalidArgumentError("energyDensity: composition holds " +
                               std::to_string(composition.size()) + " values for " +
                               std::to_string(formation.speciesCount()) + " species");
  }

  EnergyDensity out;
  // SENSIBLE: rho cp(T,Y) T -- exactly the quantity EnergyEquation transports. cp_mix comes from
  // P18.3, never recomputed here.
  out.sensible = density * mixture.specificHeatPressure(composition, temperature) * temperature;
  // CHEMICAL: rho sum_i Y_i h_f,i, in ascending species index.
  Real chemical = 0.0;
  for (Index i = 0; i < formation.speciesCount(); ++i) {
    chemical += composition[static_cast<std::size_t>(i)] * formation.massSpecific(i);
  }
  out.chemical = density * chemical;
  out.total = out.sensible + out.chemical;
  if (!std::isfinite(out.total)) {
    throw NumericalError("energyDensity: total energy density is " + exact(out.total));
  }
  return out;
}

TotalEnergy totalEnergy(const MixtureThermodynamics& mixture, const FormationEnthalpies& formation,
                        const MassFractionState& state, const cfd::fields::ScalarField& density,
                        const cfd::fields::ScalarField& temperature,
                        const cfd::fields::ScalarField& cellVolume) {
  const Index cells = state.cellCount();
  if (density.size() != cells || temperature.size() != cells || cellVolume.size() != cells) {
    throw InvalidArgumentError("totalEnergy: field sizes disagree with the state's " +
                               std::to_string(cells) + " cells");
  }
  TotalEnergy out;
  out.cellCount = cells;
  // ASCENDING CELL, so repeated evaluation is bitwise identical.
  for (Index cell = 0; cell < cells; ++cell) {
    const auto density_ = energyDensity(mixture, formation, state.cellComposition(cell),
                                        density[cell], temperature[cell]);
    const Real volume = cellVolume[cell];
    if (!std::isfinite(volume) || volume <= 0.0) {
      throw InvalidArgumentError("totalEnergy: cell volume at cell " + std::to_string(cell) +
                                 " is " + exact(volume) + "; must be finite and > 0");
    }
    out.sensible += density_.sensible * volume;
    out.chemical += density_.chemical * volume;
  }
  out.total = out.sensible + out.chemical;
  return out;
}

Real temperatureFromSensibleEnergy(const MixtureThermodynamics& mixture,
                                   const std::vector<Real>& composition, Real density,
                                   Real sensibleEnergyDensity) {
  if (!std::isfinite(density) || density <= 0.0) {
    throw InvalidArgumentError("temperatureFromSensibleEnergy: density is " + exact(density) +
                               "; must be finite and > 0");
  }
  if (!std::isfinite(sensibleEnergyDensity)) {
    throw InvalidArgumentError("temperatureFromSensibleEnergy: sensible energy density is " +
                               exact(sensibleEnergyDensity) + "; must be finite");
  }
  if (sensibleEnergyDensity <= 0.0) {
    // rho cp T > 0 for every positive T and positive cp, so a non-positive target has no root.
    // REFUSED rather than floored (authorization section 42).
    throw NumericalError(
        "temperatureFromSensibleEnergy: the target sensible energy density is " +
        exact(sensibleEnergyDensity) +
        " J/m^3, which is not positive; rho cp T > 0 for every valid state, so no temperature "
        "satisfies it. This usually means an energy update removed more energy than the state "
        "held. No temperature floor is applied.");
  }

  // f(T) = rho cp(T,Y) T - u_sens is strictly increasing wherever cp > 0, so a bracketing
  // bisection converges. cp(T,Y) is P18.3's; this function supplies no thermodynamics of its own.
  auto residual = [&](Real t) {
    return (density * mixture.specificHeatPressure(composition, t) * t) - sensibleEnergyDensity;
  };

  // THE BRACKET IS THE cp MODELS OWN COMMON VALIDITY RANGE, not an invented interval.
  //
  // The first version started the search at T = 1 K and expanded upward. That threw for any
  // RANGED cp polynomial -- TemperatureDependentProperty refuses to extrapolate a fitted property,
  // correctly -- so the recovery worked only for a constant cp. The failure is preserved at
  // logs/03_four_failures_PRESERVED.log.
  //
  // Using the intersection of the species validity ranges means a root outside it is reported as
  // UN-BRACKETED rather than found by extrapolating a polynomial past its fit, which for a fitted
  // cp can go negative and makes the inversion ill-posed.
  Real low = 0.0;
  Real high = std::numeric_limits<Real>::infinity();
  for (Index i = 0; i < mixture.speciesCount(); ++i) {
    const auto& model = mixture.species(i).enthalpyModel().specificHeatModel();
    low = std::max(low, model.minimumTemperature());
    high = std::min(high, model.maximumTemperature());
  }
  if (!(low < high)) {
    throw NumericalError(
        "temperatureFromSensibleEnergy: the species cp validity ranges do not overlap -- the "
        "common range is [" +
        exact(low) + ", " + exact(high) + "] K, so no temperature is valid for every species");
  }
  // A constant cp is valid everywhere, which comes back as (0, inf); clamp the SEARCH WINDOW (not
  // the answer) to a finite interval so the bisection terminates.
  if (!std::isfinite(high)) {
    high = 1.0e5;
  }
  if (low <= 0.0) {
    low = 1.0e-6;  // rho cp T -> 0 as T -> 0, so the root is above this for any positive target
  }

  const Real residualLow = residual(low);
  const Real residualHigh = residual(high);
  if (residualLow > 0.0) {
    throw NumericalError(
        "temperatureFromSensibleEnergy: the target sensible energy density " +
        exact(sensibleEnergyDensity) + " J/m^3 is BELOW what the lowest valid temperature " +
        exact(low) + " K gives (" + exact(residualLow + sensibleEnergyDensity) +
        " J/m^3). The root is outside the cp validity range and is NOT extrapolated.");
  }
  if (residualHigh < 0.0) {
    throw NumericalError(
        "temperatureFromSensibleEnergy: the target sensible energy density " +
        exact(sensibleEnergyDensity) + " J/m^3 is ABOVE what the highest valid temperature " +
        exact(high) + " K gives (" + exact(residualHigh + sensibleEnergyDensity) +
        " J/m^3). The root is outside the cp validity range and is NOT extrapolated.");
  }

  // Bisection to the floating-point limit: 200 halvings is far beyond the ~60 a double needs, and
  // the loop exits on the interval collapsing rather than on an iteration count being reached.
  for (int iteration = 0; iteration < 200; ++iteration) {
    const Real mid = 0.5 * (low + high);
    if (mid <= low || mid >= high) {
      break;  // the interval is one ulp wide; no further progress is representable
    }
    if (residual(mid) < 0.0) {
      low = mid;
    } else {
      high = mid;
    }
  }
  const Real root = 0.5 * (low + high);
  if (!std::isfinite(root) || root <= 0.0) {
    throw NumericalError("temperatureFromSensibleEnergy: the recovered temperature is " +
                         exact(root) + " K; must be finite and > 0");
  }
  return root;
}

// ===================================================================
// The coupled step
// ===================================================================

ReactorState advanceClosedReactor(const ReactionMechanism& mechanism,
                                  const MixtureThermodynamics& mixture,
                                  const FormationEnthalpies& formation,
                                  const ReactionProgressRates& rates, const ReactorState& state,
                                  Real dt, std::vector<std::vector<Real>>* roundOffCarry) {
  requireMatchingRegistries(mechanism, mixture, formation, "advanceClosedReactor");
  if (!std::isfinite(dt) || dt <= 0.0) {
    throw InvalidArgumentError("advanceClosedReactor: dt is " + exact(dt) +
                               "; must be finite and > 0");
  }
  const Index cells = state.composition.cellCount();
  const Index species = state.composition.speciesCount();
  if (state.temperature.size() != cells || state.density.size() != cells) {
    throw InvalidArgumentError("advanceClosedReactor: field sizes disagree with the state's " +
                               std::to_string(cells) + " cells");
  }

  // STEP 3 -- omega_i from the SUPPLIED q_r. P18.4's, not reimplemented.
  const SpeciesSources sources = reactionSpeciesSources(mechanism, mixture, rates, cells);
  // STEP 4 -- S_E from THE SAME q_r (authorization section 30).
  const cfd::fields::ScalarField heat =
      reactionHeatRelease(mechanism, mixture, formation, rates, cells);

  ReactorState next = state;

  // TD-002: a carry the caller did not size is sized and zeroed here, so a caller may hand in a
  // fresh vector. Zero is the identity -- see the header on why a zero carry reproduces the
  // pre-TD-002 arithmetic bitwise.
  if (roundOffCarry != nullptr) {
    if (roundOffCarry->size() != static_cast<std::size_t>(cells)) {
      roundOffCarry->assign(static_cast<std::size_t>(cells),
                            std::vector<Real>(static_cast<std::size_t>(species), 0.0));
    }
    for (std::vector<Real>& perCell : *roundOffCarry) {
      if (perCell.size() != static_cast<std::size_t>(species)) {
        perCell.assign(static_cast<std::size_t>(species), 0.0);
      }
    }
  }

  for (Index cell = 0; cell < cells; ++cell) {
    const Real rho = state.density[cell];
    const Real temperature = state.temperature[cell];
    const auto composition = state.composition.cellComposition(cell);

    // STEP 5 -- advance species explicitly. CONSTANT VOLUME AND CLOSED, so rho does not change:
    // sum_i omega_i = 0 (P18.4), so the total mass in the cell is unchanged by reaction.
    //
    // THE INCREMENT IS APPLIED IN MASS-FRACTION SPACE, Y_i + dt omega_i / rho, NOT by forming
    // rho Y_i, adding dt omega_i and dividing back. That round trip is not exact, and over 200
    // steps it drifted sum_i Y_i to 0.99999999999999722 -- past the 4 eps N tolerance P18.1
    // enforces, which refused the state. Preserved at logs/03_four_failures_PRESERVED.log.
    //
    // The additive form keeps the invariant because sum_i (dt omega_i / rho) = 0 whenever
    // sum_i omega_i = 0, which P18.4 establishes -- so the sum-to-one error does not accumulate
    // through a multiply-divide pair that has nothing to do with the chemistry.
    //
    // AND A ZERO INCREMENT IS AN EXACT NO-OP, which the round trip was not: Y + 0.0 == Y bitwise.
    // Section 34 requires that, and it is the same principle the P15 transient term states --
    // "no zero-valued term is added, the code is not executed at all".
    std::vector<Real> updated(static_cast<std::size_t>(species));
    bool compositionMoved = false;
    for (Index i = 0; i < species; ++i) {
      const Real omega = sources.perSpecies[i].Su[cell];
      const Real increment = (dt * omega) / rho;
      const Real partialDensity = rho * (composition[static_cast<std::size_t>(i)] + increment);
      if (partialDensity < 0.0) {
        // REFUSED, NOT CLAMPED (authorization section 41). There is no chemistry time integrator
        // in P18.6's scope, so a step this large is an invalid step rather than something to
        // normalise away -- and silently clamping then renormalising would destroy the
        // conservation this phase exists to prove.
        throw NumericalError(
            "advanceClosedReactor: the explicit step would drive rho*Y negative for species \"" +
            mechanism.registry().name(i) + "\" at cell " + std::to_string(cell) +
            ": rho*Y = " + exact(rho * composition[static_cast<std::size_t>(i)]) +
            " kg/m^3, dt*omega = " + exact(dt * omega) + " kg/m^3, sum = " + exact(partialDensity) +
            ". dt is too large for this rate. P18.6 has no stiff-chemistry integrator and does "
            "not clamp or renormalise species.");
      }
      // TD-002: compensated store when a carry is supplied, plain store when it is not.
      //
      // With `carry == 0` the two branches are the SAME arithmetic: `y = increment - 0` is exactly
      // `increment`, so `Y + y` is exactly `Y + increment`. That identity is what keeps a single
      // step -- and therefore P18.6's qualified step, P18.8's device restatement and TD-001's
      // production dispatch -- bitwise unchanged. The branches diverge only once a previous step
      // has left something in the carry.
      if (roundOffCarry != nullptr) {
        Real& carry = (*roundOffCarry)[static_cast<std::size_t>(cell)][static_cast<std::size_t>(i)];
        const Real y = increment - carry;
        const Real stored = composition[static_cast<std::size_t>(i)] + y;
        carry = (stored - composition[static_cast<std::size_t>(i)]) - y;
        updated[static_cast<std::size_t>(i)] = stored;
      } else {
        updated[static_cast<std::size_t>(i)] = composition[static_cast<std::size_t>(i)] + increment;
      }
      if (increment != 0.0) {
        compositionMoved = true;
      }
    }
    // P18.1 VALIDATES THIS -- bounds and sum-to-one are its invariant, not re-checked here.
    if (compositionMoved) {
      next.composition.setCellComposition(cell, updated);
    }

    // STEP 6 -- advance the sensible energy. u_sens = rho cp(T,Y) T, the transported quantity.
    const Real sensibleOld =
        rho * mixture.specificHeatPressure(composition, temperature) * temperature;
    const Real sensibleNew = sensibleOld + (dt * heat[cell]);
    if (!std::isfinite(sensibleNew)) {
      throw NumericalError("advanceClosedReactor: the updated sensible energy density at cell " +
                           std::to_string(cell) + " is " + exact(sensibleNew) + " J/m^3");
    }

    // STEP 7 -- recover T from the UPDATED composition (authorization section 17). Passing
    // `composition` here instead of `updated` is control N4, and it would be wrong.
    //
    // NOTHING TO RECOVER IF NOTHING MOVED. When the source is exactly zero AND the composition is
    // bitwise unchanged, rho cp(T,Y) T is bitwise unchanged, so T is unchanged -- and running the
    // bisection anyway returns a value a fraction of an ulp away. Section 34 requires the
    // non-reacting limit to be BITWISE, not merely close, so the no-op is taken explicitly rather
    // than hoped for from a root find.
    if (!compositionMoved && heat[cell] == 0.0) {
      next.temperature[cell] = temperature;
    } else {
      next.temperature[cell] = temperatureFromSensibleEnergy(
          mixture, next.composition.cellComposition(cell), rho, sensibleNew);
    }
    // rho is unchanged: closed, constant volume, and sum_i omega_i = 0.
    next.density[cell] = rho;
  }

  return next;
}

}  // namespace cfd::species
