# §11.4 Release regression — 7 known failures, ZERO new, and FIVE G8 tests fixed since TD-032

```text
STATUS:      Release configuration RUN AND CLASSIFIED. 2840 tests, 7 failures, all of which were
             failing before this campaign's work. FIVE G8 tests that TD-032 recorded as failing now
             PASS. Zero new failures.
             §11.4 is nonetheless PARTIAL: Debug+GUI and the five sanitizer configurations are NOT
             run, so the §8 ladder is one of four configurations deep.
SCOPE:       the §8 Release regression, run to decide whether step 9's production commit may
             proceed. No production change was made by this run.
EVIDENCE:    logs/01_release_regression.log
BASELINES:   TD032_lagged_time_level/logs/05_regression_scoped.log   (the comparison that matters)
             p18/final_qualification/logs/regression/cpu_only_ctest.log     0 of 2775
             p18/final_qualification/logs/regression/release_cuda_ctest.log 0 of 3016
             p18/final_qualification/logs/regression/debug_gui_ctest.log    4 of 3078 (Timeouts)
COMMIT:      none — not authorized. §4 of this document is the decision it needs.
```

---

## 1. The run

```text
configure            OK
build, every target  exit 0
library              0d0dfaa96ac97d5f…   kBuildIdentity "458457542a2e-modified"
binary newer than newest source   OK (§8's fail-closed check)
ctest --preset release --timeout 7200 -j 16
  99% tests passed, 7 tests failed out of 2840
  Total Test time (real) = 7144.02 sec
  ctest exit (unmasked) = 8
```

## 2. CLASSIFICATION — §8 requires it, and all seven are known

§8: *"A 'known' failure stays known only while it reproduces its recorded values exactly. Any new or
changed failure is a regression."*

```text
test                                                        recorded as        now
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate   known, TD-032     FAILS
SSTAutomaticWallChannel.StaysWellBehavedOnMeshes…            known, TD-032     FAILS
SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590       known, TD-032     FAILS
SSTMomentumFluxChannel.FourWayComparisonAtReTau590           known, TD-032     FAILS
DriverSeegmiller.CorrectedMeshSequence                       known, TD-032     FAILS
G8PremixedFlameTest.TheConsumptionIntegralAgreesWith…        failing at TD-032 FAILS
G8PremixedFlameTest.AVariableConductivityChangesTheAnswer…   failing at TD-032 FAILS
```

TD-032's `summary.md` line 27 names the first five verbatim: *"4 SST channel tests +
DriverSeegmiller.CorrectedMeshSequence — long-standing"*. The last two appear in that item's own
scoped-regression log.

**No failure in this run is new, and none has changed verdict class** (all `Failed`, none `Timeout`).

### 2.1 FIVE G8 tests have been FIXED

TD-032's scoped regression recorded **seven** G8PremixedFlameTest failures. Five now pass:

```text
ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature   NOW PASSES
TheEigenvalueIsIndependentOfTheInitialMassFluxGuess                NOW PASSES
TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength         NOW PASSES
TheFlameSpeedConvergesUnderMeshRefinement                          NOW PASSES
TheAnswerIsInsensitiveToTheStepsPerTrial                           NOW PASSES
TheConsumptionIntegralAgreesWithTheEigenvalue                       still fails
AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue  still fails
```

```text
TD-032:  7 G8 + 4 SST + 1 Seegmiller  =  12 failures
now:     2 G8 + 4 SST + 1 Seegmiller  =   7 failures
```

Two of those five — guess independence and the anchor/domain plateau — are the same tests TD-004's
C6 verified directly, so the suite and the gate agree.

### 2.2 The consumption-integral failure is EXPLAINED, and the explanation is today's

`TheConsumptionIntegralAgreesWithTheEigenvalue` fails because the identity `|S_L - S_c|/S_L` is now
`0.01027` against its `0.01` bound — measured hours earlier in TD-005's C3 re-measure, which found
it exceeding at **10 of 12** configurations. So this is not an unexplained failure: it is the same
finding, arriving through the suite.

`AVariableConductivity…` is **not** explained by anything measured today and is recorded as an
open known failure.

## 3. A CORRECTION TO MY OWN C6a WORK

`tests/unit/species/test_premixed_flame.cpp:514` already reads:

```cpp
for (const Index cells : {125, 250, 500}) {
```

with a comment recording `{250,500,1000}` as not finishing, `{63,125,250}` as `p = 0.523` *"NOT
asymptotic; excluded"*, and `{125,250,500}` as `p = 2.057` *"asymptotic, and close to the 1.93 on
record"*.

```text
So the cheap triplet C6a exists to re-derive was ALREADY ADOPTED in the test.
```

My 1000-cell run (`TD004…/logs/A6_02`, killed at exit 124) therefore **confirmed a choice already
made** rather than discovering it was necessary. That is still worth having — the earlier ruling-out
was at 1750 s with a masked exit code, and now it is at the real 7200 s cap — but the framing in
`amendment_A6_proposal.md` §4(C), which treated restoring `{250,500,1000}` as a live option,
overstated how open that question was.

**And the test's own rationale is now stale**: it records `p = 2.057` for `{125,250,500}`, which was
the PRE-FIX value. C6a measured `2.694` post-fix on the same triplet. The test passes anyway, so
whatever it asserts is looser than that comment implies — which is worth checking before the comment
is trusted again.

## 4. WHY §11.4 IS STILL PARTIAL, and what it means for step 9

§8's full regression is four configurations:

```text
Release                      RUN.  7 known failures, 0 new, 5 fixed.
Debug + GUI                  NOT RUN. Its recorded baseline is 4 DriverSeegmiller TIMEOUTS
                             (TD-009's unstable timeout contract), not a clean pass.
sanitizers at CI settings    NOT RUN. TD-035: the full 2860-test asan preset's cost is unmeasured.
clang-format clean           RUN. 742 files, exit 0, 0 offending.
```

**So two of four pass and two are not run.** §11.4 says *"the required regression passes"*, and one
configuration of four is not that — however good the result.

This matters more for step 9 than it did for the ten evidence-only commits that preceded it: those
carried no code to regress, so §11.4's unmet status was formal. **A commit of ~345 production and
test files is exactly the case §11.4 exists for.**

```text
WHAT IS ESTABLISHED   the Release configuration builds clean and regresses with no new failures
                      and five improvements, and formatting is clean.
WHAT IS NOT           Debug+GUI, and any sanitizer configuration, on this tree.
```

## 5. What is NOT established

```text
NOT established  that Debug+GUI would pass. Its recorded baseline already carries 4 timeouts, and
                 Debug is slower, so the G8 cases that take 40-70 min in Release are a real risk
                 against the 7200 s cap.
NOT established  that any sanitizer configuration would complete. TD-035 measured the fixture at
                 348.75 s but the full preset's cost is unmeasured, and CI runs FIVE such
                 configurations of which only ASan+UBSan has been touched.
NOT explained    AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue. It was failing
                 at TD-032 and still is; nothing measured today speaks to it.
NOT re-measured  the 4 SST failures or DriverSeegmiller.CorrectedMeshSequence beyond confirming
                 they still fail. "Long-standing" is TD-032's characterisation, carried forward;
                 their recorded VALUES were not compared because TD-032 recorded names, not values.
NOT a §8 pass    this is one configuration of four. §4.
```

**§5's fourth item is a gap in the baseline rather than in this run.** TD-032 recorded the five
long-standing failures by NAME only, so "reproduces its recorded values exactly" cannot be checked
for them — only "still fails". That is weaker than §8 asks for, and it is the baseline's limitation,
not something this run can repair.
