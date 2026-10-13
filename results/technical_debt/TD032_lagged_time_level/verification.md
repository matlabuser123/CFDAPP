# TD-032 VERIFICATION — against the frozen gate `GATE.md 52bc82554574e7b7…`

```text
run       logs/03_verification.log   2026-10-06T21:02 -> 22:02
C7(b)     logs/04_g8_cost.log        2026-10-06T22:04 -> 23:16
scoped    logs/05_regression_scoped.log  2026-10-06T23:19 -> 2026-10-07T03:18
verdict   ALL TEN frozen criteria PASS: C0, C1, C2, C3(a), C3(b), C4, C5, C6, C7, C8.
          The ITEM is nonetheless BLOCKED, on a failure no criterion names -- see summary.md
          section 4 and attribution.md.
```

## 1. Provenance — two real libraries, one unmoved instrument

```text
                        PRE-FIX (frozen)                  POST-FIX
libcfdcore.a            3b94eb849f9c8277...               a9b01668be7ccb7f...   CHANGED
PremixedFlame.cpp       dac2992006a51cce...               f9f554bf9e1fe1fd...   CHANGED
PremixedFlame.hpp       d140cb6cafc9dac6...               d140cb6cafc9dac6...   UNCHANGED
alternation_probe.cpp   6bf33900f7d56a40...               6bf33900f7d56a40...   UNCHANGED
test_premixed_flame     8c2be7e82efba3ec...               8c2be7e82efba3ec...   UNCHANGED
test_thermal_transient  da3566e1125d0786...               da3566e1125d0786...   UNCHANGED
test_scalar_transport   8a08f9f733967b59...               8a08f9f733967b59...   UNCHANGED
```

This is the two-library attribution CLAUDE.md 6 requires. The harness **fails closed** on the two
ways this comparison could be fake: it aborts if the library hash is unchanged (a stale build), and
it aborts if the probe source no longer matches its frozen hash (a moved instrument). Neither
fired. **No header and no test was touched** — the fix is nine lines of comment and two statements
moved inside one lambda.

## 2. The criteria, in frozen order

### C0 — PASS. Identical to the dry-run, against the new library.

`R = 6.224593e-01` on the monotone sequence, `7.500005e+05` on the parity sequence, and the two
degenerate cases resolving in opposite directions through the vacuity branch.

### C1 — PASS. `R = 5.000271e-01` against `1.0`, at 500 cells / cap 5 / relaxT 18.

```text
budget   marchStp     S_L cm/s        T resid        sum dev      front mm   wall s
 30945      30945      4.67194   1.841572e-05   3.947687e-07      1.940541      439
 30946      30946      4.67194   1.841328e-05   3.947746e-07      1.940519      437
 30947      30947      4.67194   1.841128e-05   3.947709e-07      1.940497      440
 30948      30948      4.67194   1.840987e-05   3.947146e-07      1.940476      432
 30949      30949      4.67194   1.840822e-05   3.945849e-07      1.940454      419

across 2.761992e-02 K at cell 108     within 5.523685e-02 K at cell 108
```

**The alternation is gone.** Every reported quantity is now monotone across the five consecutive
states, where before the field swung 1484 K and the front 3.27 mm on every step.

`within = 5.52e-02 K` is eight orders above the `1e-9 K` vacuity floor, so **the ratio was
evaluated on its merits and not through the fallback** — the gate's one route to a hollow PASS was
not taken.

`R = 0.50003` deserves recording as more than a PASS. GATE.md section 1 derived `R ~ 0.5` as the
value a smooth sequence must give, because a two-step difference is the sum of two same-signed
one-step differences. The measured value agrees to four figures, at both meshes independently. The
sequence is now linear in the step, which is a stronger statement than "under the bound".

### C2 — PASS. `R = 5.000359e-01` against `1.0`, at 250 cells.

```text
across 1.153259e-01 K at cell 75      within 2.306352e-01 K at cell 75
front  2.748260 -> 2.748173 -> 2.748086 -> 2.747998 -> 2.747911 mm   monotone
```

**The fixture matched the frozen one.** GATE.md fixed C2's budgets at 7256..7260 while the probe
derives them, and the derived budget scales as `1/S_L^2` — so a moved `S_L` would have produced a
different fixture and a gate-execution deviation. The determination solve returned 7260, identical
to the baseline, so no deviation arose. The risk was real and is recorded because it was flagged in
the verification script *before* the run, not explained afterwards.

