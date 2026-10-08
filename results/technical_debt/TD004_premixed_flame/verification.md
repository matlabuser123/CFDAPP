# TD-004 — VERIFICATION — **STOPPED AT C2**

```text
item      TD-004  Canonical premixed-flame capability · P1 · route (a)
gate      GATE.md sha256 12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73
            (recomputed at the start of this run -- unchanged)
logs      11_post_fix_balance.log   the fix's FIRST version, which broke the solver
          12_post_fix_balance_v2.log the corrected version, measured here
verdict   STOPPED. C2 FAILS: measured ratio 46.9362, threshold <= 2.0.
          The fix is correct and is NOT reverted; it removed the defect it targeted and
          exposed a second one that no part of route (a) addresses.
```

---

## 1. Criteria, in frozen order

| # | criterion | measured | verdict |
| --- | --- | --- | --- |
| C1 | the ledger is exact | established pre-freeze against the unchanged operator (`logs/05`); the ledger is the instrument that produced §3's finding | PASS (pre-freeze) |
| **C2** | **the balance no longer degrades with march length** | **ratio 46.9362** (`1.531435e-02 → 7.187978e-01` over a 9× longer march); was **14.6577** before the fix | **FAIL** |
| C3 | the balance closes at the reference configuration | `1.531435e-02` against `< 0.02` — **now passing**, from `4.327958e-02` | PASS (measured) |
| C4 | the balance is insensitive to the recentring RATE | `0.0153 / 0.6022 / 0.7188` | FAIL (measured) |
| C5 | what already works is preserved | `T_peak` **1800.00** exactly · `x_front` **3.50000** exactly · `min Y >= 0` · `|ΣY−1| = 5.96e-07 … 8.68e-07`, inside the `1.2e-06` band | PASS (measured) |
| C6 | the eigenvalue keeps its qualified properties | `S_L = 4.86230` **unchanged** from the baseline | PASS (measured) |

**Execution stopped at C2.** C3–C6 are recorded as measured, not as a basis for closing the item.

## 2. What the fix achieved, and it is substantial

Every quantity the root cause predicted would move, moved — by two orders of magnitude.

```text
                            before fix        after fix       change
worst single-step JUMP      327.51 cells      5.13 cells      64x smaller
largest SHIFT applied       164 cells         4 cells         41x smaller
worst front offset          163.96 cells      3.04 cells      54x smaller  (dead band is 3.0)
recentrings at relaxT 90    32 669            241             135x fewer
fuel balance at relaxT 10   4.327958e-02      1.531435e-02    under the 0.02 gate
S_L                         4.86230 cm/s      4.86230 cm/s    unchanged
```

The front now stays inside its dead band (`3.04` against `3.0`), the catastrophic shifts are gone,
and the eigenvalue is untouched — so the locator was latching onto a spurious crossing, exactly as
`logs/10` measured, and fixing that was correct.

## 3. Why C2 nevertheless failed — SUPERSEDED READING, kept in place

> **INVALID AS AUTHORITATIVE.** The interpretation in this section — that the profile has multiple
> `tAnchor` crossings — was **refuted** by a later measurement: `maxFrontCrossings = 1`, so there is
> never more than one crossing. The refusal counts are real; the explanation of them was wrong.
> Section 7 carries the corrected account. CLAUDE.md §4 requires an invalid result to be marked in
> place rather than deleted, and §1.5 forbids rewriting chronology, so the original reasoning stands
> below exactly as it was written.



**The refusal counter — added precisely so this could not hide — reports a flood:**

```text
relaxT   march steps   refused front crossings   fraction of steps
  10.0          3968                       472               12 %
  30.0         11905                     4 441               37 %
  90.0         35717                    16 347               46 %
```

`PremixedFlame.hpp` states what a flood means, written before this was measured: *"A LOT of
rejections means the profile really does have multiple crossings and the flame is not what the
solver thinks it is — a different problem, and one that must not hide behind a locator that quietly
declines to look."*

That is the situation. **On 46 % of steps the temperature profile has more than one `tAnchor`
crossing**, and at least one of them is not reachable from where the front was. The locator now
refuses it rather than acting on it — which is why the shifts collapsed from 164 cells to 4 — but
the profile is still growing spurious crossings, and that is a defect in the march, not in the
locator or the recentring operator.

Two further measurements point the same way:

```text
dT residual   relaxT 10: 1.6619e-01 -> 6.9730e-01     the state is FURTHER from steady
              relaxT 90: 3.7687e-01 -> 9.8982e-01     than before the fix
balance at relaxT 90   6.343790e-01 -> 7.187978e-01   slightly WORSE
```

**A plausible reading, labelled as a hypothesis rather than a finding:** the 164-cell shifts were
periodically re-seeding the profile wholesale and masking the oscillation. With them gone the
oscillation persists and accumulates, so the state drifts further from steady and the balance is
worse at long marches. This is consistent with every number above but is **not** established — it
would need the oscillation itself instrumented, which this item has not done.

## 4. The fix is kept, not reverted

