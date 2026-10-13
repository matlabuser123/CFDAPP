# TD-027 A3 — C2-A2 PASSES. And it makes C3 FAIL, because C3 was passing on an understated error.

```text
STATUS:      C2-A2 SATISFIED on a fresh run. **C3 NOW FAILS** at 4.06x against its `<= 2` bound.
             TD-027 does NOT close: its open criterion MOVED from C2 to C3. This is a §12 STOP.
SCOPE:       C2 only was amended. C3 is evaluated, not amended, and its failure is reported.
GATE:        GATE_A3.md 532e9bf1f59e34ad45d495c4ecf2a3c91342c802dc433c13c3517cb1a410e162
             frozen in logs/A3_00_freeze.log, RECOMPUTED at the start of this run and matched.
RESULT:      C2-A2 PASS (0.2030 % <= 0.28 %, 1.38x margin) · C3 FAIL (4.06x <= 2 required)
EVIDENCE:    logs/A3_01_verification.log  (fresh; reuses no earlier result, §5.3 step 6)
REGRESSION:  NOT RUN. No production change, and TD-035 blocks the §8 ladder.
COMMIT:      none — not authorized.
```

---

## 1. C2-A2 — PASS, and the ladder is reproducible

```text
level   dt              trial cap    S_L cm/s    marchSteps   fuel bal     wall s
    0   9.424300e-06       80 000    4.955332        15 283   6.3028e-03      788
    1   4.712150e-06      160 000    4.958687        30 526   2.0261e-03     2504
    2   2.356075e-06      320 000    4.958687        61 053   6.3963e-03     3871
    3   1.178038e-06      640 000    4.962045       121 940   7.9020e-03     6496
```

```text
S_L(default)        4.955332 cm/s
S_L(dt/8, finest)   4.962045 cm/s
last increment      +3.357679e-03 cm/s
halvings            3   (the frozen minimum)

measured term       0.1353 %
tail term (p >= 1)  0.0677 %
TOTAL               0.2030 %   <=  0.28 %      PASS, margin 1.38x
```

All four levels **MATCH** the dry-run to six decimals on a separately built binary, and no trial ran
out of budget — the fail-closed `settled` check did not fire. **The ladder is reproducible and the
verdict stands on a fresh run.**

## 2. AND C3 NOW FAILS

```text
C3   "the reported precision is coherent with the discretisation"
     quantity: `massFluxResidual` vs **C2's measured error**
     threshold: error / (sqrt(1 + tolerance) - 1)  <=  2
```

**C3's numerator is C2's measured error.** Amending C2 therefore changes C3's input:

```text
C2 error fed to C3                         vs sqrt(1+tol)-1   vs the REAL bracket   verdict
OLD C2 at cap 5, |S_L - 4.67589|/4.67589         1.69               1.25            PASS
C2-A2, measured term only                        2.71               2.00            FAIL
C2-A2, TOTAL (measured + tail)                   4.06               3.00            FAIL
```

```text
denominator as C3 states it   sqrt(1 + 1e-3) - 1 = 4.998751e-04 = 0.0500 %
the REAL terminal bracket     2^(1/1024)   - 1   = 6.771307e-04 = 0.0677 %   [TD-043]
```

**C3 fails on either denominator, and it fails even without the tail term.** The old `1.7x` on
record is reproduced above as `1.69`, which is what validates this arithmetic rather than asserting
it.

### 2.1 What C3's failure actually says

C3's own §3.3 rationale: *"A reported uncertainty that understates the true error by more than 2x is
a materially misleading precision claim."*

```text
the solver REPORTS a precision of   0.05 % (or 0.0677 % as the real bracket)
its true temporal error is          0.2030 %
understatement                      4.06x (or 3.00x)
```

**So the finding is not that C3's threshold is wrong — it is that the reported precision is 3-4x too
optimistic.** That is exactly the claim C3 was written to catch, and it was previously hidden
because C2's error was computed against a reference that happened to sit close to the cap-5 value.

### 2.2 Why the old C2 understated it

`4.67589` was an extrapolation whose fine difference was ONE lattice quantum, so the limit was
arbitrary (GATE_A3.md §1). It happened to land `0.0845 %` from the cap-5 `S_L` — close enough to
pass C3 at `1.69x`. **The number was not small because the error was small; it was small because the
reference was in the wrong place.** Replacing it with a measured difference plus a conservative tail
produced an honest `0.2030 %`, and C3 then fires.

## 3. THE PATTERN, for the third time in this campaign

```text
TD-004   the locator fix was CORRECT and moved C2's ratio 14.66 -> 46.94, because the removed
         defect had been MASKING a second one.
TD-032   closing it made TD-037's latent driftWindow defect reachable.
TD-027   replacing C2's undeliverable reference with an honest measurement makes C3 fail.
```

Each time the correct action made a gate worse by unmasking something. The rule that has held every
time: **keep the fix, keep the threshold, stop the item, and check whose scope the unmasked blocker
is in.** C3 is in TD-027's own scope, so TD-027 stays open rather than carving out.

## 4. THE STOP

```text
C2-A2  PASS. TD-027's previously open criterion is satisfied.
C3     FAIL at 4.06x against <= 2. The gate stops here.
```

TD-027 does **not** close. Its open criterion moved from C2 to C3, and C3's failure is a **real
measurement of a real property** — not a mis-derived threshold. Options, none taken:

```text
(a) reduce the cap further. C3's ratio scales with the temporal error, so a smaller cell diffusion
    number would shrink it. At cap 5 the error is 0.2030 %; roughly, halving dt again would need
    cap ~2.5. That is a PRODUCTION DEFAULT CHANGE with its own gate, and it would move every
    recorded S_L.
(b) tighten `settings.tolerance` (TD-043). This cuts both ways: it SHRINKS the denominator, which
    makes C3 HARDER to pass, while also resolving the lattice. It is not a fix for C3.
(c) re-derive C3. Its 2x is a labelled empirical constant, but re-deriving a threshold because the
    measurement moved is exactly what §5.3 forbids.
(d) accept and record that the reported precision understates the temporal error by 3-4x, and
    stop TD-027 on C3.
```

**(d) is what this document does.** The others are decisions, not findings.

## 5. What is NOT established

```text
NOT established  that cap 2.5 would pass C3. §4(a)'s estimate scales the error linearly in dt,
                 which the measured increments (+1, 0, +1 quanta) do not support -- the lattice
                 forbids establishing the rate. It is an order-of-magnitude guess, labelled.
NOT established  that p >= 1, which C2-A2's tail bound assumes. Stated in GATE_A3.md §3 and
                 unchanged by this run.
NOT established  that `massFluxResidual` is the right reported-precision proxy. C3 names it, and
                 this evaluation uses C3's stated denominator formula plus the real bracket as a
                 cross-check. Whether either is the quantity a user would read as "the precision"
                 is not examined.
NOT re-measured  C1, C4, C5, C6, C7. C4 PASSES (verification_A2.md); the others were recorded as
                 unaffected by the TD-032 note, and C5/C6's status predates all of this work.
NOT amended      C3. It is evaluated and reported failing.
NOT closed       TD-027.
```

**§5's first item bounds the obvious next move.** "Lower the cap until C3 passes" cannot be sized
from this data, because the quantity it would need to extrapolate is the one the lattice makes
unresolvable. That is TD-043's reach extending into a third criterion.
