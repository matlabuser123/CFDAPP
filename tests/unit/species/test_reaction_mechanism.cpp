// P18.4 -- reaction topology, stoichiometry and mass-conserving species sources.
//
// EVERY EXPECTED VALUE WAS COMPUTED OUTSIDE CFDApp and is embedded as a literal;
// the generator and its output are preserved at
// results/p18/reaction_source/data/reference_values.txt.
//
// The molecular weights of the section-36 reaction are DYADIC (powers of two), so
// M_A + 2 M_B = M_C holds EXACTLY in binary and the mass residual is exactly
// zero. That makes the closure criterion measure the source code rather than the
// roundness of a handbook number.
//
// Thresholds: G6's stoichiometric-closure clause, 4 * eps * max|M_i nu_i|,
// inherited verbatim. P18.4 implements NO kinetics, so G6's rate clause is
// P18.5's and is not claimed here.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/species/SpeciesTransport.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::species::massClosureTolerance;
using cfd::species::MassFractionState;
using cfd::species::MixtureThermodynamics;
using cfd::species::projectNegativeSpecies;
using cfd::species::Reaction;
using cfd::species::ReactionDirection;
using cfd::species::ReactionMechanism;
using cfd::species::ReactionProgressRates;
using cfd::species::reactionSpeciesSources;
using cfd::species::ReactionTerm;
using cfd::species::sourceConservation;
using cfd::species::SpeciesRegistry;
using cfd::species::SpeciesThermodynamics;
using cfd::species::zeroProgressRates;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// Dyadic molecular weights: M_A + 2 M_B = M_C EXACTLY in binary.
constexpr Real kMA = 0.0625;    // 2^-4
constexpr Real kMB = 0.03125;   // 2^-5
constexpr Real kMC = 0.125;     // 2^-3
constexpr Real kMInert = 0.25;  // 2^-2, takes part in nothing

SensibleEnthalpy cp(Real value, const std::string& name) {
  return SensibleEnthalpy(TemperatureDependentProperty::constant(value, name), 298.15, 0.0);
}

// A, B, C plus an INERT species that participates in no reaction -- section 16's
// indexing/broadcast trap.
SpeciesRegistry abcRegistry() { return SpeciesRegistry({"A", "B", "C", "Inert"}); }

MixtureThermodynamics abcMixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(kMA, cp(1000.0, "cp_A")));
  species.push_back(SpeciesThermodynamics(kMB, cp(1200.0, "cp_B")));
  species.push_back(SpeciesThermodynamics(kMC, cp(900.0, "cp_C")));
  species.push_back(SpeciesThermodynamics(kMInert, cp(800.0, "cp_Inert")));
  return MixtureThermodynamics(abcRegistry(), std::move(species));
}

// A + 2B -> C
Reaction abcReaction(ReactionDirection direction = ReactionDirection::Irreversible) {
  return Reaction("A+2B->C", abcRegistry(), {{"A", 1.0}, {"B", 2.0}}, {{"C", 1.0}}, direction);
}

ReactionMechanism abcMechanism(ReactionDirection direction = ReactionDirection::Irreversible) {
  std::vector<Reaction> reactions;
  reactions.push_back(abcReaction(direction));
  return ReactionMechanism(abcRegistry(), std::move(reactions), abcMixture());
}

// Uniform progress rate q over `cells` cells, for one reaction.
ReactionProgressRates uniformRate(Index reactionCount, Index cells, Real q) {
  ReactionProgressRates rates = zeroProgressRates(reactionCount, cells);
  for (Index r = 0; r < reactionCount; ++r) {
    rates.perReaction[r].fill(q);
  }
  return rates;
}

}  // namespace

// ===================================================================
// Reaction construction and canonicalisation -- sections 2, 3, 4, 5, 27, 28
// ===================================================================

TEST(ReactionTest, NetStoichiometryUsesTheFrozenSignConvention) {
  // Section 2: reactants negative, products positive, uninvolved exactly zero.
  const Reaction reaction = abcReaction();
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(0), -1.0);  // A
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(1), -2.0);  // B
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(2), 1.0);   // C
  EXPECT_EQ(reaction.netStoichiometry(3), 0.0);          // Inert, EXACTLY zero
  // the one-sided coefficients are retained for reporting
  EXPECT_DOUBLE_EQ(reaction.reactantCoefficient(1), 2.0);
  EXPECT_DOUBLE_EQ(reaction.productCoefficient(2), 1.0);
  EXPECT_EQ(reaction.reactantCoefficient(2), 0.0);
}

TEST(ReactionTest, ACatalystCancelsToZeroExactlyOnce) {
  // Section 27: a species on both sides must cancel ONCE, never be double-counted.
  // A + Cat -> C + Cat : the catalyst's net coefficient is exactly 0, and the
  // reaction is still valid because A and C do not cancel.
  const SpeciesRegistry registry({"A", "B", "C", "Inert"});
  const Reaction reaction("catalysed", registry, {{"A", 1.0}, {"B", 1.0}},
                          {{"C", 1.0}, {"B", 1.0}});
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(0), -1.0);  // A consumed
  EXPECT_EQ(reaction.netStoichiometry(1), 0.0);          // B catalytic, EXACTLY 0
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(2), 1.0);   // C produced
  // ...and a catalyst is distinguishable from an uninvolved species only by the
  // one-sided coefficients, which is why they are retained.
  EXPECT_DOUBLE_EQ(reaction.reactantCoefficient(1), 1.0);
  EXPECT_DOUBLE_EQ(reaction.productCoefficient(1), 1.0);
  EXPECT_EQ(reaction.reactantCoefficient(3), 0.0);
  EXPECT_EQ(reaction.productCoefficient(3), 0.0);
}

