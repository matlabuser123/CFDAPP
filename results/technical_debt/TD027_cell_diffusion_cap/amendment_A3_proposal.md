# TD-027 C2 — §5.3 derivation. The reference was NEVER derivable, and the replacement needs no extrapolation.

```text
STATUS:      §5.3 steps 2-3 COMPLETE (investigate, derive). Step 4 is REQUEST AUTHORIZATION, which
             this document does. NOTHING IS FROZEN and no threshold is changed.
SCOPE:       C2 of TD-027's GATE.md. C1, C3, C4 (re-measured and PASSING), C5, C6, C7 untouched.
EVIDENCE:    TD005_flame_speed_extraction/logs/18_dt_ladder.log      the ORIGINAL ladder, pre-fix
             TD040_recentre_conservation/logs/03_dt_ladder_fixed_tree.log   the fixed tree
             TD043_eigenvalue_lattice/audit.md                       the lattice
```

---

## 1. C2 as frozen

```text
C2   "S_L is within the temporal-error budget of the converged reference"
     one solve at the new default
     |S_L - 4.67589| / 4.67589 <= 0.28 %
     reference: Richardson limit over {dt/2, dt/4, dt/8}, p = 2.321  (GATE.md line 34)
     budget: a LABELLED empirical constant from §3.2, derived from G8's published band
```

Already on record: the reference is **contaminated**, because it was extrapolated on the
blind-locator solver and `TD040…/logs/03` excluded it — all four dt levels on the fixed tree sit
within `0.136 %` of each other near `4.96`, and `4.67589` is `5.9 %` away.

## 2. THE STRONGER FINDING: it was never derivable at all

`logs/18_dt_ladder.log` reports `bracket = 6.7713e-04` at **every** level. That is
`2^(1/1024) - 1` — TD-043's lattice, present in the original measurement. Converting its increments:

```text
dt/1 -> dt/2   -0.20615 cm/s  =  -65.39 quanta     SIGN CHANGE, correctly excluded
dt/2 -> dt/4   +0.01579 cm/s  =   +4.99 quanta
dt/4 -> dt/8   +0.00316 cm/s  =   +1.00 quanta     <- the fine difference is ONE quantum
```

The extrapolation used `{dt/2, dt/4, dt/8}`, so `n1 = 4.99` and `n2 = 1.00`:

```text
p = log2(n1/n2) = 2.322        GATE.md records 2.321 -- this arithmetic reproduces theirs
uncertainty      (1.00, inf)   because n2 - 1 = 0, the upper bound is UNBOUNDED
GATE_A6's floor  both differences >= 4 quanta        n2 = 1.00  ->  UNRESOLVED
```

**So `4.67589` is one arbitrary point in an unbounded family.** The extrapolated limit is a function
of `p`, `p` is undetermined on `(1, inf)`, and the limit is correspondingly undetermined.

This is **independent of the locator contamination** and strictly stronger: even on a perfect
solver, that ladder could not have produced a reference. The defect predates TD-004's fix.

### 2.1 And the fixed tree cannot produce one either

`TD040…/logs/03`'s increments are `+1, 0, +1` quanta. A zero difference makes the order undefined
outright. **No Richardson reference for C2 is obtainable at `settings.tolerance = 1e-3` on either
library.** Any amendment that still gates against an extrapolated limit would be unimplementable.

## 3. THE REPLACEMENT — measure the temporal error, do not extrapolate it

```text
C2-A2   | S_L(dt_default) - S_L(dt_finest) | / S_L(dt_finest)
        + | last increment |              / S_L(dt_finest)     <=  0.28 %

        over a dt ladder of AT LEAST THREE HALVINGS from the default, at fixed dx.
```

**Derivation.** The quantity C2 wants to bound is the temporal discretisation error at the default
`dt`. Write it as the distance to the finest measured level plus whatever remains beyond it:

```text
|S_L(dt) - L|  <=  |S_L(dt) - S_L(dt/8)|  +  |S_L(dt/8) - L|
                   ^ MEASURED                ^ the tail
```

