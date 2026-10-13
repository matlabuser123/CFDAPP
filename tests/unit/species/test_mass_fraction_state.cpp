// P18.1 -- the multi-species mass-fraction state: identity
// (SpeciesRegistry) and the bounded, sum-to-one composition
// (MassFractionState).
//
// Every threshold exercised here is the one frozen in
// results/p18/gates/P18_GATES.md G2 BEFORE this file existed:
//   bounds  0 <= Y_i <= 1, boolean, no tolerance
//   sum     |sum_i Y_i - 1| <= 4 * eps * N_species, per cell
// Nothing in this file derives or relaxes either. The non-vacuity tests at
// the end prove the sum criterion can both pass and fail, which is what
// stops it from being a tolerance wide enough to accept anything.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/SpeciesRegistry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::species::MassFractionState;
using cfd::species::massFractionSumTolerance;
using cfd::species::normalizeMassFractions;
using cfd::species::SpeciesRegistry;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();
const Real kNaN = std::numeric_limits<Real>::quiet_NaN();
const Real kInf = std::numeric_limits<Real>::infinity();

SpeciesRegistry binaryRegistry() { return SpeciesRegistry({"O2", "N2"}); }
SpeciesRegistry ternaryRegistry() { return SpeciesRegistry({"CH4", "O2", "N2"}); }

}  // namespace

// ===================================================================
// SpeciesRegistry -- identity (section 4, section 20)
// ===================================================================

TEST(SpeciesRegistryTest, StoresNamesInOrderAndMapsBothWays) {
  const SpeciesRegistry registry = ternaryRegistry();
  EXPECT_EQ(registry.size(), 3u);

  EXPECT_EQ(registry.name(0), "CH4");
  EXPECT_EQ(registry.name(1), "O2");
  EXPECT_EQ(registry.name(2), "N2");

  EXPECT_EQ(registry.index("CH4"), 0u);
  EXPECT_EQ(registry.index("O2"), 1u);
  EXPECT_EQ(registry.index("N2"), 2u);
}

TEST(SpeciesRegistryTest, NameIndexMappingIsBijective) {
  // Section 4: one authoritative mechanism, and it must round-trip in both
  // directions for every entry.
  const SpeciesRegistry registry = ternaryRegistry();
  for (Index i = 0; i < registry.size(); ++i) {
    EXPECT_EQ(registry.index(registry.name(i)), i);
  }
  for (const std::string& name : registry.names()) {
    EXPECT_EQ(registry.name(registry.index(name)), name);
  }
}

TEST(SpeciesRegistryTest, LookupIsIndependentOfDeclarationOrder) {
  // G2(c): the mapping is order-independent in the sense that a name always
  // resolves to ITS OWN slot, whatever order the registry was built in.
  const SpeciesRegistry forward({"CH4", "O2", "N2"});
  const SpeciesRegistry reversed({"N2", "O2", "CH4"});

  EXPECT_EQ(forward.name(forward.index("O2")), "O2");
  EXPECT_EQ(reversed.name(reversed.index("O2")), "O2");

  // ...and the two are NOT equal, because the ordering is part of the value:
  // a composition validated against one would be misread against the other.
  EXPECT_NE(forward, reversed);
  EXPECT_EQ(forward, SpeciesRegistry({"CH4", "O2", "N2"}));
}

TEST(SpeciesRegistryTest, EqualityDistinguishesDifferentSpecies) {
  // The companion to the ordering test above: equality must also see the
  // NAMES, not just the shape. Two registries of the same size holding
  // entirely different species are different identities, and a state carried
  // between them would silently reinterpret every mass fraction.
  EXPECT_NE(SpeciesRegistry({"O2", "N2"}), SpeciesRegistry({"Ar", "Kr"}));
  EXPECT_NE(SpeciesRegistry({"O2", "N2"}), SpeciesRegistry({"O2", "Ar"}));
  EXPECT_NE(SpeciesRegistry({"O2", "N2"}), SpeciesRegistry({"O2", "N2", "Ar"}));
  EXPECT_EQ(SpeciesRegistry({"O2", "N2"}), SpeciesRegistry({"O2", "N2"}));
}

TEST(SpeciesRegistryTest, SupportsOneSpecies) {
  const SpeciesRegistry registry({"N2"});
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_EQ(registry.index("N2"), 0u);
}

