#pragma once

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/species/SpeciesTransport.hpp"

// P18.4 -- reaction topology, stoichiometry and mass-conserving species sources.
//
// WHAT THIS IS NOT: kinetics. Nothing here knows about A, n, Ea or exp(). The
// reaction progress rate q is SUPPLIED, per reaction, per cell. That separation
// is the point of the phase:
//
//     P18.4:  reaction topology + q   ->  omega_i
//     P18.5:  state + Arrhenius       ->  q
//
// so P18.5 can provide rates without touching a line of stoichiometry or source
// code.
//
// WHAT THIS IS NOT, SECOND: a second species-source assembly path. P18.4 fills
// the Su field of the SpeciesSources that P18.2's speciesTransportAssembly
// ALREADY takes. The PDE, the conservative discretisation, the diffusive closure
// and the boundary treatment are all P18.2's and are untouched.
//
// ===================================================================
// SIGN CONVENTION, frozen in results/p18/reaction_source/source_formulation.md:
//
//     nu_i = nu''_i - nu'_i        net stoichiometric coefficient
//     nu_i < 0   consumed (reactant)
//     nu_i > 0   produced (product)
//     nu_i = 0   uninvolved, OR a catalyst that cancelled
//
// UNITS, one system throughout -- mol, never kmol:
//     q        [mol / (m^3 s)]
//     M_i      [kg / mol]        from P18.3. P18.4 stores NO molecular weights.
//     omega_i  [kg / (m^3 s)]    which is what P14's Su requires
//
// MASS CONSERVATION is a property of the STOICHIOMETRY, not of the solve:
//     sum_i M_i nu_i = 0   =>   sum_i omega_i = q sum_i M_i nu_i = 0  for ANY q
// so it is checked on the reaction DEFINITION, at construction, per reaction.
// ===================================================================
namespace cfd::species {

// One side of a reaction: a species name and its stoichiometric coefficient.
struct ReactionTerm {
  std::string species;
  Real coefficient{};
};

// Whether a reaction may run backwards.
//
// The two carry DIFFERENT validity semantics for the progress rate, and keeping
// them distinct is what lets the check be the right one rather than the weaker
// of the two:
//   Irreversible  q = q_f     and q_f >= 0 is ENFORCED. A negative forward rate
//                             is not a slow reaction, it is a reaction running
//                             backwards, which an irreversible one cannot do.
//   Reversible    q = q_net = q_f - q_r, ANY SIGN. Negative means net reverse.
enum class ReactionDirection {
  Irreversible,
  Reversible,
};

// One reaction, canonicalised against the authoritative species registry.
//
// Holds a DENSE net-stoichiometry vector indexed by species index -- so there is
// no map to iterate, no ordering to be accidental, and evaluation is trivially
// deterministic.
//
// THE ONE-SIDED COEFFICIENTS ARE READ BY THE ARITHMETIC. The P18.4 comment here used to say they
// were "retained for reporting only; nothing in the source arithmetic reads them", and that stopped
// being true when P18.5 landed: concentrationProduct() uses nu'_i as the REACTION ORDER. Corrected
// rather than left, because the stale version is exactly what would make a reader believe changing
// them is safe.
//
// STOICHIOMETRY AND ORDER ARE NOT THE SAME THING, and for a GLOBAL reaction they routinely differ.
// Mass action gives order == stoichiometric coefficient for an elementary step, and that remains
// the DEFAULT here. But a fitted global mechanism -- Westbrook & Dryer (1981) for methane, say --
// has orders that are not its stoichiometry: CH4 + 2 O2 -> CO2 + 2 H2O with rate exponents 0.2 on
// the fuel and 1.3 on the oxidiser. Forcing order = stoichiometry would make such a mechanism
// inexpressible; setting the coefficients to the orders instead would destroy sum_i M_i nu_i = 0,
// which is G3's and G6's own criterion. So the two are stored separately.
class Reaction {
 public:
  // Throws InvalidArgumentError, naming the reaction and the species, if:
  //   * a species name is not in the registry;
  //   * any input coefficient is negative, NaN or Inf -- never silently abs()'d;
  //   * there are no reactants or no products;
  //   * the net stoichiometry is entirely zero after canonicalisation (a
  //     reaction that changes nothing is not a reaction).
  //
  // A species may appear on BOTH sides -- a catalyst -- and cancels into
  // nu_i = 0 exactly once, never double-counted. A species appearing twice on
  // the SAME side is summed: 2A + A is 3A.
  //
  // Zero input coefficients are ACCEPTED and contribute nothing; only an
  // entirely-zero net reaction is rejected. See source_formulation.md section 3
  // for why that policy and not the other.
  // `reactantOrders` supplies the FORWARD reaction orders when they differ from the reactant
  // stoichiometry. Leave it empty -- the default -- and the orders ARE the reactant coefficients,
  // which is mass action and is bitwise what every pre-existing caller already got.
  //
  // Additionally throws, naming the reaction and the species, if:
  //   * an order is NEGATIVE. A negative order makes the rate diverge as that species is consumed
  //     (`C^-0.3 -> inf` as `C -> 0`), and the usual remedy is to floor the concentration -- which
  //     this phase forbids ("no clipping, no normalisation"). Westbrook & Dryer say so themselves
  //     of their own best-fit methane set: "the rate of methane consumption increases without limit
  //     as the methane concentration approaches zero". So the API refuses the mechanisms that would
  //     need a clip, instead of silently clipping later.
  //   * an order is NaN or Inf;
  //   * an order names a species that is not a reactant (an order on a species with nu'_i = 0 is
  //     not a reaction order, it is a third-body or inhibitor dependence this class does not
  //     model);
  //   * `direction` is Reversible while orders are supplied. The REVERSE order is not modelled, so
  //     a reversible reaction with custom forward orders would silently use product stoichiometry
  //     on the reverse side. Refused rather than half-implemented.
  Reaction(std::string name, const SpeciesRegistry& registry,
           const std::vector<ReactionTerm>& reactants, const std::vector<ReactionTerm>& products,
           ReactionDirection direction = ReactionDirection::Irreversible,
           const std::vector<ReactionTerm>& reactantOrders = {});

  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] ReactionDirection direction() const noexcept { return direction_; }
  [[nodiscard]] Index speciesCount() const noexcept { return net_.size(); }

