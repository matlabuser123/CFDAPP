#include "cfd/species/ReactionMechanism.hpp"

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

// Accumulates one side's coefficients into a dense vector over the registry.
// Rejects an unknown species and any non-finite or negative coefficient, naming
// the reaction and the species -- never silently taking an absolute value.
void accumulateSide(const std::string& reactionName, const SpeciesRegistry& registry,
                    const std::vector<ReactionTerm>& terms, const char* sideName,
                    std::vector<Real>& out) {
  for (const ReactionTerm& term : terms) {
    if (!registry.contains(term.species)) {
      throw InvalidArgumentError("Reaction \"" + reactionName + "\": unknown species \"" +
                                 term.species + "\" among the " + sideName +
                                 "; reactions refer to the authoritative species registry and "
                                 "cannot introduce a species of their own");
    }
    if (!std::isfinite(term.coefficient)) {
      throw InvalidArgumentError("Reaction \"" + reactionName +
                                 "\": stoichiometric coefficient "
                                 "for \"" +
                                 term.species + "\" among the " + sideName + " is " +
                                 exact(term.coefficient) + "; must be finite");
    }
    if (term.coefficient < 0.0) {
      throw InvalidArgumentError(
          "Reaction \"" + reactionName + "\": stoichiometric coefficient for \"" + term.species +
          "\" among the " + sideName + " is " + exact(term.coefficient) +
          "; must be >= 0. A negative coefficient is not silently treated as its magnitude -- move "
          "the species to the other side of the reaction instead.");
    }
    // A species appearing twice on the same side is SUMMED: 2A + A is 3A.
    out[registry.index(term.species)] += term.coefficient;
  }
}

}  // namespace

// ===================================================================
// Reaction
// ===================================================================

Reaction::Reaction(std::string name, const SpeciesRegistry& registry,
                   const std::vector<ReactionTerm>& reactants,
                   const std::vector<ReactionTerm>& products, ReactionDirection direction,
                   const std::vector<ReactionTerm>& reactantOrders)
    : name_(std::move(name)),
      direction_(direction),
      net_(registry.size(), 0.0),
      reactant_(registry.size(), 0.0),
      product_(registry.size(), 0.0),
      reactantOrder_(registry.size(), 0.0),
      explicitOrders_(!reactantOrders.empty()) {
  if (name_.empty()) {
    throw InvalidArgumentError("Reaction: name is empty; reaction identity is user-visible");
  }
  if (reactants.empty()) {
    throw InvalidArgumentError("Reaction \"" + name_ + "\": no reactants");
  }
  if (products.empty()) {
    throw InvalidArgumentError("Reaction \"" + name_ + "\": no products");
  }

  accumulateSide(name_, registry, reactants, "reactants", reactant_);
  accumulateSide(name_, registry, products, "products", product_);

  // Canonicalisation: nu_i = nu''_i - nu'_i, computed ONCE, over ascending
  // species index. A species on both sides cancels here exactly once -- a
  // catalyst ends with nu_i = 0 and is never double-counted.
  bool anyNonZero = false;
  for (Index i = 0; i < net_.size(); ++i) {
    net_[i] = product_[i] - reactant_[i];
    if (net_[i] != 0.0) {
      anyNonZero = true;
    }
  }
  if (!anyNonZero) {
    throw InvalidArgumentError(
        "Reaction \"" + name_ +
        "\": every net stoichiometric coefficient is zero after cancellation; a reaction that "
        "changes no species is not a reaction (a pure catalyst has nothing to catalyse)");
  }

  // ---- the FORWARD reaction orders.
  //
  // Default: the orders ARE the reactant coefficients. Assigning the vector rather than branching
  // at every evaluation keeps the arithmetic identical for every pre-existing caller -- the
  // progress rate reads reactantOrder_ unconditionally and gets exactly the bytes it read before.
  if (!explicitOrders_) {
    reactantOrder_ = reactant_;
  } else {
    if (direction_ == ReactionDirection::Reversible) {
      throw InvalidArgumentError(
          "Reaction \"" + name_ +
          "\": explicit reaction orders were supplied for a REVERSIBLE reaction. The reverse order "
          "is not modelled, so the reverse side would silently use product stoichiometry while the "
          "forward side used the supplied orders. Refused rather than half-applied");
    }
    accumulateSide(name_, registry, reactantOrders, "reactant orders", reactantOrder_);
    for (Index i = 0; i < reactantOrder_.size(); ++i) {
      if (reactantOrder_[i] == 0.0) {
        continue;
      }
      // An order on a species that is not a reactant is not a reaction order. accumulateSide
      // already refused negative, NaN and Inf inputs.
      if (reactant_[i] == 0.0) {
        throw InvalidArgumentError(
            "Reaction \"" + name_ + "\": a reaction order was given for species \"" +
            registry.name(i) +
            "\", which is not a reactant (nu'_i = 0). An order on a non-reactant is a third-body "
            "or inhibitor dependence, which this class does not model");
      }
    }
  }
}

