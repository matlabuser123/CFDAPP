# P18.1 — negative controls (§29)

```text
METHODOLOGY   P15's, reused unchanged. Each control removes ONE invariant and requires the tests
              that guard it to FAIL. Every mutation is restored from a pre-control copy and the
              restore is VERIFIED BY sha256. The test binary is DELETED before each rebuild and
              required back, so a mutation that fails to compile can never leave a stale binary
              that then "passes" and is recorded UNDETECTED.
HARNESS       tools/negative_controls.sh
FINAL RUN     logs/06_negative_controls_final.log
FIRST RUN     logs/04_negative_controls.log   (preserved -- C4 reported VACUOUS; see §2)
RESULT        5 of 5 DETECTED. Production source byte-identical to pre-control afterwards.
```

---

## 1. The controls

| # | What is removed | Mutation | Result |
|---|---|---|---|
| C1 | bounds `0 ≤ Y ≤ 1` | widened to ±1e9 | **DETECTED** — 4 tests |
| C2 | sum-to-one | tolerance widened to 1e9 | **DETECTED** — 5 named tests, 8 failing in all |
| C3 | finiteness | condition made unsatisfiable | **DETECTED** — NaN escapes |
| C4 | species identity | registry equality reduced to size-only | **DETECTED** — 2 tests |
| C5 | the frozen threshold itself | `4·eps·N` → `1e-3` | **DETECTED** — 2 tests |

```text
C1 bounds        RejectsNegativeMassFraction · RejectsATinyNegativeMassFraction
                 RejectsMassFractionAboveOne · BoundsDiagnosticNamesSpeciesCellAndValue
C2 sum           RejectsASumMeaningfullyAboveOne · RejectsASumMeaningfullyBelowOne
                 SumCriterionIsNonVacuousJustInsideAndJustOutside
                 ConstructionNeverNormalisesSilently
                 DetectsExactlyOneInvalidCellAmongManyValidOnes
C3 finiteness    RejectsNaN
C4 identity      LookupIsIndependentOfDeclarationOrder · EqualityDistinguishesDifferentSpecies
C5 threshold     ToleranceIsTheFrozenFourEpsN · SumCriterionIsNonVacuousJustInsideAndJustOutside
```

Each control asserts the **mechanism** — the specific tests that must fail — not merely a nonzero
exit, because a failure for an unrelated reason is a false DETECTED.

## 2. C4 first reported VACUOUS, and the harness was wrong, not the control

Preserved, because §29 and CLAUDE.md §1.5 both forbid quietly rewriting a failed experiment.

```text
FIRST RUN     C4 identity: **VACUOUS**
              "NOT DETECTED by MassFractionStateTest.LookupIsIndependentOfDeclarationOrder"
              ...while the run also reported 1 test failing.

INVESTIGATION logs/05_c4_investigation.log -- the mutation was applied, the build succeeded, and
              SpeciesRegistryTest.LookupIsIndependentOfDeclarationOrder DID fail, exactly as
              predicted, at test_mass_fraction_state.cpp:84 on EXPECT_NE(forward, reversed).

CAUSE         the harness grepped for the test under the WRONG SUITE NAME -- MassFractionStateTest
              instead of SpeciesRegistryTest. The control worked; its bookkeeping did not.

DIRECTION     the harness FAILED CLOSED. A name it could not find was reported as vacuity, not as
              a detection. An instrument that fails the other way -- claiming a detection it did
              not observe -- is the one that silently qualifies a vacuous gate.

FIX           the expected test names were corrected, and the identity invariant was given a
              SECOND guard (EqualityDistinguishesDifferentSpecies), because the original control
              rested on a single assertion inside one test. Both now fail under C4.
```

The corrected run is `logs/06_negative_controls_final.log`. The original is not overwritten.

## 3. C3 is deliberately narrower than it looks

```text
PREDICTED BEFORE RUNNING   only NaN escapes. +Inf is still caught by `value > 1` and -Inf by
                           `value < 0`, so RejectsInf must STILL PASS.
MEASURED                   RejectsNaN FAILED (detection). RejectsInf passed -- CONFIRMED.
```

A control that had claimed `RejectsInf` would fail here would have been wrong about its own
mechanism, and would have been recorded as a detection for the wrong reason. The prediction was
written into the harness before the run and is checked explicitly.

This also establishes that the finiteness check is **not redundant**: without it, NaN passes both
remaining checks silently, because every comparison against NaN is false.

## 4. Restoration

```text
pre-control   0984baa4d0a6abb21cb0eecd1d311a4beed29b4ed7b70a6f70aef66f3f306e6c  MassFractionState.cpp
              d55652da02e0a85f9d8fad7f87de664804057edbf82f4f3d3282eb2e9320e8dd  SpeciesRegistry.cpp

after all five controls:   sha256sum -c  ->  both OK
                           "every control restored; production source is byte-identical"
clean rebuild + rerun:     57 / 57 PASSED
```

## 5. What is NOT controlled here, and why

```text
G1 regression preservation   controlled by the regression itself: P18.1 is additive, and the
                             prediction (+57 rows, nothing else moving) is falsifiable by any
                             fifth failure or any other delta. See regression.md.
G10 solver determinism       NOT claimed by P18.1. The state-layer determinism above is not the
                             solver-level claim, which belongs to P18.8/P18.9.
transport / chemistry        no such code exists yet; a control would have nothing to break.
```
