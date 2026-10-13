# GATE_A4 — TD-027 C3's instrument, amended. FROZEN. The threshold of 2 does not move.

```text
AUTHORIZED:  the amendment by the user 2026-10-12 (audit_C3.md §6); this freeze 2026-10-13.
SUPERSEDES:  C3 as stated in GATE.md. C3 is NOT deleted, loosened or migrated -- GATE.md stands
             as written and C3's failure record in verification_A3.md stands verbatim.
             audit_C3.md is why: C3's instrument cannot express C3's own threshold.
DRY-RUN:     dry_run_A4.md · logs/A4_01_dryrun.log (9 h 29 m, four levels, idle machine)
NON-VACUITY: satisfied in that run -- 1 PASS and 2 FAIL from one instrument, and C3's own cap-20
             (58.925) and cap-10 (6.232) rejections reproduced.
NEXT:        §5.3 step 6 -- rerun fresh, reusing no earlier result. ~9.5 h.
```

## 1. THE DEFECT BEING REPAIRED

C3 read `E / (sqrt(1+tolerance) - 1) <= 2`. Both of its terms were set by the same knob:

* `E` is a difference of `S_L` values, and every `S_L` is a bisection bracket **centre**, resolvable
  only in units of the terminal bracket `q`;
* `sqrt(1+tolerance)-1` is computed from the same `tolerance` that fixes `q`.

Since consecutive brackets halve, `q ∈ (tolerance/2, tolerance]` while the denominator is
`≈ tolerance/2`, so `q/B ∈ (1,2]` for **every** tolerance. C3 could therefore only return
`m × (q/B)`, and its threshold of **2 sat in the unreachable gap** between `m=1` (1.3546) and `m=2`
(2.7092). Its lowest score, `0`, was a dt ladder that had resolved nothing. Derived in
`audit_C3.md` §3; the derivation reproduces C3's recorded `4.06` exactly.

A second defect: C3 used the **formula** `sqrt(1+tolerance)-1`, but the solver publishes
`massFluxResidual`, the measured terminal bracket, which is always the larger. C3 judged a claim the
solver does not make.

## 2. THE FROZEN CRITERION

| id | criterion | quantity | threshold |
| --- | --- | --- | --- |
| **C3-A4** | **the precision the solver PUBLISHES is coherent with its temporal discretisation error** | `E / B`, with `B :=` `result.massFluxResidual` of the audited configuration (**measured**, not computed from `tolerance`), and `E :=` the C2-A2 temporal-error estimate (measured term + tail) from a dt ladder of **at least three halvings at fixed dx**, run at a measurement tolerance `tol_meas` satisfying `q(tol_meas) <= E/20` | **`E / B <= 2`** |

**The threshold `2` is UNCHANGED from C3.** `§5.3` forbids re-deriving a threshold because a
measurement moved, and this amendment does not: the constant is untouched and it is the *instrument*
that was shown unable to express it.

**`tol_meas = 1.0e-4`**, derived: `E ≈ 1.52e-3`, so `q <= 1.0e-4` is required, and `1.0e-4` gives
`q = 8.461627e-05` — the **loosest** value meeting the requirement, so the cost is the smallest that
works (`k` goes 10 → 13, three extra bisections per level).

**The audited configuration is the shipped default.** Since commit `7c2056a` that is
`tolerance = 1.0e-3`, which is also what the gated fixture uses, so **the two now coincide and the
verdict is unambiguous**. Before that commit they disagreed (17.988 against 2.248) and the gate
would have had to choose; it no longer does.

### 2.1 What the amendment does and does not achieve — stated precisely

A4 does **not** make the ratio continuous. `E` is still an integer number of fine quanta. What it
does is make the grid **eight times finer** and put the threshold **on** it:

```text
C3   step 1.3546   reachable 1.3546, 2.7092, 4.0638 ...        threshold 2 UNREACHABLE
A4   step 0.1250   reachable ... 1.8744, 1.9994, 2.1244 ...    n = 16 -> 1.9994, ATTAINABLE
```

The step is `q(tol_meas) / B(tol_judge)`, hence `1.0` when the two tolerances are equal. **The
decoupling buys resolution only when the measuring and judging tolerances differ**, which bounds
what this criterion can do and is recorded so no one over-reads it.

## 3. WHAT THE FRESH RUN MUST REPRODUCE

Pre-registered from the dry-run. A deviation is a result, not something to absorb.

```text
E                                   0.152210 %   (+/- one fine quantum, 0.0085 %)
B, shipped default + fixture 1e-3   6.771307e-04  ->  ratio 2.248   FAIL
B, loose control 5e-3               2.711275e-03  ->  ratio 0.561   PASS
B, former default 1e-4              8.461627e-05  ->  ratio 17.988  FAIL
bisections per level                13, identical at every level
```

**Fail-closed conditions, any of which VOIDS the run and issues no verdict:**

```text
an unsettled trial at any level          the drift signs the bisection reads are untrustworthy
a non-Converged level                    the extra bisections hit a cap
fewer than four levels completing        the ladder is not three halvings deep
E deviating from 0.152210 % materially   the ladder is not reproducible; report before concluding
```

## 4. NON-VACUITY, as demonstrated in the dry-run

```text
MUST FAIL  cap 20   58.925   reproduced   <- the two rejections C3 was derived on and made
MUST FAIL  cap 10    6.232   reproduced      correctly. A replacement that cannot reproduce
                                              them is not a replacement for C3.
MUST PASS  loose configuration 5e-3   0.561   a configuration claiming a precision it can honour
OBSERVED   1 PASS and 2 FAIL from the same instrument in the same run
```

The cap-20 and cap-10 figures are arithmetic cross-checks against recorded errors, not fresh solves,
and are labelled as such in the probe.

## 5. SCOPE — what this gate does NOT touch

```text
NOT changed  the threshold 2 · maximumCellDiffusionNumber (5.0) · the production tolerance
             (1.0e-3, set by its own gate GATE_TOL) · C1 · C2-A2 · C4 · C5 · C6 · C7
NOT claimed  that any cap can satisfy C3-A4. That needs the temporal order, which TD-043's lattice
             still prevents measuring even at 8x finer resolution (dry_run_A4.md §7).
NOT closed   TD-027. C3-A4 is predicted to FAIL at 2.248, and the amendment was not proposed in
             order to obtain a PASS.
```

**C2-A2 is re-confirmed, not re-run.** The dry-run measured its honest `E` as `0.152210 %` against
the `0.203139 %` on its record — its tail was one coarse quantum where the truth is two fine quanta,
a factor of four. C2-A2's PASS **stands with more margin** (1.84× against 1.38×) on its untouched
`0.28 %` budget, so nothing reopens; the recorded figure is a conservative bound, not the error.

## 6. FREEZE

Recorded in `logs/A4_00_freeze.log`: the sha256 of this file, of `tools/c3a4_probe.cpp`, of
`include/cfd/species/PremixedFlame.hpp` and `src/species/PremixedFlame.cpp`, and of the library.
