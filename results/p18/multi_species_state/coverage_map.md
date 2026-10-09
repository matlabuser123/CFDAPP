# P18.1 — coverage map

Every P18.1 requirement mapped to the test that measures it, and every §28 test-matrix row mapped
to its test. Written because a criterion measured by nothing, or by the wrong probe, is the failure
mode this project has hit before (`results/p18/audit/` and the P17.4 coverage finding).

---

## 1. The seven checklist items

| Requirement | Measured by | Result |
|---|---|---|
| Define species state `Y_i` | the type exists and is exercised by all 60 tests; `AcceptsValidBinaryMixture`, `AcceptsValidTernaryMixture`, `CellCompositionRoundTrips` | PASS |
| Support multiple transported species | `SupportsOneSpecies`, `SupportsManySpeciesWithoutAFixedLimit` (N=64), `AcceptsValidBinaryMixture` (N=2), `AcceptsValidTernaryMixture` (N=3), `SingleSpeciesMustBeUnity` (N=1) | PASS |
| Enforce valid mass fractions | `RejectsNegativeMassFraction`, `RejectsMassFractionAboveOne`, `RejectsNaN`, `RejectsInf`, `RejectsWrongCompositionLength`, `ARejectedMutationLeavesTheStateUnchanged` | PASS |
| Verify `0 ≤ Y_i ≤ 1` | `RejectsATinyNegativeMassFraction` (−1e-12, exact sum 1), `AcceptsASpeciesAtExactlyOne`, `AcceptsZeroValuedSpeciesWithinAValidMixture`, `AcceptsNearBoundValidValues` | PASS |
| Verify `ΣY_i = 1` | `AcceptsASumOfExactlyOne`, `AcceptsASumWithinRoundOff`, `RejectsASumMeaningfullyAboveOne`, `RejectsASumMeaningfullyBelowOne`, `SumCriterionIsNonVacuousJustInsideAndJustOutside`, `ToleranceIsTheFrozenFourEpsN` | PASS |
| Preserve passive-scalar behaviour | the 37 pre-existing species unit tests; the three `ShippedPassiveCase*` tests on real committed output; the three-configuration regression | PASS |
| Add unit tests | 60 new tests (12 `SpeciesRegistryTest` + 48 `MassFractionStateTest`) | PASS |

## 2. The §28 test matrix, row by row

| §28 row | Test | Present |
|---|---|---|
| single species Y=1 | `SingleSpeciesMustBeUnity` + `SingleSpeciesRejectsAnythingButUnity` | ✓ |
| valid binary mixture | `AcceptsValidBinaryMixture` | ✓ |
| valid ternary mixture | `AcceptsValidTernaryMixture` | ✓ |
| multiple cells, different compositions | `SupportsDifferentCompositionsPerCell` | ✓ |
| zero-valued species in a valid mixture | `AcceptsZeroValuedSpeciesWithinAValidMixture` | ✓ |
| species value exactly 1 | `AcceptsASpeciesAtExactlyOne` | ✓ |
| near-bound valid values | `AcceptsNearBoundValidValues` (1e-300, 1−1e-300) | ✓ |
| sum exactly 1 | `AcceptsASumOfExactlyOne`, `AThirdsCompositionHappensToSumExactly` | ✓ |
| sum within justified round-off | `AcceptsASumWithinRoundOff` (0.5 eps, derived in Python) | ✓ |
| sum outside tolerance | `RejectsASumMeaningfullyAboveOne`, `...BelowOne`, `SumCriterionIsNonVacuous...` | ✓ |
| negative Y | `RejectsNegativeMassFraction`, `RejectsATinyNegativeMassFraction` | ✓ |
| Y > 1 | `RejectsMassFractionAboveOne` | ✓ |
| NaN | `RejectsNaN` | ✓ |
| Inf | `RejectsInf` (both signs) | ✓ |
| duplicate names | `RejectsDuplicateNames`, `DiagnosticNamesTheOffendingEntry` | ✓ |
| unknown species | `RejectsUnknownSpeciesLookup`, `RejectsUnknownSpeciesAndOutOfRangeAccess` | ✓ |
| invalid index | `RejectsOutOfRangeIndex`, `RejectsUnknownSpeciesAndOutOfRangeAccess` | ✓ |
| wrong composition length | `RejectsWrongCompositionLength`, `RejectsWrongFieldCount`, `RejectsMismatchedFieldSizes` | ✓ |
| deterministic validation | `ValidationIsDeterministicAcrossRepeats` (100 repeats, bitwise), `SumIsIdenticalAcrossCopies` | ✓ |
| passive-scalar compatibility | `HoldsAnExistingPassiveScalarFieldBitwise` + the three `ShippedPassiveCase*` tests | ✓ |