TEST(SpeciesRegistryTest, SupportsManySpeciesWithoutAFixedLimit) {
  // Section 3: N_species >= 1, not hard-coded.
  std::vector<std::string> names;
  for (Index i = 0; i < 64; ++i) {
    names.push_back("S" + std::to_string(i));
  }
  const SpeciesRegistry registry(names);
  EXPECT_EQ(registry.size(), 64u);
  EXPECT_EQ(registry.index("S63"), 63u);
  EXPECT_EQ(registry.name(0), "S0");
}

TEST(SpeciesRegistryTest, RejectsDuplicateNames) {
  EXPECT_THROW(SpeciesRegistry({"O2", "N2", "O2"}), InvalidArgumentError);
}

TEST(SpeciesRegistryTest, RejectsEmptyName) {
  EXPECT_THROW(SpeciesRegistry({"O2", ""}), InvalidArgumentError);
}

TEST(SpeciesRegistryTest, RejectsEmptyRegistry) {
  // sum(Y_i) = 1 is unsatisfiable over zero species, so an empty registry
  // could only ever produce an invalid state.
  EXPECT_THROW(SpeciesRegistry(std::vector<std::string>{}), InvalidArgumentError);
}

TEST(SpeciesRegistryTest, RejectsUnknownSpeciesLookup) {
  const SpeciesRegistry registry = binaryRegistry();
  EXPECT_THROW(registry.index("Ar"), InvalidArgumentError);
  EXPECT_FALSE(registry.contains("Ar"));
  EXPECT_TRUE(registry.contains("O2"));
}

TEST(SpeciesRegistryTest, RejectsOutOfRangeIndex) {
  const SpeciesRegistry registry = binaryRegistry();
  EXPECT_THROW(registry.name(2), InvalidArgumentError);
  EXPECT_THROW(registry.name(static_cast<Index>(-1)), InvalidArgumentError);
}

TEST(SpeciesRegistryTest, DiagnosticNamesTheOffendingEntry) {
  // Section 15: the message must identify the cause, not merely fail.
  try {
    SpeciesRegistry({"O2", "N2", "O2"});
    FAIL() << "expected a duplicate-name rejection";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("O2"), std::string::npos);
    EXPECT_NE(message.find("index 2"), std::string::npos);
  }
}

// ===================================================================
// Valid states (section 28)
// ===================================================================

TEST(MassFractionStateTest, SingleSpeciesMustBeUnity) {
  // Section 10: for N = 1 the only valid composition is Y_0 = 1. This falls
  // out of the sum invariant; no special case exists in the code.
  const MassFractionState state(SpeciesRegistry({"N2"}), 4, {1.0});
  EXPECT_EQ(state.speciesCount(), 1u);
  EXPECT_EQ(state.cellCount(), 4u);
  EXPECT_DOUBLE_EQ(state.massFraction("N2", 3), 1.0);
}

TEST(MassFractionStateTest, SingleSpeciesRejectsAnythingButUnity) {
  EXPECT_THROW(MassFractionState(SpeciesRegistry({"N2"}), 4, {0.5}), InvalidArgumentError);
  EXPECT_THROW(MassFractionState(SpeciesRegistry({"N2"}), 4, {0.0}), InvalidArgumentError);
}

TEST(MassFractionStateTest, AcceptsValidBinaryMixture) {
  const MassFractionState state(binaryRegistry(), 3, {0.25, 0.75});
  EXPECT_EQ(state.speciesCount(), 2u);
  EXPECT_DOUBLE_EQ(state.massFraction("O2", 0), 0.25);
  EXPECT_DOUBLE_EQ(state.massFraction("N2", 0), 0.75);
  EXPECT_DOUBLE_EQ(state.compositionSum(0), 1.0);
}

TEST(MassFractionStateTest, AcceptsValidTernaryMixture) {
  const MassFractionState state(ternaryRegistry(), 2, {0.5, 0.25, 0.25});
  EXPECT_EQ(state.speciesCount(), 3u);
  EXPECT_DOUBLE_EQ(state.massFraction(0, 1), 0.5);
  EXPECT_DOUBLE_EQ(state.massFraction(1, 1), 0.25);
  EXPECT_DOUBLE_EQ(state.massFraction(2, 1), 0.25);
}

TEST(MassFractionStateTest, AcceptsZeroValuedSpeciesWithinAValidMixture) {
  // A species that is simply absent here is valid -- 0 is in [0,1].
  const MassFractionState state(ternaryRegistry(), 2, {0.0, 0.3, 0.7});
  EXPECT_DOUBLE_EQ(state.massFraction("CH4", 0), 0.0);
}

