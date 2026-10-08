# TD-005 — SUMMARY — **C4d FIXED AND VERIFIED. THE ITEM REMAINS OPEN.**

```text
item        TD-005   Flame-speed extraction method not frozen · P1
authorized  2026-10-04 "do TD-005 next"; 2026-10-05 "fix the report"
gate        GATE.md  sha256 6a7ccf3cc038a310b99059b75c97062ea7564a25f4c274bbc7e110b4d9de286d
            FROZEN with the dry-run complete for all ten criteria (logs/00_freeze.log)
verdict     C4d PASSES. C3 and C6 remain FAILED. TD-005 does NOT close.
            The authorized change was "fix the report" and that is done, verified by a
            two-library bitwise control. It was never the whole item.
```

---

## 1. What the item turned out to be

The register pinned TD-005 on a **4.3 % reproducibility** blocker from
`p18/g8_closure/flame_solver_findings.md` §3.1, against a `<< 2.8 %` requirement set by G8's
`35.0–37.0 cm/s` band. That number **does not reproduce**: it was measured before
`relaxationTimesPerTrial` existed, and the current drift-window spread is `0.068 %`.

What the audit and dry-run found instead is that **the method's eigenvalue search is sound and its
instruments are not.**

```text
SOUND, and not TD-005's to redo:
  the degeneracy exclusion (the consumption integral cannot be the eigenvalue condition)
  the plateau mechanism, the unsteady inner solve, bracket-and-bisect on sign(drift)
  the bracket as an honest BOUND: 5.43e-03 / 6.77e-04 / 8.46e-05 all within their tolerance,
    and S_L converging inside the derived sqrt(1+t)-1 bounds with 5.4x and 1.9x margin
  the plateau itself: flat to SIX FIGURES at anchors 0.40 and 0.50

NOT SOUND:
  F1  the insensitivity sweeps assert on S_L ALONE and pass while a configuration returns
      a 36 %-non-conserving state                                                    -> C3  FAIL
  F2  the plateau had never been measured -- its test was recorded TIMEOUT at a cap
      that was not the repository's                                                  -> C4a PASS
  F3  the consumption-identity test FAILS on a bundled TD-003 assertion, not on the
      identity, which passes at 0.00777 < 0.01                                       -> C6  FAIL
  F4  relaxationTimesPerTrial -- the knob that replaced the fixed 128-step trial --
      is swept by no test, and produces a 2.19 % spread on its own                   -> C3
  F5  result.flamePosition was the ANCHOR INPUT echoed back                          -> C4d FIXED
```

## 2. The root cause, found by looking at the field

Three hypotheses for the inadmissible state died first, each refuted by the next measurement:

| hypothesis | refuted by |
| --- | --- |
| insufficient relaxation | **twice** — `driftWindow 64` is the longest trial and is bad; `relaxationTimesPerTrial 6.0` is the most relaxed and is bad. Non-monotonic |
| inlet proximity | explains anchor 0.30 only; windows 16 and 64 sit at the **same** anchor as the good window 32 |
| bracket width | sweep D never produced a bad mode over a **100×** range, and the *widest* bracket gave the best state |

All three were inferred from counters **derived from** the temperature profile, which is the thing
being inferred. `logs/02` printed the fields instead, and settled it in one run:

```text
                   inlet (T[0]-T_u)/(T_ad-T_u)   outlet Y_F/Y_F,u   real 1050 K crossing
GOOD (window 32)          1.04e-04                   3.98e-16          3.5233 mm   +1.17 cells
BAD  (window 16)          1.23e-04                   3.07e-18          2.5684 mm  -46.6 cells

result.flamePosition reported, PRE-FIX, in BOTH:   3.49999999999999964e-03
                                                   = 0.35 * 0.010 to the last bit
```

**No flashback, no breakthrough.** Both modes are physically sensible flames reaching `T_ad`
exactly. The bad one simply has its front 46.6 cells off the anchor — and the reported front
position was `anchorFraction * domainLength`, identical to seventeen digits in a healthy and a
broken flame.

