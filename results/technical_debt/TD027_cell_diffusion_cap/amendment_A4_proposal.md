# TD-027 AMENDMENT A4 — C3's instrument, proposed. The threshold of 2 does NOT move.

```text
AUTHORIZED:  by the user, 2026-10-12, for the amendment derived in audit_C3.md §6.
STATUS:      PROPOSAL. Nothing is frozen yet and nothing has been run for it. §5.3's order is
             proposal -> dry-run on the unchanged baseline -> non-vacuity -> FREEZE -> rerun fresh,
             and this is step one.
WHAT MOVES:  C3's INSTRUMENT only. The threshold stays `<= 2`, `maximumCellDiffusionNumber` stays
             `5.0`, the production `tolerance` stays `1.0e-4`, and C1/C2-A2/C4/C5/C6/C7 are
             untouched. No production file is proposed for change by this amendment.
WHAT IT FIXES: C3 could only return `m x (q/B)` with `q/B` in `(1,2]`, so its threshold sat in an
             unreachable gap and its best score was a ladder that resolved nothing (audit_C3.md §3).
```

---

## 1. THE TWO DEFECTS A4 REMOVES

**(a) Numerator and denominator are the same knob.** `E` is a difference of `S_L` values, each a
bisection bracket centre resolvable only in units of the terminal bracket `q`; and `B` is computed
from the same `tolerance` that sets `q`. A4 decouples them.

**(b) C3 conflates a BOUND with the PUBLISHED value.** `GATE.md` §4.3 says TD-005's C2 "bounds the
eigenvalue's reported uncertainty at `sqrt(1+tolerance)-1` ... and the solver publishes it as
`massFluxResidual`". Those are two different numbers:

```text
tol     the formula sqrt(1+tol)-1      what massFluxResidual ACTUALLY publishes
1e-3    4.998751e-04                  6.771307e-04        <- the real terminal bracket
1e-4    4.999875e-05                  8.461627e-05
```

The solver publishes the *measured* bracket, which is always the larger. C3 used the formula, so it
judged a claim the solver does not make. **A4 uses the published value**, which is both the honest
reading of "the reported precision" and formula-free.

*Cross-check that this is not a new invention:* `verification_A3.md` already computed the ratio
against "the REAL bracket" and recorded `3.00`. A4's definition reproduces exactly that number, so
A4 is adopting a denominator the evidence already carries rather than introducing one.

## 2. THE PROPOSED CRITERION

```text
C3-A4   "the precision the solver PUBLISHES is coherent with its temporal discretisation error"

  B   := result.massFluxResidual, as published by the configuration being audited.
         MEASURED, not computed from tolerance.
  E   := the C2-A2 temporal-error estimate (measured term + tail), but with the dt ladder run at a
         MEASUREMENT tolerance tol_meas chosen so that q(tol_meas) <= E/20.
  ratio := E / B
  THRESHOLD: ratio <= 2.    UNCHANGED from C3.
```

`E` and `B` now come from different runs **by design**: `E` is a property of the discretisation,
`B` is the claim a shipped configuration makes about it. Judging one against the other is the whole
point of the criterion, and nothing forces them onto a common grid any more, so **the ratio is
continuous and the threshold is reachable as a boundary** — which is what C3 could not do.

### 2.1 Choosing `tol_meas`, derived not picked

`E ~ 2.03e-3`. Requiring the lattice to be at most one twentieth of it gives `q <= 1.0e-4`:

```text
tol_meas = 1e-4  ->  k = 13,  q = 8.461627e-05,  E/q = 24.0 quanta  (resolution ~4 %)
```

`tol_meas = 1e-3` would give 3.0 quanta, which is the defect. `1e-4` is the loosest value meeting
the requirement, and it is deliberately the loosest so the cost is the smallest that works.

### 2.2 Which configuration is audited

**The production default**, because C3's intent is about what the solver publishes to a user, and
`PremixedFlame.hpp:196` ships `tolerance{1.0e-4}`. The fixture's `1.0e-3` is reported alongside, so
both numbers are on the record and the gap between them is visible rather than implied.

## 3. PREDICTED VALUES, pre-registered before the dry-run runs

