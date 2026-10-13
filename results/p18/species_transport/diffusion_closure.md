# P18.2 — the multi-species diffusive closure (§5, §6, §30)

```text
FROZEN BEFORE IMPLEMENTATION AND BEFORE ANY MEASUREMENT.
The derivation below is algebra, not a fitted model. No constant in it was chosen.
```

---

## 1. Why independent Fick fluxes are not enough

The raw Fickian flux of species `i` is

```text
    J_i^0 = -rho D_i grad(Y_i)                                    [kg / (m^2 s)]
```

Summing over species:

```text
    sum_i J_i^0 = -rho sum_i D_i grad(Y_i)
```

P18.1 established `sum_i Y_i = 1` everywhere, and therefore

```text
    sum_i grad(Y_i) = grad( sum_i Y_i ) = grad(1) = 0
```

So:

```text
    ALL D_i EQUAL (D_i = D):   sum_i J_i^0 = -rho D sum_i grad(Y_i) = 0     ALREADY CLOSED
    D_i DIFFERENT:             sum_i J_i^0 = -rho sum_i D_i grad(Y_i) != 0  NOT CLOSED
```

**A non-zero `sum_i J_i` is net mixture mass created by diffusion.** It would break
`sum_i rho Y_i = rho` (§13) and drive `sum_i Y_i` off 1 (§31) — and because §7 forbids repairing
that by renormalisation, an unclosed flux would make the phase fail rather than merely be
inaccurate. Independent Fick fluxes with species-dependent `D_i` are therefore **not** an
acceptable stopping point, exactly as §5 warns.

## 2. The correction, and its proof

Adopt the mixture correction-velocity form (§6):

```text
    J_i = J_i^0  -  Y_i sum_j J_j^0
```

**Proof that it closes, exactly:**

```text
    sum_i J_i  =  sum_i J_i^0  -  ( sum_i Y_i ) ( sum_j J_j^0 )
               =  sum_i J_i^0  -  1 * sum_j J_j^0            using sum_i Y_i = 1
               =  0                                           identically
```

This is an **algebraic identity**, not a limit. It holds for any `D_i`, any gradients and any
mixture, and it is conditional on exactly one thing: `sum_i Y_i = 1`. That is the invariant P18.1
enforces, which is why the two phases compose.

**Interpretation, so the correction is not a fudge.** `-Y_i sum_j J_j^0` is species `i` being
carried by a correction velocity `V_c = (1/rho) sum_j J_j^0` shared by the whole mixture. It is the
standard remedy for the fact that `N` independent Fick laws over-determine a mixture that has only
`N-1` independent gradients. It transports no net mass (`sum_i Y_i V_c rho = rho V_c` cancels
against `sum_j J_j^0` by construction) and it vanishes identically when all `D_i` are equal.

## 3. Where it is evaluated, and why that makes the closure exact per face

The correction is applied **at faces, to fluxes**, not to cell values:

```text
    for each face f:
        J^0_{i,f}   = -Gamma_{i,f} ( grad Y_i . n )_f  |S_f|       Gamma_{i,f} = (rho D_i)_f
        Sigma_f     = sum_j J^0_{j,f}                              one scalar per face
        J_{i,f}     = J^0_{i,f} - Y_{i,f} Sigma_f
```

```text
    sum_i J_{i,f} = Sigma_f ( 1 - sum_i Y_{i,f} )
```

so the closure at face `f` is exact **to the extent that the face composition sums to 1**. And it
does: `Y_{i,f}` is a linear interpolation of the two adjacent cell compositions, each of which sums
to 1 by P18.1's invariant, and a linear combination of two vectors that each sum to 1 with weights
summing to 1 also sums to 1 — to round-off only.

```text
CONSEQUENCE, and it is the reason for this design rather than a cell-centred one:

    sum_i J_{i,f} = 0 TO ROUND-OFF AT EVERY FACE, AT EVERY ITERATION, BY CONSTRUCTION.

It does NOT depend on the Picard loop converging, on the mesh, on the D_i, or on the solution being
smooth. A cell-centred correction would only close in the limit; this one closes per face.
```

## 4. Implicit / explicit split