### C3(a) — PASS. `1.840822e-05` against `1.0e-02`.

### C3(b) — PASS. `resid(26)/resid(18) = 0.179491` against `0.50`.

```text
relaxT   marchStp         T resid        S_L cm/s
    18      30949   1.8408222791e-05      4.67194
    26      44705   3.3041077386e-06      4.67194
```

The per-step change now **falls 5.6x over a 44 % longer march**. On the lagged tree the same
measurement gave `1.000069` — no fall at all. This is the criterion that matters most for what comes
next: it is the first evidence in this campaign that *march until the state stops changing* is a
sound stopping rule for this solver at all, which is the blocker under TD-028 and TD-031.

**The probe's own commentary is inverted here** and must not be read as the verdict: its text was
written for the baseline, where "NO FALL" was the required result. That inversion was noted in the
verification script before the run. Read the ratio.

### C4 — PASS. `3.945849e-07` against `2.0e-06`.

Better than **both** baseline branches (`8.69e-07` and `1.63e-06`), by 2.2x against the better one,
so the pre-registered reporting rule for the `8.69e-07 … 2.0e-06` band does not trigger.

**Reported rather than hidden:** at 250 cells the value is `5.645223e-07`, which sits *between* that
fixture's two baseline branches (`3.64e-07` and `6.13e-07`). C4 as frozen applies at the gated
fixture only, so this is informational — but it is not a uniform improvement and saying otherwise
would overstate the result.

### C5 — PASS. `S_L = 4.671937 cm/s`, a **0.000 %** move against a 3.99 % bound.

Identical to the six figures printed, at the gated fixture and at 250 cells (`4.822961`), across all
five budgets and both march lengths. This was the gate's pre-registered prediction and its stated
reasoning — at steady state the transient term vanishes identically whatever its form, so the fixed
point belongs to the spatial operator and the fix must not move it. It did not move.

A limit on the claim: "identical to six figures" is what the probe printed. This is **not** a bitwise
claim and none is made.

### C6 — PASS. `331 / 32 / 343`, every test passing.

```text
CFDIoTests                                            331 / 331 PASSED    201 ms
CFDAppLayerTests                                       32 /  32 PASSED   2487 ms
CFDSpeciesTests  --gtest_filter=-G8PremixedFlameTest.* 343 / 343 PASSED  13597 ms
```

Each binary was rebuilt and verified newer than the newest production source, the harness aborting
if not. `CFDSpeciesTests` hashed `6d96a2b82d382e1b…` against the frozen `732f6049fc09a18d…`,
which is positive evidence it was relinked against the fixed library rather than reused.

### C7 — **PASS**, both clauses.

```text
clause 1   median of the 4 post-warm-up gated-fixture solves
           419 / 432 / 437 / 440 s, MEDIAN 434.8 s   against the bound 695.1 s   PASS
clause 2   G8PremixedFlameTest, all 12 tests, 4343 s total. Worst single test
           TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength at 1283 s
           against the 7200 s cap -- 5.6x of headroom.                           PASS
```

