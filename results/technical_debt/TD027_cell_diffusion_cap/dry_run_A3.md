# TD-027 GATE_A3 — DRY-RUN AND NON-VACUITY for C2-A2. It can pass and it can fail. Qualified for freezing.

```text
STATUS:   DRY-RUN COMPLETE. C2-A2 executed against the UNCHANGED baseline and against the fixed
          tree, from TWO libraries, with matched ladder depth. It FAILS the configuration C2 exists
          to reject and PASSES the shipped one. GATE_A3.md is qualified for freezing.
METHOD:   C2-A2 is arithmetic on measured S_L values, so the dry-run evaluates it against the
          recorded ladders. §5.3 step 6 still requires a FRESH verification afterwards, and the
          fixed-tree leg below does NOT serve as that.
SOURCES:  TD005_flame_speed_extraction/logs/18_dt_ladder.log            PRE-FIX  (blind locator)
          TD040_recentre_conservation/logs/03_dt_ladder_fixed_tree.log  FIXED    (fa815c97/852555cf)
          TD043_eigenvalue_lattice/audit.md                             the lattice
```

---

## 1. The two ladders

```text
PRE-FIX, logs/18 (500 cells, dx fixed, r = 2 in dt)
  dt s          divisor   S_L cm/s    bracket
  3.7697e-05    dt/1      4.86230     6.7713e-04
  1.8849e-05    dt/2      4.65615     6.7713e-04
  9.4243e-06    dt/4      4.67194     6.7713e-04
  4.7122e-06    dt/8      4.67510     6.7713e-04

FIXED, logs/03 (500 cells, dx fixed, trial cap scaled as 1/dt)
  9.424300e-06  dt        4.955332
  4.712150e-06  dt/2      4.958687
  2.356075e-06  dt/4      4.958687
  1.178038e-06  dt/8      4.962045
```

`bracket = 6.7713e-04` in every pre-fix row is `2^(1/1024) - 1`, so the lattice is in both ladders.

## 2. C2-A2 evaluated

```text
                                      measured    tail      total     budget   verdict
PRE-FIX,  cap 20   dt/1 -> dt/8        4.0042 %  0.0676 %  4.0718 %   0.28 %   FAIL by 14.5x
FIXED,    cap 5    dt   -> dt/8        0.1353 %  0.0677 %  0.2030 %   0.28 %   PASS, 1.38x margin
```

Both legs are **three halvings**, so the depth is matched and the two libraries are compared
like-for-like.

```text
measured term  |S_L(default) - S_L(finest)| / S_L(finest)
tail term      |last increment|            / S_L(finest)
```

## 3. NON-VACUITY

```text
FAIL demonstrated   the PRE-FIX cap-20 configuration, by 14.5x. This is the configuration TD-027
                    exists to reject -- the unvalidated `maximumCellDiffusionNumber = 20.0` -- and
                    C2-A2 rejects it on the same budget the frozen C2 used.
PASS demonstrated   the FIXED cap-5 configuration, the shipped value, with 1.38x margin.
```

**C2-A2 is non-vacuous, and it preserves exactly the discrimination the frozen C2 had:**

```text
frozen C2   cap 20 -> 3.99 % against 0.28 %   FAIL
C2-A2       cap 20 -> 4.07 % against 0.28 %   FAIL
```

The two agree on the verdict and on its size, through a quantity that can actually be measured.

### 3.1 Why the frozen C2 could not be dry-run this way

It gated against `4.67589`, an extrapolated limit. Evaluating it requires that limit to exist, and
§1 of `GATE_A3.md` shows it does not: the fine difference behind it was ONE lattice quantum, so the
order's interval is `(1.00, inf)` and the limit is unbounded. **The frozen criterion was not merely
mis-referenced; it was not evaluable.** C2-A2 removes the extrapolation entirely, which is what
makes a dry-run possible at all.

## 4. What this dry-run does NOT establish

```text
NOT a verification  §5.3 step 6 requires a FRESH run reusing no earlier result. §2's fixed-tree
                    leg comes from TD-040's logs/03 and is the DRY-RUN.
NOT established     that p >= 1, which the tail bound assumes. The fixed tree's increments are
                    +1, 0, +1 quanta -- consistent with it, and the lattice forbids establishing
                    it. If the sequence were sub-linear the tail bound would not hold.
NOT established     that three halvings is the correct depth. GATE_A3.md §2.2 labels it a practical
                    choice: it is what both ladders span.
NOT comparable      the two legs' DEFAULT dt. Pre-fix cap 20 defaults to 3.7697e-05 and fixed cap 5
                    to 9.4243e-06, a factor of 4 apart -- that IS the difference between the two
                    caps and is the point of the comparison, but it means the two legs are not the
                    same physical ladder, only the same ladder SHAPE.
NOT re-measured     C1, C3, C4, C5, C6, C7. C4 was re-measured separately and PASSES.
```

**§4's fourth item is worth stating plainly.** The pre-fix leg is not a control in the sense of "the
same configuration with one thing changed" — it is a different cap on a different library. It
demonstrates that the criterion CAN fail, which is what §5.2 requires, and it does not isolate why.