Real Reaction::netStoichiometry(Index speciesIndex) const {
  if (speciesIndex >= net_.size()) {
    throw InvalidArgumentError("Reaction \"" + name_ + "\": species index " +
                               std::to_string(speciesIndex) +
                               " is out of range; the reaction "
                               "spans " +
                               std::to_string(net_.size()) + " species");
  }
  return net_[speciesIndex];
}

Real Reaction::reactantCoefficient(Index speciesIndex) const {
  if (speciesIndex >= reactant_.size()) {
    throw InvalidArgumentError("Reaction \"" + name_ + "\": species index " +
                               std::to_string(speciesIndex) + " is out of range");
  }
  return reactant_[speciesIndex];
}

Real Reaction::reactantOrder(Index speciesIndex) const {
  if (speciesIndex >= reactantOrder_.size()) {
    throw InvalidArgumentError("Reaction \"" + name_ + "\": species index " +
                               std::to_string(speciesIndex) + " is out of range");
  }
  return reactantOrder_[speciesIndex];
}

Real Reaction::productCoefficient(Index speciesIndex) const {
  if (speciesIndex >= product_.size()) {
    throw InvalidArgumentError("Reaction \"" + name_ + "\": species index " +
                               std::to_string(speciesIndex) + " is out of range");
  }
  return product_[speciesIndex];
}

Real Reaction::massResidual(const MixtureThermodynamics& mixture) const {
  if (mixture.speciesCount() != net_.size()) {
    throw InvalidArgumentError("Reaction \"" + name_ + "\": the mixture holds " +
                               std::to_string(mixture.speciesCount()) +
                               " species; the reaction spans " + std::to_string(net_.size()));
  }
  // sum_i M_i nu_i, in FIXED ascending species order. The molecular weights come
  // from P18.3 -- P18.4 stores none of its own, so a mismatch is impossible by
  // construction rather than by validation.
  Real total = 0.0;
  for (Index i = 0; i < net_.size(); ++i) {
    total += mixture.species(i).molecularWeight() * net_[i];
  }
  return total;
}

Real Reaction::massScale(const MixtureThermodynamics& mixture) const {
  Real scale = 0.0;
  for (Index i = 0; i < net_.size(); ++i) {
    scale = std::max(scale, std::abs(mixture.species(i).molecularWeight() * net_[i]));
  }
  return scale;
}

Real massClosureTolerance(Real massScale) noexcept {
  // G6, frozen: 4 * eps * max|W_i nu_i|. Not derived here and not adjustable.
  return 4.0 * std::numeric_limits<Real>::epsilon() * std::abs(massScale);
}

// ===================================================================
// ReactionMechanism
// ===================================================================

ReactionMechanism::ReactionMechanism(SpeciesRegistry registry, std::vector<Reaction> reactions,
                                     const MixtureThermodynamics& mixture)
    : registry_(std::move(registry)), reactions_(std::move(reactions)) {
  if (reactions_.empty()) {
    throw InvalidArgumentError("ReactionMechanism: at least one reaction is required");
  }
  if (mixture.registry() != registry_) {
    // Ordering is part of a registry's value (P18.1), so a mismatch would apply
    // the wrong molecular weight to every species -- silently, with plausible
    // numbers.
    throw InvalidArgumentError(
        "ReactionMechanism: the mixture's species registry differs from the mechanism's; "
        "molecular weights would be read against the wrong species ordering");
  }

  std::unordered_set<std::string> seen;
  for (Index r = 0; r < reactions_.size(); ++r) {
    const Reaction& reaction = reactions_[r];
    if (reaction.speciesCount() != registry_.size()) {
      throw InvalidArgumentError("ReactionMechanism: reaction \"" + reaction.name() + "\" spans " +
                                 std::to_string(reaction.speciesCount()) +
                                 " species; the mechanism holds " +
                                 std::to_string(registry_.size()));
    }
    // Reaction identity is user-visible, so a duplicate is REFUSED rather than
    // silently overwriting -- which would lose a reaction without saying so.
    if (!seen.insert(reaction.name()).second) {
      throw InvalidArgumentError("ReactionMechanism: duplicate reaction name \"" + reaction.name() +
                                 "\" at index " + std::to_string(r) +
                                 "; reaction names must be unique");
    }

    // MASS BALANCE, PER REACTION rather than in aggregate: two unbalanced
    // reactions could cancel in a mechanism-level sum, and a check that passes a
    // mechanism containing two errors is worse than no check.
    const Real residual = reaction.massResidual(mixture);
    const Real scale = reaction.massScale(mixture);
    if (std::abs(residual) > massClosureTolerance(scale)) {
      std::ostringstream detail;
      detail.precision(std::numeric_limits<Real>::max_digits10);
      detail << "ReactionMechanism: reaction \"" << reaction.name()
             << "\" does not conserve mass. sum_i M_i nu_i = " << residual
             << ", against a bound of " << massClosureTolerance(scale)
             << " (4 eps * max|M_i nu_i|, max|M_i nu_i| = " << scale
             << "). Stoichiometry is NOT renormalised to repair this -- the reaction is refused. "
                "Offending net coefficients:";
      for (Index i = 0; i < registry_.size(); ++i) {
        if (reaction.netStoichiometry(i) != 0.0) {
          detail << " " << registry_.name(i) << "=" << reaction.netStoichiometry(i)
                 << " (M=" << mixture.species(i).molecularWeight() << ")";
        }
      }
      throw InvalidArgumentError(detail.str());
    }
  }
}