TEST(ReactionTest, ASpeciesRepeatedOnOneSideIsSummed) {
  // Documented policy (source_formulation.md section 3): 2A + A is 3A.
  const SpeciesRegistry registry({"A", "B", "C", "Inert"});
  const Reaction reaction("repeat", registry, {{"A", 2.0}, {"A", 1.0}}, {{"C", 1.0}});
  EXPECT_DOUBLE_EQ(reaction.reactantCoefficient(0), 3.0);
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(0), -3.0);
}

TEST(ReactionTest, ZeroInputCoefficientsAreAcceptedAndContributeNothing) {
  const SpeciesRegistry registry({"A", "B", "C", "Inert"});
  const Reaction reaction("zeroterm", registry, {{"A", 1.0}, {"Inert", 0.0}}, {{"C", 1.0}});
  EXPECT_EQ(reaction.netStoichiometry(3), 0.0);
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(0), -1.0);
}

TEST(ReactionTest, RejectsInvalidDefinitions) {
  const SpeciesRegistry registry({"A", "B", "C", "Inert"});
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();

  // unknown species (section 3)
  EXPECT_THROW(Reaction("r", registry, {{"Xe", 1.0}}, {{"C", 1.0}}), InvalidArgumentError);
  // negative coefficient -- NOT silently abs()'d (section 4)
  EXPECT_THROW(Reaction("r", registry, {{"A", -1.0}}, {{"C", 1.0}}), InvalidArgumentError);
  // non-finite (section 4)
  EXPECT_THROW(Reaction("r", registry, {{"A", nan}}, {{"C", 1.0}}), InvalidArgumentError);
  EXPECT_THROW(Reaction("r", registry, {{"A", inf}}, {{"C", 1.0}}), InvalidArgumentError);
  // empty sides (section 26)
  EXPECT_THROW(Reaction("r", registry, {}, {{"C", 1.0}}), InvalidArgumentError);
  EXPECT_THROW(Reaction("r", registry, {{"A", 1.0}}, {}), InvalidArgumentError);
  // a reaction that changes nothing (section 26)
  EXPECT_THROW(Reaction("r", registry, {{"A", 1.0}}, {{"A", 1.0}}), InvalidArgumentError);
  // empty name -- reaction identity is user-visible
  EXPECT_THROW(Reaction("", registry, {{"A", 1.0}}, {{"C", 1.0}}), InvalidArgumentError);
}

TEST(ReactionTest, UnknownSpeciesDiagnosticNamesTheReactionAndTheSpecies) {
  const SpeciesRegistry registry({"A", "B", "C", "Inert"});
  try {
    Reaction("my-reaction", registry, {{"Xe", 1.0}}, {{"C", 1.0}});
    FAIL() << "expected an unknown-species rejection";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("my-reaction"), std::string::npos) << message;
    EXPECT_NE(message.find("Xe"), std::string::npos) << message;
  }
}

// ===================================================================
// Mass conservation -- sections 8, 9, 21. THE primary P18.4 criterion.
// ===================================================================

TEST(ReactionTest, MassResidualIsExactlyZeroForABalancedReaction) {
  // The dyadic molecular weights make M_A + 2 M_B = M_C EXACT in binary, so the
  // residual is exactly 0 rather than merely within tolerance.
  const Reaction reaction = abcReaction();
  const auto mixture = abcMixture();
  EXPECT_EQ(reaction.massResidual(mixture), 0.0);
  EXPECT_LE(std::abs(reaction.massResidual(mixture)),
            massClosureTolerance(reaction.massScale(mixture)));
}

TEST(ReactionTest, N4_MassUnbalancedReactionIsRejectedAndNotRenormalised) {
  // CONTROL N4 and section 9. A + B -> C is unbalanced: M_A + M_B = 0.09375 but
  // M_C = 0.125, a defect of 0.03125 -- a third of the product mass.
  const auto mixture = abcMixture();
  std::vector<Reaction> reactions;
  reactions.push_back(
      Reaction("unbalanced", abcRegistry(), {{"A", 1.0}, {"B", 1.0}}, {{"C", 1.0}}));

  // the reaction OBJECT is constructible -- stoichiometry alone is not wrong --
  // but the MECHANISM refuses it, because that is where molecular weights enter
  EXPECT_DOUBLE_EQ(reactions[0].massResidual(mixture), 0.125 - 0.0625 - 0.03125);
  EXPECT_THROW(ReactionMechanism(abcRegistry(), std::move(reactions), mixture),
               InvalidArgumentError);
}

TEST(ReactionTest, MassUnbalanceDiagnosticNamesTheReactionDefectAndStoichiometry) {
  // Section 9: report the reaction, the mass defect and the offending stoichiometry.
  const auto mixture = abcMixture();
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("bad-rxn", abcRegistry(), {{"A", 1.0}, {"B", 1.0}}, {{"C", 1.0}}));
  try {
    ReactionMechanism(abcRegistry(), std::move(reactions), mixture);
    FAIL() << "expected a mass-balance rejection";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("bad-rxn"), std::string::npos) << message;
    EXPECT_NE(message.find("0.03125"), std::string::npos) << message;  // the defect
    EXPECT_NE(message.find("A="), std::string::npos) << message;       // the stoichiometry
    EXPECT_NE(message.find("NOT renormalised"), std::string::npos) << message;
  }
}

