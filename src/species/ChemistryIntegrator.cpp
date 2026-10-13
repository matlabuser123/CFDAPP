// G8 closure, step 3 -- the chemistry integrator. See ChemistryIntegrator.hpp for why this exists
// and what it deliberately does not do.
#include "cfd/species/ChemistryIntegrator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

#include "cfd/core/Exception.hpp"

namespace cfd::species {
namespace {

// Formats a Real with enough digits to round-trip, so a diagnostic names the value that actually
// failed rather than a rounded version of it.
std::string exact(Real value) {
  std::ostringstream out;
  out.precision(17);
  out << value;
  return out.str();
}

}  // namespace

ChemicalTimeScale chemicalTimeScale(const ReactionMechanism& mechanism,
                                    const MixtureThermodynamics& mixture,
                                    const ReactionProgressRates& rates, const ReactorState& state) {
  const Index cells = state.temperature.size();
  const SpeciesSources sources = reactionSpeciesSources(mechanism, mixture, rates, cells);

  ChemicalTimeScale scale;
  scale.smallest = std::numeric_limits<Real>::infinity();
  scale.cell = 0;
  scale.species = 0;
  scale.anyReaction = false;

  // tau_i = rho Y_i / |omega_i|: the time for the CURRENT source to consume what is present. The
  // minimum over cells and species is what bounds an explicit step.
  //
  // A species with omega_i == 0 imposes no restriction and is skipped -- not given a huge finite
  // tau, which would make the "nothing is reacting" case indistinguishable from "everything is
  // slow".
  for (Index i = 0; i < mechanism.speciesCount(); ++i) {
    for (Index cell = 0; cell < cells; ++cell) {
      const Real omega = sources.perSpecies[i].Su[cell];
      if (omega == 0.0) {
        continue;
      }
      scale.anyReaction = true;
      const Real mass = state.density[cell] * state.composition.massFraction(i, cell);
      // A species at exactly zero with a CONSUMING source would give tau = 0 and stall the march.
      // That state is already invalid -- the qualified step refuses it -- so it is left for the
      // step to reject rather than silently turned into a zero time scale here.
      if (mass == 0.0) {
        continue;
      }
      const Real tau = mass / std::abs(omega);
      if (tau < scale.smallest) {
        scale.smallest = tau;
        scale.cell = cell;
        scale.species = i;
      }
    }
  }
  return scale;
}

ChemistryIntegratorResult integrateChemistry(const ReactionMechanism& mechanism,
                                             const MixtureThermodynamics& mixture,
                                             const FormationEnthalpies& formation,
                                             const ReactionKinetics& kinetics,
                                             const ReactorState& state, Real dt,
                                             const ChemistryIntegratorSettings& settings) {
  if (!std::isfinite(dt) || !(dt > 0.0)) {
    throw InvalidArgumentError("integrateChemistry: dt is " + exact(dt) +
                               "; must be finite and > 0");
  }
  if (!std::isfinite(settings.maximumFractionalChange) ||
      !(settings.maximumFractionalChange > 0.0) || !(settings.maximumFractionalChange <= 1.0)) {
    throw InvalidArgumentError("integrateChemistry: maximumFractionalChange is " +
                               exact(settings.maximumFractionalChange) + "; must be within (0, 1]");
  }
  if (settings.maximumSubSteps == 0) {
    throw InvalidArgumentError("integrateChemistry: maximumSubSteps is 0");
  }
  if (settings.maximumHalvings == 0) {
    throw InvalidArgumentError("integrateChemistry: maximumHalvings is 0");
  }

  // Designated initialisation, because ReactorState has no default constructor -- MassFractionState
  // has none, since there is no meaningful empty composition. Every other member has a default
  // member initialiser.
  ChemistryIntegratorResult result{.state = state};
  result.smallestSubStep = std::numeric_limits<Real>::infinity();
  result.largestSubStep = 0.0;

  Real remaining = dt;

  // TD-002: the carry lives for the whole march and is handed to every sub-step. Empty when
  // compensation is off, and `advanceClosedReactor` then takes its pre-TD-002 path -- which is what
  // makes negative control N1 a settings flip rather than a separate build.
  std::vector<std::vector<Real>> carry;
  std::vector<std::vector<Real>>* carryPointer = nullptr;
  if (settings.compensateRoundOff) {
    carry.assign(static_cast<std::size_t>(state.temperature.size()),
                 std::vector<Real>(static_cast<std::size_t>(mechanism.speciesCount()), 0.0));
    carryPointer = &carry;
  }

  const auto recordSumDeviation = [&result](const ReactorState& s) {
    for (Index cell = 0; cell < s.temperature.size(); ++cell) {
      Real sum = 0.0;
      for (Index i = 0; i < s.composition.speciesCount(); ++i) {
        sum += s.composition.massFraction(i, cell);
      }
      result.worstSumDeviation = std::max(result.worstSumDeviation, std::abs(sum - 1.0));
    }
  };
  recordSumDeviation(result.state);

  while (remaining > 0.0) {
    if (result.subSteps >= settings.maximumSubSteps) {
      throw NumericalError(
          "integrateChemistry: exhausted the sub-step budget of " +
          std::to_string(settings.maximumSubSteps) + " with " + exact(remaining) + " s of " +
          exact(dt) +
          " still to integrate. The state is NOT returned partially advanced -- a caller that "
          "believed it had advanced by the full dt would corrupt every conservation balance "
          "downstream. Either the chemistry is stiffer than an explicit march can carry here, or "
          "the requested dt is too large for it.");
    }

    // RATES FROM THE CURRENT STATE, every sub-step. Reusing them across sub-steps is the stale-rate
    // defect controls N7/N8 exist to catch, and it would make the march first-order-wrong rather
    // than merely inaccurate.
    const ReactionProgressRates rates =
        arrheniusProgressRates(mechanism, kinetics, mixture, result.state.composition,
                               result.state.density, result.state.temperature);

    const ChemicalTimeScale scale = chemicalTimeScale(mechanism, mixture, rates, result.state);

    // The step the chemistry allows. With nothing reacting there is no chemical restriction, so the
    // whole remaining time goes in one step -- which is what makes the non-reacting limit cost one
    // sub-step rather than thousands.
    Real step = remaining;
    SubStepLimit limit = SubStepLimit::TargetReached;
    if (scale.anyReaction && std::isfinite(scale.smallest)) {
      const Real allowed = settings.maximumFractionalChange * scale.smallest;
      if (allowed < step) {
        step = allowed;
        limit = SubStepLimit::ChemicalScale;
      }
    }

    // Take it, halving on refusal. A refusal is the qualified step judging the resulting state
    // invalid; the response is a smaller step, NEVER a repaired state.
    std::size_t halvings = 0;
    bool accepted = false;
    while (!accepted) {
      try {
        ReactorState next = advanceClosedReactor(mechanism, mixture, formation, rates, result.state,
                                                 step, carryPointer);
        // worst fractional change actually achieved, for reporting and for the refinement study
        for (Index i = 0; i < mechanism.speciesCount(); ++i) {
          for (Index cell = 0; cell < result.state.temperature.size(); ++cell) {
            const Real before = result.state.composition.massFraction(i, cell);
            if (before <= 0.0) {
              continue;
            }
            const Real change = std::abs(next.composition.massFraction(i, cell) - before) / before;
            result.worstFractionalChange = std::max(result.worstFractionalChange, change);
          }
        }
        result.state = std::move(next);
        recordSumDeviation(result.state);
        accepted = true;
      } catch (const NumericalError&) {
        // NumericalError, NOT InvalidArgumentError. advanceClosedReactor refuses an invalid
        // resulting state with NumericalError; an InvalidArgumentError from it would mean the
        // INPUTS are wrong, which a smaller step cannot fix and which must propagate.
        //
        // The first version of this caught InvalidArgumentError, so the retry branch was
        // unreachable and a genuine refusal would have escaped instead of being halved.
        // THE CARRY MUST BE ROLLED BACK ON A REFUSAL. advanceClosedReactor mutates it as it
        // stores each species, and a refusal can happen partway through a cell -- so the carry may
        // hold bits belonging to a step that was never accepted. Applying those to the retried
        // (smaller) step would corrupt the very invariant the carry exists to protect. Zeroing is
        // the correct rollback rather than saving and restoring: zero is the identity, so the
        // retried step behaves exactly as a fresh march would, losing only the compensation earned
        // before the refusal.
        if (carryPointer != nullptr) {
          for (std::vector<Real>& perCell : carry) {
            std::fill(perCell.begin(), perCell.end(), 0.0);
          }
        }
        ++result.refusals;
        ++halvings;
        if (halvings > settings.maximumHalvings) {
          throw NumericalError(
              "integrateChemistry: a sub-step of " + exact(step) + " s was still refused after " +
              std::to_string(settings.maximumHalvings) + " halvings, with " + exact(remaining) +
              " s of " + exact(dt) + " remaining. The smallest chemical time scale was " +
              exact(scale.smallest) + " s in cell " + std::to_string(scale.cell) + " for species " +
              std::to_string(scale.species) +
              ". Repeated refusal at a vanishing step size is not a step-size problem.");
        }
        step *= 0.5;
        limit = SubStepLimit::Refusal;
      }
    }

    result.smallestSubStep = std::min(result.smallestSubStep, step);
    result.largestSubStep = std::max(result.largestSubStep, step);
    result.lastLimit = limit;
    ++result.subSteps;

    // Subtracting in this order keeps the sum of the sub-steps equal to dt: the final sub-step is
    // whatever remains, so the caller's dt is honoured exactly rather than approached.
    remaining -= step;
    if (remaining < 0.0) {
      remaining = 0.0;
    }
  }

  if (!std::isfinite(result.smallestSubStep)) {
    result.smallestSubStep = 0.0;
  }
  return result;
}

RawFieldsChemistryResult integrateChemistryFromFields(
    const ReactionMechanism& mechanism, const MixtureThermodynamics& mixture,
    const FormationEnthalpies& formation, const ReactionKinetics& kinetics,
    const SpeciesRegistry& registry, const std::vector<cfd::fields::ScalarField>& composition,
    const cfd::fields::ScalarField& temperature, const cfd::fields::ScalarField& density, Real dt,
    const ChemistryIntegratorSettings& settings) {
  const Index species = mechanism.speciesCount();
  if (static_cast<Index>(composition.size()) != species) {
    throw InvalidArgumentError(
        "integrateChemistryFromFields: " + std::to_string(composition.size()) +
        " composition fields for " + std::to_string(species) + " species");
  }
  const Index cells = temperature.size();
  if (density.size() != cells) {
    throw InvalidArgumentError("integrateChemistryFromFields: density has " +
                               std::to_string(density.size()) + " cells, temperature has " +
                               std::to_string(cells));
  }
  for (Index i = 0; i < species; ++i) {
    if (composition[static_cast<std::size_t>(i)].size() != cells) {
      throw InvalidArgumentError("integrateChemistryFromFields: composition field " +
                                 std::to_string(i) + " has " +
                                 std::to_string(composition[static_cast<std::size_t>(i)].size()) +
                                 " cells, temperature has " + std::to_string(cells));
    }
  }

  RawFieldsChemistryResult out;
  out.composition = composition;
  out.temperature = temperature;
  out.minMassFraction = std::numeric_limits<Real>::infinity();

  // THE CLOSURE THE CALLER HANDED IN, measured before anything is done to it. This is half of gate
  // criterion C9: the output is compared against it to prove no normalisation happened.
  for (Index cell = 0; cell < cells; ++cell) {
    Real sum = 0.0;
    for (Index i = 0; i < species; ++i) sum += composition[static_cast<std::size_t>(i)][cell];
    out.worstSumDeviationIn = std::max(out.worstSumDeviationIn, std::abs(sum - 1.0));
  }

  // EACH CELL IS INTEGRATED ON ITS OWN, and that is what makes the transported closure survive.
  //
  // A cell's composition is shifted onto an exactly-closing working vector, integrated, and the
  // shift is added back afterwards. The shift is the cell's OWN deviation, spread over the species
  // in proportion to their mass -- so it is restored to the same place it came from, and the cell's
  // closure comes back as it went in (to within the march's own O(eps) error) rather than erased.
  //
  // Shifting is NOT normalising. Normalising would RESCALE the composition and discard the
  // deviation; this subtracts a known quantity, integrates, and adds that same quantity back. The
  // deviation is conserved, which is why C9's `>= 0.5 * d_in` clause passes for this and fails for
  // control N2.
  for (Index cell = 0; cell < cells; ++cell) {
    std::vector<Real> cellComposition(static_cast<std::size_t>(species));
    Real sum = 0.0;
    for (Index i = 0; i < species; ++i) {
      cellComposition[static_cast<std::size_t>(i)] = composition[static_cast<std::size_t>(i)][cell];
      sum += cellComposition[static_cast<std::size_t>(i)];
    }
    const Real deviation = sum - 1.0;

    std::vector<Real> shift(static_cast<std::size_t>(species), 0.0);
    if (deviation != 0.0 && sum != 0.0) {
      for (Index i = 0; i < species; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        shift[k] = deviation * (cellComposition[k] / sum);
        cellComposition[k] -= shift[k];
      }
    }

    // A composition that is still not a plausible working vector is REFUSED, not repaired: a
    // negative mass fraction or a non-finite one is a fault no step size can fix. The shifted
    // vector is handed to MassFractionState, which applies P18.1's own validation to it -- so the
    // invariant is enforced at the step boundary exactly as SpeciesTransport.hpp prescribes.
    MassFractionState state(registry, 1, cellComposition);
    cfd::fields::ScalarField cellTemperature(1, temperature[cell]);
    cfd::fields::ScalarField cellDensity(1, density[cell]);

    const ChemistryIntegratorResult marched =
        integrateChemistry(mechanism, mixture, formation, kinetics,
                           ReactorState{state, cellTemperature, cellDensity}, dt, settings);

    out.subSteps += marched.subSteps;
    out.refusals += marched.refusals;
    for (Index i = 0; i < species; ++i) {
      const std::size_t k = static_cast<std::size_t>(i);
      const Real value = marched.state.composition.massFraction(i, 0) + shift[k];
      out.composition[k][cell] = value;
      out.minMassFraction = std::min(out.minMassFraction, value);
    }
    out.temperature[cell] = marched.state.temperature[0];
  }

  for (Index cell = 0; cell < cells; ++cell) {
    Real sum = 0.0;
    for (Index i = 0; i < species; ++i) sum += out.composition[static_cast<std::size_t>(i)][cell];
    out.worstSumDeviationOut = std::max(out.worstSumDeviationOut, std::abs(sum - 1.0));
  }
  if (!std::isfinite(out.minMassFraction)) out.minMassFraction = 0.0;
  return out;
}

}  // namespace cfd::species
