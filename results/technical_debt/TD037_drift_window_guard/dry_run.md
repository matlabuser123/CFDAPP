# TD-037 DRY RUN — every criterion against the UNCHANGED tree, before the freeze

```text
run       logs/01_dry_run.log        2026-10-07T17:38 -> 18:0x
VOID run  logs/01_dry_run_VOID.log   discarded, preserved, see section 6
status    the gate is NOT frozen until this record is complete.
```

## 1. What was measured, and on what

```text
environment   Ubuntu-22.04 on WSL2, nproc 32, Release, OpenMP OFF, CUDA OFF
library       libcfdcore.a   a9b01668be7ccb7f...   the CURRENT tree, TD-032's fix included
production    PremixedFlame.cpp   f9f554bf9e1fe1fd...
gate as run   GATE.md  65910edc818f819f...  (the DRAFT; amended after this run, see section 5)
probe         ladder_probe.cpp  d311d49f3eb49b27...
```

**The library identity matters more than usual here.** TD-037's baseline must be the tree *with*
TD-032's fix, because TD-037 exists to repair what that fix exposed. A run against the pre-fix tree
measures the wrong thing entirely — which is exactly what the first attempt did (section 6).

## 2. The ladder — C0, C1, C3

```text
driftWindow   status          S_L cm/s    final mdot        trials  unmeasured  C3
         16   Converged       4.671937    0.0550371757          15           0  PASS 13/2/0
         32   Converged       4.671937    0.0550371757          15           0  PASS 13/2/0
         64   NoFlame         7.390346    0.0870610567           4           1  not evaluable
        128   NoFlame         7.390346    0.0870610567           4           1  not evaluable
```

```text
C0  the defect REPRODUCES at window 64, with an unmeasured trial        REPRODUCED  (required)
C1  Converged at all four windows                                       FAIL, 2 of 4 (required)
C3  no sided trial disagrees with the eigenvalue                        PASS
```

**C0 reproducing and C1 failing are the required results.** They are the gate's fail-direction
non-vacuity and, with the library hash above, its C8 negative control.

### 2.1 A prediction that held, which is worth recording in this campaign

`driftWindow` **128 was in the ladder unmeasured.** I put it there on the reasoning that it is above
the measured 40-step trial and should therefore fail the same way as 64 — and said before the run
that if it *converged* instead, my model of the mechanism would be incomplete and the gate should
not be frozen. It fails identically: same status, same 4 trials, same 1 unmeasured trial, same
reported mdot. The mechanism in `audit.md` §1 predicts both windows correctly.

### 2.2 Windows 16 and 32 run the SAME search

Identical `S_L` and identical final mdot to all ten printed digits, identical trial counts,
identical C3 outcomes. The bisection path does not depend on the window below the boundary. That is
what makes them a usable reference for C2 rather than two noisy samples.

### 2.3 C3's pass-direction dry-run succeeded, and that was its purpose

At windows 16 and 32: **13 sided trials checked against the final eigenvalue, 2 skipped inside the
converged bracket, 0 wrong.** The check works and the solver's existing sides are self-consistent.
This is what licenses relying on C3 for the newly-sided trials after the fix — a check first used on
the cases it is meant to judge is not a check.

**Its limitation, stated:** C3's reach extends to newly-sided trials only after the fix exists, so
its fail direction cannot be exercised now.

## 3. C4's baseline — the window-32 record, verbatim

```text
   #               mdot              drift     steps  meas  set  ext  big
   0        0.696488454         1.92167068       160   yes    y    n    y
   1        0.348244227        0.864981197       256   yes    y    n    y
   2        0.174122113        0.326195928       544   yes    y    n    y
   3       0.0870610567         0.20788889        40   yes    n    y    y
   4       0.0435305284      -0.0432481462       264   yes    n    y    n
   5       0.0615614636       0.0553479101        83   yes    n    y    y
   6        0.051766814      -0.0117506819      1135   yes    n    y    n
   7        0.056452111       0.0115821206       175   yes    n    y    y
   8       0.0540587267     -0.00828564743      2255   yes    n    y    n
   9       0.0552424587      0.00359064132       350   yes    n    y    y
  10       0.0546473876     -0.00742403183      2765   yes    n    y    n
  11       0.0549441175     -0.00697575844      3083   yes    n    y    n
  12       0.0550930862      0.00202351134       433   yes    n    y    y
  13       0.0550185514     -0.00686130034      3172   yes    n    y    n
  14       0.0550558062      0.00142039094       474   yes    n    y    y
```

**The shortest trial is #3 at 40 steps, and 40 > 32**, so no trial at this window is unsided and the
clamp `min(32, 39)` returns 32 unchanged. The post-fix record must therefore be this sequence
exactly. Eleven of the fifteen trials extinguished, so the extinction path this item modifies is
heavily exercised here — C4 is not a weak test.

## 4. The remaining criteria