const Reaction& ReactionMechanism::reaction(Index index) const {
  if (index >= reactions_.size()) {
    throw InvalidArgumentError("ReactionMechanism: reaction index " + std::to_string(index) +
                               " is out of range; the mechanism holds " +
                               std::to_string(reactions_.size()) + " reactions");
  }
  return reactions_[index];
}

ReactionMechanism::MassBalance ReactionMechanism::massBalance(
    const MixtureThermodynamics& mixture) const {
  MassBalance balance;
  for (Index r = 0; r < reactions_.size(); ++r) {
    const Real residual = std::abs(reactions_[r].massResidual(mixture));
    const Real scale = reactions_[r].massScale(mixture);
    const Real relative = (scale > 0.0) ? residual / scale : 0.0;
    if (residual > balance.worstResidual) {
      balance.worstResidual = residual;
      balance.worstReaction = r;
    }
    balance.worstRelative = std::max(balance.worstRelative, relative);
  }
  return balance;
}

// ===================================================================
// Sources
// ===================================================================

ReactionProgressRates zeroProgressRates(Index reactionCount, Index cellCount) {
  if (reactionCount == 0) {
    throw InvalidArgumentError("zeroProgressRates: at least one reaction is required");
  }
  if (cellCount == 0) {
    throw InvalidArgumentError("zeroProgressRates: at least one cell is required");
  }
  ReactionProgressRates rates;
  rates.perReaction.reserve(reactionCount);
  for (Index r = 0; r < reactionCount; ++r) {
    // Explicitly zero, so the zero-rate limit is verifiably exact rather than
    // default-constructed-and-hoped.
    rates.perReaction.emplace_back(cellCount, 0.0);
  }
  return rates;
}