### 2.1 The defect, in one line of production code

```text
src/species/PremixedFlame.cpp:1725      result.flamePosition = xAnchor;     // the ONLY assignment
```

and the test that existed to guard it, `test_premixed_flame.cpp:400`, asserted

```text
    anchorFraction * domainLength  ~=  anchorFraction * domainLength
```

**an identity that could not fail for any solver behaviour, including a solver that never ran.** Its
own comment claimed *"The controller really did hold the front where it was told to."*

### 2.2 The chain this explains

```text
1. the front drifts ~47 cells upstream of the anchor
2. correcting it needs a 47-cell translation, which TD-004's admissible-jump bound (~5.1 cells)
   CORRECTLY refuses as unphysical for one step
3. nothing then corrects the situation, so the front stays off-anchor
4. result.flamePosition reports the ANCHOR regardless, so nothing in the suite sees step 1
5. the fuel balance, computed from the real field, is the ONLY instrument in this chain that
   was ever telling the truth
```

This also **reframes TD-004**, whose counters were right and whose interpretation was wrong twice
(spurious crossings, then flashback). Corrected in place in that item's `summary.md` §4.1.

## 3. The fix, and its verification

```text
src/species/PremixedFlame.cpp         finalise() measures the first tAnchor crossing of the
                                      REPORTED field, UNCONDITIONALLY; the echo is removed
include/cfd/species/PremixedFlame.hpp the comment made true, with a warning that
                                      worstFinalOffsetCells cannot substitute (it is capped by
                                      the control locator's refusal, so it cannot see the large
                                      offsets it appears to report)
tests/unit/species/test_premixed_flame.cpp   assertion and tolerance UNCHANGED; the vacuity is
                                      recorded in a comment
```

**A separate scan, not a `locateFront` call.** That lambda is the *control* locator: it refuses any
crossing further than one step could carry the front, which is right for deciding whether to
translate the frame and wrong for reporting where the front is. A report that declines to look is
the fail-open the header itself warns about.

**The test's threshold was not touched.** The assertion was correct in intent; production made it
vacuous. Rewriting it would have weakened a criterion that was about to start working.

### 3.1 The companion requirement, proven by a two-library control

`flamePosition` is read in exactly **one** place in the whole tree, so the fix structurally cannot
move a computed answer. That argument was not accepted as evidence. The pre-fix sources were
reconstructed by inverting the edit and **proved against the frozen hashes** —
`b52fc24c703bc9df…` and `765fbee924693d8d…`, both `MATCHES FROZEN` — then built and measured with
the same probe:

```text
                         PRE-FIX (control)            POST-FIX
flameSpeed               4.86229685763685318e-02      IDENTICAL
massFlux                 5.72796876160792182e-02      IDENTICAL
consumptionSpeed         4.90004497995438146e-02      IDENTICAL
fuelBalanceResidual      1.53143548753620941e-02      IDENTICAL
energyBalanceResidual    1.53148254302936417e-02      IDENTICAL
massFluxResidual         6.77130693066291495e-04      IDENTICAL
peakTemperature          1.79999932998291320e+03      IDENTICAL
flamePosition            3.49999999999999964e-03  ->  3.52333179239811781e-03
```

**All seven computed quantities bitwise identical in both modes; only `flamePosition` moved.**
(`logs/07`.)

## 4. Criteria, as executed

