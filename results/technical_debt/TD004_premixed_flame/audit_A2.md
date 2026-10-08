# TD-004 AUDIT A2 — `recentre()` blinds the front locator, permanently, on its first use

```text
date        2026-10-08
authorized  TD-004, in full
status      AUDIT. No production change. The gate is ALREADY FROZEN as GATE_A1.md
            (e57bc758ca5610eb…); C2-A1 is the acceptance criterion and is not re-opened.
logs        A2_01_recentre_counters.log   the operator's own decisions
            A2_02_locator_rejections.log  the locator's rejection record
            A2_03_locator_trace.log       the raw per-step trace, via the march budget ceiling
defect      ONE LINE CLASS. `recentre()` translates the field and does not translate the
            locator's tracked position with it.
```

---

## 1. The defect

`locateFront` accepts a `tAnchor` crossing only within `admissibleJump` of `previousLocated`, the
position it last successfully located. `recentre()` translates the entire field by an integer number
of cells — `temperature`, `temperatureOld`, every species, both densities — and updates
`cumulativeShift` and `result.recentrings`.

**It does not touch `previousLocated`.** The only assignments are inside `locateFront` itself:

```text
798  Real previousLocated = 0.0;
824  previousLocated = crossing;   first call only, under !haveLocated
872  previousLocated = best;       only on a SUCCESSFUL locate
```

So the first recentring of a march moves the front out from under the locator's tracked position,
and every subsequent crossing then looks like an inadmissible jump.

## 2. The mechanism, measured end to end

`A2_03`, relaxT 10, final march stopped at successive budgets:

```text
budget    front mm  offCells  finalRc  rej<10  rej10-50  rej>50  worstRejC  worstRejPos
     1    3.500013     0.001        0      12         0       0      2.999     3.500022
   500    3.559962     2.998        0      12         0       0      2.999     3.500022
  1000    3.494269    -0.287        1     507         0       0      3.287     3.494269
  5000    3.258746   -12.063        1    3009      1498       0     15.063     3.258746
 17194    2.474665   -51.267        1    3009     12193    1499     54.267     2.474665
```

```text
1  the final march starts with the front ON THE ANCHOR (offset 0.001). The re-seed works:
   seedProfile() is called at line 1861, immediately before the march setup.
2  over ~500 steps the front drifts DOWNSTREAM to +2.998 cells, just under the 3.0 dead band.
   The locator tracks it throughout -- rej<10 stays at its inherited 12.
3  crossing 3.0, recentre() fires ONCE: shift = llround(2.998) = 3 cells.
4  previousLocated remains at 3.559962 mm -- the budget-500 position.
5  the next step measures |front - previousLocated| = 3.287 cells against an admissible jump of
   1.234 -> REJECTED. 495 rejections appear inside that one interval.
6  locateFront returns false, so recentre() is never invoked again: rejectedShifts stays 0 at
   every march length. It is not refusing; it is never consulted.
7  the front drifts UPSTREAM, away from the frozen position, so the distance grows monotonically
   and the locator refuses EXACTLY ONE crossing per step for the remaining 16 000+ steps.
```

**The arithmetic closes exactly.** At budget 1000 the front is 3.494269 mm and the rejected jump is
3.287 cells = 0.0657 mm, which puts the frozen position at **3.559962 mm — the budget-500 front
position, to six figures.**

**Exactly one rejection per step, confirmed by differencing `A2_02`:**

```text
rej>50  15254 - 1499 = 13755  over  30949 - 17194 = 13755 steps
rej>50  35888 - 15254 = 20634  over  51583 - 30949 = 20634 steps
```

**And the refused crossing is the real flame.** `worstRejectedJumpPosition` equals `flamePosition`
at every level — 2.474665, 1.940455, 1.775956 mm — and `maxFrontCrossings = 1`, so there is no
second crossing the locator could be preferring instead.

## 3. Why it never recovers, which is not obvious

A stale tracked position would be harmless if the front drifted back through it. It does not,
because **the drift REVERSES at the recentring**: downstream (+0.001 → +2.998) before, upstream
(−0.287 → −51.267) after. The shift places the front on the far side of the frozen position and the
subsequent drift carries it away monotonically.

This was the hole in the first version of this diagnosis, and the trace is what closed it. I had the
front on the wrong side.

## 4. Why it survived

```text
worstFrontJumpCells = 0.0626  the largest ACCEPTED single-step move, at every march length
```

Real front motion is **0.063 cells per step** against an admissible jump of 1.234 — a factor of 20
of headroom. The jump guard never binds on physical motion, so it looks well-tuned. It only ever
binds on the **discontinuity the solver introduces itself**, and that discontinuity happens once per
march.