TEST(ReactionTest, N1_APerturbedStoichiometricCoefficientFailsTheClosureCriterion) {
  // G6's OWN negative control: "a stoichiometric coefficient perturbed must FAIL
  // the closure criterion". Perturbed by one part in 1e9 -- far below anything a
  // human would notice, far above 4 eps.
  const auto mixture = abcMixture();
  std::vector<Reaction> reactions;
  reactions.push_back(
      Reaction("perturbed", abcRegistry(), {{"A", 1.0}, {"B", 2.0 + 1.0e-9}}, {{"C", 1.0}}));
  const Real residual = reactions[0].massResidual(mixture);
  const Real tolerance = massClosureTolerance(reactions[0].massScale(mixture));
  EXPECT_GT(std::abs(residual), tolerance)
      << "residual " << residual << " against tolerance " << tolerance;
  EXPECT_THROW(ReactionMechanism(abcRegistry(), std::move(reactions), mixture),
               InvalidArgumentError);
}

TEST(ReactionTest, EveryReactionIsCheckedIndividuallyNotInAggregate) {
  // Section 21: two unbalanced reactions could cancel in a mechanism-level sum.
  // Here one has a +0.03125 defect and the other -0.03125, so an AGGREGATE check
  // would pass a mechanism containing TWO errors.
  const auto mixture = abcMixture();
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("too-light", abcRegistry(), {{"A", 1.0}, {"B", 1.0}}, {{"C", 1.0}}));
  reactions.push_back(Reaction("too-heavy", abcRegistry(), {{"C", 1.0}}, {{"A", 1.0}, {"B", 1.0}}));
  const Real sum = reactions[0].massResidual(mixture) + reactions[1].massResidual(mixture);
  EXPECT_NEAR(sum, 0.0, 1.0e-18) << "the two defects must cancel in aggregate, or this test is not "
                                    "exercising the trap";
  EXPECT_THROW(ReactionMechanism(abcRegistry(), std::move(reactions), mixture),
               InvalidArgumentError);
}

TEST(ReactionTest, RecognizableReactionBalancesExactly) {
  // Section 37: 2 H2 + O2 -> 2 H2O with real molecular weights.
  const Real mH2 = 0.002016;
  const Real mO2 = 0.031998;
  const Real mH2O = 0.018015;  // the handbook value, which IS (2 M_H2 + M_O2)/2 in doubles
  const SpeciesRegistry registry({"H2", "O2", "H2O"});
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(mH2, cp(14307.0, "cp_H2")));
  species.push_back(SpeciesThermodynamics(mO2, cp(918.0, "cp_O2")));
  species.push_back(SpeciesThermodynamics(mH2O, cp(1996.0, "cp_H2O")));
  const MixtureThermodynamics mixture(registry, std::move(species));

  const Reaction reaction("2H2+O2->2H2O", registry, {{"H2", 2.0}, {"O2", 1.0}}, {{"H2O", 2.0}});
  EXPECT_EQ(reaction.massResidual(mixture), 0.0) << "the handbook weights balance exactly";
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(0), -2.0);
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(1), -1.0);
  EXPECT_DOUBLE_EQ(reaction.netStoichiometry(2), 2.0);

  std::vector<Reaction> reactions;
  reactions.push_back(reaction);
  const ReactionMechanism mechanism(registry, std::move(reactions), mixture);
  const auto sources = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, 0.5), 1);
  // against the independent reference
  EXPECT_DOUBLE_EQ(sources.perSpecies[0].Su[0], -0.002016);
  EXPECT_DOUBLE_EQ(sources.perSpecies[1].Su[0], -0.015999);
  EXPECT_DOUBLE_EQ(sources.perSpecies[2].Su[0], 0.018015);
  EXPECT_EQ(sources.perSpecies[0].Su[0] + sources.perSpecies[1].Su[0] + sources.perSpecies[2].Su[0],
            0.0);
}

// ===================================================================
// Mechanism -- sections 14, 25
// ===================================================================

TEST(ReactionMechanismTest, RejectsDuplicateReactionNames) {
  // Section 25: reaction identity is user-visible, so a duplicate is REFUSED
  // rather than silently overwriting -- which would lose a reaction.
  const auto mixture = abcMixture();
  std::vector<Reaction> reactions;
  reactions.push_back(abcReaction());
  reactions.push_back(abcReaction());
  EXPECT_THROW(ReactionMechanism(abcRegistry(), std::move(reactions), mixture),
               InvalidArgumentError);
}

TEST(ReactionMechanismTest, RejectsAnEmptyMechanismAndAMismatchedRegistry) {
  const auto mixture = abcMixture();
  EXPECT_THROW(ReactionMechanism(abcRegistry(), {}, mixture), InvalidArgumentError);

  // a registry whose ORDER differs would apply the wrong molecular weight to
  // every species -- silently, with plausible numbers
  const SpeciesRegistry reordered({"B", "A", "C", "Inert"});
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("r", reordered, {{"A", 1.0}}, {{"C", 1.0}}));
  EXPECT_THROW(ReactionMechanism(reordered, std::move(reactions), mixture), InvalidArgumentError);
}

TEST(ReactionMechanismTest, MassBalanceReportsTheWorstReaction) {
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const auto balance = mechanism.massBalance(mixture);
  EXPECT_EQ(balance.worstResidual, 0.0);
  EXPECT_EQ(balance.worstRelative, 0.0);
}

// ===================================================================
// Irreversible sources -- sections 11, 16, 17, 38
// ===================================================================

TEST(ReactionSourceTest, IrreversibleSourceHasCorrectSignsAndMagnitudes) {
  // Section 11, against the independent reference: q = 3, A + 2B -> C.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const auto sources = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, 3.0), 1);

  EXPECT_DOUBLE_EQ(sources.perSpecies[0].Su[0], -0.1875);  // A consumed
  EXPECT_DOUBLE_EQ(sources.perSpecies[1].Su[0], -0.1875);  // B consumed
  EXPECT_DOUBLE_EQ(sources.perSpecies[2].Su[0], 0.375);    // C produced
  EXPECT_EQ(sources.perSpecies[3].Su[0], 0.0);             // INERT, exactly zero
  // the Patankar implicit part stays exactly zero -- P18.4 has no kinetics
  // derivative to linearise with
  for (Index i = 0; i < 4; ++i) {
    EXPECT_EQ(sources.perSpecies[i].Sp[0], 0.0) << "species " << i;
  }
}

