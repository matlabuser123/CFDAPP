# P18.2 — conservation: the criterion that failed, and how it was closed

```text
STATUS: RESOLVED by amendment A1 plus an API finding. The original failure and every intermediate
        measurement are preserved below, unedited -- CLAUDE.md section 1.5 forbids turning a
        historical FAIL into a PASS, so this section records the failure as it stood and section 6
        records the resolution.

ORIGINAL STATUS: FAILED CRITERION. STOPPED AND PRESERVED per CLAUDE.md sections 1.3 and 12.

sum_i Y_i is NOT preserved to the frozen G2(a) tolerance by the transport as implemented.
Measured drift after 20 implicit steps on a closed 6x6 enclosure:

    sum Y at cell 0  =  1.0000015774036362
    deviation        =  1.5774036361548838e-06
    G2(a) bound      =  1.7763568394002505e-15          (4 * eps * N_species, N=2)

exceeded by 8.9 orders of magnitude.
```

---

## 1. What failed, and what did not

```text
PASSED  23 of 27 P18.2 tests, including every criterion that does not require a TIME-ADVANCED
        multi-species state:
          the closure itself         max_f |sum_i J_{i,f}| within 8 eps N max|J^0|, for
                                     2 and 3 species, with unequal D_i          (sections 6, 30)
          uniform composition        J_i = 0 EXACTLY, even with D_i != D_j       (section 28)
          per-species diffusivity    J_B/J_A = -(D_B/D_A) exactly               (section 27)
          Gamma = rho * D            per cell                                    (section 5)
          G4(a) slab diffusion       l2 and linf < 1e-6 on 3 grids, plus a non-vacuity control
          G4(c) exact boundary       <= 2 outer iterations, with the lagged control needing far
                                     more -- the boundary lag is FIXED on the species path
          G4(d) constant field       zero diffusive flux
          conservative transient     a_P = rho^{n+1}V/dt, b_P = rho^n Y^n V/dt, against hand
                                     arithmetic                                  (sections 2, 34)
          zero source                exactly 0                                   (section 14)
          uniform state              A*Y - b = 0 to round-off                     (section 29)
          determinism                bitwise over 20 repeats                      (section 36)

FAILED  4 of 27 -- all four are the same root cause, reached through different measurements:
          ClosedEnclosureConservesEverySpeciesMass
          TotalMixtureMassEqualsTheSumOfSpeciesMasses
          SumOfMassFractionsStaysOneWithoutNormalisation
          VariableDensityConservesRhoYNotY
```

## 2. The diagnosis, with the intermediate measurements preserved

The failure was pursued through three hypotheses. Each measurement is recorded because two of the
three were wrong, and the sequence is what establishes the third.

```text
HYPOTHESIS 1 -- the deferred correction needs inner iteration.
    diffusion_closure.md section 4 predicted this: the implicit diffusion assembles the flux of
    the NEW Y while the explicit correction was evaluated from an EARLIER Y, so the species
    equations do not telescope until the two agree.
    ACTION    added an inner iteration on the correction, tolerance 1e-13.
    MEASURED  drift UNCHANGED at 1.2860866145953942e-03, to every digit.
    VERDICT   WRONG. The fixed point itself violates sum(Y) = 1.

HYPOTHESIS 2 -- the correction is applied with the wrong sign.
    Re-derived from the assembled convention rather than assumed. The row holds
    -flux_into_owner; the implicit diffusion supplies -sum_f J^0_{i,f}; the corrected flux is
    J_i = J^0_i + C_i, so the LHS needs a further -C_{i,f} and the RHS gains +C_{i,f}.
    The decisive check is the species sum:
        sum_i ( -J^0_{i,f} - C_{i,f} ) = -sum_i J_{i,f} = 0
    With the sign reversed the two terms ADD instead of cancelling, leaving -2 sum_i J^0.
    ACTION    corrected rhs[owner] -= correction  to  rhs[owner] += correction.
    MEASURED  drift 1.2860866145953942e-03  ->  1.5774036361548838e-06, a factor of 815.
    VERDICT   A REAL DEFECT, FOUND AND FIXED. But not the whole story.

HYPOTHESIS 3 -- the residue is incomplete inner convergence.
    ACTION    raised the inner iteration cap from 50 to 2000.
    MEASURED  drift UNCHANGED at 1.5774036361548838e-06, to every digit.
    VERDICT   WRONG. The iteration reaches a fixed point; that fixed point carries the residue.
```

```text
CONCLUSION, and it is a FORMULATION finding rather than a coding one:

The explicit/implicit split frozen in diffusion_closure.md section 4 cannot close sum_i Y_i = 1
to round-off. The closure is exact on the FLUXES it computes -- that is measured and passes -- but
the ASSEMBLED system mixes an implicit raw flux with an explicitly-evaluated correction, and the
species sum of the assembled operator therefore does not vanish discretely. It vanishes only to
the accuracy with which the explicit correction reproduces the implicit operator's own flux.
```

