# TD-003 audit — criterion 2 ANSWERED, criterion 1 satisfied with a carved-out defect, criterion 3 has a mechanism and a one-constant candidate fix

```text
STATUS:      AUDIT COMPLETE. No production change, no test change, no gate frozen yet.
             C1  SATISFIED in substance -- all four reacting-path solves check status. But the
                 ESCALATION is broken, which is a separate defect (TD-047).
             C2  **ANSWERED DECISIVELY.** The class is solvable iff r <= 1. At r = 1.05, where
                 the problem is still well-posed, BiCGSTAB BREAKS DOWN and GMRES(30) stagnates
                 at 5000 iterations while forward substitution is EXACT. A direct triangular
                 solve is adequate; neither available Krylov method is. The repository has no
                 such solver.
             C3  the 1e-8 gate IS reachable and FAILS at 88x. Mechanism identified; the fix is
                 a candidate ONE-CONSTANT change that the source already intends. The decisive
                 confirming measurement is NAMED and NOT RUN.
SCOPE:       audit + one probe. Criterion 2's measurement is complete; criteria 1 and 3 are
             diagnosed, not fixed. Nothing is authorized beyond this audit.
INSTRUMENT:  tools/plugflow_matrix_class_probe.cpp  e0e5b81e41f30178...
             library 0d0dfaa96ac97d5f... (recorded, not gated)
             Reference = forward substitution, exact and independent of any CFDApp solver (§6).
             Three controls, ALL behaved -- §2.1.
EVIDENCE:    logs/01_matrix_class.log · this file
             G8 values read from TD006_g8_closure/logs/01 (the 2026-10-11 Release run)
COMMIT:      none sought beyond carrying this audit as evidence.
```

---

## 1. CRITERION 1 — satisfied in substance; the ESCALATION is not

> *"Every reacting-path solve's status checked or its residual judged against `||b||`."*

Four solve sites exist on the reacting path. All four check:

```text
PremixedFlame.cpp:689   acceptable = raw.converged() || finalResidual <= 1e-9 * ||b||   BOTH forms
SpeciesSolver.cpp:91     if (!converged()) -> SpeciesStatus::LinearSolveFailure, break
ProjectRunner.cpp:931    if (converged()) use it, else reactingSolveFailed = true
                         and carries an explicit "C12 / TD-003: the status is CHECKED" comment
ProjectRunner.cpp:976    if (converged()) use it, else reactingSolveFailed = true
```

**So the criterion as written is met.** What is not met is anything happening as a result.

### 1.1 The failure cannot reach the exit code — TD-047

```text
ProjectRunner.cpp:501    out.status = statusFor(result.status)      <- from the FLOW (SIMPLE) solve
ProjectRunner.cpp:785    the reacting block BEGINS here, after :501
ProjectRunner.cpp:1039   reactingRun = std::move(run); out.reactingResult = reactingRun;
```

`out.status` is never revisited by the reacting block, and `exitCodeFor()` switches on `out.status`
alone. **A reacting march in which every species and energy solve failed exits 0, provided the
upstream flow solve converged.**

```text
run.anyLinearSolveFailed       ONE writer (:1017), ZERO readers anywhere in src/, apps/, tests/
run.temperatureSolveConverged  set at :1016, NOT printed by apps/cli/main.cpp
run.speciesResults[i].status   IS printed -- "Species X converged: no (linear solve failed)"
```

So a species failure is visible in stdout and nowhere else; a temperature failure is visible
nowhere; and neither moves the exit code. For a CI job or a script, the run succeeded.

**This is adjacent to C1, not C1 itself**, so it is carved out as **TD-047** rather than folded in —
the same treatment TD-002 → TD-019 got.

## 2. CRITERION 2 — ANSWERED. Measured, with controls.

> *"A solver demonstrated adequate on the plug-flow matrix class."*

### 2.1 The controls first, because the result is only worth what they are

```text
CONTROL 3  exactness of the reference, n=10, r=0.5    worst |x - x*| = 0.000e+00     PASS
CONTROL 1  positive, r = 0.5 (strictly dominant)      BiCGSTAB Converged, err 2.0e-16 PASS
CONTROL 2  negative, CG on a NON-SYMMETRIC matrix     MaxIterations, err 8.8e+01      FAILS as
                                                      required, at EVERY r and n
```

All three behaved. CG never once "passes", which is what makes the passes below mean something.

### 2.2 The class, and where the cliff is

`A` is lower bidiagonal — `a_ii = d = 2.0`, `a_{i,i-1} = -s`, `r = s/d`. That is pure first-order
upwind convection with a transient diagonal and **no diffusion**, which is the matrix P18.7 broke on
(`03_plugflow_diagnosis.log`: diagonal 2.0 on every row). It is **not** the class
`test_bicgstab_breakdown.cpp` already covers — that one is convection-*diffusion*, a tridiagonal
M-matrix at cell Peclet 0.5. Dropping the diffusion drops the superdiagonal and the M-matrix
property with it.

At `n = 200`:

```text
   r    kappa_inf     BiCGSTAB          GMRES(30)              fwd-subst
0.50    3.000e+00   Converged  19 it   Converged   35 it      exact, err 2.2e-17
0.90    1.900e+01   Converged 158 it   Converged  214 it      exact, err 0.0
1.00    4.000e+02   Converged 311 it   Converged  988 it      exact, err 0.0
1.05    7.090e+05   BREAKDOWN  14 it   MaxIter   5000 it      exact, err 0.0
2.00    4.821e+60   BREAKDOWN  12 it   MaxIter   5000 it      err 1.1e+41
16.0   7.557e+240   BREAKDOWN  10 it   MaxIter   5000 it      err inf
```

**`r = 1.05` is the decisive row.** `kappa = 7.09e5` is perfectly solvable in double precision, and
forward substitution returns the exact answer with zero error. BiCGSTAB breaks down after 14
iterations with a true relative error of **3.0**; GMRES(30) burns all 5000 iterations and still has
an error of **0.80**. So:

```text
ADEQUATE        a direct triangular (forward-substitution) solve -- exact, O(n), no tolerance.
NOT ADEQUATE    BiCGSTAB (breaks down) and restarted GMRES(30) (stagnates), at a ratio where
                the problem itself is well-conditioned.
AND             the repository HAS no direct/triangular solver. CG, BiCGSTAB, GMRES only.
```

This confirms the register's "restarted GMRES(30), the library default, stagnated on the same
matrix" — and shows it is not a sizing question at small `n`: at `n = 10`, `m = min(30, 10) = 10`,
so GMRES is FULL GMRES and converges in exactly 10 iterations at `r <= 0.9`, as it must.

### 2.3 Two findings that were not being looked for

```text
JACOBI BUYS NOTHING ON THIS CLASS. Every /none and /jac pair above is bit-identical, because the
diagonal is CONSTANT and Jacobi is then a scalar rescaling, which cannot change a Krylov
trajectory. PremixedFlame.cpp:688 applies JacobiPreconditioner to exactly this kind of system.
(Its own comment justifies Jacobi by the chemistry's Patankar Sp term spanning orders of
magnitude -- which DOES vary the diagonal in the flame, so this finding is about the plug-flow
class, not an error in that comment.)

r > 1 IS NOT A SOLVER DEFICIENCY. At r = 2, n = 200, kappa = 4.8e60 and even exact forward
substitution returns 1.1e+41, because the true solution spans 2^200. That regime is ILL-POSED,
and no solver is owed a result there. Only r <= ~1 is a fair target, which bounds what
criterion 2 can reasonably demand.
```

## 3. CRITERION 3 — reachable, failing at 88x, and the mechanism is a residual-vs-error confusion

> *"The `1e-8` ΣY test gate in `G8PremixedFlameTest` reachable."*

```text
test_premixed_flame.cpp:471 (2026-10-11 Release run)
  Expected: (flame.maxCompositionSumDeviation) < (1.0e-8),
    actual: 8.822866316426925e-07 vs 1e-08
```

**Reachable: YES.** It executes and reports, unmasked — the preceding `EXPECT_LT(mismatch, 0.01)` at
`:467` is non-fatal, so `:470`-`:471` run. **Passing: no — 88x over.** And the value has moved from
TD-003's recorded `6.29e-07` to `8.82e-07` (+40 %), on a tree that has since had TD-004, TD-028 and
TD-037 land, so this is a different tree rather than a regression.

### 3.1 ΣY = 1 is EMERGENT. Nothing enforces it.

`grep -iE 'normalis|normaliz|clip'` over `PremixedFlame.cpp` returns **nothing**. There is no
normalisation and no clipping, deliberately — `ReactionMechanism.hpp:110-112` records the policy.
The solver's own comment at `:615`-`:619` states what ΣY = 1 rests on instead:

> *"with unity Lewis number every species shares the same matrix, so the N systems sum to the
> mixture continuity equation, and that cancellation only holds to the tolerance each one is solved
> to. maxCompositionSumDeviation is what reports whether it held."*

### 3.2 Three naive readings were refuted before the right frame appeared

Measured across TD-005's twelve C3 configurations:

```text
"tolerance-limited"      predicts |sumY-1| ~ CONSTANT. Observed spread is a factor of 4.8
                         (4.12e-7 .. 1.983e-6), and sweeping the bracket tolerance 64x gives
                         1.288e-6 -> 8.823e-7 -> 1.674e-6, which is not monotonic. REFUTED.
"round-off accumulation" predicts growth with step count. Steps rise 2.89x (45 399 -> 131 265)
                         while |sumY-1| FALLS 0.57x. The SIGN is wrong. REFUTED.
"driven by march length" march spans 15 201..15 639, a 2.9 % spread, while |sumY-1| spans 4.8x.
                         REFUTED.
```

Sweep A is the control for all three: at drift windows 16/32/64 the value is **bit-identical**
(8.8230e-07), so the quantity is reproducible and the variation in B/C/D is real — a property of the
STATE each configuration lands on, not noise.

