# TD-037 VERIFICATION & CLOSURE — against the frozen gate `bf4b9bcff8f70423…`

```text
STATUS:            TD-037 RESOLVED 2026-10-07, on explicit user authorization, with an
                   EXPLICITLY PARTIAL regression -- see §5.
SCOPE:             the drift-reading condition on BOTH extinction paths in `runTrial`. No change
                   to `trialSaysTooLarge`, to the normal path's window-boundary logic, to any
                   threshold, or to any test.
GATE:              GATE.md  sha256 bf4b9bcff8f7042319 5112ca8726d7b53f2f3c23e03309c7f832b4292165717e5
                   (frozen before the fix; never amended after)
RESULT:            C0 no longer reproduces · C1 PASS 4/4 · C2 PASS 0.000 % · C3 PASS · C4 PASS
                   C5 PASS · C6 PASS · C7 PASS · C8 satisfied by the dry-run.
                   ATTEMPT 1 FAILED C1 and is preserved -- see §3.

FILES CHANGED:     src/species/PremixedFlame.cpp   (production, 1 file)
PRODUCTION FILES:  f9f554bf9e1fe1fd... -> 8c85bc9f5f457bf2... (via 6ef2ce21d851a7ac, attempt 1)
TEST FILES:        none -- test_premixed_flame.cpp hash-identical to the freeze
HEADER:            none -- PremixedFlame.hpp hash-identical to the freeze
EVIDENCE:          results/technical_debt/TD037_drift_window_guard/

FOCUSED TESTS:     the frozen gate, logs/03
REGRESSION:        CFDIoTests 331/331 · CFDAppLayerTests 32/32 · CFDSpeciesTests 343/343 under
                   its G8 exclusion. NOT RUN: Debug+GUI, the full sanitizer suite (§5).
SANITIZERS:        NOT RUN -- infeasible as specified for this suite (TD-035).

KNOWN FAILURES:    none introduced.
NEW FAILURES:      none. The G8 method-insensitivity test that TD-032 broke now PASSES, with its
                   drift-window spread reported as exactly 0.

GIT STATUS:        large pre-existing uncommitted tree; this item added 1 production file change
COMMIT:            none -- not authorized, and TD-034 blocks commits from this tree
PUSH:              none -- not authorized
```

---

## 1. The fix

On **both** early-return paths that read the extinction drift, the span is now
`min(driftWindow, available)` rather than a required full window, refused outright below 4
positions:

```text
line ~1415  the DIVERGENCE path      marchStatus != Converged
line ~1459  the LOCATE-FRONT path    locateFront() stops finding a crossing -- the flame goes out
```

**The floor of 4 is this code's own standard.** `window` is already
`std::max<Index>(4, settings.driftWindow)`, so a 4-position span is the smallest the solver was
already willing to measure. Below it the trial stays unsided. The change makes a measurement
possible where the data supports one; it never invents a side where it does not — and
`trialSaysTooLarge()` still fails closed, which is the correction that replaced a version that
inferred the side and halved the mass flux thirteen times without bracketing anything.

## 2. What the gate established

```text
driftWindow   status      S_L cm/s   final mdot       trials  unmeasured  C3
         16   Converged   4.671937   0.0550371757         15           0  PASS 13/2/0
         32   Converged   4.671937   0.0550371757         15           0  PASS 13/2/0
         64   Converged   4.671937   0.0550371757         15           0  PASS 13/2/0
        128   Converged   4.671937   0.0550371757         15           0  PASS 13/2/0
```

**All four windows now run the identical search** — same trial count, same eigenvalue to every
printed digit, same C3 outcome. The search is window-independent, which is the property the
method-insensitivity test exists to assert, and that test now reports its spread as **exactly 0**.

**C3 is the criterion that was least certain and it is the one that matters.** The audit's open risk
was that a drift read over a short window on an extinguishing flame might measure the extinction
transient and assign a **wrong** side — worse than undecided. At windows 64 and 128 the
newly-sided trials are now inside the check: 13 sided trials tested against the independent
eigenvalue, 2 skipped inside the converged bracket, **0 wrong**, at every window. The short-window
drift gets the side right.

**C2: `S_L` did not move at all** — 4.671937 cm/s and final mdot 0.0550371757, identical to the
baseline at every window, against a bound of 1e-3 derived from the solver's own bisection
tolerance.