TEST(ReactionSourceTest, InertSpeciesSourceIsExactlyZero) {
  // Section 16: catches accidental indexing or broadcast errors. Checked over
  // many cells and a non-trivial rate, so a stray write would show up.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const Index cells = 32;
  const auto sources =
      reactionSpeciesSources(mechanism, mixture, uniformRate(1, cells, 7.25), cells);
  for (Index cell = 0; cell < cells; ++cell) {
    EXPECT_EQ(sources.perSpecies[3].Su[cell], 0.0) << "cell " << cell;
    EXPECT_NE(sources.perSpecies[0].Su[cell], 0.0) << "cell " << cell
                                                   << ": the participating "
                                                      "species must be non-zero, or the inert "
                                                      "check is vacuous";
  }
}

TEST(ReactionSourceTest, SourceRatioRecoversTheProgressRateForEverySpecies) {
  // Section 38: omega_i / (M_i nu_i) must give the SAME q for every participating
  // species. This detects incorrect per-species scaling.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const Real q = 2.75;
  const auto sources = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, q), 1);
  const auto& reaction = mechanism.reaction(0);
  Real worst = 0.0;
  for (Index i = 0; i < 4; ++i) {
    const Real nu = reaction.netStoichiometry(i);
    if (nu == 0.0) {
      continue;
    }
    const Real recovered =
        sources.perSpecies[i].Su[0] / (mixture.species(i).molecularWeight() * nu);
    EXPECT_NEAR(recovered, q, 8.0 * kEps * q) << "species " << i;
    worst = std::max(worst, std::abs(recovered - q) / q);
  }
  EXPECT_LE(worst, 8.0 * kEps) << "max source-ratio error = " << worst;
}

TEST(ReactionSourceTest, StoichiometricConsumptionRatiosAreMolarNotMass) {
  // Section 39. For A + 2B -> C the MOLAR consumption ratio is nu'_B/nu'_A = 2.
  // The MASS ratio is (M_B nu_B)/(M_A nu_A) = (0.03125*2)/(0.0625*1) = 1, which
  // is NOT 2 -- so comparing mass rates directly to stoichiometric coefficients
  // would be wrong, which is exactly what this section warns against.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const auto sources = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, 1.0), 1);

  const Real molarA = sources.perSpecies[0].Su[0] / mixture.species(0).molecularWeight();
  const Real molarB = sources.perSpecies[1].Su[0] / mixture.species(1).molecularWeight();
  EXPECT_DOUBLE_EQ(std::abs(molarB) / std::abs(molarA), 2.0) << "molar ratio must be nu'_B/nu'_A";

  const Real massRatio = std::abs(sources.perSpecies[1].Su[0] / sources.perSpecies[0].Su[0]);
  EXPECT_DOUBLE_EQ(massRatio, 1.0);
  EXPECT_NE(massRatio, 2.0) << "the mass ratio must NOT equal the stoichiometric ratio here, or "
                               "the test cannot distinguish the two bases";
}

TEST(ReactionSourceTest, ZeroProgressRateGivesExactlyZeroSource) {
  // Section 17: the zero-rate limit must recover P18.2's no-reaction behaviour
  // EXACTLY, not approximately.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const Index cells = 16;
  const auto sources =
      reactionSpeciesSources(mechanism, mixture, zeroProgressRates(1, cells), cells);
  for (Index i = 0; i < 4; ++i) {
    for (Index cell = 0; cell < cells; ++cell) {
      EXPECT_EQ(sources.perSpecies[i].Su[cell], 0.0) << "species " << i << " cell " << cell;
      EXPECT_EQ(sources.perSpecies[i].Sp[cell], 0.0);
    }
  }
}

// ===================================================================
// Reversible sources -- sections 12, 13, 40
// ===================================================================

TEST(ReactionSourceTest, ReversibleNetRateDrivesDirection) {
  // Section 12: q_net > 0 forward, q_net < 0 reverse, q_net = 0 nothing.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism(ReactionDirection::Reversible);

  const auto forward = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, 3.0), 1);
  EXPECT_LT(forward.perSpecies[0].Su[0], 0.0);  // A consumed
  EXPECT_GT(forward.perSpecies[2].Su[0], 0.0);  // C produced

  const auto reverse = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, -3.0), 1);
  EXPECT_GT(reverse.perSpecies[0].Su[0], 0.0);  // A PRODUCED
  EXPECT_LT(reverse.perSpecies[2].Su[0], 0.0);  // C CONSUMED

  // q_forward == q_reverse, so q_net = 0 (section 40)
  const Real qF = 4.25;
  const Real qR = 4.25;
  const auto equilibrium =
      reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, qF - qR), 1);
  for (Index i = 0; i < 4; ++i) {
    EXPECT_EQ(equilibrium.perSpecies[i].Su[0], 0.0) << "species " << i;
  }
}

TEST(ReactionSourceTest, ReversibleDirectionSymmetryIsExact) {
  // Section 13: omega_i(-q) = -omega_i(q), quantitatively. A strong
  // sign-convention test -- and BITWISE here, because negating q negates each
  // product exactly.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism(ReactionDirection::Reversible);
  const Real q = 1.875;
  const auto plus = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, q), 1);
  const auto minus = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, -q), 1);
  for (Index i = 0; i < 4; ++i) {
    EXPECT_EQ(minus.perSpecies[i].Su[0], -plus.perSpecies[i].Su[0]) << "species " << i;
  }
}

