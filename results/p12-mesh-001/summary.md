# P12-MESH-001 — Production Non-Orthogonal Structured Meshes

**Authorized:** 2026-09-15 (P12-MESH-001 only). **Status:** complete —
every gate below passed; working tree **uncommitted** (no commit/push was
authorized, TODO.md working rule 16).

Environment: WSL2 Ubuntu, GCC 11.4, `build/release` (Release `-O3`, GUI off,
OpenMP off) for all measurements; `build/debug` (Debug, GUI on) for the Debug
and GUI regression. Baseline: `HEAD = b66310ca871811c4c7671056beec291a451af55c`.

Raw logs: `logs/01..14_*.log` (every number quoted below comes from them;
`06a` is a labelled verbatim transcription, see its header).

---

## 1. Scope

Implemented exactly P12-MESH-001: a vertex-defined, possibly
non-orthogonal structured quadrilateral mesh that is loaded, validated,
built, solved, and exported through the normal production case path, with
quantitative validation. Not started: P12-MESH-002 and later (no grading,
generator, multi-block, general geometry, 3D or moving meshes), P12-TURB,
P12-SPECIES, P12-MULTI, compressible energy, higher Mach, P13.

## 2. Architecture decision

* **Representation — the smallest one that fits the existing mesh.** A new
  `mesh.json` type `"structured_quad"` gives the `(nx+1) x (ny+1)` vertex
  grid explicitly (row-major, `i` fastest). Topology, face order,
  owner/neighbour pairs and the four patches `left/right/bottom/top` are
  *exactly* those of `createCartesian2D`; only the vertex positions are free.
  The domain stays the `geometry.json` rectangle (boundary vertices must lie
  on it).
* **No second mesh representation.** `MeshGeometry::createStructuredQuad2D`
  builds the ordinary `cfd::mesh::Mesh` (cells, faces, patches) with exact
  polygon geometry (shoelace area/centroid, edge-midpoint face centroid,
  rotated-edge area vector). The mesh additionally carries its generating
  vertex grid (`cfd::mesh::StructuredGrid`, `Mesh::structuredGrid()`), set
  by both builders, used **only** by the exporters (true VTK points) — the
  numerics never read it. The P12-NUM test generator
  `tests/unit/discretization/DistortedMesh.hpp` now delegates to the
  production builder (proven bit-identical: 326 400 geometry values over 35
  meshes compared at `-O3` and `-O0`, 0 mismatches).
* **One validity gate for every mesh type.** `MeshQuality::evaluate` gained
  topology-generic checks and a `problems` list; `CaseBuilder` runs it on
  every built mesh (Cartesian included) and refuses to hand an invalid mesh
  to any solver.
* **Reuse of P12-NUM.** GG/LS gradients, skewness correction, non-orthogonal
  diffusion and the geometric pressure correction are used unchanged. Two
  genuine production-mesh defects were found by measurement and fixed at the
  root (section 7): the Neumann boundary-face value in the scalar gradient
  and the SST wall distance. Both fixes are guarded to be bit-identical on
  Cartesian meshes (proved in section 6).

## 3. Files changed

New:

* `include/cfd/mesh/StructuredGrid.hpp` — the vertex grid type.
* `cases/poiseuille_distorted/` — committed distorted production case (64x8).
* `tests/unit/mesh/test_structured_quad_mesh.cpp` (26 tests),
  `tests/unit/io/test_structured_quad_case.cpp` (16),
  `tests/unit/discretization/test_oblique_neumann_gradient.cpp` (3),
  `tests/integration/case/test_structured_quad_production_case.cpp` (9).
* `results/p12-mesh-001/` — this evidence.

Modified (library): `include/cfd/mesh/{Mesh,MeshGeometry,MeshQuality}.hpp`,
`src/mesh/{Mesh,MeshGeometry,MeshQuality}.cpp` (builder, grid, validity
checks); `include/cfd/io/case/MeshConfig.hpp`,
`src/io/case/MeshConfigParser.cpp`, `src/io/CaseReader.cpp`,
`src/io/CaseWriter.cpp`, `src/io/CaseBuilder.cpp` (case format, cross-file
checks, build + validity gate); `src/io/StructuredMeshInfo.{hpp,cpp}`,
`src/io/VTKWriter.cpp` (export); `src/discretization/Gradient.cpp` (fix F1);
`include/cfd/turbulence/WallDistance.hpp`, `src/turbulence/WallDistance.cpp`
(fix F2).