```text
C2  baseline S_L 4.671937 cm/s, final mdot 0.0550371757, at windows 16 and 32
C5  TheAnswerIsInsensitiveToTheStepsPerTrial  FAILS, 770 s
      ASSERT_EQ(flame.status, Converged) -> NoFlame at window 64   (required)
C6  CFDIoTests 331/331 PASSED · CFDAppLayerTests 32/32 PASSED
    CFDSpeciesTests 343/343 PASSED under --gtest_filter=-G8PremixedFlameTest.*
C7  re-derived after this run -- see section 5
C8  SATISFIED by this run: the unchanged tree reproduces C0 and fails C1 and C5, hash recorded
```

### 4.1 NON-VACUITY, per criterion

```text
C0   designed to FAIL on the baseline, and it does                      fail direction DEMONSTRATED
C1   FAIL 2/4                                                           fail direction DEMONSTRATED
C3   PASS on 13 existing sided trials                                   pass direction DEMONSTRATED;
                                                                        fail direction not exercisable
                                                                        until the fix exists
C5   FAILS                                                              fail direction DEMONSTRATED
C2   a NO-REGRESSION criterion. 0 % by construction on the baseline.    not exercisable now
C4   an IDENTITY criterion. Trivially true against itself.              not exercisable now
C6   a COUNT MATCH. A guard, not a discriminator.                       not exercisable now
C7   a CAP. 770 s against 7200 s.                                       not exercisable now
```

**The discriminating criteria are C0, C1, C3 and C5.** C2, C4, C6 and C7 are guards, and three of
them cannot fail on the baseline by construction. Recording that split is the point: a gate whose
only demonstrated failures are its guards discriminates nothing.

**C2 and C4 are the ones that can stop this item**, and neither can be dry-run in its fail
direction — so their thresholds are derived rather than observed: C2 from the solver's own bisection
tolerance, C4 from an identity that the measured 40-step minimum makes exact.

## 5. CRITERIA CHANGED BY THIS DRY RUN — before the freeze, which is what it is for

`logs/01` ran against **GATE.md draft `65910edc818f819f…`**. Two changes, both recorded in GATE.md
with the original text preserved inline:

```text
C7  RE-DERIVED. The draft bounded the window-32 solve at 1.25x its baseline wall time. That cannot
    stand: the baseline at windows 64 and 128 ABORTS after 4 trials instead of running 15, so a
    fixed solver necessarily does MORE work -- the C5 test takes 770 s only because window 64
    quits in ~14 s. Bounding a repaired path against an aborting one measures nothing, and a factor
    chosen now would be fitted to make the expected outcome pass. C7 keeps only the repository's
    own constraint, ctest --timeout 7200, and reports wall times ungated.
C2, C4  BASELINES FILLED IN from the measurements above. No threshold moved.
```

Thresholds **not** touched: C2's `1.0e-3`, C3's eigenvalue comparison and its bracket skip, C4's
identity, C6's counts, C7's 7200 s.

## 6. THE FIRST ATTEMPT WAS VOID, and it was operator error

`logs/01_dry_run_VOID.log` is preserved with an `INVALID AS AUTHORITATIVE` banner. It measured
`libcfdcore.a 3b94eb849f9c8277…` and source `dac2992006a51cce…` — **the pre-TD-032 tree**.

**Cause.** It was launched while TD-032's pre-fix trial-record probe was still executing. That probe
swaps the source tree to the pre-fix form, builds it, runs, and restores on exit — so for most of
that run the tree was pre-fix, and its exit trap then restored the tree mid-run at 17:30:18. The
probe binary had already linked against the pre-fix library while later test targets would relink
against whatever existed at that moment, making the log a mixture of two libraries.

**This is not a tool failure.** CLAUDE.md's hardware policy gives an evidence-producing run the
machine, and a job that MUTATES the source tree must never overlap another. The re-run was launched
only after verifying both the source hash and that no other process was active.

## 7. FREEZE DECISION — FREEZE

```text
C0  REPRODUCED      the defect appears at the gate's own fixture
C1  FAIL  2 of 4    windows 64 and 128 both NoFlame
C3  PASS  13/2/0    the side-check works and existing sides are consistent
C5  FAIL            the test TD-032 broke still fails
C2  baseline recorded: 4.671937 cm/s / 0.0550371757
C4  baseline recorded: the 15-trial record in section 3
C6  331 / 32 / 343, all passing
C7  770 s against 7200 s
C8  SATISFIED: this run is the negative control, hashes recorded
```

All four discriminating criteria behave as required on the unchanged tree, the one criterion with a
reachable pass direction (C3) was dry-run in it, and the two criteria that can stop the item have
derived rather than fitted thresholds. **No threshold was weakened to obtain any of this**: C7's
change removed a bound that would have been *easier* to pass than the physics warrants, and it
happened before the freeze and before any fix existed.

Frozen at `logs/00_freeze.log`. Any change to GATE.md after that point is an amendment requiring its
own file, its own dry-run and its own authorization (CLAUDE.md §5.3).
