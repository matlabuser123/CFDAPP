# P12-MESH-005 — acceptance gate (fixed BEFORE implementation and before any verification run)

Written after the architecture audit (summary.md §4–5) and **before any MESH-005 production code
change or verification run**. Every expected order below is derived from the truncation error of
the scheme as implemented (the derivation is given with each item), not from a probe. Every
exactness tolerance is a round-off bound for the stated field magnitudes. **Nothing here is
changed after the verification runs.** Items marked *reported* are run and reported in full but
are not pass/fail criteria; that status is decided here, in advance.

Test names below are the planned gtest names. If a test is split or renamed during implementation,
the criterion does not change; summary.md maps each item to the test that checks it.

## A. Geometry and topology (exact / round-off), `Cartesian3DMeshTest.*`

Meshes: **M1** 1×1×1 unit cube; **M2** 2×1×1; **M3** 2×2×1; **M4** 2×2×2 (all on [0,1]³);
**M5** 3×4×5 cuboid Lx = 1.5, Ly = 0.7, Lz = 2.3, origin (−1.2, 0.4, 3.1) (translated, non-unit,
non-cubic cells); **M6** 8×8×8 unit cube. L = the largest domain extent.

| id | criterion |
| --- | --- |
| A1 counts | cells = nx·ny·nz; grid vertices = (nx+1)(ny+1)(nz+1); faces = (nx+1)·ny·nz + nx·(ny+1)·nz + nx·ny·(nz+1); interior faces = (nx−1)·ny·nz + nx·(ny−1)·nz + nx·ny·(nz−1); boundary faces = 2(ny·nz + nx·nz + nx·ny). Exact integer equality on M1–M6. The formulas are derived by counting planes of faces, independently of the builder (derivation in summary.md §10). |
| A2 volumes | every V_P finite, > 0, and \|V_P − dx·dy·dz\| ≤ 1e-14·dx·dy·dz; \|Σ V_P − Lx·Ly·Lz\| ≤ 1e-12·Lx·Ly·Lz. M1–M6. |
| A3 area vectors | \|Sf\| equals the face rectangle's area (dy·dz, dx·dz or dx·dy) within 1e-14 relative; Sf is parallel to its axis (the two other components exactly 0); boundary Sf points out of the domain ((x_f − x_P)·Sf > 0 and along the patch's outward axis); interior Sf points owner → neighbour ((x_N − x_P)·Sf > 0); per cell \|Σ_f ±Sf\| ≤ 1e-13·Σ_f\|Sf\| (owner +, neighbour −). M1–M6. |
| A4 centroids | cell centroid = the geometric centre of its box within 1e-14·L and strictly inside the box; face centroid = the centre of its rectangle within 1e-14·L and on the face plane (\|(x_f − corner)·n\| ≤ 1e-14·L); for every interior face x_N − x_P is parallel to Sf (cross product exactly 0) and x_f lies on the segment P–N (the face plane cuts it at t = 1/2 within 1e-14). M1–M6. |
| A5 connectivity | each cell lists exactly 6 distinct valid face ids in the canonical local order west, east, south, north, bottom, top (outward normal −x, +x, −y, +y, −z, +z from that cell); every face has a valid owner; each interior face has one neighbour ≠ owner; boundary faces have none; every face appears in exactly 1 (boundary) or 2 (interior) cell face lists, and those cells are exactly its owner/neighbour; no two faces share the same (owner, neighbour) pair; the owner of an interior face is the lower-index cell. M1–M6. |
| A6 determinism | building the same mesh twice gives identical cells, faces and patches (every value bitwise) and the same `computeMeshFingerprint`; documented face order: x-faces (k, j, i), then y-faces, then z-faces; cell id = (k·ny + j)·nx + i. |
| A7 patches | exactly the patches xmin, xmax, ymin, ymax, zmin, zmax (in that order); counts ny·nz, ny·nz, nx·nz, nx·nz, nx·ny, nx·ny; every boundary face in exactly one patch, no interior face in any patch; each patch's faces lie on its plane with Sf along the outward axis (−x̂ for xmin, …); each patch's total area equals the domain side area within 1e-13 relative. `MeshQuality::evaluate` reports the mesh valid with no warning, non-orthogonality 0, skewness 0, expansion ratio 1, aspect ratio max(dx,dy,dz)/min(dx,dy,dz) within 1e-13 relative. M1–M6. |
| A8 dimension | `Mesh::dimension()` is 3 for every 3D mesh and 2 for every existing 2D builder (Cartesian, graded, structured_quad, multi-block). |
| A9 invalid input | nx, ny or nz = 0, or a non-finite / non-positive extent, or a non-finite origin → `InvalidArgumentError`. |

## B. Fields, `Fields3DTest.*`

