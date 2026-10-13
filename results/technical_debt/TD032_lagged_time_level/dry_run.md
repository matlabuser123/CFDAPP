# TD-032 DRY RUN — every criterion against the UNCHANGED tree, before the freeze

```text
pass 1    logs/01_dry_run.log        2026-10-06T19:33 -> 20:26   C0, C1, C2, and the raw
                                     numbers C3(a), C4, C5 and C7's references come from
pass 2    logs/02_dry_run_pass2.log  C3(b)'s baseline, then C6's counts
status    the gate is NOT frozen. It is frozen only after this record is complete.
```

## 1. What was measured, and on what

```text
environment   Ubuntu-22.04 on WSL2, nproc 32, 31 GiB visible, c++ 11.4.0, Release,
              OpenMP OFF, CUDA OFF
library       libcfdcore.a   3b94eb849f9c8277...   <- THE LAGGED LIBRARY, C8's negative control
production    PremixedFlame.cpp  dac2992006a51cce...   PremixedFlame.hpp  d140cb6cafc9dac6...
probe         alternation_probe.cpp  6bf33900f7d56a40...  binary  6b7d2007798a07d4...
gate as run   GATE.md  51f4a4107437b4da...   <- the DRAFT, see section 6
build check   ninja reported no work to do; library 17:27:04 is newer than the newest
              production source 17:25:10
```

**No production file was touched.** The probe stops the march at consecutive budgets using
`finalMarchSteps`, which already exists as a hard ceiling, so no diagnostic was added for the gate.

## 2. C0 — the instrument self-test. PASS, in both directions.

```text
(a) monotone relaxation, T_k = 300 + 1500 exp(-k/2)
      across 5.902040e+02 K   within 9.481808e+02 K   R = 6.224593e-01   PASS, ratio meaningful
(b) parity alternation with a 0.001 K per-step within-branch drift
      across 1.500001e+03 K   within 2.000000e-03 K   R = 7.500005e+05   FAIL on the ratio
(c) a PURE alternation, within == 0 exactly
      across 1.500000e+03 K   within 0            VACUOUS -> fallback -> FAIL
(d) a stationary field, across == within == 0
      across 0                within 0            VACUOUS -> fallback -> PASS
```

(a) is the criterion's **pass-direction non-vacuity** and it is the sub-test that matters most: with
no corrected implementation yet in existence, (a) is what shows a smooth sequence lands comfortably
inside the bound rather than near it. (c) and (d) show the vacuity branch resolving the two
degenerate cases in **opposite** directions, which is what stops it from being a free pass.

## 3. C1 and C2 — both FAIL, as required

```text
C1  500 cells / cap 5 / relaxT 18, budgets 30945..30949
      across 1.484054e+03 K  at cell 37     within 2.645849e-01 K  at cell 17
      R = 5.608991e+03   against 1.0   FAIL

C2  250 cells / cap 5 / relaxT 18, budgets 7256..7260  (derived budget 7260, determined in logs/01)
      across 1.484262e+03 K  at cell 18     within 2.858500e+00 K  at cell  8
      R = 5.192449e+02   against 1.0   FAIL
```

The defect is present at **both** fixtures, so it is not a property of the configuration the
evidence base happens to use. On the unchanged tree these failures are the required result: they are
the gate's fail-direction non-vacuity and, with the library hash above, its C8 negative control.

### 3.1 My predicted `R` was 107x too large, and the prediction is recorded wrong

```text
predicted   across ~ 1.4836e+03 K   within ~ 2.47e-03 K   R ~ 6.0e+05
measured    across   1.484054e+03 K within   2.645849e-01 K  R   5.608991e+03
```

`across` was right to four figures. **The entire error is in `within`**, which I predicted from the
five cells `TD031/logs/05` happened to print, and which the criterion actually takes as a maximum
over all 500. The real maximum sits at **cell 17**, far upstream of the sampled window, at 107x the
sampled value. The verdict is unaffected — it fails either way — but the error is not cosmetic,
because C3's margin was derived from that same sampled figure. See section 6.

This is the third time in this campaign that a quantity predicted from a convenient sample has come
out wrong against the quantity a criterion actually evaluates.

## 4. A finding the gate was not looking for: the reported FRONT alternates

```text
budget   30945    30946    30947    30948    30949
front     3.535    0.261    3.535    0.261    3.535   mm     (anchor = 3.500 mm)
```

The cold branch puts the front on the anchor; the hot branch reports it **at the inlet**, because a
nearly fully burnt field puts the `T_anchor` crossing against the upstream boundary.