TEST(ReactionSourceTest, N6_IrreversibleReactionRefusesANegativeRate) {
  // CONTROL N6 and section 24: the two directions carry DIFFERENT validity
  // semantics, and keeping them distinct is what lets the check be the right one.
  const auto mixture = abcMixture();
  const auto irreversible = abcMechanism(ReactionDirection::Irreversible);
  EXPECT_THROW(reactionSpeciesSources(irreversible, mixture, uniformRate(1, 1, -1.0), 1),
               InvalidArgumentError);
  // ...while the reversible form accepts it
  const auto reversible = abcMechanism(ReactionDirection::Reversible);
  EXPECT_NO_THROW(reactionSpeciesSources(reversible, mixture, uniformRate(1, 1, -1.0), 1));
}

TEST(ReactionSourceTest, N8_RejectsNonFiniteProgressRates) {
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism(ReactionDirection::Reversible);
  for (const Real bad :
       {std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity(),
        -std::numeric_limits<Real>::infinity()}) {
    EXPECT_THROW(reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, bad), 1),
                 InvalidArgumentError);
  }
}

// ===================================================================
// Multiple reactions -- sections 14, 15, 41
// ===================================================================

namespace {

// A -> B, B -> C : species B is produced by one reaction and consumed by the
// other. Molecular weights must be equal for a 1:1 conversion to conserve mass,
// so this mechanism uses its own registry with equal weights.
MixtureThermodynamics chainMixture() {
  std::vector<SpeciesThermodynamics> species;
  for (const char* name : {"cp_A", "cp_B", "cp_C", "cp_Inert"}) {
    species.push_back(SpeciesThermodynamics(0.0625, cp(1000.0, name)));
  }
  return MixtureThermodynamics(abcRegistry(), std::move(species));
}

ReactionMechanism chainMechanism() {
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("A->B", abcRegistry(), {{"A", 1.0}}, {{"B", 1.0}}));
  reactions.push_back(Reaction("B->C", abcRegistry(), {{"B", 1.0}}, {{"C", 1.0}}));
  return ReactionMechanism(abcRegistry(), std::move(reactions), chainMixture());
}

}  // namespace

TEST(ReactionSourceTest, MultipleReactionsSuperposeLinearly) {
  // Section 14: omega_i,total = sum_r M_i nu_{i,r} q_r.
  const auto mixture = chainMixture();
  const auto mechanism = chainMechanism();
  const Index cells = 1;

  ReactionProgressRates rates = zeroProgressRates(2, cells);
  rates.perReaction[0].fill(2.0);  // A -> B
  rates.perReaction[1].fill(0.5);  // B -> C
  const auto both = reactionSpeciesSources(mechanism, mixture, rates, cells);

  // each reaction alone
  ReactionProgressRates only0 = zeroProgressRates(2, cells);
  only0.perReaction[0].fill(2.0);
  ReactionProgressRates only1 = zeroProgressRates(2, cells);
  only1.perReaction[1].fill(0.5);
  const auto first = reactionSpeciesSources(mechanism, mixture, only0, cells);
  const auto second = reactionSpeciesSources(mechanism, mixture, only1, cells);

  for (Index i = 0; i < 4; ++i) {
    EXPECT_DOUBLE_EQ(both.perSpecies[i].Su[0],
                     first.perSpecies[i].Su[0] + second.perSpecies[i].Su[0])
        << "species " << i;
  }
}

TEST(ReactionSourceTest, SharedSpeciesAccumulatesBothContributionsWithCorrectSigns) {
  // Section 15: B is PRODUCED by A->B and CONSUMED by B->C.
  const auto mixture = chainMixture();
  const auto mechanism = chainMechanism();
  const Real m = 0.0625;

  ReactionProgressRates rates = zeroProgressRates(2, 1);
  rates.perReaction[0].fill(2.0);
  rates.perReaction[1].fill(0.5);
  const auto sources = reactionSpeciesSources(mechanism, mixture, rates, 1);

  EXPECT_DOUBLE_EQ(sources.perSpecies[0].Su[0], m * -1.0 * 2.0);          // A: consumed
  EXPECT_DOUBLE_EQ(sources.perSpecies[1].Su[0], (m * 2.0) + (m * -0.5));  // B: +2 then -0.5
  EXPECT_DOUBLE_EQ(sources.perSpecies[2].Su[0], m * 0.5);                 // C: produced
  EXPECT_GT(sources.perSpecies[1].Su[0], 0.0) << "net production of B at these rates";
}

TEST(ReactionSourceTest, N5_OmittingOneReactionChangesTheResult) {
  // CONTROL N5: dropping a reaction from the accumulation must change omega.
  const auto mixture = chainMixture();
  const auto mechanism = chainMechanism();
  ReactionProgressRates both = zeroProgressRates(2, 1);
  both.perReaction[0].fill(2.0);
  both.perReaction[1].fill(0.5);
  ReactionProgressRates dropped = zeroProgressRates(2, 1);
  dropped.perReaction[0].fill(2.0);  // second reaction omitted

  const auto withBoth = reactionSpeciesSources(mechanism, mixture, both, 1);
  const auto withOne = reactionSpeciesSources(mechanism, mixture, dropped, 1);
  EXPECT_NE(withBoth.perSpecies[1].Su[0], withOne.perSpecies[1].Su[0]) << "species B";
  EXPECT_NE(withBoth.perSpecies[2].Su[0], withOne.perSpecies[2].Su[0]) << "species C";
  EXPECT_EQ(withOne.perSpecies[2].Su[0], 0.0) << "C is only produced by the dropped reaction";
}