**All 20 rows are covered.** None is covered only by a test that would also pass with the invariant
removed — that is what the five negative controls establish (`negative_controls.md`).

## 3. Requirements from the other sections

| § | Requirement | Test |
|---|---|---|
| 4 | stable identity, bijective mapping | `NameIndexMappingIsBijective`, `LookupIsIndependentOfDeclarationOrder`, `EqualityDistinguishesDifferentSpecies` |
| 4 | reject empty names / empty registry | `RejectsEmptyName`, `RejectsEmptyRegistry` |
| 8 | normalisation is never silent | `ConstructionNeverNormalisesSilently` |
| 9 | explicit normalisation API + pathological input | `ExplicitNormalisationSumsToOne`, `NormalisationAcceptsEntriesGreaterThanOne`, `NormalisationRejectsPathologicalInputs`, `NormalisedCompositionIsAcceptedByTheState` |
| 10 | single-species limit | `SingleSpeciesMustBeUnity`, `SingleSpeciesRejectsAnythingButUnity` |
| 15 | diagnostics identify the cause | `BoundsDiagnosticNamesSpeciesCellAndValue`, `SumDiagnosticNamesCellSumAndTolerance`, `DiagnosticNamesTheOffendingEntry` |
| 16 | per-cell, not aggregate | `DetectsExactlyOneInvalidCellAmongManyValidOnes` (1 bad cell in 1000, global min/max unmoved) + its control `AValidThousandCellStatePasses` |
| 17 | deterministic summation | `ValidationIsDeterministicAcrossRepeats` |
| 18 | mutation policy, no invalid leak | `ARejectedMutationLeavesTheStateUnchanged`, `SetCellCompositionAcceptsAValidComposition`, `SetCellCompositionRejectsAnOutOfRangeCell` |
| 19 | bulk composition assignment validated | `SetCellCompositionAcceptsAValidComposition`, `RejectsWrongCompositionLength` |
| 20 | lookup API, valid and invalid | `StoresNamesInOrderAndMapsBothWays`, `RejectsUnknownSpeciesAndOutOfRangeAccess` |
| 21 | copy / move semantics | `CopyPreservesIdentityValuesAndOrdering`, `CopyIsIndependentOfTheOriginal`, `MovePreservesIdentityAndValues` |
| 22 | valid-by-construction initialisation | both constructors validate; no default constructor exists |
| 25 | conservation interface for P18.2 | `FieldAccessorReturnsTheWholeFieldForExport` (field access for ρY_i later) |
| 27 | export compatibility | `FieldAccessorReturnsTheWholeFieldForExport` |
| 30 | sum-tolerance non-vacuity | `SumCriterionIsNonVacuousJustInsideAndJustOutside` + the ULP ladder in `logs/03_measurements.log` |
| 31 | compatibility from a real case | `ShippedPassiveCaseValuesAreAllWithinBounds`, `ShippedPassiveCaseIsNotByItselfAValidMixture`, `ShippedPassiveCaseBecomesAValidMixtureWithACarrier` |

## 4. Deliberately NOT covered, with the reason

```text
§26 serialization round-trip   NOT APPLICABLE YET -- MassFractionState is not reachable from a
                               case file, so there is no configuration -> state -> serialization
                               -> state path. Recorded as a LIMIT in compatibility.md section 4,
                               not claimed as a pass.
§23 boundary composition       NOT TOUCHED. The boundary lag is a P18.0 finding for P18.2.
§24 remainder species          NOT IMPLEMENTED, deliberately -- no frozen design chose it.
§32 performance                NOT A GATE. No layout was chosen or changed for performance.
§33 GPU                        NO CUDA CODE. P18.8 owns equivalence.
G3 G4 G5 G6 G7 G8 G9 G11       NOT CLAIMED -- §36 forbids claiming them from state-only tests, and
                               none is measurable from this change.
G10 solver determinism         NOT CLAIMED. Only the STATE-LAYER determinism above is measured.
```

## 5. Negative-control coverage of the map itself

Each enforced invariant has at least one test that **fails when the invariant is removed** —
otherwise the row above would be decoration:

```text
bounds        C1 -> 4 tests fail
sum           C2 -> 5 named tests fail (8 in all)
finiteness    C3 -> RejectsNaN fails
identity      C4 -> 2 tests fail
the threshold C5 -> 2 tests fail
```