### 2.1 The C7 re-derivation was vindicated by the outcome

```text
ladder    812 s (baseline) -> 1425 s        windows 64 and 128 now run 15 trials, not 4
C5 test   770 s (baseline) -> 1019 s        ratio 1.32
```

The draft C7 bounded cost at **1.25x** the baseline. **It would have failed this correct fix.** The
baseline is cheap only because window 64 aborts in ~14 s, so bounding a repaired path against an
aborting one measures nothing — which is why the dry-run re-derived C7 to keep only the
repository's own `ctest --timeout 7200` and report wall times ungated. Max observed: 1019 s.

## 3. ATTEMPT 1 FAILED C1, and the failure is preserved

`logs/02_verification.log` stands as recorded (CLAUDE.md §5.3: preserve the original failure;
explaining it does not waive it). Source `6ef2ce21d851a7ac…`, library `5399f5d5d6f8aa84…`.

**Cause: my fix was incomplete, not wrong.** There are **two** early-return sites that read the
extinction drift, with byte-identical nine-line blocks, and I patched one. The audit cited the
divergence path and asserted it was *the* extinction path; the trial that actually fails takes the
other one, where `locateFront` stops finding a crossing. The mechanism in `audit.md` §1 was right;
the code-location claim was incomplete.

**The gate caught it, which is what it is for.** C1 failed at exactly the two windows the mechanism
predicts. Attempt 2 clamped both sites, and the patch script asserted the end state rather than
trusting it: zero old-condition sites remaining, exactly two clamped sites, the normal path's
`(step % window) == window - 1` intact, `trialSaysTooLarge`'s fail-closed branch byte-identical.

### 3.1 A LIMIT ON C4, found by the failure rather than by design

**C4 PASSED on the broken fix.** The window-32 record was byte-identical in attempt 1 too. That
confirms the clamp does not perturb the normal case — 15 trials, 11 of which extinguish — but it
also means **C4 cannot distinguish a correct fix from one that never fires.** It is a guard against
over-reach, not evidence of function. Recorded here rather than counted as a second passing
criterion.

## 4. Carved out, not repaired here

```text
TD-038  the two extinction-drift blocks are DUPLICATED and must stay in step. That duplication is
        what made attempt 1 incomplete and cost a full verification cycle. They now cross-reference
        each other, which is a mitigation and not a fix. Factoring them into one helper is the
        right engineering answer and was deliberately NOT done inside this item: it would have
        conflated a structural change with a behavioural one under a frozen gate that expects only
        the latter, and a C4 failure would then have been unattributable.
```

## 5. WHAT CLOSURE DOES **NOT** REST ON

```text
Debug + GUI regression   NOT RUN
full sanitizer suite     NOT RUN, and infeasible as specified -> TD-035 (ASan costs 38x on this
                         suite; several G8 tests would exceed ctest's 7200 s cap and record as
                         TIMEOUTs rather than results)
clang-format             still failing on this tree in files this item did not touch -> TD-034,
                         which blocks any commit (CLAUDE.md §11). Nothing is committed.
```

**So this is `REGRESSION: PARTIAL`, closed on its frozen gate rather than on a §8 full regression**,
by explicit user authorization recorded here rather than smoothed over.

## 6. What a reader must NOT conclude

```text
NOT validated   any premixed-flame number. TD-027 C4, TD-028, TD-029, TD-030, TD-031 and TD-004's
                C2 were all measured on TD-032's alternating field and still need re-measuring
                under their own gates.
NOT closed      P18 G8. This item removed a search fragility; it validated no flame speed.
NOT fixed       TD-036. `NoFlame` is still mislabelled and still reports an empty diagnostic. A
                search that no longer aborts simply stops exercising that path, which is not the
                same as repairing it.
```

## 7. What this item delivered

The eigenvalue search can now decide a side for every trial across a `driftWindow` ladder that
straddles the measured trial lifetime, and it returns the same eigenvalue at all four windows
rather than aborting at two of them. The constraint it removes was never stated, never checked, and
held before TD-032 only by seven steps of margin on a quantity nobody controls.

That is the shape of this whole campaign in miniature: the defect was latent, the margin was
accidental, and it took a correct fix elsewhere to expose it.