SpeciesSources reactionSpeciesSources(const ReactionMechanism& mechanism,
                                      const MixtureThermodynamics& mixture,
                                      const ReactionProgressRates& rates, Index cellCount) {
  const Index ns = mechanism.speciesCount();
  const Index nr = mechanism.reactionCount();
  if (rates.perReaction.size() != nr) {
    throw InvalidArgumentError(
        "reactionSpeciesSources: " + std::to_string(rates.perReaction.size()) +
        " progress-rate fields for " + std::to_string(nr) + " reactions");
  }
  if (cellCount == 0) {
    throw InvalidArgumentError("reactionSpeciesSources: at least one cell is required");
  }
  if (mixture.speciesCount() != ns) {
    throw InvalidArgumentError("reactionSpeciesSources: the mixture holds " +
                               std::to_string(mixture.speciesCount()) +
                               " species; the mechanism holds " + std::to_string(ns));
  }
  for (Index r = 0; r < nr; ++r) {
    if (rates.perReaction[r].size() != cellCount) {
      throw InvalidArgumentError("reactionSpeciesSources: the progress-rate field for reaction \"" +
                                 mechanism.reaction(r).name() + "\" holds " +
                                 std::to_string(rates.perReaction[r].size()) + " cells; expected " +
                                 std::to_string(cellCount));
    }
  }

  // Su carries omega_i. Sp stays ZERO -- see the header note: linearising omega
  // in Y needs a kinetics derivative P18.4 does not have.
  SpeciesSources sources = zeroSpeciesSources(ns, cellCount);

  // ACCUMULATION ORDER, frozen: ascending REACTION index, then ascending SPECIES
  // index, over dense vectors. No unordered container is involved anywhere, so
  // repeated evaluation is bitwise identical.
  for (Index r = 0; r < nr; ++r) {
    const Reaction& reaction = mechanism.reaction(r);
    const auto& q = rates.perReaction[r];
    for (Index cell = 0; cell < cellCount; ++cell) {
      const Real rate = q[cell];
      if (!std::isfinite(rate)) {
        throw InvalidArgumentError("reactionSpeciesSources: progress rate for reaction \"" +
                                   reaction.name() + "\" at cell " + std::to_string(cell) + " is " +
                                   exact(rate) + "; must be finite");
      }
      if (reaction.direction() == ReactionDirection::Irreversible && rate < 0.0) {
        throw InvalidArgumentError(
            "reactionSpeciesSources: progress rate for IRREVERSIBLE reaction \"" + reaction.name() +
            "\" at cell " + std::to_string(cell) + " is " + exact(rate) +
            "; an irreversible reaction cannot run backwards. Declare it Reversible if a net "
            "negative rate is intended.");
      }
      if (rate == 0.0) {
        // A zero rate contributes exactly nothing. Skipping is not an
        // optimisation: adding 0.0 * x would also give 0, but skipping makes the
        // zero-rate limit EXACTLY the untouched zero source rather than a sum of
        // zeros, which is what section 17 asks to be verifiable.
        continue;
      }
      for (Index i = 0; i < ns; ++i) {
        const Real nu = reaction.netStoichiometry(i);
        if (nu == 0.0) {
          // An uninvolved species -- or a catalyst that cancelled -- contributes
          // EXACTLY nothing, not 0 * M * q.
          continue;
        }
        // omega_i += M_i * nu_i * q, grouped consistently as (M*nu)*q so a
        // regrouping would be a visible change rather than a silent one.
        sources.perSpecies[i].Su[cell] += (mixture.species(i).molecularWeight() * nu) * rate;
      }
    }
  }
  return sources;
}

SourceConservation sourceConservation(const SpeciesSources& sources, Index cellCount) {
  SourceConservation result;
  const Index ns = sources.perSpecies.size();
  for (Index cell = 0; cell < cellCount; ++cell) {
    // PER CELL, not globally: a positive error in one cell and a negative one
    // elsewhere must not be allowed to cancel (section 22).
    Real sum = 0.0;
    for (Index i = 0; i < ns; ++i) {
      const Real value = sources.perSpecies[i].Su[cell];
      sum += value;
      result.maxAbsoluteSource = std::max(result.maxAbsoluteSource, std::abs(value));
    }
    if (std::abs(sum) > result.maxAbsoluteSum) {
      result.maxAbsoluteSum = std::abs(sum);
      result.worstCell = cell;
    }
  }
  return result;
}

NegativeSourceProjection projectNegativeSpecies(const SpeciesSources& sources,
                                                const MassFractionState& state,
                                                const cfd::fields::ScalarField& density, Real dt) {
  NegativeSourceProjection projection;
  const Index ns = sources.perSpecies.size();
  if (state.speciesCount() != ns) {
    throw InvalidArgumentError("projectNegativeSpecies: the state holds " +
                               std::to_string(state.speciesCount()) +
                               " species; the sources hold " + std::to_string(ns));
  }
  if (density.size() != state.cellCount()) {
    throw InvalidArgumentError("projectNegativeSpecies: the density field holds " +
                               std::to_string(density.size()) + " cells; the state holds " +
                               std::to_string(state.cellCount()));
  }
  projection.worstProjectedValue = std::numeric_limits<Real>::max();
  for (Index cell = 0; cell < state.cellCount(); ++cell) {
    for (Index i = 0; i < ns; ++i) {
      // DIAGNOSTIC ONLY. Nothing is clamped and no step is taken -- P18.4 builds
      // no chemistry integrator. This exists so a future one can DETECT the
      // condition rather than discover it as a negative mass fraction.
      const Real projected =
          (density[cell] * state.massFraction(i, cell)) + (dt * sources.perSpecies[i].Su[cell]);
      if (projected < projection.worstProjectedValue) {
        projection.worstProjectedValue = projected;
        projection.worstSpecies = i;
        projection.worstCell = cell;
      }
      if (projected < 0.0) {
        projection.anyNegative = true;
      }
    }
  }
  return projection;
}

}  // namespace cfd::species
