# P12-MESH-003 — acceptance gate (fixed BEFORE the final runs)

Written after the preliminary feasibility probe
([logs/02_preliminary_probe.log](logs/02_preliminary_probe.log)) and before any production test
of the final cases was run. The probe was used only to confirm feasibility, choose the
evaluation region and the Reynolds number, and discard one unusable quantity (a two-point
wall-shear reconstruction, not monotone, first order at best). No threshold below is a probe
value scaled to pass; each comes from a solver tolerance, a stated convergence theory
criterion, or an earlier P12-MESH gate, as given in its rationale. **Thresholds are not changed
after the final run.**

## Benchmark (quantitative case B)

`cases/curved_channel_multiblock` — a genuinely non-rectangular, non-convex domain: a 270°
curved channel (annular bend, inner wall r1 = 1, outer wall r2 = 2, centre at the origin),
three conformal 90° structured blocks `bend_a`/`bend_b`/`bend_c` joined by two interfaces (at
90° and 180°), patches `inlet` (θ = 0), `outlet` (θ = 270°), `inner_wall`, `outer_wall`.
Uniform inflow U = 1, outlet p = 0, no-slip walls, ρ = 1, μ = 0.1 (Re = U (r2 − r1)/ν = 10),
SIMPLE, `linear_upwind`, Green–Gauss, `non_orthogonal_corrections` 1, relaxation 0.7/0.3,
tolerances 2e-5 / 5e-4 / 1e-6, BiCGSTAB momentum, CG pressure. Committed grid 12 × 30 per
block (1080 cells); study grids 8 × 20, 12 × 30, 18 × 45 per block (refinement ratio 1.5).

**Reference (exact, analytical).** Fully developed flow in the bend is an exact Navier–Stokes
solution: u_r = 0, u_θ(r) = A (r ln r + a r − a / r) with a = −(4/3) ln 2 (so u_θ(1) = u_θ(2) =
0), p = G θ + f(r), G = ∂p/∂θ = 2 μ A, ∂p/∂r = ρ u_θ² / r. The flow rate Q = ∫ u_θ dr = U (r2 −
r1) = 1 fixes A = Q / (2 ln 2 − 3/4 + a (3/2 − ln 2)) ≈ −9.1411, G ≈ −1.828221.

**Evaluation region.** The middle block `bend_b` (90° ≤ θ ≤ 180°): at least 90° of arc
(≥ 1.57 channel widths at the inner wall) from both the inlet and the outlet. In the probe the
u_θ error in 15° bands was θ-independent across this block (to 2–3 significant digits, on all
three grids, at Re = 5 and 10) and |u_r| ≤ 3e-4 there — the flow is developed to well below the
discretisation error, so the exact developed solution is the reference.

**Quantities.**
- `velocity_l2_error`: volume-weighted RMS over `bend_b` of |u − u_exact| (vector, u_exact =
  u_θ(r) e_θ at the cell centroid).
- `pressure_gradient` G: for every ring of `bend_b` cells (equal centroid radius), the
  volume-weighted least-squares slope of p against θ; the mean over rings.
- `radial_pressure_rise`: for every θ-row of `bend_b`, p(outermost cell) − p(innermost cell),
  averaged over rows; exact ρ ∫ u_θ² / r dr between the same two centroid radii (quadrature).
  This quantity exists only through the convective (centrifugal) terms on the curved mesh.
  Because those two radii move with the grid (r1 + h/2, r2 − h/2), its exact value is
  grid-dependent; the grid-convergence analysis (G4 c) therefore uses the ratio
  `radial_pressure_rise / exact` (exact value 1), and G4 (a) its error against the exact value
  at the same radii. (Clarification written before the final run, while implementing the test.)

## Gate criteria — ALL must hold

**G1 — production path and validity.** The committed case is read by `CaseReader` (mesh type
`multiblock`, geometry type `mesh_defined`) and built by `CaseBuilder` through
`MeshGeometry::createMultiBlock2D`; `MeshQuality` valid, exactly one connected region, 1080
cells, patches exactly {inlet 12, outlet 12, inner_wall 90, outer_wall 90 faces}, every
interface face internal (owner in the `first` block, neighbour in the `second`); run by
`ProjectRunner`: status Converged, every field finite.

**G2 — conservation (every grid).** Global mass imbalance ≤ 1e-6; the mass flow through every
one of the 3 nθ + 1 radial face lines (inlet, lines inside the blocks, both interfaces, outlet)
equals Q = 1 within 1e-6; the net mass flow out of each block ≤ 1e-6; the interface flow seen
from the `first` block and from the `second` block are the same stored face fluxes (their sum
of opposite-signed contributions is exactly 0).
*Rationale:* 1e-6 is the case's continuity tolerance and the conservation bound of the
P12-MESH-001 and P12-MESH-002 gates.

**G3 — interface transparency.** The same geometry as ONE block (identical vertices, no
interfaces, the same patches) must give the same solution: max |Δu| ≤ 1e-6 U and max |Δp| ≤
1e-6 × |G| 3π/2 (the total azimuthal pressure drop).
*Rationale:* both meshes have identical cells and face geometry and differ only in face
enumeration and in the interface faces being built from two blocks, so the discrete systems are
identical up to floating-point summation order; any difference above round-off amplified by the
iterative solvers (≤ 1e-6, two orders below the SIMPLE velocity tolerance 2e-5 and four below
the discretisation error) would be an interface defect.

**G4 — accuracy against the exact solution.**
- (a) `velocity_l2_error`, |G − G_exact| and |radial_pressure_rise − exact| decrease
  monotonically over the three grids.
- (b) The observed order of `velocity_l2_error` and of the G error is ≥ 1.5 on both grid pairs.
  *Rationale:* formal order 2; ≥ 1.5 is the P12-MESH-001 grid-study criterion.
- (c) The P12-NUM-005 fine-grid uncertainty brackets the exact value for G and for the radial
  pressure rise: |φ_fine − φ_exact| ≤ GCI21 |φ_fine|.
  *Rationale:* the GCI is the stated uncertainty of a converged grid study; an interface or
  curvature defect that converges to a wrong answer fails it.
- (d) On the committed grid, max |u_r| over `bend_b` ≤ `velocity_l2_error` of the same run.
  *Rationale:* the exact u_r is 0; the spurious radial velocity must be at or below the
  discretisation-error level.

**G5 — scalar interface conservation** (`cases/annular_sector_conduction_multiblock`: the same
three-block 270° sector, fluid at rest, T = 1 on the θ = 0 end, T = 0 on the θ = 270° end,
adiabatic arcs, k = 1; exact T = 1 − θ / (3π/2), heat flow through every radial section Q_T =
k ln 2 / (3π/2); `non_orthogonal_corrections` 0 so the discrete face flux is exactly the
two-point coefficient × ΔT). Grids 8 × 20, 12 × 30, 18 × 45 per block:
- thermal solve Converged on every grid;
- the discrete heat flows through the hot end, both interfaces and the cold end agree within
  1e-6 Q_T (*rationale:* the thermal solver's 1e-8 outer / 1e-8 relative linear tolerances
  leave per-cell imbalances orders below 1e-6 of the throughput; a lost or duplicated interface
  face would change an interface flow by a whole face flux, ~1/nr of Q_T);
- the temperature L2 error decreases monotonically with observed order ≥ 1.5 on both pairs
  (same rationale as G4 b).

The item's other final-gate conditions (invalid-geometry and disconnected-domain rejection,
VTK export, backward compatibility, focused tests and full regression) are verified by their
own tests and logs and reported in summary.md; they are not relaxed by this gate.