| # | threshold | measured | verdict |
| --- | --- | --- | --- |
| C1 | `flameSpeed == massFlux/unburntDensity` bitwise | holds in both modes | **PASS** |
| C2 | derived `sqrt(1+t)-1` bounds | `1.017e-03` vs `5.49e-03`; `2.962e-04` vs `5.50e-04` | **PASS** |
| **C3** | `fuelBalanceResidual < 0.05` at **every** swept level | **`0.2380 … 0.3721` at 5 of 12** | **FAIL** |
| C4a | a verdict under `--timeout 7200` | plateau test **997 s**, no code change needed | **PASS** |
| C4b | plateau spread `<= 0.02` | `0.001355` | **PASS** |
| C4c | successive differences non-increasing | exactly `0` vs `6.77e-04` | **PASS** |
| **C4d** | reported front within one cell of the field's crossing | measures `3.5233` / `2.5684 mm`; companions bitwise unchanged | **PASS** |
| C5 | two controls | unconverged: `MaxIterations` **and** `4.1421e-01 > 1e-3`. No-flame: **UNMEASURED** | **PARTIAL** |
| **C6** | the identity reported on its own | identity passes `0.00777 < 0.01`; test fails on TD-003's `6.29e-07` vs `1e-8` | **FAIL** |
| C7 | 343 unchanged | **343 passing** | **PASS** |

## 5. Regression (`logs/08`, `logs/09`) — COMPLETE

```text
C7  CFDSpeciesTests minus G8PremixedFlameTest        343 PASSED        unchanged
MalformedConfigurationsAreRefused                    PASS      0s      unchanged
TheBurntCompositionConsumesTheFuelExactly...         PASS      0s      unchanged
TheObviousChecksAreVacuousOnTheSeededProfile         PASS      0s      unchanged
ItConvergesAndReachesTheIndependentlyDerived...      PASS    256s      was FAIL -- see 5.1
TheConsumptionIntegralAgreesWithTheEigenvalue        FAIL    256s      unchanged (C6)
TheAnswerIsInsensitiveToTheStepsPerTrial             PASS    759s      unchanged -- and still
                                                                       fail-open, as C3 records
TheEigenvalueIsIndependentOfTheInitialMassFluxGuess  PASS    471s      unchanged
TenTimesThePreExponentialMovesTheFlameSpeedFar       PASS    798s      unchanged
AVariableConductivityChangesTheAnswer...             FAIL      0s      unchanged (named, property 8)
TheEigenvaluePlateausInTheAnchorPositionAnd...       FAIL    910s      was PASS 997s -- see 5.2
TheFlameSpeedConvergesUnderMeshRefinement            PASS    268s      unchanged (was 301s)
WithReactionsDisabledNoFlamePropagates               NOT RUN             C5's control, TD-020
```

**Exactly one verdict changed, and it is the predicted one.** Ten of eleven tests and the 343-test
suite are unchanged, which is what `logs/07`'s bitwise control requires: no computed quantity moved,
and `flamePosition` has exactly one reader in the tree.

### 5.1 `ItConverges…` improved, and it is NOT attributable to this fix

TD-004's per-test baseline (`logs/06`) recorded this test **FAIL at 237 s**; it now **PASSES at
256 s**. That baseline was taken on the tree **before TD-004's locator fix**, while the tree frozen
for TD-005 **contains** that fix. The improvement therefore belongs to **TD-004**, not here — and
the two-library control settles it: no computed quantity moved across the C4d fix, so no test
reading a computed quantity can have moved because of it.

**A gap in this regression, stated rather than glossed:** GATE §5.1 asks for per-test before and
after, and a per-test "before" at the *frozen* tree exists for only two of the eleven tests
(`logs/03`). For the rest, `logs/07`'s bitwise control is what licenses the attribution. A proper
per-test baseline at the frozen tree would have been better and was not taken.

### 5.2 The plateau test now FAILS, at the assertion that was an identity — as predicted

The prediction was recorded in `tools/run_regression.sh` **before** the run. The measured failure,
confirmed in isolation (`logs/09`):