Latching onto a crossing the front could not have reached is unambiguously wrong, and the fix
removed it without moving the eigenvalue. Reverting a correct fix because it exposed a worse problem
would put the catastrophic shifts back and re-hide the oscillation. The fix stands; TD-004 does not
close.

## 5. A defect introduced BY the fix, and caught by its own guard

The fix's first version (`logs/11`) **broke the solver**: `S_L` came back at `29.56138 cm/s` against
`4.86230`, the final march took **0 steps**, and the fuel balance went to `9.49e-01`.

Cause: `previousLocated` / `haveLocated` were function-scope, so the locator's tracked position
outlived the profile it tracked. Every trial calls `seedProfile()`, which discontinuously returns
the front to the anchor; the stale previous position then made that legitimate front look like an
inadmissible jump, so it was refused, `locateFront` returned false, every trial reported the front
as gone and the bisection collapsed onto its seed. **A correctness check given state with the wrong
lifetime.**

It was caught by the probe's own fail-closed guard — `*** PROBE INVALID: the final march length did
not change (0 every time)` — which exists because an earlier version of that probe printed a false
plateau. Without it, three identical rows and a zero-step march would have read as yet another
plateau. Corrected by forgetting the tracked front at all three re-seed points.

## 6. Remaining blocker

```text
The temperature profile develops spurious tAnchor crossings on up to 46 % of march steps.
```

Not route (a), and not the recentring operator — the locator and the shift are now both behaving.
Closing TD-004 requires either the oscillation removed, or an eigenvalue method that does not depend
on locating a single crossing (which is TD-005's territory).

**No threshold was weakened, no criterion dropped, and the gate's sha256 is unchanged.** C2's
`<= 2.0` stands at a measured `46.9362`.

---

## 7. THE CORRECTED ACCOUNT, and five refuted hypotheses

`logs/13` through `logs/16` dismantled §3's reading and three successors. Recorded as a chain
because the pattern matters more than any single step: **each hypothesis fitted the data available
at the time and was killed by the next measurement.**

| # | hypothesis | refuted by | measurement |
| --- | --- | --- | --- |
| 1 | recentring is the root cause | `logs/09` | the content ledger explains 131 % of the imbalance at `relaxT 10` but only 24 % at `relaxT 90`, and is **not** proportional to the recentring count (8 857 recentrings produced a *smaller* net change than 920) — so the shifts largely cancel and "each shift leaks a cell" was wrong as an accumulation model |
| 2 | the profile has multiple `tAnchor` crossings | `logs/13` | `maxFrontCrossings = 1`. Never more than one crossing; the single crossing is simply refused |
| 3 | the admissible-jump bound is too tight | `logs/14` | only **108** of 4 441 rejections are under 10 cells, and that count is **identical** at every march length — a fixed startup artefact. 15 858 are over 50 cells |
| 4 | `seedProfile()` reconstructs the field mid-march | `logs/15` | **zero** rejections land on the anchor; the worst jump lands at `0.1969 mm`, cell 9 of 500 |
| 5 | the inlet is igniting because it is too close | `logs/16` | upstream room `2.85 -> 4.07` preheat thicknesses changed the `>50c` count `3952 -> 3951`. The worst jump grew `168 -> 243` cells, i.e. the **same landing point** — the crossing relocates to the inlet regardless of how far away the inlet is |

### 7.1 What is actually established

```text
* there is only ever ONE tAnchor crossing                            (logs/13)
* it relocates to within ~9 cells of the inlet on up to 46 % of steps (logs/15)
* that is independent of the distance to the inlet                    (logs/16)
* the >50-cell jump count tracks the fuel balance exactly:
      0 jumps -> 1.53 % imbalance ; 3952 -> 60 % ; 15858 -> 72 %
```

For the **only** crossing to sit nine cells from the inlet, everything downstream of it must be
above `tAnchor` — the flame has filled essentially the whole domain. That is **flashback**, not a
boundary artefact, and it is a flame-stabilisation problem rather than anything route (a) touches.

### 7.2 A genuine side-result

`S_L = 4.86230 cm/s` at `anchorFraction` **0.35 and 0.50** — identical to six figures. That is
**property 3's own criterion** (plateau in anchor position) passing at two points, for a criterion
the suite currently cannot run at all because its test times out.

### 7.3 The fix's performance cost, which fails §5.1 independently

The locator change cost **~2x** in wall time after the early-out (507 s per configuration against a
~250 s baseline), and ~6x before it. `GATE.md` §5.1 requires that **no other test acquire a
timeout**, and three already have at 900 s. So the fix as written is likely to fail that clause on
performance alone, separately from C2.

### 7.4 Why the investigation was stopped rather than continued

Five successive refutations is not a sequence of unlucky guesses; it is evidence that the
instrument was wrong. Every hypothesis was inferred from **counters derived from the profile** —
jump magnitudes, rejection counts, landing positions — when the thing being inferred **is** the
profile. The next step that would be worth trusting is dumping the temperature field at a step
where a jump occurs and looking at it directly. That is a sixth hypothesis and a new line of work,
and it was put to the user rather than started.
