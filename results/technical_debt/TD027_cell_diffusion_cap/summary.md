# TD-027 — SUMMARY — **RESOLVED**

```text
item        TD-027   maximumCellDiffusionNumber = 20.0 was an unvalidated resolution cap · P0
authorized  2026-10-05; cap value 5.0 chosen by the user the same day
gate        GATE.md   sha256 56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75
amendment   GATE_A1.md sha256 f5ac7f98a75378aab67c0337c24638c7f4bd2a713403c8941e3b6ad3a6f4883d
            adds C7 (verdict-movement accounting). Changes no threshold.
verdict     C1-C7 ALL PASS. RESOLVED.
```

---

## 1. The defect

```text
include/cfd/species/PremixedFlame.hpp     Real maximumCellDiffusionNumber{20.0};   ->   {5.0}
```

`dt = cap * dx^2 / alpha_max`, and the diffusion branch binds at the eigenvalue, so **`D` is
identically the cap** — not an upper bound loosely respected, but the realized cell diffusion number.
The value `20.0` had never been checked against a measured error.

```text
dt ladder at FIXED dx -- the first dt-only self-similar family ever run on this solver:
 cap   S_L cm/s     error vs the converged value
  20    4.86230       +3.987 %     <- the OLD default. OUTSIDE the asymptotic range.
  10    4.65615       -0.422 %
   5    4.67194       -0.084 %     <- the new default
 2.5    4.67510       -0.017 %
Richardson limit 4.67589 cm/s, GCI 0.021 %.   G8's band is +-2.80 %.
```

**The old default put a gated quantity 3.99 % off — 1.4x the band it was to be judged against — and
outside the asymptotic range, where no convergence statement is possible at all.** Every `S_L`
recorded in this repository before TD-027, including the `4.86230` quoted throughout TD-004 and
TD-005, carries that error. The corrected synthetic value is **4.67589 cm/s**.

## 2. Verdict