TEST(ReactionSourceTest, TwoReactionsCanCancelForOneSpeciesWithoutTheTestBeingVacuous) {
  // Section 41: construct a cancellation, and ALSO verify the individual
  // contributions were non-zero -- otherwise "the result is zero" proves nothing.
  const auto mixture = chainMixture();
  const auto mechanism = chainMechanism();

  // B is produced by A->B at rate q and consumed by B->C at the same q, so its
  // net source is exactly zero while both contributions are large.
  ReactionProgressRates rates = zeroProgressRates(2, 1);
  rates.perReaction[0].fill(3.0);
  rates.perReaction[1].fill(3.0);
  const auto sources = reactionSpeciesSources(mechanism, mixture, rates, 1);

  EXPECT_EQ(sources.perSpecies[1].Su[0], 0.0) << "B's net source must cancel exactly";
  // NON-VACUITY: the individual contributions really were computed and large
  ReactionProgressRates only0 = zeroProgressRates(2, 1);
  only0.perReaction[0].fill(3.0);
  const auto first = reactionSpeciesSources(mechanism, mixture, only0, 1);
  EXPECT_GT(std::abs(first.perSpecies[1].Su[0]), 0.1)
      << "the contributions must be substantial, or the cancellation is trivial";
}

// ===================================================================
// Per-cell source conservation -- sections 20, 22
// ===================================================================

TEST(ReactionSourceTest, EveryCellConservesMassIndependently) {
  // Section 22: a positive error in one cell and a negative one elsewhere must
  // not be allowed to cancel. Uses a SPATIALLY VARYING rate, including negatives
  // and zeros, so the cells are genuinely different.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism(ReactionDirection::Reversible);
  const Index cells = 64;
  ReactionProgressRates rates = zeroProgressRates(1, cells);
  for (Index cell = 0; cell < cells; ++cell) {
    rates.perReaction[0][cell] = -5.0 + (0.25 * static_cast<Real>(cell));
  }
  const auto sources = reactionSpeciesSources(mechanism, mixture, rates, cells);
  const auto conservation = sourceConservation(sources, cells);

  EXPECT_GT(conservation.maxAbsoluteSource, 0.0) << "the sources must be non-trivial";
  EXPECT_LE(conservation.maxAbsoluteSum, 8.0 * kEps * conservation.maxAbsoluteSource)
      << "max per-cell |sum omega| = " << conservation.maxAbsoluteSum << " at cell "
      << conservation.worstCell;
}

// ===================================================================
// Units -- section 31, control N9
// ===================================================================

TEST(ReactionSourceTest, N9_AKmolMolecularWeightWouldBeWrongByExactlyAThousand) {
  // CONTROL N9. M_i is [kg/mol]. A kmol value is 1000x larger, and omega scales
  // linearly with M, so the error is exactly 1000x -- unmistakable.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const auto sources = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, 1.0), 1);

  const Real correct = sources.perSpecies[0].Su[0];
  const Real kmolVersion = correct * 1000.0;
  EXPECT_DOUBLE_EQ(correct, -0.0625) << "M_A [kg/mol] * nu * q = 0.0625 * -1 * 1";
  EXPECT_DOUBLE_EQ(kmolVersion, -62.5);
  // A molecular weight of 62.5 kg/mol is not a plausible species, which is the
  // second way this error announces itself.
  EXPECT_GT(std::abs(kmolVersion / correct), 999.0);
}

TEST(ReactionSourceTest, N2_OmittingTheMolecularWeightChangesTheSourceByTheMolecularWeight) {
  // CONTROL N2: omega = M nu q, not nu q. For species A, M = 0.0625, so omitting
  // it inflates the source by 16x.
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const Real q = 1.0;
  const auto sources = reactionSpeciesSources(mechanism, mixture, uniformRate(1, 1, q), 1);
  const Real withMass = sources.perSpecies[0].Su[0];
  const Real withoutMass = mechanism.reaction(0).netStoichiometry(0) * q;  // the defect
  EXPECT_DOUBLE_EQ(withMass, -0.0625);
  EXPECT_DOUBLE_EQ(withoutMass, -1.0);
  EXPECT_DOUBLE_EQ(withoutMass / withMass, 16.0);
}

TEST(ReactionSourceTest, N3_AWrongMolecularWeightForOneSpeciesBreaksTheMassBalance) {
  // CONTROL N3: perturb ONE species' molecular weight and the reaction no longer
  // conserves mass, so the mechanism refuses it.
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(kMA * 1.05, cp(1000.0, "cp_A")));  // 5% wrong
  species.push_back(SpeciesThermodynamics(kMB, cp(1200.0, "cp_B")));
  species.push_back(SpeciesThermodynamics(kMC, cp(900.0, "cp_C")));
  species.push_back(SpeciesThermodynamics(kMInert, cp(800.0, "cp_Inert")));
  const MixtureThermodynamics wrong(abcRegistry(), std::move(species));

  std::vector<Reaction> reactions;
  reactions.push_back(abcReaction());
  EXPECT_THROW(ReactionMechanism(abcRegistry(), std::move(reactions), wrong), InvalidArgumentError);
}

// ===================================================================
// Determinism -- sections 29, 30
// ===================================================================

TEST(ReactionSourceTest, ConstructionAndEvaluationAreBitwiseDeterministic) {
  const auto mixture = chainMixture();
  const Index cells = 16;
  ReactionProgressRates rates = zeroProgressRates(2, cells);
  for (Index cell = 0; cell < cells; ++cell) {
    rates.perReaction[0][cell] = 1.0 + (0.1 * static_cast<Real>(cell));
    rates.perReaction[1][cell] = 0.5 - (0.02 * static_cast<Real>(cell));
  }

  const auto mechanism = chainMechanism();
  const auto first = reactionSpeciesSources(mechanism, mixture, rates, cells);
  for (int repeat = 0; repeat < 50; ++repeat) {
    // reconstruct the mechanism too, so parsing/canonicalisation is included
    const auto again = chainMechanism();
    const auto sources = reactionSpeciesSources(again, mixture, rates, cells);
    for (Index i = 0; i < 4; ++i) {
      for (Index cell = 0; cell < cells; ++cell) {
        EXPECT_EQ(sources.perSpecies[i].Su[cell], first.perSpecies[i].Su[cell])
            << "repeat " << repeat << " species " << i << " cell " << cell;
      }
      EXPECT_EQ(again.reaction(0).netStoichiometry(i), mechanism.reaction(0).netStoichiometry(i));
    }
  }
}