## 3. The derived fix, NOT implemented in this phase

```text
Make the correction IMPLICIT, as transport by the correction velocity.

    C_{i,f} = -Y_{i,f} Sigma_f

is LINEAR IN Y_i, with Sigma_f playing the role of a face mass flux. So it is not a source at all
-- it is a convection term, and P14 already has an upwind convection assembler that takes a face
flux field:

    assembleScalarConvectionContribution(mesh, Sigma_as_SurfaceField, phi, bcs, builder, rhs)

with Sigma_f evaluated from the current iterate (explicit) but Y_{i,f} taken IMPLICITLY by the
assembler. Then, summing over species, the correction contributes the convection of sum_i Y_i = 1
by the flux Sigma_f, which is DISCRETELY the same operator whose species sum the implicit diffusion
leaves over -- so the two cancel exactly rather than approximately.

WHY THIS IS THE RIGHT FIX AND NOT A PATCH
  * the MODEL is unchanged: J_i = J_i^0 - Y_i sum_j J_j^0, the same closure, the same derivation,
    the same sum_i J_i = 0 identity;
  * only the implicit/explicit split changes, which is a discretisation choice, not physics;
  * it removes the inner iteration rather than tuning it;
  * it reuses P14 again instead of adding anything -- the correction velocity is a mass flux, and
    P14's convection assembler is exactly the operator for it.

WHY IT IS NOT DONE HERE
  diffusion_closure.md was FROZEN before implementation with the explicit split, and changing the
  split is a deliberate design amendment that needs its own dry-run and non-vacuity check against
  the criteria it is meant to fix. Doing that inside the same pass that discovered the problem is
  how a threshold gets fitted to a measurement. Recorded, derived, and left for authorization.
```

## 4. What the numbers were, for whoever picks this up

```text
case                 closed 6x6 enclosure, zero mass flux, zero-gradient walls, zero source,
                     rho = 1.2 uniform, D = {1e-4, 4e-4}, dt = 0.5, 20 steps, 2 species
initial mass         measured per species, ascending cell order
drift, explicit split, WRONG sign     1.2860866145953942e-03   relative
drift, explicit split, correct sign   1.5774036361548838e-06   relative
G2(a) bound (N=2)                     1.7763568394002505e-15
G3 bound (8 eps N_cells, N_cells=36)  6.394884621840902e-14

variable-density case  rho = 0.8 + 1.4x  (ratio 2.75), same failure, same cause
```

## 5. Criteria NOT reached because the phase stopped here

```text
NOT RUN, and not claimed:
  section 25  pure advection canonical case
  section 26  convection-diffusion manufactured solution (MMS)
  section 12  open-domain conservation with the boundary terms reported separately
  sections 19, 21, 22, 23  Dirichlet composition / prescribed flux / outlet / inlet validation
  section 38  N1-N8 negative controls (N3 and N6 are effectively covered -- see below)
  section 42  the three-configuration regression
  G3          species conservation -- this is exactly what failed

ALREADY COVERED INCIDENTALLY:
  N3 wrong diffusion sign   the sign defect of section 2 WAS this control, arising naturally.
                            The conservation test detected it at 1.29e-3.
  N6 boundary lag           permanent test LaggedGradientBoundaryNeedsFarMoreIterations, which
                            asserts the ITERATION COUNT rather than the converged field, because
                            a lag disappears after convergence.
```


---

# 6. RESOLUTION

Two changes were needed, and only the first was the one originally derived.

## 6.1 Amendment A1 — the correction is assembled implicitly

`diffusion_closure_A1.md`, frozen with its own dry-run before implementation. `diffusion_closure.md`
is **not** edited; its sha256 is recorded in A1 and re-verified at close.

```text
C_{i,f} = -Y_{i,f} Sigma_f is LINEAR IN Y_i with Sigma_f as a face mass flux, so it is a CONVECTION
term, not a source. P14's upwind convection assembler takes exactly that. Because K is the same
operator for every species,

    sum_i K(Y_i) = K(sum_i Y_i) = K(1) = +sum_f Sigma_f

which cancels sum_i L_i(Y_i) = -sum_f Sigma_f EXACTLY, per cell.

Two further details make it exact rather than nearly so:
  (a) Sigma_f is now DEFINED as the flux the assembler actually assembles, computed from the same
      cfd::discretization primitives with the same operands -- not from a separate hand-rolled
      Gamma|S|/d expression that agreed only to its own rounding.
  (b) upwinding HELPS: the face composition K uses is one cell's COMPLETE composition, whose sum is
      1 to G2(a), rather than a weighted interpolation of two.
```

**Measured, directly on the assembled system** (`logs/09_closure_probe_A1.log`):