| # | criterion | measured | verdict |
| --- | --- | --- | --- |
| C1 | the cap IS the realized `D` | `D = 4.999999999999999`, rel `1.78e-16` against `1e-12` | **PASS** |
| C2 | `S_L` within the temporal budget | `0.084 %` against `0.28 %` (G8's band / 10) | **PASS** |
| C3 | reported precision coherent | `1.7x` against `2x` — was `80x` at the old default | **PASS** |
| C4 | the limit cycle suppressed | `max-min 8.07e-04 <= mean 8.48e-02`, by 105x | **PASS — INVALID AS AUTHORITATIVE, see the TD-032 note at the end of this file** |
| C5 | nothing changes outside the flame | **343 / 331 / 32**, zero failed lines | **PASS** |
| C6 | no case breaches 7200 s | longest 1791 s, 0 timeouts, suite 2.45 h | **PASS** |
| C7 | every verdict movement named | twelve movements, twelve named | **PASS** |

## 3. Three corrections made during the item, all against my own work

**C1 failed first, on my probe's constant.** The probe hardcoded `R = 8.314` where the code uses
`kUniversalGasConstant = 8.31446261815324`. Predicted discrepancy from that alone:
`4.999721799126`. Measured: `4.999721799126`. **Residual zero.** Fixing the instrument — drawing
`R_u`, `T_ad` and `lambda(T_ad)` from the code and the solver's own report instead of retyping them —
brought C1 to `1.78e-16`. The threshold was never touched, and the contingency I had written into the
probe for a *mis-derived* threshold did not fire.

**The audit corrected TD-005's `D` figures.** TD-005 §M.4 reported `D = 3.33` at the default using
the **unburnt** diffusivity; the code uses the **burnt** one, 6x larger, giving `D = 20` exactly.
Corrected in place there, and it made the item simpler and worse than first described.

**The attribution was refuted after verification.** `GATE.md` §4.4 claims the cap **governs** the
limit cycle. TD-031 `logs/01` measured the cycle at `D = 5` on a 250-cell mesh — the same `D` at
which it is suppressed at 500 cells — and found `dt = 3.7699e-05` produces it at both `(cap 5, 250
cells)` and `(cap 20, 500 cells)`: **same `dt`, different `D`, same behaviour.** The cycle follows
absolute `dt`. Recorded forward in `attribution_correction.md` because `GATE.md`'s hash is the freeze
record. **C4's verdict stands** *[WITHDRAWN 2026-10-06 — see the TD-032 note at the end of this file]* — the march does converge at the gated fixture, confirmed
independently by TD-005 `logs/19` — but **TD-028 is suppressed at one configuration, not resolved.**

## 4. What the fix exposed, and why that is the point

Suppressing the limit cycle removed a mask, and three defects underneath it became visible. All three
predate TD-027 and none is made worse by it.

```text
TD-029   TWO PASSING TESTS PASSED BY ERROR CANCELLATION. dt is DERIVED from dx, so a cell-count
         ladder refines both, and the spatial and temporal errors have OPPOSITE SIGNS (+0.81 %,
         -4.24 %). `TheFlameSpeedConverges...` now FAILS with refinement differences growing 23x,
         and `TheConsumptionIntegral...`'s 0.78 % identity agreement at the old default was
         cancellation, not correctness -- at the converged dt the mismatch is 7.9 %.
TD-030   the converged fuel balance is 8.47e-02, failing two tests regardless of march length.
         The reaction integral is 107.892 % of fuelThrough while convection is exactly
         -100.000 %, so the excess is in the reaction term. Spatial lead: S_c reaches
         41 % -> 68 % -> 90 % of S_L as dx falls.
TD-031   the reported state is a TRANSIENT at the default march length -- the front sits 34 cells
         off the anchor at relaxT 10 and returns by 18. And its own measurement shows the fix
         cannot be a default VALUE: two of three configurations have no convergence point at all.
```

**And one defect was resolved outright.** `TheAnswerIsInsensitiveToTheStepsPerTrial` now returns the
**identical state** at all three drift windows, spread exactly `0`, where at cap 20 it passed with a
`6.8e-04` spread while two of three configurations sat 24x outside the tree's own admissibility
bound. **TD-005's F1 — the fail-open sweep — is resolved.**

## 5. A finding worth more than this item's verdict

The stage-D control (`logs/08`, a two-library comparison whose reconstruction matched the frozen hash
exactly) measured `WithReactionsDisabledNoFlamePropagates` as **PASS in 42 s at the OLD cap**.

```text
TD-027 gets NO credit for it.
G8's negative control 1 is AVAILABLE -- TD-020 recorded it as a TD-006 blocker on the strength of
a 900 s timeout that is no longer real.
TD-020's stated premise, "the no-flame case costs MORE than the flame case", is FALSE: 42 s
against the flame cases' 256-1791 s.
```

The `brackets = 13` exit matches TD-004's locator fix rather than anything here. **Which of TD-004's
locator fix or TD-005's C4d did it is not established** — this control only excludes TD-027.

## 6. Files changed, and the two left untouched

```text
PRODUCTION   include/cfd/species/PremixedFlame.hpp   one default, with the derivation, the
             measurement, the three requirements 5 satisfies, and the 4x cost in-comment
EVIDENCE     audit · GATE · GATE_A1 · verification (logs/01-02) · regression · summary ·
             attribution_correction · 8 logs · 4 probes · 6 scripts
UNTOUCHED    GATE.md and audit.md, verified bit-identical to logs/00_freeze.log at the A1 freeze.
             Their hashes ARE the freeze record; the refuted attribution they contain is
             corrected FORWARD rather than edited in, which is the only way to preserve both
             CLAUDE.md §4 (mark invalid results) and §10 (release evidence is immutable).
NOT AMENDED  TD-004 GATE.md / GATE_A1.md and TD-005 GATE.md all carry values measured at D = 20
             and now invalidated. Each needs its own §5.3 amendment with its own authorization.
             TD-027 does not grant them, and none was edited.
```

## 7. Outstanding corroboration, recorded rather than waited for

TD-031's stage 2 re-measures `500 cells / cap 5` on a different relaxT ladder. It is **not** a control
on C4 — that verdict comes from this item's own `logs/01`, and TD-005 `logs/19` is a second
independent measurement of the same configuration — but a disagreement would be a reproducibility
problem worth knowing about. It was in flight when A1 was frozen.

## 8. No commit, no push

Neither was authorized. The working tree carries TD-004's locator fix, TD-005's C4d fix and TD-027's
cap, none committed.

---

# TD-032 NOTE — C4 IS INVALID AS AUTHORITATIVE (recorded 2026-10-06, after this item closed)

Nothing above is deleted or altered beyond the two inline markers that point here.

**What was found.** `TD031_final_march_length/logs/05_cell35_history.log` stopped the final march at
five consecutive budgets and found the reported field **alternating every single step** between the
cold preheat zone (~310 K) and a nearly fully burnt field (~1790 K) across cells 25–45. The root
cause is `src/species/PremixedFlame.cpp:1189-1191`: the transient term's old time level is copied
**before** the state is updated, so it trails the state by one step from step 2 onward, the
transient path connects step `k-1` to step `k+1`, and the even and odd steps become two interleaved
sub-sequences that need not share a fixed point. Audit:
`technical_debt/TD032_lagged_time_level/audit.md`.

**Why C4 specifically cannot stand.** C4's four levels — relaxT 18/22/26/30 — have march budgets
30949, 37827, 44705 and 51583. **All four are odd**, because consecutive levels differ by 6878, an
even number, so the ladder preserves step parity *by construction*. `max-min 8.07e-04 <= mean
8.48e-02, by 105x` is therefore four samples of one branch of the alternation. It measures
reproducibility of a parity, not convergence of a march.

**What is unaffected.**

```text
C1  D = cap, bitwise to 1.78e-16          reads dt and alpha_max, never the march's field
C2  S_L converges in dt, Richardson        taken from the SIGN of the trial drift; see the
    limit 4.67589 cm/s, gated value          narrowing in TD-032 audit section 5 -- the trials
    +3.99 % off                              run the same advance(), so this is evidence the
                                             SIGN is robust, NOT that the march is correct
C3  derived from C2                        same standing as C2
C5  343 / 331 / 32                         outside the flame entirely
C6  no case breaches 7200 s                wall times, not fields
C7  verdict-movement accounting (A1)       counts verdicts, not field values
```

**The shipped change is not withdrawn.** `maximumCellDiffusionNumber 20.0 -> 5.0` rests on C1, C2
and C3, which are the criteria that chose the value 5.0. What is withdrawn is the claim "the limit
cycle is suppressed", and with it the ground on which this item was marked RESOLVED.

**Whether to reopen TD-027 is a user decision.** It is recorded here, in `TECHNICAL_DEBT.md`, in
`TODO.md` and in `TD031_final_march_length/PARITY_FINDING.md` §6 — and deliberately not acted on.