// ===================================================================
// The negative-source diagnostic -- section 23
// ===================================================================

TEST(ReactionSourceTest, NegativeProjectionIsDiagnosedAndNothingIsClamped) {
  // Section 23: detect that rho Y_i + dt omega_i would go negative. NOTHING is
  // clamped and no step is taken -- P18.4 builds no chemistry integrator.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index cells = mesh.numberOfCells();
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const MassFractionState state(abcRegistry(), cells, {0.1, 0.2, 0.3, 0.4});
  const ScalarField density(cells, 1.0);

  // a large rate and a large dt will drive A negative
  const auto sources =
      reactionSpeciesSources(mechanism, mixture, uniformRate(1, cells, 10.0), cells);
  const auto aggressive = projectNegativeSpecies(sources, state, density, 1.0);
  EXPECT_TRUE(aggressive.anyNegative);
  EXPECT_LT(aggressive.worstProjectedValue, 0.0);

  // a small dt does not
  const auto gentle = projectNegativeSpecies(sources, state, density, 1.0e-6);
  EXPECT_FALSE(gentle.anyNegative);
  EXPECT_GT(gentle.worstProjectedValue, 0.0);

  // AND THE SOURCE IS UNCHANGED BY EITHER CALL -- the diagnostic does not clamp
  EXPECT_DOUBLE_EQ(sources.perSpecies[0].Su[0], 0.0625 * -1.0 * 10.0);
}

// ===================================================================
// P18.2 source integration -- sections 18, 19, 20
// ===================================================================