| id | criterion |
| --- | --- |
| B1 scalar | `ScalarField` sized to the cell count holds constant, x, y, z and x + 2y + 3z sampled at the centroids (the same ScalarField type as 2D — no 3D-specific field class); the volume-weighted mean of x, y, z and x + 2y + 3z over M5 equals the value at the domain centre within 1e-13 relative (midpoint rule, exact for linear fields). |
| B2 vector | `VectorField` holds three components (u, v, w): zero init, constant init, component access and assignment of all three, magnitude √(u²+v²+w²) (e.g. (2,3,6) → 7 exactly), size, copy and move (source w preserved; moved-to equal), the arithmetic operators (+, −, ×, ÷) act on w. A 2D-constructed value `Vector2{a, b}` has z = 0. |

## C. Operators, exactness (round-off), `Operators3DTest.*`

Per-face exact Dirichlet data (one singleton patch per boundary face, as every existing operator MMS
test). Fields are O(10) on O(1) domains.

| id | criterion |
| --- | --- |
| C1 interpolation | constant field: every face value exact (≤ 1e-12 abs); linear field 1 + x − 2y + 3z: every interior face value (x-, y- and z-directed faces, each counted and non-empty) within 1e-12 abs, boundary faces equal the prescribed value; a 3-component linear vector field interpolates exactly on interior faces (≤ 1e-12). M5 and M6. |
| C2 gradients | Green–Gauss and least-squares, every cell including boundary, edge and corner cells: constant → \|∇φ\| ≤ 1e-9; φ = x, y, z → (1,0,0), (0,1,0), (0,0,1) and φ = 2x − 3y + 4z → (2, −3, 4) within 1e-9 per component. On M1–M6 with Dirichlet data, and on M5/M6 with exact per-face `FixedGradient` (Neumann) data. `solveLeastSquaresGradient` with non-coplanar 3D displacements recovers a linear gradient exactly (≤ 1e-12) and reports coplanar (rank-2) 3D data as ill-conditioned. |
| C3 diffusion | explicit `diffusion(…, Γ = 1.7, …)` of the quadratic φ = x² + 2y² + 3z² + xy − yz + 2xz − x + 1 returns 1.7·∇²φ = 1.7·12 = 20.4 in every cell within 1e-8 abs, on meshes with ≥ 3 cells in each direction (a 6×5×4 mesh on M5's domain and M6). Zero-flux (FixedGradient 0) boundaries: Σ_P V_P·diffusion(φ)_P = 0 within 1e-10·Σ\|flux\| (global conservation). |
| C4 convection | mass flux ṁ_f = ρ u(x_f)·Sf of the divergence-free u = (1 + 0.5y, 1 + 0.5z, 1 + 0.5x), ρ = 1.3 (exact for linear u over a planar rectangle): per-cell net flux \|Σ ±ṁ_f\| ≤ 1e-13·Σ\|ṁ_f\|; for every scheme (upwind, central, linear_upwind, quick) a constant field gives \|conv\| ≤ 1e-10 in every cell; the linear field 1 + x − 2y + 3z gives ρ u(x_P)·∇φ within 1e-9 for central, linear_upwind and quick in every cell more than 2 layers from the boundary (M6; 64 cells). |
| C5 guards | a 3D mesh passed to any 2D-only component (momentum assembly, pressure correction, velocity gradient, wall distance, derived fields, the 2D solution writers, restart writer) throws `InvalidArgumentError` naming the component — never a silent 2D result. |

## D. Sparse assembly (hand-derived), `SparseAssembly3DTest.*`

Production implicit scalar assembly `thermal::assembleEnergyEquation` (the dimension-independent
diffusion + upwind convection + source assembly): k = 2, cp = 1, uniform source Q = 5, on M1, M2
(dx = 0.5, dy = dz = 1), M3 and M4, FixedValue T_b = 1 on every patch, zero mass flux; plus M2 with a
uniform x-flux ṁ = 3·(area) and a FixedGradient(0) xmax patch. Every matrix entry and RHS value is
written out by hand in the test from D_f = k·A_f/d (interior d = cell spacing, boundary d = half
spacing) — not computed by a helper that mirrors the code.

| id | criterion |
| --- | --- |
| D1 | matrix n × n with n = cell count; the stored sparsity pattern (row offsets and column indices) equals the hand-derived pattern exactly (diagonal + one entry per face neighbour, sorted, no duplicate, every index in [0, n)). |
| D2 | every coefficient and RHS value equals the hand-derived value within 1e-13 relative. Includes the neighbour coefficients in all three directions and the boundary contributions of all six patches. |
| D3 | assembling twice gives bitwise identical arrays (deterministic). |

## E. VTK, `VTK3DTest.*`

`VTKWriter::writeCellFields` on M5 (and a 2×1×1 mesh for hand-checked connectivity), parsed back by
the test:

| id | criterion |
| --- | --- |
| E1 | header `# vtk DataFile Version 3.0`, ASCII, `DATASET UNSTRUCTURED_GRID`; POINTS count (nx+1)(ny+1)(nz+1) and every point equals the grid vertex (origin + (i·dx, j·dy, k·dz)) within 1e-15 relative. |
| E2 | CELLS n and 9n entries, each `8` + 8 point ids; for cell (i,j,k) the ids are p(i,j,k), p(i+1,j,k), p(i+1,j+1,k), p(i,j+1,k), then the same at k+1 (VTK_HEXAHEDRON order); the hexahedron built from those points has the cell's centroid and volume (within 1e-14 relative). CELL_TYPES n entries, all 12. |
| E3 | CELL_DATA n; each scalar field block and each 3-component vector block round-trips the field values (17 significant digits → bitwise). |
| E4 | a non-finite value or a wrongly sized field → error before the file is written. |

## F. 3D manufactured solutions, refinement study, `MMS3DTest.*` (release gate)

Unit cube [0,1]³, **levels n = 8, 16, 32, 64** (n³ cells; 262 144 at the finest),
h = representativeGridSize(1, n³, 3) = 1/n. Errors: volume-weighted L1, L2, L∞ against the exact
field at cell centroids (`computeErrorNorms` / `computeVectorErrorNorms`, vector error magnitude).
Observed order for each consecutive pair: **p = ln(E_c / E_f) / ln(h_c / h_f)**.

**Pass rule for each gated (quantity, norm):** the observed order of the **finest pair
(32 → 64)** is ≥ 0.75 × the expected order, **and** the error decreases at every refinement
(E(16) < E(8), E(32) < E(16), E(64) < E(32)). All pairs are reported.

**Manufactured fields** (genuinely three-dimensional: every field varies in z, and u has a
non-zero z-component that varies in x — not a 2D field extruded in z):
- φ = sin(πx)·cos(πy/2)·exp(z/2) + x·y·z
  - ∇φ = (π cos(πx) cos(πy/2) e^{z/2} + yz, −(π/2) sin(πx) sin(πy/2) e^{z/2} + xz, ½ sin(πx) cos(πy/2) e^{z/2} + xy)
  - ∇²φ = (¼ − 5π²/4) sin(πx) cos(πy/2) e^{z/2} (∇²(xyz) = 0).
- u = (1 + 0.5y, 1 + 0.5z, 1 + 0.5x), div u = 0, every component ≥ 1 in the domain (inflow on
  xmin/ymin/zmin, outflow on xmax/ymax/zmax).
- ψ = exp(0.3x + 0.5y + 0.7z) (smooth and monotone along every grid line, so the TVD limiter never
  clips it); u·∇ψ = ψ·(1.5 + 0.35x + 0.15y + 0.25z).
- Forcing for the solves: Poisson −k∇²φ = Q, k = 1; convection–diffusion ρcp u·∇φ − k∇²φ = Q,
  ρ = cp = 1, k = 0.1.

**Independent source verification (F0, regular test):** at 20 fixed pseudo-random points in the
cube, ∇φ, ∇²φ, div u, u·∇ψ and both forcings Q agree with 4th-order central differences
(step 1e-3) of the closed-form φ, ψ, u within 1e-7 relative (sources derived by hand, never from a
discrete operator of the code under test).

Mass flux for convection: ṁ_f = ρ u(x_f)·Sf (exact for linear u). Boundary data: per-face exact
Dirichlet.

| id | quantity (operator, scheme) | expected order p (derivation) | gated norms → threshold |
| --- | --- | --- | --- |
| F1 | Green–Gauss ∇φ (production `gradient`, skew-corrected GG) | 2: face values by linear interpolation are O(h²) on the uniform grid; boundary cells use the paired quadratic (B, P, N) fit, O(h²). | L1, L2, L∞ (global) ≥ 1.5 |
| F2 | least-squares ∇φ (`gradient(…, LeastSquares)`), interior (cells not adjacent to the boundary) | 2: the six neighbours are symmetric (central difference). | L1, L2, L∞ interior ≥ 1.5 |
| F3 | least-squares ∇φ, global | boundary cells O(h) (neighbours at +h and −h/2 along the normal: error (h/8)φ''), a layer of O(h) volume fraction: L∞ 1, L2 1.5, L1 2. | L1 ≥ 1.5, L2 ≥ 1.125, L∞ ≥ 0.75 (global) |
| F4 | explicit diffusion `diffusion(φ, Γ = 1)` vs ∇²φ | 2: interior 7-point central Laplacian O(h²); boundary face flux by the 4-point cubic fit, O(h³) flux → O(h²) Laplacian. | L1, L2, L∞ (global) ≥ 1.5 |
| F5 | Poisson solve: `ThermalSolver`, ṁ = 0, k = 1, per-face Dirichlet (implicit sparse assembly + linear solve) | 2: cell-centred two-point flux with half-cell Dirichlet distance is second order in the solution (supraconvergence on uniform grids). | L1, L2, L∞ (global) ≥ 1.5 |
| F6 | explicit convection, upwind (`convection(ψ, ṁ, upwind)`) vs ρ u·∇ψ | 1 (upwind truncation −(h/2)ψ''u; inflow ghost value 2ψ_B − ψ_P is first-order consistent). | L1, L2, L∞ (global) ≥ 0.75 |
| F7 | explicit convection, central / linear_upwind / quick, interior = cells more than 2 layers from the boundary (`invertMask(boundaryAdjacentCells(mesh, 2))`) | 2: on the monotone ψ the van Leer limiter gives ψ-limited face values with O(h²) smooth error. The first layer's inflow-side faces are degraded to upwind (no far-upstream cell, P12-NUM-001), which makes the next layer O(1): hence the 2-layer exclusion. | L1, L2, L∞ interior ≥ 1.5, for each of the three schemes |
| F8 | convection–diffusion solve: `ThermalSolver` (production energy convection = upwind), ρ = cp = 1, k = 0.1 | 1 (upwind). | L1, L2, L∞ (global) ≥ 0.75 |

*Reported, not gated:* F7 global norms (the documented O(1) boundary-band error of the upwind
degradation; P12-NUM-001 measured a global order ≈ 0.5 in 2D); F1–F8 all pairs; boundary-ring norms.

Solver settings for F5/F8: `ThermalSolverSettings` with linear relative tolerance 1e-10 (absolute
1e-14, ≤ 20000 iterations) and outer tolerance 1e-10, so the iteration error is at least 3 orders of
magnitude below the discretisation error at 64³. Every solve must report converged, with all
values finite; a rejected solve fails the gate — it is never excluded.

## G. Two-dimensional backward compatibility (mandatory)

| id | criterion |
| --- | --- |
| G1 | full regression passes in Release (-O3) and Debug + GUI (Qt 6.2.4, offscreen), 0 failures; exact counts recorded. |
| G2 | the MESH-001 (distorted), MESH-002 (graded), MESH-003 (multi-block) and MESH-004 (quality, MMS) test suites pass by name (focused run before the full regression). |
| G3 | every committed case (`cases/*`) and the CLI data fixtures run with the pre-MESH-005 CLI (`$HOME/m5ref/base`, the working tree before any MESH-005 change) and the new CLI: fields.csv, solution.vtk, residuals.csv **byte-identical**, metadata.json equal as parsed JSON, stdout identical. |
| G4 | no existing 2D case file, fixture or test input is modified by MESH-005 (checked against the starting-tree log); 2D mesh fingerprints (restart compatibility) are unchanged — golden values recorded from the pre-MESH-005 build. |
| G5 | the tracked validation outputs regenerated by the full regression differ from the pre-MESH-005 working tree only in runtime-only fields (classified file by file). |

*Reported, not gated:* the cost of the 3-component vector (the same 2D benchmark runs timed with
the pre-MESH-005 and the new Release build).

## Clarification (written after the code audit, before any verification run or test execution)

**C3, zero-flux conservation clause.** The explicit operator `diffusion()` evaluates a boundary
face's flux from the 4-point cubic fit through the boundary *value*
`boundaryValue(φ_P, d)` (Diffusion.cpp `uncorrectedBoundaryFlux`, unchanged since P0). For a
`FixedGradient(0)` face that value is φ_P, and the fitted flux is zero only when φ is constant along
the normal line. For a non-constant field the clause as first written could therefore not hold, in
2D or 3D. This is a pre-existing property of the explicit operator, unrelated to dimension: the
implicit assemblies impose the prescribed flux exactly (MESH-003 fix). It is reported as a finding,
not changed here.

The clause is therefore evaluated as the existing 2D test `DiffusionTest.ZeroFluxBoundaryConservesGlobally`
evaluates it: a constant field with zero-flux boundaries, where Σ_P V_P·diffusion_P must be exactly
0. It is **complemented, not replaced,** by a stronger telescoping check on the non-constant
quadratic field with Dirichlet data: Σ_P V_P·diffusion(φ)_P = Γ ∮ ∂φ/∂n dA = Γ·12·|Ω|, with the
boundary integral computed analytically, within 1e-10 relative. Every interior flux must cancel
pairwise for this to hold. No threshold, field, mesh or norm of any other item is affected.

## H. Stop rules

- A failed item in A–G stops the phase: the failure is recorded as evidence and MESH-005 is
  reported BLOCKED / FAILED GATE. No threshold, level, norm or mask is changed after a run.
- TODO items are marked `[x]` only after this gate and the full regression pass.