```text
anchor 0.300000:  S_L = 4.85572 cm/s   S_c = 3.03387 cm/s   delta = 0.734479 mm   <- the bad mode

test_premixed_flame.cpp:409: Failure
  flame.flamePosition              0.002084565084450981 m  =  2.0846 mm
  fraction * settings.domainLength 0.003000000000000000 m  =  3.0000 mm
  difference                       0.000915434915549019 m  =  0.9154 mm = 45.8 cells
  tolerance (0.02 * domainLength)  0.000200000000000000 m  =  0.2000 mm = 10.0 cells
  -> exceeded by 4.58x

anchor 0.400000  and  anchor 0.500000   PASS the clause (both in the good mode)
anchor plateau: 4.85572 .. 4.8623 cm/s  -- C4b and C4c still PASS
```

**That is `test_premixed_flame.cpp:409`: the `EXPECT_NEAR(flame.flamePosition, ...)` line whose two
sides were the same expression.** It is now a measurement against the anchor, and it reports that at
anchor 0.30 the front sits **45.8 cells upstream** of where the solver was told to hold it.

Three things follow, and the distinctions matter:

```text
* this is NOT damage from the fix. Nothing the test reads changed except flamePosition, and
  logs/07 proves every computed quantity is bitwise identical across the change.
* it is a VACUOUS ASSERTION BEGINNING TO WORK, and the defect it detects is the one C3 already
  records as a frozen FAIL. The plateau test was passing at 997 s while containing a
  configuration whose fuel balance is 0.3571.
* the SPREAD clauses still pass. C4b (0.001355 <= 0.02) and C4c (exactly 0) are unaffected,
  so the plateau in S_L is real AND the front is badly off-anchor at the closest anchor. Both
  are true, which is only visible because the clauses are separate.
```

**This is exactly why GATE §5.1 required a per-test report rather than a total.** A count would have
shown "10 pass, 2 fail" before and after and hidden the entire finding.

### 5.3 The honest status of this new failure

`TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength` is a **NEW FAILURE** relative to the
frozen baseline (`logs/03`: PASS, 997 s), and CLAUDE.md §8 says only the failures a gate names are
permitted. **The frozen gate does not name it**, because at freeze time the clause was an identity
and nobody could have predicted which configurations it would catch.

It is reported as a new failure, not reclassified. What it is **not** is unexplained: its cause is
C3, which the same gate records as FAIL with the same configurations, and its mechanism is the
defect C4d repaired. **TD-005 stays OPEN**, so nothing is being closed over it.

## 6. Corrections made during this item

Six before the freeze, two to TD-004, all recorded in place:

```text
1. the 900 s cap was TD-004's HARNESS, not the repository's 7200 s. Two of TD-004's three
   recorded "timeouts" were never repository failures; the plateau test misses that cap by
   97 SECONDS                                                               GATE 4.4, 5.2
2. C4a needs no code change -- the work the first draft assumed does not exist      GATE 5.2
3. C4d was ADDED: the draft would have frozen a criterion set with the front
   position unchecked                                                               GATE 4.7
4. an admissibility criterion REJECTED as vacuous -- |S_c-S_L|/S_L <= balance +
   bracket reads 0.3736 <= 0.3743 and PASSES on the 36 %-violating state. The
   mismatch and the balance are the same measurement, the third face of TD-004's
   e/f = 1.00000                                                                    GATE 4.3
5. the register's three controls are TWO: "no flame" is reachable only via
   reactionsEnabled = false, which is "reactions disabled"                        GATE 4.5.1
6. audit 3's "~40x margin" was computed from the drift window alone; the full
   method-knob spread is 2.19 %, which EXCEEDS the in-tree 2 % bound             audit 3
--- and to TD-004 ---
7. its predicted clause-4a failure was wrong (~9x headroom, not thin)       TD-004 summary 4.3
8. its FLASHBACK blocker is refuted for the final state                     TD-004 summary 4.1
```

A pointed one: **C1, the bitwise criterion that looked like pedantry when it was derived, passes** —
while the reported front position beside it was an echoed input. C1 checked the one relation that
happened to be honest. A bitwise identity between two reported fields proves they agree; it cannot
prove either is measured.

## 7. What remains, and TD-005 does NOT close