namespace {

// A CLOSED HOMOGENEOUS REACTOR: no flow, no diffusion, zero-gradient walls.
// The species equation collapses to
//
//     d(rho Y_i)/dt = omega_i
//
// and with implicit Euler and a CONSTANT explicit source the discrete update is
// EXACT, not approximate:
//
//     (rho V/dt) Y^{n+1} = (rho Y^n V/dt) + omega V
//     =>  rho Y^{n+1} = rho Y^n + omega dt
//
// so the analytical reference needs no integration scheme of its own.
struct ReactorResult {
  std::vector<ScalarField> fields;
  Real initialTotalMass{};
  Real finalTotalMass{};
  Real maxSumDeviation{};
  Index steps{};
};

ReactorResult runClosedReactor(const Mesh& mesh, const MixtureThermodynamics& mixture,
                               const ReactionMechanism& mechanism, const MassFractionState& initial,
                               const ScalarField& density, const ReactionProgressRates& rates,
                               Real dt, Index steps) {
  const Index n = mesh.numberOfCells();
  const Index ns = initial.speciesCount();

  std::vector<std::unique_ptr<BoundaryConditionSet>> sets;
  for (Index i = 0; i < ns; ++i) {
    auto set = std::make_unique<BoundaryConditionSet>();
    for (const char* patch : {"left", "right", "bottom", "top"}) {
      set->set(mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
    sets.push_back(std::move(set));
  }
  std::vector<const BoundaryConditionSet*> bcs;
  for (const auto& s : sets) {
    bcs.push_back(s.get());
  }

  // THE SOURCES COME FROM P18.4 AND GO STRAIGHT INTO P18.2's EXISTING HOOK.
  // No parallel species update exists (section 18).
  const auto sources = reactionSpeciesSources(mechanism, mixture, rates, n);

  const auto diffusivities =
      cfd::species::makeSpeciesDiffusivities(density, std::vector<Real>(ns, 1.0e-5));
  const SurfaceField noFlow(mesh.numberOfFaces(), 0.0);

  cfd::algebra::LinearSolverSettings settings;
  settings.absoluteTolerance = 1.0e-15;
  settings.relativeTolerance = 1.0e-15;
  settings.maxIterations = 3000;
  const cfd::algebra::BiCGSTAB solver(settings);

  ReactorResult result;
  result.steps = steps;
  std::vector<ScalarField> current = initial.fields();

  auto totalMass = [&](const std::vector<ScalarField>& fields) {
    Real total = 0.0;
    for (Index i = 0; i < ns; ++i) {
      for (Index c = 0; c < n; ++c) {
        total += density[c] * fields[i][c] * mesh.cell(c).volume();
      }
    }
    return total;
  };
  result.initialTotalMass = totalMass(current);

  cfd::species::SpeciesTransportOptions options;
  options.convection = false;  // U = 0
  options.diffusion = false;   // J_i = 0 -- isolates the SOURCE (section 19)

  for (Index step = 0; step < steps; ++step) {
    const std::vector<ScalarField> old = current;
    cfd::species::SpeciesTimeState time;
    time.transient = true;
    time.dt = dt;
    time.fieldsOld = &old;
    time.rhoOld = &density;
    time.rhoNew = &density;

    for (Index i = 0; i < ns; ++i) {
      const auto assembly = cfd::species::speciesTransportAssembly(
          mesh, i, initial.registry(), old, noFlow, diffusivities, *bcs[i], sources.perSpecies[i],
          time, nullptr, options);
      cfd::algebra::Vector guess(n, 0.0);
      for (Index c = 0; c < n; ++c) {
        guess[c] = old[i][c];
      }
      const auto solved = solver.solve(assembly.system, guess);
      for (Index c = 0; c < n; ++c) {
        current[i][c] = solved.solution[c];
      }
    }
    for (Index c = 0; c < n; ++c) {
      Real sum = 0.0;
      for (Index i = 0; i < ns; ++i) {
        sum += current[i][c];
      }
      result.maxSumDeviation = std::max(result.maxSumDeviation, std::abs(sum - 1.0));
    }
  }
  result.fields = current;
  result.finalTotalMass = totalMass(current);
  return result;
}

}  // namespace

TEST(ReactionTransportTest, ClosedReactorMatchesTheAnalyticalSourceUpdate) {
  // Section 19: with U = 0, J = 0 and a constant omega_i, the exact update is
  // rho Y_i(t) = rho Y_i(0) + omega_i t. The reference is computed here from
  // M nu q, NOT by calling the production source function.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const ScalarField density(n, 2.0);
  const MassFractionState initial(abcRegistry(), n, {0.30, 0.30, 0.20, 0.20});

  const Real q = 0.02;
  const Real dt = 0.5;
  const Index steps = 10;
  const auto result =
      runClosedReactor(mesh, mixture, mechanism, initial, density, uniformRate(1, n, q), dt, steps);

  // independent reference: omega_i = M_i nu_i q, and rho Y_i advances linearly
  const Real elapsed = dt * static_cast<Real>(steps);
  const Real nu[4] = {-1.0, -2.0, 1.0, 0.0};
  const Real molecularWeight[4] = {kMA, kMB, kMC, kMInert};
  for (Index i = 0; i < 4; ++i) {
    const Real omega = molecularWeight[i] * nu[i] * q;
    const Real expected = (2.0 * initial.massFraction(i, 0)) + (omega * elapsed);
    for (Index c = 0; c < n; ++c) {
      EXPECT_NEAR(density[c] * result.fields[i][c], expected, 1.0e-12)
          << "species " << i << " cell " << c;
    }
  }
  // the INERT species must not have moved at all
  for (Index c = 0; c < n; ++c) {
    EXPECT_NEAR(result.fields[3][c], initial.massFraction(3, 0), 1.0e-14) << "cell " << c;
  }
}

TEST(ReactionTransportTest, ClosedReactorConservesTotalSpeciesMass) {
  // Section 20: with sum_i omega_i = 0 the total species mass is invariant.
  // Reported, not just asserted.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const ScalarField density(n, 1.5);
  const MassFractionState initial(abcRegistry(), n, {0.30, 0.30, 0.20, 0.20});

  const auto result = runClosedReactor(mesh, mixture, mechanism, initial, density,
                                       uniformRate(1, n, 0.015), 0.5, 20);

  const Real drift = std::abs(result.finalTotalMass - result.initialTotalMass);
  const Real relative = drift / std::abs(result.initialTotalMass);
  EXPECT_LE(relative, 8.0 * kEps * static_cast<Real>(n))
      << "initial " << result.initialTotalMass << " final " << result.finalTotalMass
      << " absolute drift " << drift;

  // ...and sum(Y) stayed 1 without any normalisation
  EXPECT_LE(result.maxSumDeviation,
            cfd::species::massFractionSumTolerance(4) * static_cast<Real>(result.steps) * 8.0)
      << "max |sum Y - 1| = " << result.maxSumDeviation;
}

TEST(ReactionTransportTest, ZeroProgressRateReproducesP18_2Exactly) {
  // Section 17 and 47: with a mechanism present but zero progress rate, the
  // result must be BITWISE what P18.2 produces with no reaction at all.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = abcMixture();
  const auto mechanism = abcMechanism();
  const ScalarField density(n, 1.5);
  const MassFractionState initial(abcRegistry(), n, {0.30, 0.30, 0.20, 0.20});

  const auto withMechanism = runClosedReactor(mesh, mixture, mechanism, initial, density,
                                              zeroProgressRates(1, n), 0.5, 10);

  // the same run with P18.2's OWN zero source, constructed without any reaction
  const auto zeroSources = cfd::species::zeroSpeciesSources(4, n);
  const auto reactionZero = reactionSpeciesSources(mechanism, mixture, zeroProgressRates(1, n), n);
  for (Index i = 0; i < 4; ++i) {
    for (Index c = 0; c < n; ++c) {
      EXPECT_EQ(reactionZero.perSpecies[i].Su[c], zeroSources.perSpecies[i].Su[c])
          << "a zero-rate mechanism must give BITWISE the same source as no mechanism at all";
      EXPECT_EQ(reactionZero.perSpecies[i].Sp[c], zeroSources.perSpecies[i].Sp[c]);
    }
  }
  // nothing moved
  for (Index i = 0; i < 4; ++i) {
    for (Index c = 0; c < n; ++c) {
      EXPECT_NEAR(withMechanism.fields[i][c], initial.massFraction(i, 0), 1.0e-14)
          << "species " << i << " cell " << c;
    }
  }
}

TEST(ReactionTransportTest, N7_AnUnknownSpeciesCannotEnterAMechanism) {
  // CONTROL N7, stated as its own test: a reaction cannot introduce a species the
  // registry does not hold, so a mechanism can never address a species the state
  // and the mixture do not share.
  const SpeciesRegistry registry = abcRegistry();
  EXPECT_THROW(Reaction("bad", registry, {{"Krypton", 1.0}}, {{"C", 1.0}}), InvalidArgumentError);
  EXPECT_THROW(Reaction("bad", registry, {{"A", 1.0}}, {{"Krypton", 1.0}}), InvalidArgumentError);
  // and the empty-string species, which a naive name lookup might accept
  EXPECT_THROW(Reaction("bad", registry, {{"", 1.0}}, {{"C", 1.0}}), InvalidArgumentError);
}