TEST(MassFractionStateTest, AcceptsASpeciesAtExactlyOne) {
  const MassFractionState state(ternaryRegistry(), 2, {1.0, 0.0, 0.0});
  EXPECT_DOUBLE_EQ(state.massFraction("CH4", 1), 1.0);
}

TEST(MassFractionStateTest, AcceptsNearBoundValidValues) {
  const MassFractionState state(binaryRegistry(), 1, {1.0e-300, 1.0 - 1.0e-300});
  EXPECT_DOUBLE_EQ(state.massFraction(0, 0), 1.0e-300);
}

TEST(MassFractionStateTest, SupportsDifferentCompositionsPerCell) {
  ScalarField light(3);
  ScalarField heavy(3);
  light[0] = 0.1;
  light[1] = 0.5;
  light[2] = 0.9;
  heavy[0] = 0.9;
  heavy[1] = 0.5;
  heavy[2] = 0.1;

  const MassFractionState state(binaryRegistry(), {light, heavy});
  EXPECT_EQ(state.cellCount(), 3u);
  EXPECT_DOUBLE_EQ(state.massFraction("O2", 0), 0.1);
  EXPECT_DOUBLE_EQ(state.massFraction("O2", 2), 0.9);
  EXPECT_DOUBLE_EQ(state.massFraction("N2", 2), 0.1);
}

TEST(MassFractionStateTest, CellCompositionRoundTrips) {
  const MassFractionState state(ternaryRegistry(), 2, {0.5, 0.25, 0.25});
  const std::vector<Real> composition = state.cellComposition(1);
  ASSERT_EQ(composition.size(), 3u);
  EXPECT_DOUBLE_EQ(composition[0], 0.5);
  EXPECT_DOUBLE_EQ(composition[1], 0.25);
  EXPECT_DOUBLE_EQ(composition[2], 0.25);
}

// ===================================================================
// Bounds -- G2(b), boolean, no tolerance (section 6)
// ===================================================================

TEST(MassFractionStateTest, RejectsNegativeMassFraction) {
  // The sum here is exactly 1, so ONLY the bounds check can reject it --
  // which is what makes this a bounds test rather than a sum test.
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {-0.25, 1.25}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsATinyNegativeMassFraction) {
  // G2(b) is boolean: -1e-12 has no tolerance to hide in. Sum is exactly 1.
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {-1.0e-12, 1.0 + 1.0e-12}),
               InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsMassFractionAboveOne) {
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {1.25, -0.25}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsNaN) {
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {kNaN, 1.0}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsInf) {
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {kInf, 1.0}), InvalidArgumentError);
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {-kInf, 1.0}), InvalidArgumentError);
}

TEST(MassFractionStateTest, BoundsDiagnosticNamesSpeciesCellAndValue) {
  // Section 15: species, cell, value and admissible range.
  ScalarField a(3, 0.5);
  ScalarField b(3, 0.5);
  a[2] = -0.003;
  b[2] = 1.003;
  try {
    MassFractionState state(binaryRegistry(), {a, b});
    FAIL() << "expected a bounds rejection";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("O2"), std::string::npos);
    EXPECT_NE(message.find("cell 2"), std::string::npos);
    EXPECT_NE(message.find("-0.003"), std::string::npos);
    EXPECT_NE(message.find("[0, 1]"), std::string::npos);
  }
}

// ===================================================================
// Sum -- G2(a) (section 7), and its non-vacuity (section 30)
// ===================================================================

TEST(MassFractionStateTest, ToleranceIsTheFrozenFourEpsN) {
  // The one place the frozen number is asserted literally, so that a change
  // to it breaks a test rather than silently widening every other check.
  EXPECT_DOUBLE_EQ(massFractionSumTolerance(1), 4.0 * kEps);
  EXPECT_DOUBLE_EQ(massFractionSumTolerance(2), 8.0 * kEps);
  EXPECT_DOUBLE_EQ(massFractionSumTolerance(3), 12.0 * kEps);
}

TEST(MassFractionStateTest, AcceptsASumOfExactlyOne) {
  const MassFractionState state(binaryRegistry(), 1, {0.5, 0.5});
  EXPECT_DOUBLE_EQ(state.compositionSum(0), 1.0);
}