```text
C3  FAIL, and DIAGNOSED -- after one wrong diagnosis that its own falsification tests
          killed. dry_run.md section G is marked INVALID AS AUTHORITATIVE in place; section H
          carries the replacement, from logs/13.

          THE FRONT OSCILLATES; IT DOES NOT RUN AWAY. The final march reaches no steady
          state. It executes a large-amplitude, long-period oscillation in front position AND
          flame structure, and the reported state depends on the PHASE at which the march
          stops:

              relaxT 10  offset   +1.17   balance 1.531e-02   delta 1.2289 mm
              relaxT 20  offset -163.08   balance 7.124e-01   delta 0.4081 mm
              relaxT 30  offset -162.24   balance 6.022e-01   delta 0.4188 mm
              relaxT 40  offset   +1.21   balance 1.049e-02   delta 1.2232 mm   <- RECOVERS

          The recovery is not marginal: 1.049e-02 is the best balance measured anywhere in
          this item. An exponentially unstable front cannot come back, so the previous
          "e-folding ~530 steps" reading was the rising limb of a half-cycle -- locally it
          fits an exponential, and I fitted one. The dead band is also refuted: over a 10x
          range at identical mdot the offset is flat to 2 %, and 252 recentrings against 7
          changed nothing.

          THIS EXPLAINS ESSENTIALLY EVERY ANOMALY IN THE ITEM as phase-at-stop: C3's 5 of 12,
          sweep A (16 bad / 32 good / 64 bad), sweep C (1.5 bad / 3.0 good / 6.0 bad), sweep
          D's uniform pass (all three marched 3968-3976 steps -- the SAME phase, which is why
          no bracket width produced a bad mode), and the plateau test's anchor-0.30 failure.

          AND IT INVALIDATES TD-004's C2 PREMISE. C2 bounds the ratio of a MONOTONE
          degradation with march length; there is no monotone trend. TD-004 sampled relaxT
          10/30/90 and caught one good phase and two bad. Its measured 46.9362 was real and
          its stop was correct, but no fix to the recentring operator could have satisfied
          that criterion. Per CLAUDE.md 5.3 that is a mis-derived frozen criterion found after
          the freeze, and amending it is NOT TD-005's to do -- it needs its own authorization.
          Cross-check that makes this trustworthy: logs/13's relaxT 30 row gives 6.022e-01 at
          11905 march steps against TD-004's 6.022411e-01 at 11905 -- identical, from a
          different probe.

          STILL UNKNOWN: the period (four points cannot establish one; the spacing suggests
          ~11900 steps ~ 0.45 s against the flame's own 0.55 s structure time, which would
          make it intrinsic), and whether it is a LIMIT CYCLE or a decaying transient -- which
          decides whether any march length converges. Falsification tests for section H are
          pre-registered at H.6 and none is authorized.
NEW FAIL  TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength, not named by the frozen
          gate, reported as a new failure rather than reclassified (5.3).
C6  FAIL  the consumption identity passes on its own; the test fails on TD-003's tolerance-
          limited SigmaY bound, which is a REAL defect detector and so is NOT migrated,
          loosened or removed -- only stopped from masking a passing result.
C5  PARTIAL  the no-flame control is unmeasured; TD-020 governs its cost.
F4  the 2.19 % relaxationTimesPerTrial spread exceeds the in-tree 2 % bound. Mitigating:
    the eigenvalue IS converging in that knob (differences contract 15x), so the spread is
    dominated by an unconverged level -- and whether such a level belongs in an
    insensitivity sweep is an open GATE-DESIGN question, deliberately left unresolved rather
    than settled to suit the result.
```

**The precision precondition §3.1 demands is therefore still not demonstrated.** The 4.3 % figure is
stale, but the honest replacement is `2.19 %` against a `2.8 %` half-width — not `0.068 %`.

## 7a. THE C3 INVESTIGATION — twelve probe runs, five refuted readings, and where it ends