**This is the shape of TD-004's recorded blocker** — *the single `tAnchor` crossing relocates to the
inlet independently of upstream room* — which is the reading that classified TD-004's remaining work
as flame **stabilisation**, put it in TD-005's scope, and inverted the critical path. Recorded as a
lead and **not merged**: TD-004's runs were at cap 20, these are at cap 5, and `TD031/logs/02`'s
displaced-but-normal flame at 2.57 mm still matches neither branch. Full statement in
`TD031_final_march_length/PARITY_FINDING.md` section 9.

## 5. The other criteria's baseline numbers

```text
C3(a)  temperatureResidual   9.893696e-01   against 1.0e-02   FAIL      (discriminating)
C4     maxCompositionSumDeviation, the five budgets:
         8.692478e-07 / 1.627901e-06 / 8.690573e-07 / 1.627046e-06 / 8.688710e-07   <- BIMODAL
C5     S_L  4.671937 cm/s, identical to six figures at ALL FIVE budgets and across both
       branches; within 0.08 % of TD-027's Richardson limit 4.67589 cm/s
C7     post-warm-up walls 547 / 556 / 556 / 562 s, MEDIAN 556.1 s  ->  bound 695.1 s
       (250 cells: 71 / 71 / 71 / 72 s, median 71.1 s)
```

`S_L`'s independence of the budget is the measured form of the claim that the eigenvalue comes from
the trials' drift **sign**: it does not move with the march length or with which branch is reported.

### 5.1 NON-VACUITY, accounted for honestly and per criterion

```text
C0      both directions DEMONSTRATED on synthetic sequences                    (a)-(d) above
C1      fail direction DEMONSTRATED (R = 5.6e3); pass direction reachable via C0(a)
C2      fail direction DEMONSTRATED (R = 5.2e2); same
C3(a)   fail direction DEMONSTRATED (0.989 against 1.0e-02)
C3(b)   fail direction measured in pass 2 -- the lagged tree must show NO fall
C4      NO-REGRESSION criterion. The baseline satisfies it by construction. Its fail direction
        CANNOT be exercised before a fix exists, and that is stated rather than hidden.
C5      NO-REGRESSION criterion, same limitation: on the baseline the move is 0 % by definition.
C6      a COUNT MATCH. Vacuous against itself; it is a guard, not a discriminator.
C7      NO-REGRESSION criterion, same limitation.
C8      SATISFIED BY THIS RUN: the lagged library fails C1 and C2 with its hash recorded.
```

**The discriminating criteria are C1, C2, C3(a) and C3(b).** C4, C5, C6 and C7 are guards against
the fix breaking something else and are vacuous on the baseline by construction. Recording that
distinction is the point: a gate whose only *demonstrated* failures are its guards would be a gate
that discriminates nothing.

## 6. CRITERIA CHANGED AS A RESULT OF THIS DRY RUN — before the freeze, which is what it is for

`logs/01` ran against **GATE.md draft `51f4a4107437b4da...`**. Four criteria were then amended, and
the amended file is what gets frozen. Every change is recorded in GATE.md itself with the original
text preserved inline.

```text
C3  REPLACED.  Draft: temperatureResidual <= 1.0e-3, justified as "600x above the value a
    correct scheme produces" -- where that value came from the SAMPLED within-branch drift
    (2.47e-3 K). The full-field value is 0.2646 K, so one step of one sub-sequence is about
    1.76e-4 normalised and the real margin was about 5x, not 600x. A 5x margin cannot tell a
    correct fix from a marginal one, so a CORRECT FIX COULD HAVE FAILED C3 BY ACCIDENT.
    Replaced by C3(a) a level at 1.0e-2 (99x below the saturated 0.9894) and C3(b) a DIRECTION:
    the residual must at least halve between relaxT 18 and 26. The factor 2 is 1000x outside
    the saturated spread of +-2e-4 that TD-031 measured over six configurations, and asking for
    a direction rather than a level makes it immune to the sampling error that broke the draft.
    This is NOT a loosening to obtain a pass: no fix exists yet, and C3(b) is strictly harder
    than the bound it replaces. C3(b) is dry-run in pass 2.
C4  BOUND FIXED at 2.0e-06, from the WORST of the five baseline values rather than the better
    branch, because comparing a post-fix single-branch value against the cherry-picked better
    half of a bimodal baseline is not like-for-like. Labelled EMPIRICAL. A pre-registered
    reporting rule covers the 8.69e-07..2.0e-06 band.
C5  BASELINE RECORDED: 4.671937 cm/s. Threshold 3.99 % unchanged.
C7  BASELINE RECORDED: median 556.1 s -> bound 695.1 s. Factor 1.25 unchanged. The warm-up
    rationale did NOT reproduce -- the first solve was the FASTEST here (542 s) and the slowest
    in TD031/logs/05 (829 s) -- and the pre-registered exclusion is kept anyway, because
    revising it on seeing the data would be fitting. Immaterial: the five-solve median is also
    556 s.
C2  BUDGETS FIXED at 7256..7260 from the determination solve, so the post-fix run marches
    exactly as far as the baseline did.
```