TEST(MassFractionStateTest, AcceptsASumWithinRoundOff) {
  // G2(a) is explicitly NOT zero, because a normalised set cannot be exactly
  // 1 in binary floating point for general N. This is that case, and it is
  // what the tolerance exists for.
  //
  // {0.03, 0.282, 0.688} sums to exactly 1 in EXACT arithmetic -- the three
  // are 30/1000, 282/1000 and 688/1000 -- but none is representable in
  // binary, and accumulated left to right they land half an eps away from 1.
  // The deviation was computed independently in Python (outside CFDApp)
  // before this test was written: 1.1102230246251565e-16 == 0.5 * eps,
  // against a bound of 12 * eps for three species.
  const MassFractionState state(ternaryRegistry(), 1, {0.03, 0.282, 0.688});
  const Real deviation = std::abs(state.compositionSum(0) - 1.0);
  EXPECT_GT(deviation, 0.0) << "this composition is meant to be inexact in binary";
  EXPECT_DOUBLE_EQ(deviation, 0.5 * kEps);
  EXPECT_LE(deviation, massFractionSumTolerance(3));
}

TEST(MassFractionStateTest, AThirdsCompositionHappensToSumExactly) {
  // Recorded because it is counter-intuitive and was a genuine P18.1
  // test-authoring error: 3 * fl(1/3) rounds to EXACTLY 1.0, because
  // 1 - 2^-54 is the tie between 1 - 2^-53 and 1, and round-half-to-even
  // picks 1. Thirds are therefore NOT a round-off example, which is why the
  // test above uses a composition that genuinely is one.
  const Real third = 1.0 / 3.0;
  const MassFractionState state(ternaryRegistry(), 1, {third, third, third});
  EXPECT_EQ(state.compositionSum(0), 1.0);
}

TEST(MassFractionStateTest, RejectsASumMeaningfullyAboveOne) {
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {0.5, 0.51}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsASumMeaningfullyBelowOne) {
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {0.5, 0.49}), InvalidArgumentError);
}

TEST(MassFractionStateTest, SumCriterionIsNonVacuousJustInsideAndJustOutside) {
  // SECTION 30, the whole point: the tolerance must distinguish a
  // round-off-valid composition from a meaningfully invalid one. Both cases
  // are built by stepping the SAME composition across the frozen bound in
  // ULPs, so nothing here depends on a hand-picked magnitude.
  const Real tolerance = massFractionSumTolerance(2);

  // Just INSIDE: a deviation of one eps, well under the 8*eps bound.
  const Real inside = 0.5 + kEps;
  const MassFractionState accepted(binaryRegistry(), 1, {inside, 0.5});
  EXPECT_LE(std::abs(accepted.compositionSum(0) - 1.0), tolerance);

  // Just OUTSIDE: the smallest deviation the bound must reject. 16*eps is
  // twice the bound and still ~3.6e-15 -- far below anything a human would
  // call an error, which is exactly why a loose tolerance like 1e-3 would
  // be worthless here.
  const Real outside = 0.5 + 16.0 * kEps;
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {outside, 0.5}), InvalidArgumentError);
}

TEST(MassFractionStateTest, SumDiagnosticNamesCellSumAndTolerance) {
  // The offending value is DYADIC (0.5625 = 9 * 2^-4), so the sum is
  // exactly 1.0625 and its 17-significant-digit rendering is exactly
  // "1.0625". A decimal like 0.541 would render as "1.0409999999999999" --
  // correct, but it makes the assertion depend on how many digits the
  // formatter emits rather than on what the diagnostic says. Choosing an
  // exactly representable control instead is the convention the P18 gate
  // rules already ask for.
  ScalarField a(3, 0.5);
  ScalarField b(3, 0.5);
  b[2] = 0.5625;  // cell 2 sums to exactly 1.0625
  try {
    MassFractionState state(binaryRegistry(), {a, b});
    FAIL() << "expected a sum rejection";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("cell 2"), std::string::npos) << message;
    EXPECT_NE(message.find("1.0625"), std::string::npos) << message;
    // and it must state the bound the caller was judged against
    EXPECT_NE(message.find("within"), std::string::npos) << message;
  }
}

// ===================================================================
// Per-cell validation (section 16)
// ===================================================================

TEST(MassFractionStateTest, DetectsExactlyOneInvalidCellAmongManyValidOnes) {
  // The trap section 16 names: a single bad cell whose defect is invisible
  // to a global min/max/mean. Here the global min is 0.5 and the global max
  // is 0.5 for species O2 -- only a per-cell sum can see the defect.
  const Index cells = 1000;
  ScalarField a(cells, 0.5);
  ScalarField b(cells, 0.5);
  b[717] = 0.5 + 1.0e-6;

  EXPECT_THROW(MassFractionState(binaryRegistry(), {a, b}), InvalidArgumentError);

  try {
    MassFractionState state(binaryRegistry(), {a, b});
    FAIL() << "expected a per-cell rejection";
  } catch (const InvalidArgumentError& error) {
    EXPECT_NE(std::string(error.what()).find("cell 717"), std::string::npos);
  }
}

