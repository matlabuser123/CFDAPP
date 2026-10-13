# P18.4 — mass conservation (§8, §9, §20, §21, §22)

```text
A reaction conserves mass  <=>  sum_i M_i nu_i = 0
and therefore, for ANY progress rate q,   sum_i omega_i = q sum_i M_i nu_i = 0

THE SECOND FOLLOWS FROM THE FIRST IDENTICALLY. So the criterion is checked on the REACTION
DEFINITION (section 8: "Do not merely verify this after running the PDE"), and the per-cell source
sum is then a consequence -- measured anyway, because "follows identically" is a claim about code.

THRESHOLD, INHERITED VERBATIM FROM G6:
    | sum_i M_i nu_i |  <=  4 * eps * max_i | M_i nu_i |
```

---

## 1. Measured

| Quantity | Measured | Allowance |
|---|---|---|
| max per-reaction `\|Σ M_i ν_i\|` (dyadic weights) | **0** exactly | 1.110223e-16 |
| max per-cell `\|Σ ω_i\|` (dyadic weights) | **0** exactly | — |
| max per-reaction `\|Σ M_i ν_i\|` (round-off weights) | **0** | 9.989799e-17 |
| max per-cell `\|Σ ω_i\|` (round-off weights) | **1.1102230246251565e-16** | 8·eps·scale = 7.198e-15 |
| — relative | **1.2338538725026906e-16** | 8·eps = 1.776e-15 |
| closed-reactor total-mass drift | within `8·eps·N_cells` | — |

## 2. Two mechanisms, and why both are needed

```text
DYADIC WEIGHTS (M_A = 2^-4, M_B = 2^-5, M_C = 2^-3) make M_A + 2 M_B = M_C EXACT in binary, so
every residual is exactly 0. That is the SHARPEST possible statement of the criterion -- but it
never exercises the tolerance, because nothing rounds.

NON-DYADIC WEIGHTS were therefore found by search OUTSIDE CFDApp, chosen so that
M_C - M_A - 2 M_B is NON-ZERO yet within 4 eps * scale:
    M_A = 0.05552842445740003
    M_B = 0.028473354135886624
    M_C = 0.11247513272917328
On these the per-cell source sum is 1.11e-16, i.e. 0.5 eps -- non-zero, within allowance, and
MEASURED rather than bypassed.

REPORTING ONLY THE DYADIC ZEROS WOULD HAVE BEEN A VACUOUS PASS: a criterion that is satisfied
exactly by construction tests nothing about the tolerance it carries.
```

```text
A DETAIL WORTH RECORDING, because it is counter-intuitive. On the round-off weights the PER-REACTION
residual measures 0 while the PER-CELL source sum measures 1.11e-16. Both are correct:
  * massResidual sums M_i nu_i in ASCENDING SPECIES ORDER, and for this triple that association
    happens to cancel exactly -- the search found the residual non-zero under a DIFFERENT
    association (M_C - M_A - 2 M_B).
  * the source sum adds (M_i nu_i) q terms, and at q != 1 those products do not cancel exactly.
Floating-point addition is not associative, so "the residual" is a property of a summation ORDER as
well as of the numbers. P18.4 fixes that order (ascending species index) and states it.
```

## 3. Per reaction, not per mechanism — §21

```text
Each reaction is validated INDIVIDUALLY at mechanism construction.

THE TRAP THIS AVOIDS, asserted as its own test: two unbalanced reactions can cancel in an aggregate.
  "too-light"  A + B -> C      residual +0.03125
  "too-heavy"  C -> A + B      residual -0.03125
  aggregate                    0
A mechanism-level check would PASS a mechanism containing two errors. The per-reaction check
refuses it, and the test asserts that the two defects really do cancel in aggregate -- otherwise it
would not be exercising the trap.
```

## 4. Per cell, not globally — §22

```text
Measured with a SPATIALLY VARYING, SIGNED progress rate across 256 cells (q from -8 to +8), so the
cells are genuinely different and a positive error in one could cancel a negative one elsewhere.

max per-cell | sum_i omega_i |   is the reported quantity, not the global sum.
```

## 5. Rejection, never repair — §9

```text
A mass-unbalanced reaction is REFUSED at mechanism construction, with a diagnostic naming:
    the reaction             "bad-rxn"
    the mass defect          0.03125
    the offending net stoichiometry, species by species, with each molecular weight
    and the explicit statement that stoichiometry is NOT renormalised to repair it

Asserted by MassUnbalanceDiagnosticNamesTheReactionDefectAndStoichiometry, which checks the message
contains all four.
```

## 6. Closed-reactor conservation — §19, §20

```text
U = 0, J_i = 0, zero-gradient walls, so the species equation collapses to d(rho Y_i)/dt = omega_i.
With implicit Euler and a CONSTANT explicit source the discrete update is EXACT:

    (rho V/dt) Y^{n+1} = (rho Y^n V/dt) + omega V   =>   rho Y^{n+1} = rho Y^n + omega dt

so the analytical reference needs no integration scheme of its own and the test compares against
M_i nu_i q computed independently, not against the production source function.

MEASURED over 10 steps: every species matches rho Y_i(0) + omega_i t to 1e-12, and the INERT
species does not move at all. Over 20 steps the TOTAL species mass drift is within 8 eps N_cells
relative, and sum(Y) stays 1 WITHOUT any normalisation.
```

## 7. Elemental conservation — deferred, and why (§10)

```text
NO ELEMENTAL COMPOSITION EXISTS IN THE REPOSITORY. P18.3's SpeciesThermodynamics carries a molecular
weight and a cp(T) model; there is no atomic composition anywhere.

Section 10 anticipates exactly this and says not to invent an elemental subsystem. So, in the words
that section asks for:

    ELEMENTAL VALIDATION DEFERRED
    MASS CONSERVATION ENFORCED

P18.7 can own the broader elemental check. Mass conservation is the stronger of the two to have
first: an elementally-balanced reaction with wrong molecular weights would still violate mass, and
mass is what the transported quantity rho Y_i actually is.
```