Thresholds **not** touched: C1 and C2's `R <= 1.0`, the `1e-9 K` vacuity floor, C5's 3.99 %, C7's
1.25, C6's counts.

## 7. PASS 2 — C3(b) and C6, on the same lagged library

`logs/02_dry_run_pass2.log`, 2026-10-06T20:26 -> 20:54. Same library
`libcfdcore.a 3b94eb849f9c8277...`, unchanged since pass 1. Stages run **sequentially**, never
concurrently, per the hardware policy: a `ctest` alongside a solve would corrupt both.

### 7.1 C3(b) — the residual does NOT fall with march length. FAIL, which is what was needed.

```text
relaxT   budget   temperatureResidual   worstStateChange   S_L cm/s   wall s
    18    30949      9.8936959608e-01       9.895679e-01    4.67194      579
    26    44705      9.8943789689e-01       9.895690e-01    4.67194      715

ratio resid(26)/resid(18) = 1.000069   against the required <= 0.50   ->  FAIL
```

A **44 % longer march moves the per-step change by 7e-5 relative, and in the wrong direction.** That
is saturation, and C3(b) therefore has a demonstrated fail direction and may be frozen. Had the
lagged tree already halved the residual, C3(b) would have been vacuous and excluded.

Two independent confirmations fell out of this run:

* `1.000069` sits **inside** the `+-2e-4` saturated spread TD-031 recorded across six
  configurations — a quantity measured here by a different probe, at a march length TD-031 never
  used, agreeing with the earlier spread. The saturation is not an artefact of one probe.
* **`S_L` is identical to six figures at both march lengths** (4.67194 at 30949 and at 44705
  steps). Together with its invariance across the five consecutive budgets in pass 1, that is the
  measured form of the claim that the eigenvalue is fixed by the trials before the final march
  begins. It is *not* evidence that the trials are unaffected by the lag — they run the same
  `advance()` — only that the final march does not move it.

### 7.2 C6 — the three suites, rebuilt and re-measured on this tree

```text
build check   all three targets rebuilt, then each binary verified NEWER than the newest
              production source; the harness aborts if not (CLAUDE.md 8, fail closed)
              CFDIoTests 646a71a12886502f...  CFDAppLayerTests f195a2f9b4472cb4...
              CFDSpeciesTests 732f6049fc09a18d...

CFDIoTests                                            331 / 331 PASSED   (278 ms)
CFDAppLayerTests                                       32 /  32 PASSED  (2912 ms)
CFDSpeciesTests  --gtest_filter=-G8PremixedFlameTest.* 343 / 343 PASSED (14212 ms)
```

All three match the expected counts exactly, so C6's expectation is a measurement on this tree
rather than a citation. The `CFDSpeciesTests` exclusion is TD-004 territory, carried from its
baseline, and is recorded as part of the invocation so the count is never quoted without its
conditions.

## 8. FREEZE DECISION — FREEZE

Every criterion has been dry-run against the unchanged tree:

```text
C0     PASS, both directions demonstrated on synthetic sequences
C1     FAIL   R = 5.608991e+03   against 1.0
C2     FAIL   R = 5.192449e+02   against 1.0
C3(a)  FAIL   9.893696e-01       against 1.0e-02
C3(b)  FAIL   ratio 1.000069     against 0.50
C4     baseline recorded, bimodal; bound 2.0e-06 taken from the worse branch
C5     baseline recorded, 4.671937 cm/s
C6     331 / 32 / 343, all passing, binaries verified newer than source
C7     baseline median 556.1 s -> bound 695.1 s
C8     SATISFIED: the lagged library fails C1 and C2, hash recorded
```

**All four discriminating criteria fail on the unchanged tree, and the instrument is shown to pass
on a smooth sequence.** The guards (C4, C5, C6, C7) are vacuous on the baseline by construction and
that is recorded in section 5.1 rather than counted as evidence. No threshold was weakened to
obtain any of this: C3's replacement is documented in section 6 with the original preserved, and it
happened before the freeze and before any fix existed.

The gate is frozen at `logs/00_freeze.log`. **Any change to GATE.md after that point is an amendment
requiring its own file, its own dry-run and its own authorization (CLAUDE.md 5.3.)**