`logs/10` through `logs/21`, recorded step by step in `dry_run.md` §G–§P. This is the substance of
the item beyond the authorized C4d fix, and it was only possible because C4d made the front position
a measurable quantity for the first time.

### 7a.1 The chain of readings, each refuted by the test it specified

| # | reading | killed by |
| --- | --- | --- |
| 1 | insufficient relaxation | non-monotonic: the **longest** trial and the **most** relaxed run are both bad |
| 2 | inlet proximity | explains one anchor only; windows 16 and 64 share an anchor with the good 32 |
| 3 | bracket width | no bad mode at any width over a **100x** range; the widest gave the best state |
| 4 | exponential runaway, e-folding ~530 steps | the state **recovers** at a longer march with the best balance in the item. It was the rising limb of a half-cycle, and I fitted an exponential to it |
| 5 | the chemistry sub-step limiter | tightening it made the mismatch **worse** and flipped its sign; and the limiter was never idle — it binds at ~500 sub-steps per march step |

Two probe **verdicts** also had to be corrected, both because an intervention invalidated the
observable the verdict used: `logs/16` judged health by front position in a test that removed what
holds the front at the anchor, and `logs/19` judged by a bad-row count when the informative signal
was the flatness of the tail.

### 7a.2 What is ESTABLISHED

```text
THE LIMIT CYCLE
  * the front executes a nearly SQUARE limit cycle between the anchor (cell ~176) and the inlet
    (cell ~11). Fourteen measurements, NOT ONE intermediate.
  * period 14 relaxation times (5558 march steps, 0.2095 s) at the default dt. One parameter
    predicted all 17 verdicts across three independent ladders, out to 8 cycles.
  * amplitude UNDIMINISHED at 8 periods over an 11.6x span of march length: a limit cycle, not a
    decaying transient. So NO march length converges, and "march until steady" is unsound --
    which is why finalMarchTolerance was found unreachable by construction (F13). There was no
    steady state to reach.
  * duty cycle 29-57 % in the bad half, which PREDICTS C3's 5-of-12 failure rate (42 %).
  * it is NOT the recentring operator: cleared twice, by a 10x dead-band range and by disabling
    recentring entirely (0 recentrings, same pattern).
  * it is NUMERICAL: the period moves with the mesh (14 -> ~18 relaxation times when dx doubles),
    and Ze(Le-1) = 0 puts this flame far below any pulsating-instability threshold.
  * it is SUPPRESSED at a cell diffusion number D = 0.833 (dt/4): four march lengths over a 1.67x
    range then agree to 2 %.

THE TIME STEP
  * S_L is NOT converged in dt. dt -> dt/2 moves it -4.24 %, against G8's +-2.80 % band.
  * the default dt sits OUTSIDE the asymptotic range. {dt/2, dt/4, dt/8} is a clean family with
    successive differences shrinking 5.00x; Richardson limit 4.67589 cm/s, GCI 0.021 %.
  * so the default dt is +3.99 % high, and EVERY S_L in this repository -- including the 4.86230
    quoted throughout TD-004 and TD-005 -- carries that error.
  * cause: maximumCellDiffusionNumber = 20.0 admits D = 3.33 where accuracy needs D <~ 1. A
    resolution cap set by assertion and never validated against a measured error.

THE CONSERVATION RESIDUAL
  * at dt/4 the march converges and leaves a steady 8.48e-02 fuel residual, failing C3 by 1.7x.
  * term by term: convection is EXACTLY -100.000 % of fuelThrough, inlet diffusion -0.572 %, the
    omitted outlet flux 1e-14, the omitted accumulation term +0.350 %, and the REACTION INTEGRAL
    -107.892 %. The reaction term is the sole oversized one.
  * that excess is identically the S_c/S_L ratio (1.07892 both ways), and it GREW tenfold when dt
    was refined -- so at the default dt the two agreed by CANCELLATION, not correctness.

THE EIGENVALUE
  * S_L survived FIVE independent interventions at 0.135 % or better: every limit-cycle phase
    (19 solves, 6 figures), recentring disabled, a 10x dead band, a 100x bracket range, and a
    10x chemistry-limiter range. Its ONLY established sensitivity is dt.
```