  // nu_i, dense over the registry, in species-index order.
  [[nodiscard]] const std::vector<Real>& netStoichiometry() const noexcept { return net_; }
  [[nodiscard]] Real netStoichiometry(Index speciesIndex) const;

  // nu'_i and nu''_i, retained for reporting. A catalyst has both non-zero and
  // a net of zero, which is the only way to tell it from an uninvolved species.
  [[nodiscard]] Real reactantCoefficient(Index speciesIndex) const;
  [[nodiscard]] Real productCoefficient(Index speciesIndex) const;

  // The FORWARD reaction order used by the progress rate. Equals reactantCoefficient() unless
  // explicit orders were supplied.
  [[nodiscard]] Real reactantOrder(Index speciesIndex) const;

  // True when explicit orders were supplied, so a report can say which mechanism is mass-action and
  // which is a fitted global one rather than leaving the reader to infer it.
  [[nodiscard]] bool hasExplicitOrders() const noexcept { return explicitOrders_; }

  // sum_i M_i nu_i. Zero for a mass-conserving reaction. Exposed so the residual
  // can be REPORTED (section 50) rather than only thresholded.
  [[nodiscard]] Real massResidual(const MixtureThermodynamics& mixture) const;

  // max_i |M_i nu_i| -- the scale the residual is judged against.
  [[nodiscard]] Real massScale(const MixtureThermodynamics& mixture) const;

 private:
  std::string name_;
  ReactionDirection direction_{ReactionDirection::Irreversible};
  std::vector<Real> net_;
  std::vector<Real> reactant_;
  std::vector<Real> product_;
  std::vector<Real> reactantOrder_;
  bool explicitOrders_{false};
};

// The frozen G6 closure tolerance: 4 * eps * max_i |M_i nu_i|.
//
// INHERITED VERBATIM from the gate -- "stoichiometric closure: sum_i W_i nu_i = 0
// EXACTLY for each reaction (mass is conserved by the stoichiometry itself), to
// <= 4 eps * max|W_i nu_i|". Relative, because an absolute tolerance on a
// dimensional mass is meaningless across mechanisms.
[[nodiscard]] Real massClosureTolerance(Real massScale) noexcept;