Modified (GUI): `apps/gui/CaseModelAdapter.{hpp,cpp}`,
`apps/gui/SimulationControllerEditing.cpp`, `apps/gui/qml/MeshEditor.qml`
(vertex grid preserved through edit/save; read-only vertex count; the
preview is labelled schematic).

Modified (tests): `tests/unit/discretization/DistortedMesh.hpp` (delegates
to the production builder), `tests/unit/turbulence/test_wall_distance.cpp`
(+2), `apps/gui/tests/test_case_editing.cpp` (+2), four `CMakeLists.txt`.

Modified (docs/tracking): `docs/user_guide/case_format.md`, `TODO.md`,
`ROADMAP.md`; regenerated `results/validation/mms/distorted_mesh_*_mms.*`
and `results/validation/production/poiseuille_distorted_grid_convergence.json`
(section 6.3).

## 4. Case-format change and backward compatibility

```json
{ "type": "structured_quad", "nx": 2, "ny": 1,
  "vertices": [[0,0],[0.5,0],[1,0],[0,1],[0.6,1],[1,1]] }
```

Acceptance chain (each failure is a case error naming the vertex / cell /
face): parser — `vertices` required for `structured_quad`, rejected for
`structured_cartesian`, exactly `(nx+1)(ny+1)` entries, each two finite
numbers (an overflowing literal such as `1e400` is already rejected by the
JSON reader); `CaseReader` — boundary rows/columns on the rectangle edges
(tolerance `1e-9 * max(length, height)`), corners at the corners, each
edge strictly monotone; `CaseBuilder` — every cell strictly convex and
counter-clockwise (rejects folded, self-intersecting, clockwise, zero-area,
collapsed-edge cells; non-finite coordinates), cells tile the rectangle
(total area within `1e-9` relative), then the `MeshQuality` gate: cells with
`>= 3` faces, face ownership/connectivity, closure
`|sum Sf| <= 1e-10 sum|Sf|`, internal `(x_N - x_P).Sf > 0` and boundary
`(x_f - x_P).Sf > 0` (usable diffusion coefficients), every boundary face in
exactly one patch. Cell/Face constructors already reject non-finite and
non-positive geometry, so NaN/Inf geometry cannot reach a solve.

**Backward compatibility.** `structured_cartesian` is unchanged: same parser
result (no `vertices`), same `mesh.json` written (`{type, nx, ny}` only),
bit-identical mesh from `CaseBuilder`, and the exporters produce identical
bytes (the Cartesian vertex grid `(i*dx, j*dy)` equals the old
reconstruction exactly). No existing case needs editing. Evidence in
section 6.

## 5. Mesh quality (MeshQuality::evaluate, measured)

| mesh | max non-orth (deg) | mean non-orth | max skewness | mean skewness |
|---|---|---|---|---|
| `poiseuille_distorted` 64x8 (committed) | 44.76 | 14.58 | 0.1318 | 0.0369 |
| same mapping 96x12 | 47.18 | 15.14 | 0.0910 | — |
| same mapping 144x18 | 48.25 | 15.44 | 0.0609 | — |
| distorted slab 20x4 (thermal/species) | 32.51 | 12.99 | 0.221 | 0.058 |
| lubrication channel 48x8 (CompressibleSIMPLE) | 20.94 | 6.96 | 0.086 | 0.025 |
| turbulent channel 48x12, tilted walls | 30.96 | 13.25 | 0.080 | 0.016 |

Mapping of every distorted mesh: `x = xi + ax sin(pi xi/L) sin(2 pi eta/H)`,
`y = eta + ay sin(2 pi xi/lambda) sin(pi eta/H)`, boundary vertices snapped
onto the rectangle (`ax` tilts grid lines at the walls; `ay` makes the
horizontal lines wavy). Poiseuille: `ax = 0.1, ay = 0.05, lambda = H`.