TEST(MassFractionStateTest, AValidThousandCellStatePasses) {
  // The control for the test above: the same size, no planted defect.
  const Index cells = 1000;
  const ScalarField a(cells, 0.5);
  const ScalarField b(cells, 0.5);
  const MassFractionState state(binaryRegistry(), {a, b});
  EXPECT_NO_THROW(state.validate());
  EXPECT_EQ(state.cellCount(), cells);
}

// ===================================================================
// Structural rejection (section 19, section 20)
// ===================================================================

TEST(MassFractionStateTest, RejectsWrongCompositionLength) {
  EXPECT_THROW(MassFractionState(ternaryRegistry(), 1, {0.5, 0.5}), InvalidArgumentError);
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {0.3, 0.3, 0.4}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsWrongFieldCount) {
  const ScalarField field(2, 0.5);
  EXPECT_THROW(MassFractionState(ternaryRegistry(), {field, field}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsMismatchedFieldSizes) {
  const ScalarField shortField(2, 0.5);
  const ScalarField longField(3, 0.5);
  EXPECT_THROW(MassFractionState(binaryRegistry(), {shortField, longField}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsZeroCells) {
  EXPECT_THROW(MassFractionState(binaryRegistry(), 0, {0.5, 0.5}), InvalidArgumentError);
  const ScalarField empty(0);
  EXPECT_THROW(MassFractionState(binaryRegistry(), {empty, empty}), InvalidArgumentError);
}

TEST(MassFractionStateTest, RejectsUnknownSpeciesAndOutOfRangeAccess) {
  const MassFractionState state(binaryRegistry(), 2, {0.5, 0.5});
  EXPECT_THROW(state.massFraction("Ar", 0), InvalidArgumentError);
  EXPECT_THROW(state.massFraction(2, 0), InvalidArgumentError);
  EXPECT_THROW(state.massFraction(0, 2), InvalidArgumentError);
  EXPECT_THROW(state.field("Ar"), InvalidArgumentError);
  EXPECT_THROW(state.field(2), InvalidArgumentError);
  EXPECT_THROW(state.cellComposition(2), InvalidArgumentError);
  EXPECT_THROW(state.compositionSum(2), InvalidArgumentError);
}

// ===================================================================
// Mutation policy (section 18, section 19)
// ===================================================================

TEST(MassFractionStateTest, SetCellCompositionAcceptsAValidComposition) {
  MassFractionState state(ternaryRegistry(), 3, {0.5, 0.25, 0.25});
  state.setCellComposition(1, {0.2, 0.3, 0.5});

  EXPECT_DOUBLE_EQ(state.massFraction(0, 1), 0.2);
  EXPECT_DOUBLE_EQ(state.massFraction(1, 1), 0.3);
  EXPECT_DOUBLE_EQ(state.massFraction(2, 1), 0.5);

  // Neighbouring cells are untouched.
  EXPECT_DOUBLE_EQ(state.massFraction(0, 0), 0.5);
  EXPECT_DOUBLE_EQ(state.massFraction(0, 2), 0.5);
}

TEST(MassFractionStateTest, ARejectedMutationLeavesTheStateUnchanged) {
  // Section 18: no temporary invalid state may leak. The write is atomic --
  // validation happens before any element is touched.
  MassFractionState state(binaryRegistry(), 2, {0.5, 0.5});

  EXPECT_THROW(state.setCellComposition(0, {0.9, 0.9}), InvalidArgumentError);
  EXPECT_DOUBLE_EQ(state.massFraction(0, 0), 0.5);
  EXPECT_DOUBLE_EQ(state.massFraction(1, 0), 0.5);

  EXPECT_THROW(state.setCellComposition(0, {kNaN, 0.5}), InvalidArgumentError);
  EXPECT_DOUBLE_EQ(state.massFraction(0, 0), 0.5);

  EXPECT_THROW(state.setCellComposition(0, {0.5}), InvalidArgumentError);
  EXPECT_DOUBLE_EQ(state.massFraction(0, 0), 0.5);

  EXPECT_NO_THROW(state.validate());
}

TEST(MassFractionStateTest, SetCellCompositionRejectsAnOutOfRangeCell) {
  MassFractionState state(binaryRegistry(), 2, {0.5, 0.5});
  EXPECT_THROW(state.setCellComposition(2, {0.5, 0.5}), InvalidArgumentError);
}

// ===================================================================
// Determinism (section 17)
// ===================================================================

TEST(MassFractionStateTest, ValidationIsDeterministicAcrossRepeats) {
  // G10 at the state layer: the fixed ascending-index accumulation must
  // give a BITWISE identical sum every time, or the sum criterion is not
  // reproducible and no gate resting on it means anything.
  const Real third = 1.0 / 3.0;
  const MassFractionState state(ternaryRegistry(), 8, {third, third, third});

  const Real first = state.compositionSum(3);
  for (int repeat = 0; repeat < 100; ++repeat) {
    EXPECT_EQ(state.compositionSum(3), first) << "sum changed on repeat " << repeat;
    EXPECT_NO_THROW(state.validate());
  }
}

TEST(MassFractionStateTest, SumIsIdenticalAcrossCopies) {
  const Real third = 1.0 / 3.0;
  const MassFractionState original(ternaryRegistry(), 4, {third, third, third});
  const MassFractionState copy = original;
  EXPECT_EQ(copy.compositionSum(0), original.compositionSum(0));
}

// ===================================================================
// Copy / move semantics (section 21)
// ===================================================================

TEST(MassFractionStateTest, CopyPreservesIdentityValuesAndOrdering) {
  const MassFractionState original(ternaryRegistry(), 2, {0.5, 0.25, 0.25});
  const MassFractionState copy = original;

  EXPECT_EQ(copy.registry(), original.registry());
  EXPECT_EQ(copy.registry().names(), original.registry().names());
  EXPECT_EQ(copy.speciesCount(), original.speciesCount());
  EXPECT_EQ(copy.cellCount(), original.cellCount());
  for (Index i = 0; i < copy.speciesCount(); ++i) {
    for (Index cell = 0; cell < copy.cellCount(); ++cell) {
      EXPECT_EQ(copy.massFraction(i, cell), original.massFraction(i, cell));
    }
  }
  EXPECT_NO_THROW(copy.validate());
}

TEST(MassFractionStateTest, CopyIsIndependentOfTheOriginal) {
  MassFractionState original(binaryRegistry(), 2, {0.5, 0.5});
  MassFractionState copy = original;
  copy.setCellComposition(0, {0.1, 0.9});

  EXPECT_DOUBLE_EQ(copy.massFraction(0, 0), 0.1);
  EXPECT_DOUBLE_EQ(original.massFraction(0, 0), 0.5);
}

TEST(MassFractionStateTest, MovePreservesIdentityAndValues) {
  MassFractionState original(ternaryRegistry(), 2, {0.5, 0.25, 0.25});
  const MassFractionState moved = std::move(original);

  EXPECT_EQ(moved.speciesCount(), 3u);
  EXPECT_EQ(moved.cellCount(), 2u);
  EXPECT_EQ(moved.registry().name(1), "O2");
  EXPECT_DOUBLE_EQ(moved.massFraction("O2", 1), 0.25);
  EXPECT_NO_THROW(moved.validate());
}

// ===================================================================
// Normalisation -- explicit only (section 8, section 9)
// ===================================================================

TEST(MassFractionStateTest, ConstructionNeverNormalisesSilently) {
  // THE section 8 case, verbatim: [0.8, 0.8] must NOT become [0.5, 0.5].
  EXPECT_THROW(MassFractionState(binaryRegistry(), 1, {0.8, 0.8}), InvalidArgumentError);

  MassFractionState state(binaryRegistry(), 1, {0.5, 0.5});
  EXPECT_THROW(state.setCellComposition(0, {0.8, 0.8}), InvalidArgumentError);
  EXPECT_DOUBLE_EQ(state.massFraction(0, 0), 0.5);
}

TEST(MassFractionStateTest, ExplicitNormalisationSumsToOne) {
  const std::vector<Real> normalized = normalizeMassFractions({0.8, 0.8});
  ASSERT_EQ(normalized.size(), 2u);
  EXPECT_DOUBLE_EQ(normalized[0], 0.5);
  EXPECT_DOUBLE_EQ(normalized[1], 0.5);
}

TEST(MassFractionStateTest, NormalisedCompositionIsAcceptedByTheState) {
  const std::vector<Real> normalized = normalizeMassFractions({2.0, 1.0, 1.0});
  const MassFractionState state(ternaryRegistry(), 1, normalized);
  EXPECT_DOUBLE_EQ(state.massFraction(0, 0), 0.5);
  EXPECT_DOUBLE_EQ(state.massFraction(1, 0), 0.25);
}

TEST(MassFractionStateTest, NormalisationAcceptsEntriesGreaterThanOne) {
  // [2, 2] is a legitimate un-normalised ratio, not an invalid composition.
  const std::vector<Real> normalized = normalizeMassFractions({2.0, 2.0});
  EXPECT_DOUBLE_EQ(normalized[0], 0.5);
  EXPECT_DOUBLE_EQ(normalized[1], 0.5);
}

TEST(MassFractionStateTest, NormalisationRejectsPathologicalInputs) {
  EXPECT_THROW(normalizeMassFractions({}), InvalidArgumentError);
  EXPECT_THROW(normalizeMassFractions({0.0, 0.0}), InvalidArgumentError);
  EXPECT_THROW(normalizeMassFractions({-1.0, 2.0}), InvalidArgumentError);
  EXPECT_THROW(normalizeMassFractions({kNaN, 1.0}), InvalidArgumentError);
  EXPECT_THROW(normalizeMassFractions({kInf, 1.0}), InvalidArgumentError);
}

// ===================================================================
// Passive-scalar compatibility (section 11, section 31)
// ===================================================================

TEST(MassFractionStateTest, HoldsAnExistingPassiveScalarFieldBitwise) {
  // Section 31: the pre-P18 stack stores one cfd::fields::ScalarField per
  // species. The new state must be able to hold exactly those objects and
  // give them back UNCHANGED -- bitwise, not "close" -- so that migrating a
  // field into the state cannot perturb a number.
  //
  // The values are deliberately awkward binary fractions, of the kind a
  // converged solve actually produces.
  ScalarField solved(5);
  solved[0] = 0.123456789012345;
  solved[1] = 0.999999999999999;
  solved[2] = 1.0e-17;
  solved[3] = 0.333333333333333;
  solved[4] = 0.5;

  // The complementary species makes it a closed mixture without altering
  // the field under test.
  ScalarField complement(5);
  for (Index cell = 0; cell < solved.size(); ++cell) {
    complement[cell] = 1.0 - solved[cell];
  }

  const MassFractionState state(binaryRegistry(), {solved, complement});

  for (Index cell = 0; cell < solved.size(); ++cell) {
    EXPECT_EQ(state.massFraction("O2", cell), solved[cell]) << "cell " << cell;
    EXPECT_EQ(state.field("O2")[cell], solved[cell]) << "cell " << cell;
  }
}

TEST(MassFractionStateTest, FieldAccessorReturnsTheWholeFieldForExport) {
  // Export and P18.2 transport both work field-at-a-time; this is the
  // accessor they use, and it must agree with the per-cell one.
  const MassFractionState state(binaryRegistry(), 4, {0.25, 0.75});
  const ScalarField& field = state.field("N2");
  ASSERT_EQ(field.size(), 4u);
  for (Index cell = 0; cell < field.size(); ++cell) {
    EXPECT_EQ(field[cell], state.massFraction("N2", cell));
  }
}

// ===================================================================
// Compatibility with a REAL shipped passive-species case (section 31)
// ===================================================================
//
// Section 31 asks for a compatibility test built from an existing
// passive-species case rather than from invented numbers. These are the
// actual converged values of the `concentration_CO2` column of
//     cases/species_diffusion/results/fields.csv
// -- the committed output of a shipped production case, 80 cells, produced
// by the pre-P18 stack through case -> parser -> CaseBuilder ->
// ProjectRunner -> SpeciesSolver -> CSV.
namespace {
constexpr Real kShippedCO2[80] = {
    9.74999968057570721e-01, 9.24999904744416734e-01, 8.74999843788056730e-01,
    8.24999786675065128e-01, 7.74999734819272645e-01, 7.24999689492682231e-01,
    6.74999651814304769e-01, 6.24999622713549918e-01, 5.74999602906218654e-01,
    5.24999592877470556e-01, 4.74999592875987420e-01, 4.24999602904767126e-01,
    3.74999622715138481e-01, 3.24999651815602919e-01, 2.74999689491227661e-01,
    2.24999734817638131e-01, 1.74999786677441954e-01, 1.24999843787758413e-01,
    7.49999047460126467e-02, 2.49999680492757233e-02, 9.74999968833643260e-01,
    9.24999907029396606e-01, 8.74999847546644283e-01, 8.24999791802617954e-01,
    7.74999741191870317e-01, 7.24999696956380002e-01, 6.74999660187608819e-01,
    6.24999631783392351e-01, 5.74999612447757880e-01, 5.24999602663432596e-01,
    4.74999602667509624e-01, 4.24999612451578690e-01, 3.74999631779301290e-01,
    3.24999660183676264e-01, 2.74999696961221507e-01, 2.24999741194345709e-01,
    1.74999791800387799e-01, 1.24999847538424122e-01, 7.49999070445418053e-02,
    2.49999688141461819e-02, 9.74999968826535501e-01, 9.24999907034239843e-01,
    8.74999847542622500e-01, 8.24999791802822124e-01, 7.74999741195039449e-01,
    7.24999696957025930e-01, 6.74999660182707295e-01, 6.24999631782793164e-01,
    5.74999612453740316e-01, 5.24999602664521503e-01, 4.74999602660498899e-01,
    4.24999612449832032e-01, 3.74999631787060195e-01, 3.24999660186268524e-01,
    2.74999696952984096e-01, 2.24999741190847036e-01, 1.74999791808757355e-01,
    1.24999847542865597e-01, 7.49999070362877135e-02, 2.49999688089898066e-02,
    9.74999968051095567e-01, 9.24999904746902746e-01, 8.74999843787566789e-01,
    8.24999786675854607e-01, 7.74999734817678476e-01, 7.24999689492682786e-01,
    6.74999651816040602e-01, 6.24999622713771630e-01, 5.74999602903960905e-01,
    5.24999592877113175e-01, 4.74999592878608323e-01, 4.24999602905393237e-01,
    3.74999622712221814e-01, 3.24999651814662116e-01, 2.74999689494313360e-01,
    2.24999734818939812e-01, 1.74999786674288227e-01, 1.24999843786125692e-01,
    7.49999047490612220e-02, 2.49999680513054191e-02};
}  // namespace

TEST(MassFractionStateTest, ShippedPassiveCaseValuesAreAllWithinBounds) {
  // The first half of compatibility: every value the shipped case produced
  // satisfies the new BOUNDS invariant. Had any converged value fallen
  // outside [0,1], the mass-fraction interpretation would be unavailable for
  // the data this project actually ships, and that would be a finding.
  for (Index cell = 0; cell < 80; ++cell) {
    EXPECT_GE(kShippedCO2[cell], 0.0) << "cell " << cell;
    EXPECT_LE(kShippedCO2[cell], 1.0) << "cell " << cell;
  }
}

TEST(MassFractionStateTest, ShippedPassiveCaseIsNotByItselfAValidMixture) {
  // THE GAP, RECORDED RATHER THAN PAPERED OVER (section 31: "if old
  // semantics do not map one-to-one, document the gap instead of faking
  // compatibility").
  //
  // The shipped case declares ONE species. Its concentration field is a
  // passive TRACER -- values in [0,1] that do not sum to anything, because
  // there is no second species and no carrier. Read as a one-species
  // MIXTURE it must be rejected, since the only admissible one-species
  // composition is Y = 1.
  //
  // This is correct behaviour, not an incompatibility: a passive tracer is
  // not a composition. It is recorded here because P18.2 has to decide how
  // a case declares the complementary/carrier species, and this test is
  // what will change when it does.
  ScalarField tracer(80);
  for (Index cell = 0; cell < 80; ++cell) {
    tracer[cell] = kShippedCO2[cell];
  }
  EXPECT_THROW(MassFractionState(SpeciesRegistry({"CO2"}), {tracer}), InvalidArgumentError);
}

TEST(MassFractionStateTest, ShippedPassiveCaseBecomesAValidMixtureWithACarrier) {
  // The second half: with an explicit carrier species the same data IS a
  // valid mixture, and the original values come back BITWISE -- not
  // "close". Migrating a shipped field into the new state cannot perturb a
  // number.
  ScalarField co2(80);
  ScalarField carrier(80);
  for (Index cell = 0; cell < 80; ++cell) {
    co2[cell] = kShippedCO2[cell];
    carrier[cell] = 1.0 - kShippedCO2[cell];
  }

  const MassFractionState state(SpeciesRegistry({"CO2", "N2"}), {co2, carrier});

  for (Index cell = 0; cell < 80; ++cell) {
    EXPECT_EQ(state.massFraction("CO2", cell), kShippedCO2[cell]) << "cell " << cell;
    EXPECT_EQ(state.field("CO2")[cell], kShippedCO2[cell]) << "cell " << cell;
  }
  EXPECT_NO_THROW(state.validate());
}
