# P12-MESH-002 — quantitative acceptance gate (fixed BEFORE the final comparison)

Recorded 2026-09-15, after the preliminary probes (`logs/02`, `logs/03`) and
before any final production comparison was run. Not to be changed afterwards.

## Why the wall-gradient case is the transpiration channel, not plain Poiseuille

Preliminary evidence (`logs/02`): for plain Poiseuille at equal cell count,
wall clustering *worsens* the solver's integral wall shear (dp/dx error
0.76% uniform → 0.83% / 1.06% / 1.45% at r = 1.1 / 1.2 / 1.3). This is a
property of the problem, not a defect. The exact discrete solution of the
cell-centred scheme on a y-graded mesh has face fluxes exact at the faces and
a profile error that accumulates to ~G Δ_max²/8 at the centre, because the
parabola's curvature is constant. So the error is set by the largest cells,
and a uniform grid is already optimal: plain Poiseuille has no thin wall
layer to resolve. It is reported, not gated.

The wall-gradient problem used instead is still a channel flow: the exact
Navier–Stokes solution for a channel with **uniform wall transpiration**
(injection V at the bottom wall, suction V at the top wall; v = V,
p = p(x)):

    u(y) = C [H (e^{λy} − 1)/(e^{λH} − 1) − y],  λ = V/ν,
    C = U / (1/λ − H/(e^{λH} − 1) − H/2),  dp/dx = ρ C V,
    τ_top = μ C (λH e^{λH}/(e^{λH} − 1) − 1).

At Re_w = VH/ν = 20 the suction wall carries a boundary layer of thickness
ν/V = 0.05 H and a wall shear about 7× that of Poiseuille. Uniform refinement
converges to it (dp/dx error 4.7%, 2.1%, 0.72%, 0.20% at ny = 16…128,
`logs/03`), so the case is set up correctly.

## Design fixed in advance

* Case: L = 4, H = 1, ρ = 1, μ = 0.1, U = 1 (left inlet), V = 2 (bottom and
  top inlet-type transpiration walls), zero-gradient outlet with p = 0;
  `linear_upwind`, pressure CG, the Poiseuille production tolerances.
* Comparison at **equal cell count 48 × 24** (not the 32 × 16 probe grid):
  uniform vs **y-graded, cluster "both", ratio 1.2** — the conventional
  channel-wall clustering with the common adjacent-cell growth limit 1.2,
  chosen without knowing which wall carries the boundary layer. Identical
  physics, solver settings, tolerances and scheme; only the mesh differs.
* Errors over the developed region 0.50 L ≤ x_c ≤ 0.85 L.

## Gate (all must hold)

1. **Integral wall shear** (force balance ≡ dp/dx) relative error:
   graded ≤ **0.75 ×** uniform.
2. **Suction-wall shear** reconstructed from the near-wall solution
   (second-order, through the wall and the two top cells), mean relative
   error: graded ≤ **0.75 ×** uniform.
3. Global velocity L2 error: graded ≤ **1.0 ×** uniform (clustering must not
   buy wall accuracy at the cost of the field).
4. Both runs converged, all fields finite, global mass imbalance ≤ 1e-6,
   every face column carries U H to ≤ 1e-6 (the production conservation
   tolerance used by P12-MESH-001).

Rationale for 0.75: reducing the first-cell height (48 × 24 both/1.2 gives
a first cell of 0.5·0.2/(1.2¹² − 1) = 0.0126, centroid y1 = 0.0063, vs
0.0208 uniform — a factor of ~0.30) should cut first-order wall-gradient
errors roughly in proportion, before the interior non-uniformity costs
anything back. Requiring at least a 25%
reduction is a material improvement well inside that expectation and does
not depend on the preliminary ratios (0.54 / 0.55 at 32 × 16).

Cost is reported (iterations, wall time, error × cells, and the uniform
resolution needed to match the graded error) but is not part of the pass
condition.
