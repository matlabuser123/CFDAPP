# TD-046 — RESOLVED. All five frozen criteria pass, and λ(T)-is-consumed is now TESTED for the first time.

```text
STATUS:      **RESOLVED.** `AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue`
             **PASSES**, in 2866 s against the 7200 s cap. Every criterion in the frozen gate
             is met, in gate order, and nothing was weakened to get there:
               C1 lambda(1800) = 0.200 > 0.150      PASS   (was 0.110 -- the 6 ms death)
               C2 lambda(300)  = 0.05 to 1e-15      PASS
               C3 both solves Converged             PASS
               C4 S_L 8.97806 > 1.3 x 4.95533       PASS by 39 %
               C5 thickness 1.90729 > 1.19636       PASS by 59 %
               C7 CFDSpeciesTests minus G8           PASS  343/343, 22.3 s
             **The property this test exists for is now established by measurement rather than
             by inspection:** a 4x conductivity gives a 1.81x faster flame, where a lambda
             frozen at the inlet would give exactly 1.00x.
             G8 goes from 2 failures to **1**. The remaining one is
             `TheConsumptionIntegralAgreesWithTheEigenvalue`, which is TD-003's §12 stop.
SCOPE:       one constant plus its stale comment, in one test. NO ASSERTION CHANGED. No
             production file touched -- `PremixedFlame.cpp` hash-verified `fa815c9721658448...`
             before the build and unchanged after.
GATE:        GATE.md, frozen `0df08809b21ae1c5b038f970b05ce464dc59e3dc13c28680db5adfbebde3530b`
             BEFORE the edit (logs/00_freeze.log).
EVIDENCE:    logs/00_freeze.log · logs/01_verification.log
             test binary f748885cab21bd00762934c919e7edeca947bf8258cb23038fc6d8effcfecc5a,
             verified newer than its source (§8 fail-closed check).
COMMIT:      carried as evidence.
```

---

## 1. THE RESULT

```text
                      S_L cm/s   S_c cm/s   delta mm        dt        steps   |sumY-1|
lambda constant        4.95533    4.90442    1.19636   9.4243e-06    73683   8.82287e-07
lambda rising (4x)     8.97806    9.05054    1.90729   2.3561e-06    83968   4.18671e-06
ratio                   1.8118                1.5942     0.2500       1.14       4.75
```

Both cases are internally consistent: `S_c` reproduces `S_L` to **1.0 %** (constant) and **0.8 %**
(rising), so these are genuine flames and not the unresolved states that TD-003's step-2 audit
found at 125 and 250 cells.

## 2. C4 BEAT MY OWN PRE-REGISTERED ESTIMATE, IN THE SAFE DIRECTION

```text
predicted S_L ratio, GATE.md section 1.2    1.581   (midpoint lambda, lambda(1050))
MEASURED                                    1.8118
```

The measurement exceeds the estimate by 15 %. Inverting it, `lambda_eff = 0.1641`, i.e. the
effective conductivity sits at `T ~ 1441 K`, not the `1050 K` midpoint I assumed — which is
physically right, because the reaction zone sits toward the **burnt** side where `lambda` is
largest. **My estimate was conservative, so the margin is larger than the gate assumed, not
smaller.** Had it gone the other way, GATE.md section 2.1 committed me to reporting a C4 failure
rather than re-tuning the slope, and that commitment was never tested.

## 3. TWO INDEPENDENT CONFIRMATIONS THAT WERE NOT BEING SOUGHT

### 3.1 `PremixedFlame.cpp:718` behaves exactly as documented

```text
dt constant  9.424300e-06
dt rising    2.356080e-06      ratio 4.0000
```

`lambda(T_ad)` went `0.05 -> 0.20`, a factor of 4, and `dt = maximumCellDiffusionNumber * dx^2 /
alphaMax` predicts exactly `4.0000`. The derivation chain read during TD-006's audit is confirmed
by an experiment that was not designed to test it.

### 3.2 TD-003's floor model predicts this run's `|sum Y - 1|`

```text
|sumY-1|  8.822870e-07 -> 4.186710e-06      factor 4.75
```

TD-003 established `|sum Y - 1| ~ eps * ||b|| * ||A^-1||_inf`. With `dt` 4x smaller, `rho V/dt`
is 4x larger, so `||b||` is ~4x larger and the floor should rise by ~4x. **Measured 4.75x.**

