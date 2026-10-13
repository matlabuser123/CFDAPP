# TD-027 GATE AMENDMENT A3 — C2 only

```text
amends        C2 of GATE.md
authorized    2026-10-10, after the §5.3 investigation in amendment_A3_proposal.md
supersedes    C2's reference value 4.67589 cm/s and the form |S_L - ref|/ref <= 0.28 %
leaves intact C1, C3, C4 (re-measured and PASSING on a parity-crossing ladder, verification_A2.md),
              C5, C6, C7 — and C2's BUDGET of 0.28 %, which is §3.2's labelled empirical constant
              from G8's published band and is NOT re-derived here
status        DESIGN. NOT FROZEN. dry_run_A3.md is the dry-run and non-vacuity check; this file's
              sha256 is frozen in logs/A3_00_freeze.log only after it passes.
```

---

## 1. Why C2's reference is replaced rather than re-measured

Two independent reasons, and the second is the stronger:

```text
CONTAMINATED   4.67589 was extrapolated on the BLIND-LOCATOR solver. TD-040's dt ladder on the
               fixed tree excluded it: all four levels sit within 0.136 % of each other near 4.96,
               and 4.67589 is 5.9 % away with the sequence moving AWAY from it.

NEVER DERIVABLE  logs/18_dt_ladder.log reports `bracket = 6.7713e-04` at every level -- that is
               2^(1/1024) - 1, TD-043's eigenvalue lattice, present in the ORIGINAL measurement.
               Its increments in quanta:
                   dt/1 -> dt/2   -65.39   (sign change, correctly excluded from the fit)
                   dt/2 -> dt/4    +4.99
                   dt/4 -> dt/8    +1.00   <- the FINE difference is ONE quantum
               so with n1 = 4.99 and n2 = 1.00, p = 2.322 (GATE.md records 2.321, which this
               arithmetic reproduces) and the uncertainty interval is (1.00, INFINITY), because
               n2 - 1 = 0. The extrapolated limit is a function of p; p is undetermined on an
               unbounded interval; therefore so is the limit.
```

**The second reason is independent of the locator.** Even on a perfect solver that ladder could not
have produced a reference. The defect predates TD-004's fix.

### 1.1 And no replacement reference can be extrapolated either

TD-040's fixed-tree ladder has increments `+1, 0, +1` quanta. A zero difference makes the order
undefined outright. **No Richardson reference for C2 is obtainable at `settings.tolerance = 1e-3` on
either library**, so an amendment that still gated against an extrapolated limit would be
unimplementable by construction. C2-A2 is built to need no extrapolation.

## 2. C2-A2

| id | criterion | quantity | threshold |
| --- | --- | --- | --- |
| C2-A2 | **the temporal discretisation error at the default `dt` is inside the budget** | `\|S_L(dt) - S_L(dt_finest)\|/S_L(dt_finest)` **plus** `\|last increment\|/S_L(dt_finest)`, over a dt ladder of at least THREE halvings at fixed `dx` | `<= 0.28 %` — §3.2's labelled empirical constant, **UNCHANGED** |

### 2.1 Derivation

The quantity C2 wants to bound is the temporal error at the default `dt`. Split it at the finest
measured level:

```text
|S_L(dt) - L|  <=  |S_L(dt) - S_L(dt/8)|   +   |S_L(dt/8) - L|
                   ^^^^^^^^ MEASURED            ^^^^^^^^ the tail
```

For a monotone sequence converging at order `p`, the tail is `last / (2^p - 1)`, which is `<= last`
for every `p >= 1`. So bounding the tail by the **last measured increment** requires only `p >= 1`.

**That is strictly weaker than what the frozen C2 relied on.** It assumed `p = 2.321` — a specific
order, extrapolated from a one-quantum difference. C2-A2 assumes only that the order is at least
first. Neither the order nor the limit has to be resolved.

### 2.2 Why three halvings

Both existing ladders span three halvings, so the depth is what the available evidence supports and
the two libraries can be compared like-for-like. **This is a practical choice, not a derived one**,
and it is labelled as such. A deeper ladder would shrink the measured term and leave the tail
unchanged, so it can only make the criterion harder to pass.

## 3. What this amendment does NOT do

```text
does NOT weaken  the budget. 0.28 % is unchanged, and the tail assumption moves from p = 2.321 to
                 p >= 1, which is weaker and therefore safer.
does NOT lose    C2's discriminating power. The frozen C2 rejected cap 20 at 3.99 % against 0.28 %;
                 C2-A2 rejects it at 4.07 % against the same 0.28 % (dry_run_A3.md §2).
does NOT bound   the error to the TRUE limit if the sequence is SUB-LINEAR (p < 1). The measured
                 increments are consistent with p >= 1 and do NOT establish it -- the lattice
                 forbids establishing it. Stated here rather than hidden.
does NOT touch   C1, C3, C4, C5, C6, C7, or any production file.
does NOT close   TD-027. Closure needs the whole gate; this addresses its one open criterion.
```

## 4. Dry-run and non-vacuity — REQUIRED BEFORE FREEZING

`dry_run_A3.md` evaluates C2-A2 on **both libraries** from measurements already on record:

| | measured | tail | total | vs 0.28 % |
| --- | --- | --- | --- | --- |
| PRE-FIX, cap 20 (`dt/1 -> dt/8`) | 4.0042 % | 0.0676 % | **4.0718 %** | **FAIL by 14.5x** |
| FIXED, cap 5 (`dt -> dt/8`) | 0.1353 % | 0.0677 % | **0.2030 %** | **PASS**, 1.38x margin |

The cap-20 configuration C2 exists to reject fails; the shipped cap-5 configuration passes. **Both
legs are three halvings and neither uses an extrapolation anywhere.**

## 5. Execution order, and the stop rule

```text
1  dry-run C2-A2 on both libraries                                   -> dry_run_A3.md
2  confirm non-vacuity: it must be shown able to FAIL                -> the cap-20 leg
3  ONLY THEN freeze this file's sha256 with the source and library hashes in logs/A3_00_freeze.log
4  re-run FRESH on the fixed tree at cap 5, reusing no earlier result (§5.3 step 6). The dry-run's
   fixed-tree leg comes from TD-040's logs/03 and does NOT serve as the verification.
```

**A failing C2-A2 is a stop.** It would mean the shipped `maximumCellDiffusionNumber = 5.0` does not
sit inside the temporal-error budget, which is the question TD-027 exists to answer — and the answer
would then be that the cap needs lowering further, not that the criterion needs changing again.