**Not everything got faster, and the speed-up claim is narrowed accordingly.** The plateau test was
recorded at ~997 s pre-fix (it missed TD-004's 900 s harness cap by 97 s) and now takes 1283 s, up
29 %. So the 22 % improvement holds for the gated-fixture solves and **not** across the suite. The
likely reason is benign -- that test previously short-circuited on a saturated quantity and now does
real work -- but it is recorded as a slowdown rather than explained away.

Clause 2 **cannot be reported from the C6 run**, because C6 deliberately excludes
`G8PremixedFlameTest` — precisely the tests that drive the premixed flame and therefore the only
ones whose cost this fix could change. Reporting C7 as a PASS on clause 1 alone would be exactly
the kind of partial measurement this gate exists to prevent. `logs/04` measures it.

**The fix made the solver faster, by 22 %** (434.8 s against the baseline 556.1 s; 59.2 s against
71.1 s at 250 cells). A plausible reason is that the chemistry sub-stepper no longer has to
integrate a field swinging 1484 K per step, so its adaptive control stops binding — **but
`chemistrySubSteps` was not recorded, so that is a hypothesis and not an attribution.** The
criterion only required no slowdown; the speed-up is reported, not claimed as a result.

## 3. Three corrections against myself

**The C3 replacement was unnecessary.** Post-fix `temperatureResidual` is `1.84e-05`, so the
original draft bound of `1e-3` would have passed with 54x margin. My predicted post-fix value was
`1.76e-4` — 10x too pessimistic. The re-derivation was defensible on the information available
(the draft's stated margin really was computed from a 107x-wrong sampled figure), but it did not
need doing, **and this result does not vindicate loosening the level from `1e-3` to `1e-2`.** What
earned its place is C3(b), the direction clause: `0.179` post-fix against `1.000069` pre-fix. The
lesson is that a mis-derived *margin* is not the same thing as a mis-derived *criterion*, and I
replaced the criterion when recomputing the margin would have done.

**My predicted `R` was 107x wrong in the dry-run**, for the same reason: predicted from the five
cells a prior log happened to print, not from the maximum over all 500 that the criterion takes.
Recorded in `dry_run.md` section 3.1.

**The pre-fix front position was an artefact in BOTH branches.** The cold branch reported
3.535 mm — on the anchor — and the hot branch 0.261 mm. A correctly-marched solve puts the front at
**1.940 mm**, about 78 cells *upstream* of the anchor. So "the front is on the anchor" was as much
an artefact as "the front is at the inlet", and I had treated the cold branch as the physical one in
`PARITY_FINDING.md` section 1 before the mechanism was known. TD-004 therefore still has a real
displacement to account for, smaller than the alternation's and closer to `TD031/logs/02`'s 2.57 mm
than to either branch.

## 4. What this verification does NOT establish

It establishes that the march's time-level staging is fixed and that nothing outside the flame
moved. It does **not** re-validate any premixed-flame number.

```text
TD-027  C4 must be re-run on this march with a ladder that CROSSES parity. REOPENED.
TD-004  C2, and the front displacement now measured at 1.940 mm rather than on the anchor
TD-005  C3 and C6
TD-028  the "limit cycle" is root-caused; the item's own closure is not in TD-032's scope
TD-029  the two tests passing by error cancellation -- dt is still derived from dx
TD-030  the 107.892 % reaction integral
TD-031  the march length, whose convergence measurement was invalidated
```

Each needs its own gate, its own dry-run and its own authorization. None of it is TD-032's scope,
and the full regression required to close TD-032 itself (Debug + GUI, sanitizers at CI settings,
clang-format) has **NOT** been run yet.

## 5. The scoped regression — `logs/05_regression_scoped.log`

```text
clean-first Release build         203 s; libcfdcore.a reproduced a9b01668... BIT FOR BIT, so the
                                  verification above was not an incremental-build artefact
full Release ctest -j16 --timeout 7200   2296 s, 12 failures:
                                    7  G8PremixedFlameTest  (section 4, and summary.md section 4)
                                    4  SST channel tests    ) long-standing P20 / TD-009 debt,
                                    1  DriverSeegmiller      ) 39-85 files each in results/, and
                                                             ) unreachable from a lambda inside
                                                             ) solvePremixedFlame
ASan + UBSan at CI settings, scoped to the changed path:
    343 non-G8 species tests      PASSED   102 s
    2 named G8 tests              1 passed, 1 failed (the same assertion it fails in Release)
                                  11612 s -- ASan costs 38x here
    CFDThermalTests   163 tests   PASSED     4 s
    CFDTransportTests  22 tests   PASSED     0 s
    SANITIZER DIAGNOSTICS         ZERO -- no ASan error, no LeakSanitizer report under
                                  detect_leaks=1, no UBSan runtime error, across 528 tests
clang-format                      FAILS, in files this item did not touch -> TD-034
```

**The sanitizer result is the one this leg existed for.** The fix changes *when* two vector copies
happen, so memory safety was the specific risk; 528 tests including the premixed-flame path returned
zero diagnostics. The G8 test that failed under ASan failed on its own assertion, identically to
Release — it is not a sanitizer finding.

**Scope stated, not glossed:** 2 of 12 G8 tests ran under ASan. The other 10 are not covered, and
the full sanitizer run that would cover them is infeasible as specified (TD-035).

**A flaw in this harness:** ctest was piped through `tail -40` and the disabled-test list is longer
than that, so the aggregate pass/fail counts were truncated out of the log. The failure *list* came
from ctest's own `LastTestsFailed.log`, which is authoritative, but CLAUDE.md section 4 wants exact
counts from the run and this log cannot supply them. The pipe must be fixed before the Debug and
sanitizer passes.