// A set of reactions over one registry.
//
// REJECTS a mass-unbalanced reaction at construction, per reaction rather than
// in aggregate -- two unbalanced reactions could cancel in a mechanism-level sum,
// and a check that passes a mechanism containing two errors is worse than none.
// Also rejects a duplicate reaction name: reaction identity is user-visible, so
// silently overwriting one would lose a reaction without saying so.
class ReactionMechanism {
 public:
  ReactionMechanism(SpeciesRegistry registry, std::vector<Reaction> reactions,
                    const MixtureThermodynamics& mixture);

  [[nodiscard]] const SpeciesRegistry& registry() const noexcept { return registry_; }
  [[nodiscard]] Index reactionCount() const noexcept { return reactions_.size(); }
  [[nodiscard]] Index speciesCount() const noexcept { return registry_.size(); }
  [[nodiscard]] const Reaction& reaction(Index index) const;

  // The worst per-reaction mass residual, and which reaction it came from.
  // Reported (section 50) rather than only asserted.
  struct MassBalance {
    Real worstResidual{};
    Real worstRelative{};
    Index worstReaction{};
  };
  [[nodiscard]] MassBalance massBalance(const MixtureThermodynamics& mixture) const;

 private:
  SpeciesRegistry registry_;
  std::vector<Reaction> reactions_;
};

// Progress rates: one field per reaction, in reaction-index order, each holding
// q per cell. [mol / (m^3 s)]
struct ReactionProgressRates {
  std::vector<cfd::fields::ScalarField> perReaction;
};

// N_reactions x N_cells of exactly zero -- the verifiable zero-rate limit that
// must recover P18.2's no-reaction behaviour exactly (section 17).
[[nodiscard]] ReactionProgressRates zeroProgressRates(Index reactionCount, Index cellCount);

// omega_i = sum_r M_i nu_{i,r} q_r, per cell, as a SpeciesSources ready for
// P18.2's speciesTransportAssembly.
//
// Su carries omega_i. Sp STAYS ZERO: linearising omega_i in Y_i needs
// d(omega)/d(Y), a kinetics derivative P18.4 does not have, and an explicit
// source is the correct Patankar split for something treated explicitly. The
// implicit part belongs to P18.5. See audit.md section 4.
//
// Throws InvalidArgumentError for a non-finite progress rate, or a NEGATIVE rate
// on an IRREVERSIBLE reaction, naming the reaction and the cell.
//
// ACCUMULATION ORDER is ascending reaction index, then ascending species index,
// over dense vectors -- so repeated evaluation is bitwise identical.
[[nodiscard]] SpeciesSources reactionSpeciesSources(const ReactionMechanism& mechanism,
                                                    const MixtureThermodynamics& mixture,
                                                    const ReactionProgressRates& rates,
                                                    Index cellCount);

// max_cell | sum_i omega_i | and the scale it is judged against. The per-cell
// sum, not the global one: a positive error in one cell and a negative one
// elsewhere must not be allowed to cancel (section 22).
struct SourceConservation {
  Real maxAbsoluteSum{};
  Real maxAbsoluteSource{};
  Index worstCell{};
};

[[nodiscard]] SourceConservation sourceConservation(const SpeciesSources& sources, Index cellCount);

// DIAGNOSTIC ONLY (section 23). Reports where an explicit update
// rho Y_i + dt omega_i would drive a species negative. NOTHING IS CLAMPED and no
// step is taken -- P18.4 builds no chemistry integrator, so this exists so a
// future one can detect the condition rather than discover it as a negative mass
// fraction. The rate is valid; the bounded time step is deferred.
struct NegativeSourceProjection {
  bool anyNegative{false};
  Index worstSpecies{};
  Index worstCell{};
  Real worstProjectedValue{};  // the most negative rho*Y + dt*omega encountered
};

[[nodiscard]] NegativeSourceProjection projectNegativeSpecies(
    const SpeciesSources& sources, const MassFractionState& state,
    const cfd::fields::ScalarField& density, Real dt);

}  // namespace cfd::species
