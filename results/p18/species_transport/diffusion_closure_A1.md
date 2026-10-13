# P18.2 — diffusion closure, AMENDMENT A1

```text
AMENDS      diffusion_closure.md section 4 (the implicit/explicit split) ONLY.
            The original is NOT edited. Its sha256 is recorded here and re-verified at close.
            diffusion_closure.md  sha256 3ee2221d39e7805e...  (frozen before implementation)

WHAT CHANGES        the discretisation of the correction term: explicit -> implicit.
WHAT DOES NOT       the MODEL. J_i = J_i^0 - Y_i sum_j J_j^0 is unchanged, its derivation is
                    unchanged, and the sum_i J_i = 0 identity is unchanged.
THRESHOLDS CHANGED  NONE. Not one. The criterion this is meant to fix -- the frozen G2(a) bound
                    4 eps N_species -- is the SAME bound the explicit split failed against.
AUTHORIZED          by the user, after the failure was preserved and reported.
```

---

## 1. Why the original split failed (from `conservation.md`, not re-derived here)

```text
The implicit diffusion assembles the flux of the NEW Y. The explicit correction was evaluated from
an EARLIER Y. Summing the N species equations therefore leaves a residue equal to the difference
between the two, and it does not vanish at the fixed point:

    measured drift  1.5774036361548838e-06   against the G2(a) bound 1.7763568394002505e-15
    with 50 inner iterations and with 2000: IDENTICAL to every digit.
```

## 2. The amendment

```text
C_{i,f} = -Y_{i,f} Sigma_f    IS LINEAR IN Y_i, with Sigma_f in the role of a face mass flux.

It is therefore NOT a source term. It is a CONVECTION term: transport of species i by the mixture
correction velocity. P14 already has the operator for it --
assembleScalarConvectionContribution -- which takes an owner-oriented face flux and the
transported quantity at coefficient 1.

SIGN. The row holds -flux_into_owner. C_{i,f} is the correction flux INTO the owner, so as an
OUT-of-owner flux it is +Y_{i,f} Sigma_f. Passing Sigma_f as P14's massFlux therefore assembles
exactly the correction, with no sign adjustment and no separate loop.
```

## 3. Why the species sum now cancels DISCRETELY — the proof that matters

```text
Let L_i be the implicit diffusion operator with Gamma_i, and K the convection operator with face
flux Sigma_f. K is the SAME operator for every species, because Sigma_f does not depend on i.

    sum_i [ L_i(Y_i) ]  =  -sum_f Sigma_f                  by the DEFINITION of Sigma_f below
    sum_i [ K(Y_i)   ]  =  K( sum_i Y_i )  =  K(1)  =  +sum_f Sigma_f
    ------------------------------------------------------------------
    sum_i [ L_i(Y_i) + K(Y_i) ]  =  0                      identically, per cell

Two things make this EXACT rather than approximate, and both are new in A1:

(a) Sigma_f IS DEFINED AS THE ASSEMBLED FLUX, not as a separately-computed estimate of it:

        Sigma_f  :=  sum_i ( the diffusive flux into the owner that L_i ACTUALLY ASSEMBLES at f )

    computed from the same cfd::discretization primitives, with the same coefficients, in the same
    operand order. The original computed it from a hand-rolled Gamma |S| / d expression, so the two
    agreed only to their own rounding -- which is the residue that survived.

(b) UPWINDING HELPS HERE, it does not hurt. K takes the face value UPWIND, so the composition it
    uses is one cell's COMPLETE composition, whose sum is 1 to G2(a) exactly. An interpolated face
    composition sums to 1 only as a weighted combination. Upwind is the stronger choice for the
    closure, not a compromise.
```

## 4. Dry-run of the amended criteria against the UNCHANGED baseline (§5.2)

Run before implementing, so the criteria are known to be able to fail.

```text
CRITERION                                      BASELINE (explicit split)     CAN IT FAIL?
sum_i Y_i = 1 after 20 steps, G2(a) bound      FAILS at 1.577e-06            YES -- it does fail
per-species mass conservation, G3 bound        FAILS                          YES -- it does fail
sum_i M_i = mixture mass                       FAILS                          YES -- it does fail
variable-density rho*Y conservation            FAILS                          YES -- it does fail
max_f |sum_i J_{i,f}|, closure residual        PASSES                         see NV1 below
uniform composition -> J = 0 exactly           PASSES                         see NV2 below
G4(a) slab profile, G4(c) <= 2 iterations      PASS                           unaffected by A1
```

```text
THE FOUR FAILING CRITERIA ARE THE DRY-RUN. They are measured, reproducible, and they fail on the
unchanged tree -- which is exactly the non-vacuity §5.2 demands: a criterion that the baseline
fails and the corrected implementation must pass. Nothing here is a criterion the baseline already
passed vacuously.
```

## 5. Non-vacuity controls for A1 specifically

```text
NV1  the raw fluxes must be genuinely UNCLOSED before correction, or the correction is credited
     for nothing. Asserted in SumOfCorrectedFluxesVanishesWithUnequalDiffusivities
     (EXPECT_GT(maxRawSum, 0.0)). Retained unchanged.
NV2  uniform composition must give EXACTLY zero, which is a product with an exactly zero factor
     and so cannot be produced by a broken correction. Retained unchanged.
NV3  NEW: disabling the correction (closureCorrection = false) must make sum(Y) = 1 FAIL. This is
     control N5 of the authorization, and A1 makes it meaningful -- under the explicit split the
     criterion failed either way, so N5 could not have discriminated.
NV4  NEW: Sigma_f must be non-zero somewhere in the test case, or K is the zero operator and the
     cancellation is vacuous. Asserted directly.
```

## 6. What could go wrong with A1, stated before measuring

```text
RISK 1  upwinding the correction makes it first-order in the correction velocity. That is an
        ACCURACY property, not a conservation one -- the cancellation in section 3 holds for any
        consistent face value. If the slab profile (G4(a)) degrades, that is a real finding.
RISK 2  Sigma_f is evaluated from the current iterate, so the correction's COEFFICIENT is still
        lagged even though its Y_i is implicit. That affects the convergence rate, not the
        conservation, and section 3's proof does not depend on Sigma_f being converged.
RISK 3  at a Dirichlet face the correction transports the prescribed boundary composition. That
        composition must sum to 1 or the closure is inexact there -- which is why section 19 of the
        authorization requires validating it, and why the slab case sets the exact complement.
```

## 7. Verification at close

```text
diffusion_closure.md unchanged:  sha256 to be re-verified and recorded in summary.md
```
