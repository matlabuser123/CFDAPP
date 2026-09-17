# P12-DIFF-001 — acceptance gate: NOT FROZEN

This gate was **never frozen**, because the mandatory pre-freeze dry-run (workflow step 2) showed
that the authorized formulation cannot satisfy the criteria it would have contained. Freezing a gate
for a correction already proven to be ineffective would have produced a meaningless FAILED verdict,
which is the exact failure mode the dry-run requirement exists to prevent (see
`results/p12-grad-002/a1/dryrun.md` and the memory rule it produced).

## Criteria that were drafted

Had it been frozen, the gate's decisive criterion — the one the authorization named in workflow
step 5 and in the stop rules — would have been:

> **D1. The Dirichlet wall diffusive flux converges at second order.** With the exact analytic field
> imposed, the total wall force against its analytic value must show an observed order ≥ 1.8 on the
> 64×8 / 96×12 / 144×18 family of the distorted-Poiseuille mesh, against the measured pre-fix order
> of 1.00.

Supporting criteria would have covered constant/linear/quadratic exactness, Cartesian and translated
Cartesian invariance, the distortion sweep, thermal and species Dirichlet diffusion, the two
production cases rerun unchanged, and GRAD-002 remaining intact.

## Why it was not frozen

The dry-run ([logs/01](logs/01_wallflux_dryrun.log), [logs/02](logs/02_identity.log),
[logs/03](logs/03_geometry.log)) established three facts, each measured against the unchanged
library:

1. **The production coefficient is already the wall-normal-distance coefficient**, to machine
   precision. `boundaryFaceDiffusionTerms` computes `Γ|S_orth|/distance` with `distance = |d|`, and
   because `S_orth = (S·S)/(d·S) d` and `d·S = |S| d_n`, that equals **Γ|S|/d_n** identically.
   Measured agreement: **2.8e-16, 4.0e-16, 4.5e-16** relative at distortion 0, 0.5, 1.0. There is no
   straight-line-distance defect to correct.
2. **The remaining first-order error is the standard half-cell one-sided difference, not a
   non-orthogonality defect.** On a *perfectly orthogonal* Cartesian mesh the wall-force error is
   6.2500e-02 / 4.1667e-02 / 2.7778e-02 — equal to **0.5 h** to five significant figures
   (0.5/8, 0.5/12, 0.5/18), with observed order **1.000**. At 48° non-orthogonality it is
   6.2574e-02 / 4.1691e-02 / 2.7785e-02: the same numbers to 0.1 %. Analytically, for φ = 6y(1−y)
   the two-point wall difference gives (0 − φ_P)/(h/2) = −6 + 3h against an exact −6, i.e. a relative
   error of exactly h/2.
3. **The proposed correction cannot change that order.** Implemented in the probe in both signs
   alongside a normal-distance form, it moves the wall force by at most 0.05 % of the wall jump and
   leaves the observed order at 1.00: production 1.002 / 1.001, normal-foot 1.002 / 1.001,
   plus-tangent 1.000 / 1.000. The reason is not that the geometry is benign — max |d_t|/d_n reaches
   **0.724** on this mesh — but that the wall-cell gradient of a fully developed profile is nearly
   wall-normal, so ∇φ_P·d_t is ≤ 4.8e-04 of the wall jump, ~58× smaller than the 2.78 % error.

Criterion D1 is therefore unreachable by the authorized formulation, and stop rule 2 ("the proposed
formulation does not restore the expected boundary-flux convergence") applies.

## Sign convention, derived as instructed

For completeness, the sign the authorization asked to be verified. The production term is the
diffusion contribution to the owner's balance, `Γ∇φ·S_out`, approximated as
`coefficient (φ_b − φ_P) + Γ S_nonorth·∇φ_P`. The normal foot is `x_N = x_P + d_n n̂ = x_f − d_t`,
so transferring a *known* boundary value from the face centroid to the normal foot is

```text
phi_at_normal_foot = phi_b - grad(phi)_P . d_t        (minus, not plus)
```

The `+ d_t` form in the authorization is the correct sign for the *opposite* transfer — from the
normal foot out to the face centroid — which is what P12-MESH-001's oblique **Neumann** treatment
does (`faceValues[f] = boundaryValue(φ_P, d_n) + ∇φ_P·d_t`), since there the value is constructed at
the foot and needed at the centroid. Both signs were measured; neither changes the order.