### 7a.3 Two PASSING tests that pass by cancellation

```text
TheFlameSpeedConvergesUnderMeshRefinement   refines CELLS, and dt is DERIVED from dx. The spatial
                                            and temporal errors have OPPOSITE SIGNS (+0.81 % and
                                            -4.24 %) and partially cancel. The family is not
                                            self-similar, so its observed order is meaningless.
TheConsumptionIntegralAgreesWithTheEigenvalue  asserts mismatch < 0.01 and passes at 0.00776 at the
                                            default dt. At the converged dt the mismatch is
                                            7.892 %. It passes where two errors cancel.
```

**Both are tests that currently report success on a solver that is not converged in either
parameter.** This is the most consequential finding in the item, because it means the suite's
reassurance about convergence is not evidence.

### 7a.4 `S_c` is not a usable consistency instrument in this regime

```text
under a 10x chemistry-limiter sweep:   S_L spread 0.135 %      S_c spread 167.0 %
```

`PremixedFlame.hpp` introduces `S_c` as *"a sharp CONSISTENCY check"*. It swings by 167 % under a
knob that moves the quantity it is checking by 0.135 %. That weakens C6, and it weakens any G8
validation resting on the consumption identity.

### 7a.5 The leading open candidate, labelled as a candidate

The chemistry limiter binds at **~500 sub-steps per march step at the default settings**, so the
chemical timescale is ~500x shorter than a `dt` chosen from **transport limits only** — nothing in
`min(Courant*dx/u_in, D*dx^2/alpha)` references the chemistry. A **first-order Lie split at 500x the
chemical time** is exactly the regime where the splitting error is large, and it would explain why
the `S_c`/`S_L` mismatch grew when `dt` was refined.

**Not established.** Testing it needs Strang splitting, or a `dt` set from the chemical timescale
(~470x smaller, ~470x the cost), or `omega` integrated as APPLIED during the march rather than
recomputed at the end. All three require production changes.

### 7a.6 What this does to the other items

```text
TD-004 C2   DEFINITIVELY MIS-DERIVED. It bounds the ratio of a MONOTONE degradation with march
            length; the signal is a constant-amplitude limit cycle. TD-004 sampled relaxT 10/30/90
            and caught one good phase and two bad. Its measured 46.9362 was real and stopping was
            correct, but NO fix to the recentring operator could have satisfied that criterion.
            Cross-check: logs/13's relaxT 30 row gives 6.022e-01 at 11905 march steps against
            TD-004's 6.022411e-01 at 11905 -- identical, from a different probe.
            Per CLAUDE.md 5.3 this needs its own authorization to amend. NOT amended here.
TD-004      its "flashback" observation is VINDICATED as an observation: at the bad phase the front
            sits at cell 10-13, x = 0.20-0.26 mm, and TD-004 measured its worst rejected crossing
            at 0.1969 mm = cell 9. The same place. It was wrong only as a description of a STEADY
            state, because the state is not steady.
TD-006      G8 cannot be decided. Two independent measures of the synthetic flame speed differ by
            7.892 % against a +-2.80 % band, and the default dt carries a further +3.99 %.
```

## 8. Not in scope, and untouched

methane, `cp`, `lambda(T)`, any absolute `S_L` claim (**TD-006**); the off-anchor drift itself
(**TD-004**, stopped and preserved); the `1e-9`-limited inner solve (**TD-003**); the no-flame
early-out (**TD-020**); route (b)'s drifting-frame reformulation; mesh convergence; any performance
claim. No commit, no push — neither was authorized.

**Reported and deliberately not fixed:** `worstFinalOffsetCells` is capped by the control locator's
refusal, so it cannot see the offsets it appears to report. It is the same *class* as F5 but a
different field, and fixing it was outside "fix the report" as authorized. It needs a register entry.