## 6. Validation

### 6.1 Case and solver configuration

`cases/poiseuille_distorted`: planar Poiseuille channel `L = 8, H = 1`,
`rho = 1, mu = 0.1`, uniform inlet `U = 1` (`Re = 10`), zero-gradient outlet
with `p = 0`, no-slip walls. Solver: SIMPLE, relaxation 0.7/0.3, outer gates
u 2e-5 / p 5e-4 / continuity 1e-6 (the NUM-005/007 Poiseuille settings),
`convection_scheme: linear_upwind`, `gradient_scheme: green_gauss`,
`non_orthogonal_corrections: 1`, momentum BiCGSTAB 1e-10/1e-8, **pressure
CG** 1e-10/1e-8 (5000). Reference: analytical `u = 6U (y/H)(1-y/H)`,
`dp/dx = -12 mu U/H^2 = -1.2`. Errors over the developed region
`0.50 L <= x_c <= 0.85 L`, volume-weighted; dp/dx by a volume-weighted
least-squares fit of `p(x)` over the same cells (robust to the collocated
odd-even mode). Accuracy is gated relative to the **Cartesian scheme's own
exact discretization error at the same `ny`** (`<= 1.5x`).

### 6.2 Committed case (64x8) through ProjectRunner — log 09

| quantity | value | gate |
|---|---|---|
| status | Converged, 1071 iterations | Converged |
| fields finite | yes | yes |
| global mass imbalance | 8.53e-12 | <= 1e-6 |
| max face-column flow error (65 wall-to-wall columns incl. inlet/outlet) | 3.89e-11 | <= 1e-6 |
| wall mass flux | 0 | <= 1e-12 |
| velocity L2 error | 1.7488e-2 (Cartesian 1.5183e-2 = 1.15x) | <= 1.5x Cartesian |
| dp/dx | -1.173711 (2.19%; Cartesian 3.03%) | <= 1.5 x 3.03% |
| exported VTK points | 585, identical to the case vertices | exact |

CLI run of the same case (`cfdapp --case`, final code, log 11): Converged,
1071 iterations, mass imbalance 8.5e-12, `NaN/Inf: no`, VTK `POINTS 585`
with the displaced vertex (10,2) = (1.2971396736825997, 0.28535533905932736)
written exactly.

### 6.3 Grid refinement (64x8 / 96x12 / 144x18, r = 1.5) — log 09

Each grid written by `CaseWriter`, run by `ProjectRunner`, analysed by the
P12-NUM-005 `runGridConvergenceStudy` (report:
`results/validation/production/poiseuille_distorted_grid_convergence.json`).

| grid | iterations | velocity L2 | Cartesian | dp/dx | rel. error | column-flow error |
|---|---|---|---|---|---|---|
| 64x8 | 1071 | 1.7488e-2 | 1.5183e-2 | -1.1737108 | 2.191% | 3.9e-11 |
| 96x12 | 673 | 7.4225e-3 | 6.9492e-3 | -1.1860408 | 1.163% | 6.0e-11 |
| 144x18 | 682 | 3.2779e-3 | 3.1294e-3 | -1.1929851 | 0.585% | 3.4e-10 |

* Velocity error: pair orders 2.114, 2.016; NUM-005 triplet `p = 2.188`,
  **asymptotic** (ratio 1.079).
* dp/dx: pair orders 1.561, 1.697; triplet `p = 1.416`,
  monotonic_not_asymptotic; Richardson `-1.2019386` (0.16% from exact);
  `GCI21 = 0.938%` bounds the true fine-grid error 0.585%.
* Gates: every pair order `>= 1.5` (formal 2); GCI bounds the true error.

Variants (same gates, all pass): `gradient_scheme: least_squares` 64x8 —
velocity (u) L2 1.7807e-2, dp/dx 2.36%, 976 iterations (log 08).

### 6.4 Connections (production path) — logs 08, 09

