# TD-003 C3 — SOLVED. `|ΣY−1|` is round-off in the RHS amplified by a near-singular operator, and the 1e-8 gate asks for closure 65× BELOW double precision. §12 STOP: the gate is MIS-DERIVED.

```text
STATUS:      **MECHANISM ESTABLISHED, NOT INFERRED.** The predictor and the measured deviation
             are the SAME NUMBER to seven significant digits:
                 max |A^-1 (sum_i b_i - A*1)|  =  2.301527e-06
                 max_c |sum_i y_EXACT - 1|     =  2.301527e-06
             The assembled RHS closes to **3.162982e-11** -- 3.5x the round-off floor
             `eps*||b||` -- and the final march's operator amplifies that by **7.276e+04**.
             **THE 1e-8 GATE IS UNACHIEVABLE BY CONSTRUCTION.** It would require the RHS to
             close to `1.374e-13`, which is **65x BELOW** the assembly's round-off floor of
             `8.938e-12` at this `||b||`. No solver tolerance, mechanism, fill value or
             assembly change can deliver sub-round-off closure.
             **This is CLAUDE.md section 12: "a gate criterion looks mis-derived."** I STOP here.
             Amending a frozen threshold needs authorization (section 5.3) and I have not
             touched it.
SCOPE:       the closure test. Production source UNCHANGED and hash-gated. No test change, no
             threshold change, no gate frozen.
INSTRUMENT:  tools/instrument_residuals.patch, now carrying BOTH the residual decomposition
             and the closure block. Pure addition. Applies with `patch -p0` from the repo root
             and reproduces the instrumented source to the exact hash
             `90368768c2d66e002eb6b68a6555f5598d6f72c9b82663dcb24adc3cd3179e9e`, with
             production still `fa815c9721658448...`. Reproduces the gated values EXACTLY -- §1.
             This patch SUPERSEDES the decomposition-only version (`d3e1f95c571a3727...`) that
             logs/02 ran; it is a strict superset and reproduced every one of that run's
             numbers bit-identically, so nothing is lost.
             **ONE INSTRUMENT DEFECT FOUND AND REPORTED -- section 6.**
EVIDENCE:    logs/03_closure_test.log
COMMIT:      carried as evidence.
```

---

## 1. THE CONTROL

```text
                              this run          previous run / recorded     match
S_L                           4.955332 cm/s     4.955332 / 4.95533          yes
maxCompositionSumDeviation    8.822866e-07      8.822866e-07                EXACT
worstLinearResidual           9.999987e-10      9.999987e-10                EXACT
recentrings                   223               223                         yes
totalTimeSteps                73683             73683                       yes
```

Bit-identical to the previous instrumented run, which was itself bit-identical to the gated test.

## 2. THE TELESCOPING CANDIDATE IS ELIMINATED

```text
max |A_i - A_0| across species, all bands     0.000000e+00
```

The per-species matrices are **bit-identical**, so `sum_i (A y_i) = A (sum_i y_i)` holds exactly
and the constraint's algebra is sound. Had this been nonzero, no amount of exact solving could
have closed `sum Y = 1`. It was worth testing and it is now excluded.

## 3. THE CHAIN, CLOSED

```text
max_c |sum_i b_i - (A*1)_c|          3.162982e-11     the RAW closure residual
  at cell 0                          1.177771e-11
  at the last cell                   1.781599e-11
  interior cells                     3.162982e-11     the worst is INTERIOR, cell 115
max |A^-1 (sum_i b_i - A*1)|         2.301527e-06
max_c |sum_i y_EXACT - 1|            2.301527e-06     <- THE SAME NUMBER
```

**The predictor equals the measurement to all seven printed digits.** `|sum Y - 1|` is `A^-1`
applied to the RHS closure residual, and nothing else is acting.

### 3.1 The residual is round-off, and the arithmetic says so

```text
measured closure residual            3.162982e-11
round-off floor  eps * ||b||_max     8.937575e-12     (eps 2.220e-16, ||b||_max 4.025126e+04)
ratio                                3.5              round-off over a handful of summed terms
```

This is consistent with every source site preserving `sum Y = 1` exactly, which was established
separately: the inlet composition sums to bit-exactly 1.0, `sum_i omega_i` cancels exactly, the
recentring fill sums to exactly 1.0, the seeded profile is a convex blend of two compositions that
each sum to exactly 1, and `sum_i burnt_i = 1 + xi * sum_i (nu_i W_i)` with `sum_i nu_i W_i`
exactly zero. **No site injects a deviation. The assembly simply cannot sum N terms to 1 more
tightly than double precision allows.**

### 3.2 The amplification is the other half

```text
realized amplification               7.276e+04
||A^-1||_inf of the assembled matrix 1.200e+05
realized / bound                     0.61             a bound need not be attained
```

`||A^-1||_inf = 1.2e+05` because the FINAL MARCH uses a much larger `dt` than the trials, so
`rho V/dt` stops dominating and the operator approaches the steady convection-diffusion one, which
is nearly singular under a zero-gradient outlet — a constant is almost in its null space. That
near-null-space direction is exactly the direction `sum Y` lives in, which is why a round-off
residual lands on it with full force.

## 4. THE GATE ASKS FOR SUB-ROUND-OFF CLOSURE