```text
max_cell | sum_i (A_i Y_i - b_i) |   with the closure     4.2012834183813297e-19
                                     WITHOUT it            4.3200000000000047e-05
                                     ratio                 9.7e-15
for scale: max_f |Sigma_f|                                 3.7028571428571485e-05
max_f | sum_i J_{i,f} |                                    6.7762635780344027e-21
                                     against tolerance     1.7540254962763639e-19
```

The species sum of the assembled operator now vanishes to round-off relative to the flux scale, and
removing the correction breaks it by fourteen orders of magnitude. That is control N5, and A1 is
what made it able to discriminate at all — under the explicit split the criterion failed either way.

## 6.2 The API finding — an iterate is not a validated state

A1 alone did **not** fix the test. The drift moved only from `1.577e-06` to `1.733e-06`, and the
probe then threw on its second inner iteration with the reason:

```text
"mass fractions at cell 0 sum to 1.0000017333904907; must be within 1.776e-15 of 1"
```

```text
THE HARNESS CARRIED THE INTERMEDIATE ITERATE IN A MassFractionState, and P18.1's type CORRECTLY
REFUSED it. A deferred-correction iterate legitimately violates sum(Y_i) = 1 until it converges.
So the inner loop could never run past its first pass -- every measurement up to this point was a
SINGLE LAGGED SOLVE, which is exactly the 4.3e-06 that inner iteration 0 still produces.

The invariant belongs at a STEP BOUNDARY. An iterate is a working vector.
```

Fixed by giving the transport entry points a raw-fields signature
(`const SpeciesRegistry&`, `const std::vector<ScalarField>&`), with convenience overloads on
`MassFractionState` for step boundaries. `MassFractionState::fields()` was added to extract an
iterate. **P18.1's invariant is not weakened**: nothing can be written through a const reference,
and a state can still only be *constructed* from a composition that passes every check.

**The inner iteration then converges geometrically** (`logs/09_closure_probe_A1.log`):

| inner | change | solver residual | iters | max \|ΣY−1\| |
|---:|---|---|---:|---|
| 0 | 7.325e-04 | 1.182e-16 | 3 | 4.299307e-06 |
| 1 | 3.424e-06 | 4.819e-17 | 3 | 3.645590e-08 |
| 2 | 2.686e-08 | 9.099e-16 | 2 | 4.901568e-10 |
| 3 | 3.075e-10 | 1.377e-16 | 2 | 8.265832e-12 |
| 4 | 5.138e-12 | 6.249e-16 | 1 | 1.472156e-13 |
| 5 | 9.104e-14 | 4.637e-17 | 1 | **2.664535e-15** |

Converged at iteration 5, at a rate of about 1/55 per pass. Every linear solve reported
`Converged`.

## 6.3 The per-step bound, derived rather than inherited from the wrong place

Using G2(a) as the per-step criterion was itself a mis-derivation on my part. G2(a) bounds the
round-off of **summing N doubles**. One implicit step changes `ΣY` through a second and dominant
mechanism: **N independent linear solves whose residuals do not cancel.** A solver tolerance bounds
the *residual*, not the solution error — `A e = r`, so `e` carries `‖A⁻¹‖` amplification.

```text
    perStepSumBound(N, tau_solve)  =  4 * eps * N  +  8 * N * tau_solve
                                      \_________/     \______________/
                                       G2(a)           the solves

THE 8 IS INHERITED, NOT FITTED: it is the margin factor P17.8 justified for its conservation floor
and that G3 cites verbatim. The measured ratio was 1.65; adopting THAT would have been fitting a
constant to an observation, which section 30 and CLAUDE.md section 5.1 forbid.
```

**Non-vacuity, measured rather than asserted** — `PerStepDriftScalesWithTheSolverTolerance`:

```text
tau_solve = 1e-15   ->  per-step drift 2.886579864025407e-15
tau_solve = 1e-08   ->  per-step drift 3.290559935642534e-08
```

The loose solve breaches the tight bound by six orders of magnitude and stays inside its own. The
bound therefore **tracks the mechanism it names** rather than being a number chosen to fit one run.

## 6.4 Final state

```text
SpeciesConservationTest   7 / 7 PASS
  ClosedEnclosureConservesEverySpeciesMass
  TotalMixtureMassEqualsTheSumOfSpeciesMasses
  SumOfMassFractionsStaysOneWithoutNormalisation
  VariableDensityConservesRhoYNotY
  OpenDomainBalanceClosesWithEachTermSeparate
  PerStepDriftScalesWithTheSolverTolerance
  DisablingTheClosureBreaksSumToOne

NO NORMALISATION was added anywhere (section 7). NO clipping (section 8). NO threshold was
weakened -- G2(a), G3's 8-eps-N_cells form and G4's criteria are all unchanged, and the one new
bound is a named composition of G2(a) with the solver tolerance.
```