| module | case / reference | result |
|---|---|---|
| SIMPLE + NUM-003 (GG, skew-corrected; non-orth. diffusion; geometric pressure correction) | distorted Poiseuille (6.2, 6.3) | pass |
| least-squares gradients | same, LS | pass |
| `non_orthogonal_corrections` threaded | 64x8 with `0` | velocity L2 3.31e-2 (1.9x the corrected, 2.2x Cartesian); dp/dx 6.2%; 96x12 and 144x18 **diverge** (NumericalFailure at iteration 201 / 59) |
| thermal | distorted slab 20x4, `T = 310 - 20x` | max error 1.53e-6 K (GG), 1.01e-7 K (LS); without correction 0.313 K |
| species | same slab, `Y = 1 - x` | max error 4.31e-7 (GG), 3.90e-7 (LS); without correction 1.57e-2 |
| CompressibleSIMPLE | distorted lubrication channel 48x8, Arkilic et al. `p(x)^2` linear | Converged (3575 it), p error L2 0.84% / Linf 2.23% (Cartesian 1.29% / 2.55%; gate 5%); face-column mass-flow deviation 1.7e-8 |
| CompressibleSIMPLE (low Mach) | distorted Poiseuille, Ma 4.2e-3 | matches SIMPLE: velocity (u) L2 3.0994e-2 vs 3.1042e-2 (both upwind — CompressibleSIMPLE has no convection-scheme option) |
| k-epsilon | turbulent channel Re_b 5600, 48x12, tilted walls | Re_tau (force balance) 448.18 vs Cartesian 447.90 (0.06%; 96x24: 0.02%) |
| SST | same | Re_tau 333.33 vs 322.75 (3.3%; 96x24: 205.40 vs 205.03, 0.2% — shrinks with refinement) |

Turbulence is a **consistency** check (distorted vs Cartesian through the
same production path), not a validation: the coarse Cartesian Re_tau itself
is far from the DNS 180 (P2-TURB-007 documents the near-wall-resolution
bias).

### 6.5 Conservation

Global mass imbalance <= 3.4e-10 on every accepted Poiseuille run; every
face column (a wall-to-wall curve of faces on the distorted mesh) carries
`U H` to <= 3.4e-10; wall mass flux exactly 0; turbulent channels (CG pressure) <= 6.6e-9;
coupled compressible face-column deviation 1.7e-8 (relative).

### 6.6 Backward compatibility (no Cartesian regression) — log 10

HEAD (`b66310c…`, pre-P12-MESH-001) and working-tree CLIs, both Release
`-O3` with identical options, on every committed Cartesian case:
`lid_driven_cavity{,_40x40,_80x80}`, `poiseuille_flow`, `heated_cavity`,
`species_diffusion`, `heated_species_diffusion`, `compressible_validation`,
`compressible_channel_coupled`, `multiphase_validation` and the
`tests/data/cases/{valid_cavity, valid_cavity_cli_smoke, does_not_converge}`
fixtures: **13 / 13 byte-identical** `fields.csv`, `solution.vtk`,
`residuals.csv`, `metadata.json` (no key differs) and CLI stdout; identical
exit codes (0, and 3 for `does_not_converge`).

Unit-level Cartesian regression tests (new): VTK bytes identical with and
without the vertex grid (3 meshes); `CaseBuilder` Cartesian mesh
bit-identical to `createCartesian2D`; Cartesian `mesh.json` written as
`{type, nx, ny}`; wall distance bit-identical to the old face-centroid
algorithm; Cartesian gradients unchanged/exact.

**Changed existing results (expected, justified):** exactly two P12-NUM
studies, whose DistortedMesh meshes have fixed-gradient pressure walls met
obliquely by the grid lines (fix F1 acts there); both regenerated by the
Debug run, as the committed ones were; **every gate still passes**:

* `distorted_mesh_momentum_mms` — corrected u observed order 2.166 -> 2.141,
  v 1.902 -> 1.892; finest-grid corrected u L2 2.2038e-5 -> 2.2646e-5
  (+2.8%; Cartesian 2.2017e-5, gate "within 10% of Cartesian"), v L2
  5.2691e-5 -> 5.2701e-5; uncorrected/corrected ratio 49.3 -> 48.0.