```text
    J_{i,f}  =   J^0_{i,f}                  <-- IMPLICIT. This is exactly what P14's diffusion
                                                assembler produces, with Gamma_i = rho D_i.
                                                Reused unchanged; no new assembler (§1).
             -   Y_{i,f} Sigma_f            <-- EXPLICIT, deferred correction. Cross-species by
                                                nature, so it cannot be implicit in a per-scalar
                                                system without coupling all N systems.
```

The explicit part is applied conservatively, owner and neighbour from the same value:

```text
    rhs[owner]     -= C_{i,f}
    rhs[neighbour] += C_{i,f}          C_{i,f} = -Y_{i,f} Sigma_f
```

which cancels to **exactly zero** for any `C`, in any floating-point arithmetic, because the two
contributions are the same value negated. That is §33's internal-face cancellation, structurally
rather than by tolerance.

```text
WHAT THE EXPLICIT TREATMENT COSTS, stated honestly: the corrected flux uses the PREVIOUS iterate's
Y and gradients. So the CONVERGED solution satisfies the corrected equation, and sum_i J = 0 holds
per face at every iteration -- but the correction's contribution to the matrix is lagged, so it
affects the convergence RATE, not the answer or the closure. This is a deliberate, documented
choice, unlike the boundary lag, which was a defect because an exact closed form existed five lines
away.
```

## 5. The degenerate cases, predicted before measuring

```text
ALL D_i EQUAL            Sigma_f = -rho D sum_i (grad Y_i . n) |S| = 0 to round-off, so the
                         correction is zero to round-off and the scheme reduces to plain Fick.
                         PREDICTION: the correction must be measurably ~0 here, NOT merely small.
UNIFORM COMPOSITION      grad Y_i = 0 for every i, so J^0_{i,f} = 0, Sigma_f = 0, J_{i,f} = 0
                         EXACTLY -- even with D_i != D_j. This is §28's criterion and it must be
                         exactly zero, not approximately, because it is a product with an exactly
                         zero factor.
SINGLE SPECIES           Y_1 = 1 everywhere, so grad Y_1 = 0 and J = 0. A one-species mixture
                         cannot diffuse, which is physically right and falls out rather than being
                         special-cased.
TWO SPECIES              grad Y_2 = -grad Y_1 exactly, so
                             Sigma = -rho (D_1 - D_2) (grad Y_1 . n) |S|
                         which is non-zero whenever D_1 != D_2 -- so a binary mixture with unequal
                         diffusivities is the SIMPLEST case that exercises the correction, and is
                         what the N5 control attacks.
```

## 6. What this closure is NOT

```text
NOT Stefan-Maxwell        no binary diffusion coefficient matrix D_ij, no matrix inversion. A
                          mixture-averaged model with a correction velocity is a documented,
                          weaker closure and is labelled as such. P18.2 does not claim
                          multicomponent diffusion.
NOT thermal diffusion     no Soret or Dufour term.
NOT pressure diffusion    absent.
NOT a bounded scheme      the correction does not guarantee 0 <= Y_i <= 1. Bounds are CHECKED and
                          REPORTED after each update (§8, §35), never imposed. Inventing a limiter
                          is outside this authorization.
NOT D_i(T, Y)             D_i is given. A composition- or temperature-dependent model is P18.3's.
```

## 7. The criterion this document is judged by

```text
§30   max |sum_i J_{i,f}| over all faces, for representative non-uniform multi-species states,
      INCLUDING unequal D_i.

DERIVED THRESHOLD, before measurement: the sum of N face fluxes each of magnitude <= |Sigma_f|
accumulates O(N) eps of round-off relative to the largest term, so the defensible bound is

      max_f | sum_i J_{i,f} |  <=  8 * eps * N_species * max_f max_i |J^0_{i,f}|

i.e. a RELATIVE bound scaled by the largest raw flux present, with the same 8*eps*N form G3
inherits from P17.8's conservation floor. It is NOT an absolute number, because an absolute
tolerance on a dimensional flux is meaningless across meshes and diffusivities.

§28   uniform composition: EXACTLY 0, bitwise, because every J^0 has an exactly zero factor.
```