The tail is bounded by the last increment whenever the sequence is monotone with `p >= 1`:
for a convergent sequence of order `p`, the remaining tail is `last / (2^p - 1) <= last` for
`p >= 1`. **`p >= 1` is a strictly weaker assumption than the `p = 2.321` the original relied on**,
and unlike `p = 2.321` it does not require the order to be resolved.

```text
the budget 0.28 % is UNCHANGED. It is §3.2's labelled empirical constant from G8's published
band and is not re-derived here.
```

## 4. DRY-RUN AND NON-VACUITY — both from existing measurements, at zero cost

```text
                                    measured    tail      total    vs 0.28 %
PRE-FIX,  cap 20  (dt/1 -> dt/8)     4.0042 %  0.0676 %  4.0718 %  FAIL by 14.5x
FIXED,    cap 5   (dt   -> dt/8)     0.1353 %  0.0677 %  0.2030 %  PASS
```

Both legs are **three halvings**, so the ladder depth is matched and the comparison is like-for-like.

```text
NON-VACUITY   the cap-20 configuration C2 EXISTS TO REJECT fails by 14.5x.
              the shipped cap-5 configuration passes, with 1.38x margin.
              Neither evaluation uses an extrapolation anywhere.
```

**C2's discriminating power is preserved.** The frozen C2 rejected cap 20 at `3.99 %` against
`0.28 %`; C2-A2 rejects it at `4.07 %` against the same `0.28 %`. The criterion does the same job
through a quantity that can actually be measured.

## 5. What C2-A2 does NOT do

```text
does NOT weaken  the budget. 0.28 % is unchanged, and the tail bound assumes p >= 1 where the
                 original assumed p = 2.321 -- a weaker, safer assumption.
does NOT bound   the error to the TRUE limit beyond the p >= 1 tail. If the sequence were
                 sub-linear (p < 1) the tail bound fails. The measured increments are
                 +1, 0, +1 quanta on the fixed tree, which is consistent with p >= 1 but does
                 not establish it -- the lattice forbids establishing it. STATED, not hidden.
does NOT resolve the order. It is designed not to need it.
does NOT change  C1, C3, C4, C5, C6 or C7. C4 was re-measured and PASSES on a parity-crossing
                 ladder (verification_A2.md).
does NOT close   TD-027. Closure needs the whole gate, and this addresses one criterion.
```

## 6. THE AUTHORIZATION REQUESTED

§5.3 step 4. Nothing below is done without an explicit decision.

```text
(A) freeze C2-A2 as GATE_A3 for TD-027, with §4 as its dry-run and non-vacuity check, then
    re-run FRESH on the fixed tree. The fresh run needs ONE dt ladder at cap 5 -- about 4 h, since
    it is the same four-level ladder TD-040 ran. C2 would then PASS and TD-027's only open
    criterion would be closed.
(B) freeze C2-A2 but accept §4 as the verification too, on the grounds that the fixed-tree leg
    IS a measurement on the current tree from logs/03. Saves 4 h but reuses an earlier result,
    which §5.3 step 6 forbids -- so this needs an explicit waiver, not a silent one.
(C) do not amend. Record that C2's reference is undeliverable at this tolerance, leave C2 FAILED,
    and let TD-027 stay open pending TD-043's tolerance change.
```

**I recommend (A).** The 4 h is the same ladder TD-040 already ran once, so the cost is known
exactly rather than estimated, and it is the only option that leaves TD-027 with a criterion that
both passes and was verified fresh.

## 7. What is NOT established

```text
NOT established  that p >= 1 on either tree. §5. The lattice prevents it, which is why the
                 criterion is built to need only the assumption and not the measurement.
NOT established  that three halvings is the right ladder depth. It is chosen because it is what
                 both existing ladders happen to span, which is a practical reason and not a
                 derived one. A deeper ladder would tighten the measured term and leave the tail
                 unchanged.
NOT re-measured  C2 on the fixed tree with this form. §4's fixed-tree leg comes from logs/03,
                 which is the DRY-RUN. Option (A) is what would make it a verification.
NOT addressed    whether 0.28 % is still the right budget. It is §3.2's labelled empirical
                 constant and is carried over untouched; re-deriving it is not in this scope.
```