* `distorted_mesh_simple_mms` — u order 2.330 -> 2.278, v 2.279 -> 2.230,
  p 1.685 -> 1.678, face flux 3.223 -> 3.207; 32x32 continuity Linf
  3.17e-7 -> 1.47e-7; mass imbalance unchanged.

Every other regenerated `results/validation/**` file (Cartesian MMS,
distorted scalar-diffusion MMS, natural convection, production Poiseuille /
cavity / scheme comparison, turbulent channel, grid-convergence reports)
differed from HEAD in runtime fields only and was reverted. New report:
`results/validation/production/poiseuille_distorted_grid_convergence.json`.

## 7. Defects found and fixed (root causes, measured)

**F1 — Neumann boundary value in the scalar gradient (`Gradient.cpp`).** A
Neumann-type face (fixed gradient / adiabatic / zero flux) met obliquely by
its grid line had its value extrapolated along the normal but placed at the
face centroid with the straight-line owner distance, ignoring the
tangential variation of the field — an O(1) gradient error in wall cells,
feeding the explicit non-orthogonal correction. Measured (distorted slab,
walls met at 30–49 degrees, Jacobi-preconditioned probe, log 06a;
production path: log 04): temperature L2 error 4.49e-2, 4.50e-2, 2.65e-2 K on
20x4, 40x8, 80x16 (160x32 unconverged) — versus 1.14e-2, 9.9e-4, 3.9e-5 K on
the same distortion made orthogonal at the walls.
Fix: split `d = x_f - x_P` into `d_n = d.n` and `d_t`; face value
`boundaryValue(phi_P, d_n) + grad(phi)_P . d_t` (GG: inside the existing
skew-correction sweeps; LS: the row `n.grad(phi) = g` at the normal foot).
After: 5.0e-7, 1.4e-7, 2.6e-7 K (solver-tolerance level) on every converged
grid; unit test: LS gradient of a linear field exact to 5e-15, GG to 3.3e-7.
The paired one-sided boundary fit (a 1D normal formula) now also requires an
orthogonal boundary face. Guard: exactly-parallel faces (every Cartesian
face) keep the old code path.

**F2 — SST wall distance (`WallDistance.cpp`).** Face-centroid distance
overestimated wall-adjacent distances by 21.7% (48x12) / 24.1% (96x24) on a
channel mesh tilted at the walls — an O(1) relative error (log 04). Now the
exact point-to-segment distance: 0.0000 error (log 06); bit-identical on
Cartesian meshes (unit test).

## 8. Failed experiments and findings (kept)

1. **First-order upwind on a flow-misaligned grid.** With the default
   `upwind` the distorted Poiseuille velocity error converged at ~1.3 and dp/dx
   error was non-monotone and stalled (1.04%, 1.57%, 1.36%; log 01). Isolated
   (log 02): x-only distortion reproduces the Cartesian result to 3 digits;
   y-only (wavy lines crossing the flow) causes it. `central` /
   `linear_upwind` restore second order (log 03). Not a mesh defect —
   documented; the case uses `linear_upwind`.
2. **`non_orthogonal_corrections: 0`** on the distorted case: 2x error at
   64x8, divergence at 96x12/144x18 (logs 01, 08). The Cartesian default (0)
   is kept; the case format doc tells users to set 1 for structured_quad.
3. **Unpreconditioned BiCGSTAB pressure solve** breaks down on the refined
   distorted grids (PressureCorrectionFailure at iteration 188 / 35; log 05)
   — same root cause as P12-COMP-002. CG (the matrix is symmetric) or the
   NUM-004 fallback converge to identical results; the case uses CG.