```text
to reach |sum Y - 1| < 1e-08 at amplification 7.276e+04,
  the RHS must close to                        1.374e-13
the assembly's round-off floor at this ||b||   8.938e-12
the gate demands closure                       65x BELOW the floor
```

```text
derived floor on |sum Y - 1| = eps * ||b|| * amplification   6.503e-07
observed                                                      2.302e-06
observed / derived floor                                      3.54
```

The observed value sits **at** the derived floor, up to the same factor 3.5 by which the residual
exceeds `eps*||b||` — which is what "round-off limited" means quantitatively.

**So the `1e-8` bound is not a threshold this formulation fails. It is a threshold double
precision forbids.** `1e-8` is an absolute bound on a quantity whose floor is set by `eps`,
`||b||` and the operator's conditioning, none of which `1e-8` refers to.

### 4.1 The repository already does this correctly elsewhere, which is the precedent

`SpeciesTransport.hpp:149` states the section-30 bound for the *diffusive* closure as:

> *"8 * eps * N_species * max_f max_i |J^0_{i,f}|. A RELATIVE bound scaled by the largest raw flux
> actually present, because an absolute tolerance on a dimensional flux is meaningless across
> meshes and diffusivities."*

That is the same quantity class and the same reasoning, derived in `diffusion_closure.md` section 7
**before** measurement. The `1e-8` on `sum Y` is the absolute-constant version of exactly what that
comment warns against.

## 5. §12 STOP — AND THE DECISION IS NOT MINE

CLAUDE.md section 12 lists *"a gate criterion looks mis-derived, contradictory or vacuous after
freezing"* as a stop condition, and section 5.3 requires, for a wrong frozen threshold: preserve
the failure verbatim, investigate without changing production, derive the replacement from first
principles, **request authorization**, then freeze the amendment as a separate file with its own
dry-run and non-vacuity check.

Steps 1-3 are done. **Step 4 is the user's.** Nothing was amended.

```text
OPTION A  amend the gate to a DERIVED relative bound, in the form the repository already uses:
            |sum Y - 1|  <=  C * eps * ||b||_inf * ||A^-1||_inf
          with C a small integer fixed before re-measurement. Measured here: C ~ 4 suffices
          (observed/floor = 3.54). This accepts the formulation and corrects the number.
          COST: a section 5.3 amendment file with its own dry-run. No production change.
OPTION B  reduce the AMPLIFICATION rather than the bound -- the 1.2e+05 comes from the final
          march's dt. A formulation that keeps the operator diagonally dominant would amplify
          far less and might reach 1e-8 honestly. COST: a production change to the march, with
          its own equivalence evidence, and it would move every recorded S_L.
OPTION C  enforce sum Y = 1 explicitly. **FORBIDDEN** by the project's own stated policy -- the
          flame solver carries "no clipping, no normalisation" deliberately
          (ReactionMechanism.hpp:110-112). Listed only to record that it was considered.
```

**My recommendation is A, with a caveat.** A is cheap, matches an existing precedent in the same
file, and is honest about what double precision permits. The caveat is that A *accepts* an
amplification of `1.2e+05`, and a near-singular operator in the final march is worth its own debt
item regardless of this gate — it means any round-off anywhere in that assembly reaches the
solution magnified by 10^5.

## 6. AN INSTRUMENT DEFECT IN MY OWN REPORT

```text
printed:  "interior cells with residual > 1e-14      36019193 of 500"
```

**The pair is mislabelled.** `36019193` accumulates over every step; `500` is the per-step cell
count. The correct statement is `36019193` of `73683 x 498 = 36694134` **(cell, step) pairs**, i.e.
**98.2 %** — nearly every interior cell carries a residual above `1e-14` at nearly every step,
which is consistent with round-off at `||b|| ~ 1e+04`.

The number is right and the label is wrong. **No conclusion in this document depends on it**: the
findings rest on `maxClosureAbs`, `maxPredictedDev` and `maxSumExact`, all of which are per-step
maxima correctly reduced. Recorded because an unreported mislabelled counter is how a wrong
conclusion gets built later.

## 7. WHAT IS NOT ESTABLISHED

```text
NOT established  that C = 4 is the right constant for Option A. It is read off this measurement,
                 and section 5.3 forbids deriving a replacement threshold from observed output.
                 A real amendment must derive C from the number of summed terms in the assembly
                 and then dry-run it.
NOT established  that ||A^-1||_inf = 1.2e+05 is necessary. It is measured for THIS march's dt;
                 whether the dt is itself well chosen is a separate question (Option B).
NOT established  that the same floor applies to the PRODUCTION reacting path. This measured the
                 FLAME fixture. ProjectRunner's march shares the assembler but not the settings,
                 and its own |sum Y - 1| is reported without a gate.
NOT established  that 1e-8 was unreachable when it was FROZEN. The bound may have been derived
                 against a different mesh, dt or ||b||, in which case it was reasonable then and
                 is stale now. Nothing here examines its derivation.
NOT measured     the diffusive closure's own sum_i J_i in this run. It did not need measuring:
                 the closure residual on the ASSEMBLED RHS is 3.16e-11, which bounds every
                 contribution to it including the closure's. `closureResidual()` already gates
                 that quantity at a round-off bound and its tests pass.
NOT changed      anything. The 1e-8 threshold stands untouched and the test still fails.
```