Every prior observable on this item was a quantity derived from the field being diagnosed, and
cumulative counters cannot say *when* anything happened. Five diagnoses were refuted before this
session and two more today. What broke it was the per-step trace, built with no production change
from the existing `finalMarchSteps` ceiling — the same technique that broke TD-032.

**The codebase already documents this exact failure mode in the other place it occurs.** Line 1862,
for re-seeding: *"FORGET THE TRACKED FRONT. The locator takes the crossing nearest the previous one,
so that state is only meaningful for a continuous march. Re-seeding discontinuously moves the front
back to the anchor, and carrying the previous trial's position across that would make the new front
look like an inadmissible jump — which is exactly what happened: every trial reported no front, the
bisection collapsed, and S_L came back 29.56 cm/s."* That path resets `haveLocated`. **Recentring is
the same discontinuity and is unhandled.**

Same class as TD-032: an operator mutating state that another consumer tracks across steps, without
telling the consumer.

## 5. The candidate fixes

### 5.1 Translate the tracked position with the field — PREFERRED

```text
previousLocated -= static_cast<Real>(shift) * cellSize;
```

**Derivation of the sign.** `shiftField` sets `shifted[c] = field[c + shift]`, so a feature at index
`i` moves to index `i − shift`: its position decreases by `shift * cellSize`. The tracked position
must move the same way. This is consistent with the existing `cumulativeShift += shift * cellSize`,
which adds the displacement back to recover absolute positions for the drift history.

**Checked against the measurement:** `previousLocated` 3.559962 mm, `shift` 3, `cellSize` 2e-5 m
gives 3.499962 mm — and the front immediately after the shift is at ~3.4999 mm. The distance
collapses to ~0 and tracking continues.

**It preserves the guard**, which is the point: the admissible-jump test stays armed against spurious
crossings for every step, including the one after a recentring.

### 5.2 Reset `haveLocated`, as the re-seed path does — REJECTED, with reason

Correct in effect but cruder: the first-call branch accepts **any** crossing, so the jump guard is
disarmed for one step after every recentring. On a march that recentres often, that is a recurring
hole in exactly the defence that exists to stop the locator latching onto a spurious crossing. The
re-seed path can afford it because re-seeding is discontinuous in a way that makes any prior position
meaningless; a translation is not — the displacement is known exactly.

### 5.3 Widen `admissibleJump` — REJECTED

It would mask this by admitting jumps up to the shift size, and in doing so would disarm the guard
against the 164-cell excursion that `recentre`'s own refusal branch was added to stop. Treating the
symptom at the cost of the protection.

## 6. A prediction, registered before the fix, with both directions named

The budget decomposes as `gap = |convection − reaction − inletDiffusion|`. On the current tree
(TD-030 `logs/01`, relaxT 22): `|−100 + 103.922 + 4.965| = 8.887 %`.

If holding the front at the anchor returns the inlet term to roughly its §O value (~0.572 %), and
the reaction excess is unchanged:

```text
|−100 + 103.922 + 0.572| = 4.49 %   ->  BELOW the 0.05 bound  ->  C2-A1 clause (a) would PASS
```

```text
if the gap falls below 5 %          clause (a) passes and TD-004 may close, subject to (b) and (c)
if the inlet term shrinks but the   the fix is correct and insufficient; the residue is TD-030's
   gap stays above 5 %                 reaction excess and TD-004 stops again, on a named item
if the gap does not move            the inlet term was not driven by front position after all, and
                                       TD-030's audit §3 is wrong
if S_L moves                        the fix changed the eigenvalue, which it must not: the locator
                                       feeds recentring, and recentring is supposed to be
                                       conservation-neutral. That would be a stop.
```

**The third and fourth are the ones that would stop the item**, and they are named here so neither
can be rationalised afterwards.

## 7. What this audit does NOT establish

```text
NOT established  that fixing this closes C2-A1. §6 is a prediction, not a result.
NOT established  that the 87-cell displacement is the ONLY anchoring defect. It is the one that
                 explains the measured trace; a correctly-tracked front may still drift.
NOT measured     the trials. All of this is the FINAL MARCH. The trials recentre 120 times and
                 their locator behaviour is not characterised here.
NOT touched      TD-026, whose `worstFinalOffsetCells` cannot see these offsets precisely because
                 it is updated only when the control locator accepts -- which, after step ~500,
                 it never does. This audit explains WHY that field reads near-zero while the
                 front is 51 cells out.
```

## 8. Next, in gate order

The acceptance criterion is already frozen: **C2-A1 in `GATE_A1.md`**, re-measured on this tree in
`remeasurement_A1.md` as failing at clause (a) alone. No new gate is needed and none should be
written — the fix is verified by re-running the frozen criterion, with the §6 predictions as the
reading.