That is the TD-003 mechanism predicting a result from a different experiment it was never fitted
to — the first independent check it has had. It also means this test's `lambda rising` case sits
at `4.19e-06`, **419x** over the `1e-8` bound discussed in TD-003, which strengthens that item's
§12 stop: the bound is not merely unreachable at one state, it moves with `dt`.

## 4. A RISK I FLAGGED THAT DID NOT BITE — and why that is not a dismissal

GATE.md section 2.1 named a specific C3 risk: a 4x `lambda` shrinks `dt` 4x via `:718`, inflating
the required step count via `:1419`, against this fixture's **unscaled** `maximumStepsPerTrial =
20000` at `:1423` — TD-027's mechanism.

```text
total steps       73683 -> 83968, only 1.14x, not 4x
worst per-trial   8256 steps, against the cap of 20000
```

**The cap never bound, because it had headroom.** The mechanism is real and unchanged; it simply
was not reached at these settings. A fixture with a tighter cap, or a larger `lambda` ratio, would
reach it — so this is recorded as "did not trigger here", not as "is not a problem".

## 5. WHAT CHANGED, EXACTLY

```text
tests/unit/species/test_premixed_flame.cpp
  the LinearProperty slope           4.0e-5 -> 1.0e-4
  a stale comment                    "triples" -> "quadruples"
  a derivation added in place        why 1.0e-4 and not the 6.667e-5 minimum
assertion lines changed              0  (audited on the diff)
production files changed             0  (PremixedFlame.cpp hash-verified both sides)
```

**This was a fixture repair, not a threshold change.** The assertions stand exactly as their
author wrote them — `> 3 * kConductivity`, `1e-15`, `1.3x`, and the thickness comparison — and the
fixture now delivers what they already demanded. CLAUDE.md section 7's prohibition on loosening a
test is therefore not engaged, and the audit in section 5 is the evidence for that claim rather
than an assertion of it.

## 6. VERIFICATION LADDER

```text
§7 clang-format    742 files considered, 0 offending
build              CFDSpeciesTests rc=0; binary newer than source (fail-closed check passed)
focused test       1 test, 1 passed, 0 failed, 2866 s
C7 suite           CFDSpeciesTests --gtest_filter=-G8PremixedFlameTest.*
                   343 tests from 56 suites, 343 PASSED, 0 failed, 22337 ms, rc=0
```

**C7 is now MEASURED, not argued.** The edit sits inside one `TEST` body, so the claim was that no
other test could observe it; this run is the check rather than the assertion. The count `343` also
matches the figure on record for this suite-minus-G8, so no test was added, removed or renamed by
the edit. The heavy `G8PremixedFlameTest` cases are excluded deliberately -- each takes 40-70
minutes and the one that matters was run in full in section 1.

## 7. WHAT IS NOT ESTABLISHED

```text
NOT established  C6 (non-vacuity) by demonstration. GATE.md section 4 argued it: a frozen
                 lambda gives an S_L ratio of exactly 1.000 against a 1.3 threshold, and the
                 measured 1.8118 is 81 % above that. But no MUTANT solver with lambda frozen at
                 the inlet was built, so the test's power against that regression is argued from
                 the numbers, not measured. `PremixedFlame.cpp:1052` evaluates lambda per cell,
                 so the defect does not currently exist to catch.
NOT established  that 1.0e-4 is the best slope. It is a DERIVED slope that satisfies both
                 assertions with margin; a different value satisfying the same derivation would
                 serve equally.
NOT run          the FULL §8 regression -- Release across all targets, Debug+GUI, and the
                 sanitizer configurations. C7 was verified on CFDSpeciesTests-minus-G8 (343/343),
                 which covers every test that could observe this edit, but it is one target of
                 many and does not discharge §11.4.
NOT closed       TD-006. This clears ONE of its two G8 failures and touches none of its eight
                 open dependencies. G8's preconditions 2 and 3 (cp and lambda(T) cited;
                 Westbrook & Dryer Set 3 wired) are untouched.
NOT addressed    that neither case reaches steady state -- both report "final march ... (NOT
                 steady)". That is pre-existing, identical in the constant case to the
                 2026-10-11 Release run, and out of scope here.
```