Computed from already-recorded errors. **These are predictions; the dry-run may refute them, and if
it does, that is a result and not something to adjust.**

```text
configuration           B (published)     E          ratio     predicted verdict
PRODUCTION  tol 1e-4    8.461627e-05      0.203139 % 24.007    FAIL
fixture     tol 1e-3    6.771307e-04      0.203139 %  3.000    FAIL   (= A3's recorded 3.00)
```

So C3-A4 is **predicted to still fail**, and more clearly than C3 did. That is the expected outcome
of making an instrument honest, and A4 is not proposed in order to obtain a PASS.

## 4. NON-VACUITY — the instrument must be able to do both

```text
MUST FAIL  cap 20, judged at the fixture tolerance   E 3.9900 %  ratio 58.925   FAIL
MUST FAIL  cap 10, judged at the fixture tolerance   E 0.4220 %  ratio  6.232   FAIL
           ^ these are the two values C3 was DERIVED on and correctly rejected. A replacement
             that cannot reproduce those rejections is not a replacement for C3 (pre-registered
             in audit_C3.md §6).
MUST PASS  a configuration whose published bracket is commensurate with the error:
           tol 5e-3 -> q = 2.711275e-03, ratio 0.749   PASS
           This is free: B is read from a cheap run, E is reused from the ladder.
```

The PASS control is a configuration that claims a precision it can honour — exactly the property —
so the instrument is shown to span both verdicts rather than being stuck in FAIL.

## 5. COST, now that the cap premise is corrected

`audit_C3.md` wrongly claimed the finest level would breach `ctest --timeout 7200`. It would not:
the ladder is a standalone probe (`tools/c2a2_probe.cpp` -> `tools/td027_c2a2`, driven by a shell
script), not a gtest case, so **no ctest timeout applies** and the recorded 4 h run was never under
one. Wall-clock is the only constraint:

```text
recorded at k=10   788 + 2504 + 3871 + 6496 s = 13 659 s = 3.79 h   (4 levels)
tol_meas 1e-4      k = 13, so +3 marches per level
estimate           ~ +3150 s  ->  ~ 16 800 s = ~4.7 h
```

The per-march costs behind that estimate are derived by dividing each level's wall time by its
march count, which is **approximate** — the dry-run measures the real figure and the estimate is
labelled, not gated.

## 6. WHAT COULD INVALIDATE THIS PROPOSAL

```text
if  the ladder at tol_meas = 1e-4 does not reproduce C2-A2's S_L values at the levels they share,
    the finer bisection has changed the answer and E is not the same quantity -> STOP and report.
if  E measured at tol_meas differs materially from 0.203139 %, the recorded C2-A2 value was
    lattice-limited in a way not yet understood -> that is a finding about C2-A2, report before
    proceeding.
if  any level returns a non-Converged status at the finer tolerance, the extra bisections have run
    into maxOuterIterations or a trial cap -> STOP; the instrument is not usable as designed.
```

## 7. A SEPARATE PRODUCTION FINDING THIS AMENDMENT DOES *NOT* FIX

`PremixedFlame.hpp:256-258`'s comment justifies the cap default's coherence requirement at
`tolerance = 1e-3`, which is not the shipped default, and the shipped default understates its
temporal error by ~24x on A4's own definition. **That is a production change and needs its own
authorization.** It is recorded in `audit_C3.md` §4 and deliberately left out of A4's scope so that
an instrument amendment and a production correction are not entangled in one gate.

## 8. ORDER OF EXECUTION FROM HERE

```text
1  build a NEW probe, tools/c3a4_probe.cpp. The frozen c2a2_probe.cpp is C2-A2's evidence and is
   NOT modified.
2  DRY-RUN C3-A4 on the unchanged baseline. Record whether §3's predictions hold.
3  NON-VACUITY: the cap-20 and cap-10 rejections and the loose-tolerance PASS (§4).
4  FREEZE GATE_A4.md and record every hash in logs/A4_00_freeze.log.
5  RERUN FRESH, reusing no earlier result (§5.3 step 6).
```

Steps 2 and 3 need the machine for ~5 h and nothing else heavy may run alongside them (hardware
policy). Step 4 cannot precede step 2: a gate frozen before its dry-run is exactly what §5.2
forbids.