4. **Turbulence on the doubly distorted channel** (`ay != 0`): Re_tau differs
   from Cartesian by ~9% (k-eps) / ~8% (SST) at both 48x12 and 96x24 (log
   07): that mesh varies the first-cell wall-normal height by +/-16% along the
   wall at every resolution, and the near-wall solution is unresolved at
   these grids. With the tilt kept but uniform wall-normal spacing, the
   difference is 0.06% / 3.3% and shrinks with refinement — so the tilt is
   handled consistently; the residual effect is grid sensitivity, not a
   mesh-handling defect.
5. **CaseWriter drops `compressible.coupled`** (pre-existing, P12-COMP):
   discovered because a CaseWriter-written coupled case silently ran the
   post-hoc path (log 04). Not fixed (outside scope); the P12-MESH test writes
   that key directly. A GUI save of a coupled case loses the flag.
6. **Species/thermal fixed linear-solver settings** (unpreconditioned
   BiCGSTAB, abs 1e-12): species LinearSolveFailure from 40x8 up on the slab
   **on the Cartesian mesh too** (log 05); thermal on distorted 80x16/160x32.
   Pre-existing, not a mesh defect; the connection tests use 20x4.

## 9. Test counts

New tests: 58 — mesh unit 26 (`StructuredQuadMesh` 15, `MeshValidity` 11),
case format/export 16 (`StructuredQuadCase`), discretization 3
(`ObliqueNeumannGradient`), wall distance +2, production integration 9
(`StructuredQuadProductionCase`), GUI +2 (`CaseEditingTest.StructuredQuad*`).

Focused, in the required order (Release, log 12) — all passed:

| stage | tests |
|---|---|
| 1 mesh / geometry unit | 70 / 70 |
| 2 case format / export | 131 / 131 |
| 3 discretization (gradients, skewness, non-orthogonal, distorted) | 112 / 112 |
| 4 SIMPLE / CompressibleSIMPLE (incl. MMS, Poiseuille, cavity) | 241 / 241 |
| 5 thermal / species / turbulence | 334 / 334 |
| 6 new end-to-end | 9 / 9 |

Full regression:

* Release (`build/release`, GUI off): **1670 / 1670 passed**, 1695 listed,
  25 disabled (the pre-existing `DISABLED_` set), 131 s (log 13).
* Debug + GUI (`build/debug`): **1712 / 1712 passed**, 1737 listed, 25
  disabled, 845 s, incl. 42 GUI tests (log 14). Debug build warnings: only
  the three pre-existing `-Wconversion` warnings in
  `SimulationControllerEditing.cpp:83-86` (not P12-MESH lines).
* Before the P12-MESH new tests existed (library/format changes only),
  Release: 1614 / 1614 (baseline count unchanged).
* `clang-format-18 --dry-run --Werror` on every changed/new C++ file: clean.
* Backward compatibility CLI comparison: 13 / 13 byte-identical (§6.6).
* NOT run here: ASan and clang-tidy (CI jobs; no commit was authorized, so no
  CI run exists for this work). Debug durations of the heaviest new tests:
  SST channel 244 s, grid convergence 239 s, lubrication channel 136 s.

## 10. Limitations

* Structured topology only: the user supplies the vertex grid (no
  generator, grading, multi-block, general geometry, 3D, moving meshes —
  MESH-002+). Domain = the `geometry.json` rectangle.
* `non_orthogonal_corrections` defaults to 0 (Cartesian behaviour
  preserved) and must be set for accurate structured_quad results; no
  automatic warning.
* `upwind` convection has an O(h) error on flow-misaligned grids;
  CompressibleSIMPLE has no convection-scheme option (upwind only).
* Vector (velocity) gradient: outlet/symmetry faces keep the face-centroid
  owner value (no oblique treatment) — not exercised obliquely here, NOT
  VERIFIED on oblique outlets.
* SST WallOmega uses the straight-line owner–face distance (measured
  insensitive: 0.2% at 96x24; not changed).
* Turbulence verified for consistency against Cartesian only.
* GUI: no vertex editor; mesh preview and field heat map are schematic
  (logical nx x ny); the exported VTK carries the true geometry.
* Pre-existing (not P12-MESH): CaseWriter `coupled` drop; thermal/species
  linear-solver settings; collocated SIMPLE without Rhie–Chow.