**Three refutations is the point at which this campaign's own rule says to suspect the instrument
class**, and `maxCompositionSumDeviation` is precisely the pattern that rule names: a reduction
(`max` over cells) of a quantity derived from the field being diagnosed. Going to its definition
rather than hunting a fourth correlation is what produced §3.3.

### 3.3 The frame that fits: |ΣY−1| is an ERROR, and the acceptance bounds a RESIDUAL

```text
PremixedFlame.cpp:625   linearSettings.relativeTolerance = 1.0e-11      <- what the author INTENDS
PremixedFlame.cpp:662   residualAcceptanceFraction       = 1.0e-9
PremixedFlame.cpp:686   scaled.absoluteTolerance = 1e-9 * ||b||         <- OVERRIDES the above
PremixedFlame.cpp:690   acceptable = converged() || finalResidual <= 1e-9 * ||b||
```

A relative **residual** of `1e-9` bounds the **error** only by `kappa * 1e-9`. The 4.8x spread in
§3.2 is then expected, not anomalous: anchor position moves the front, which changes the matrix,
which changes `kappa`.

```text
to reach |sum Y - 1| < 1e-8, the solve needs a relative residual < 1e-8 / kappa.
```

**And the required tolerance is already written in the file.** For `kappa ~ 1e3` the requirement is
`1e-11` — exactly the `relativeTolerance` set at `:625` and then undercut at `:686`. So criterion 3's
candidate fix is a one-constant change, and it restores the solver's own stated intent rather than
inventing a threshold.

### 3.4 THE NUMBER THAT LOOKS LIKE PROOF AND IS NOT

The probe establishes `kappa_inf = 2n` **exactly** for the bidiagonal class at `r = 1` (measured
2.000e+01 / 1.000e+02 / 4.000e+02 at n = 10 / 50 / 200; ratio to `2n` = 1.000 in all three). Applied
to the flame's 500 cells that gives `kappa = 1000`, hence a bound of `1000 * 1e-9 = 1.0e-6` against
the observed `8.823e-07` — agreement to 12 %.

**That agreement is NOT established, and I am flagging it rather than banking it.** Two of its
premises are unverified:

```text
r = 1 WAS ASSUMED, not measured. For pure upwind with a transient term, a_P = rho V/dt + F and
  a_W = -F, so r < 1 strictly -- and at r = 0.9, n = 200 the measured kappa is 19, not 400,
  which would predict 1.9e-8 rather than 1e-6. The prediction is sensitive to r near 1.
THE FLAME'S MATRIX IS NOT THIS CLASS. PremixedFlame.cpp:1061 supplies
  gamma = lambda(T)/(cp * Le), so the flame's species matrix is TRIDIAGONAL with diffusion,
  while kappa = 2n was derived for a bidiagonal matrix. The plug-flow class and the flame's
  class are different matrices.
```

So the **mechanism** (error <= kappa x residual) is sound and general; the **12 % agreement may be
coincidence**. What settles it is one measurement, named in §5.

## 4. WHY THIS DOES NOT PROCEED TO A FIX HERE

The protocol is AUDIT → FREEZE GATE → REPRODUCE → FIX, and a gate for criterion 3 cannot be derived
yet because its threshold depends on the flame's own `kappa`, which §3.4 shows is unmeasured.
Freezing a gate on the assumed `kappa = 1000` would be deriving a threshold from a number I have
just said may be coincidental — which is the specific failure §5.1 and §5.3 exist to prevent.

Criterion 2's fix is also larger than one constant: the repository has no direct or triangular
solver, so "adequate on this class" needs either a new solver, an ILU-type preconditioner (exact on
a bidiagonal matrix), or a documented restriction to `r <= 1`. That is a production architecture
decision, not an audit's call.

## 5. WHAT IS NOT ESTABLISHED

```text
NOT measured     the FLAME's own kappa. This is the one measurement that decides criterion 3's
                 threshold and converts §3.4 from suggestive to established. It needs the
                 flame's assembled species matrix dumped once and its condition number taken --
                 no solver run, no production change.
NOT established  that tightening residualAcceptanceFraction to 1e-11 brings |sum Y - 1| under
                 1e-8. It is a PREDICTION. It also costs iterations: at r = 1, n = 200,
                 BiCGSTAB needed 311 iterations for 1e-11 against 19 for r = 0.5, and the flame
                 already runs ~73 000 steps, so the cost must be measured before it is adopted.
NOT established  that r <= 1 always holds for the flame or the plug-flow case in practice. The
                 algebra in §3.4 argues it; no run was instrumented to confirm it.
NOT established  that the 12 % agreement in §3.4 is causal rather than coincidental. §3.4.
NOT done         any fix for C1's escalation gap (TD-047), C2's missing solver, or C3's
                 constant. All three are production changes outside an audit's scope.
NOT re-run       the 6.29e-07 figure TD-003 records. It is carried forward from a different
                 tree and the move to 8.82e-07 is attributed to intervening fixes, not measured
                 against a reconstructed baseline.
NOT covered      r > 1. §2.3 shows that regime is ill-posed, so it is excluded deliberately
                 rather than left untested.
```
