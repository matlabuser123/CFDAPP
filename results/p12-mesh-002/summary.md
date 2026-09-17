# P12-MESH-002 — Stretched / Graded Meshes

**Authorized:** 2026-09-15 (P12-MESH-002 only; P12-MESH-001 complete in the
same uncommitted working tree). **Status:** see §18. Nothing committed or
pushed (not authorized).

Environment: WSL2 Ubuntu, GCC 11.4; `build/release` (Release `-O3`, GUI off,
OpenMP off) for all measurements; `build/debug` (Debug, GUI on, Qt 6.2.4)
for the GUI and Debug regression. Raw logs: `logs/00..11_*.log`.

## 1. Scope

Implemented exactly P12-MESH-002: geometric cell-size grading of structured
meshes along x and/or y, with one-sided and two-sided boundary clustering,
through the production case path (mesh.json → parser → validation →
CaseBuilder → Mesh → ProjectRunner → solver → export), the CLI and the GUI
mesh editor, validated on a wall-gradient problem against an exact solution.
Not started: P12-MESH-003 and later, general/multi-block geometry, 3D,
moving meshes, P12-TURB/SPECIES/MULTI, compressible scope, P13.

## 2. Starting state

`HEAD = b66310ca871811c4c7671056beec291a451af55c`; working tree = the
uncommitted, verified P12-MESH-001 (43 status entries — `logs/00` records
`git status --short`, `git diff --stat`, `git diff --name-status` before any
MESH-002 change). All MESH-001 source, tests, cases, docs and evidence are
preserved (its tests are part of the regression below). A snapshot of that
tree (`/tmp/cfd_mesh001`, Release CLI) is the "pre-MESH-002" baseline of §7.

## 3. Architecture decision

* **Grading is a property of the node distribution along each axis**, not a
  new mesh kind: `cfd::mesh::gradedNodeCoordinates` / `AxisSpacing`
  (`include/cfd/mesh/MeshGrading.hpp`, `src/mesh/MeshGrading.cpp`).
* **One structured topology builder.** `MeshGeometry.cpp` now has a single
  `buildStructuredMesh` (cells, faces, owner/neighbour, face order, patches,
  vertex grid) driven by geometry callbacks. `createCartesian2D` became
  `createRectilinear2D(AxisSpacing::uniform, AxisSpacing::uniform)`; the
  graded mesh is `createRectilinear2D` with graded spacings
  (`createGraded2D`); `createStructuredQuad2D` (MESH-001) uses the same
  builder with its own polygon geometry. This *removed* the duplicated
  connectivity/patch code MESH-001 had (Cartesian and quad each had a copy)
  instead of adding a third. Connectivity, patches, face ownership and the
  mesh-quality gate are shared; no second structured-mesh implementation.
* **Bit-identity by construction.** A uniform `AxisSpacing` evaluates
  exactly the expressions `createCartesian2D` always used (`dx = L/n`,
  node `i*dx`, centre `(i+0.5)*dx`, volume `dx*dy`). Proven (`logs/01`):
  960 meshes (480 Cartesian, 480 distorted structured_quad; 65,343,360 bytes
  of cell/face/patch/vertex data) dumped by the pre-MESH-002 library and by
  the new one — byte-identical, driver compiled at `-O3` and `-O0`.
* **Scope of grading: `structured_cartesian` only.** A graded rectilinear
  mesh is exactly orthogonal (rectangular cells, cross products exactly 0),
  so the orthogonal fast paths stay exact. `structured_quad` takes explicit
  vertices — users grade them there; `"grading"` on a `structured_quad` is
  rejected. Compatibility of graded + non-orthogonal is verified by building
  a structured_quad from the graded node distribution plus a distortion
  (§11), not by a combined option.

## 4. Mathematical definition

Per axis with n cells and length L; `ratio` r ≥ 1 = growth factor of
adjacent widths away from the clustered boundary:

* uniform: `w_k = L/n`;
* cluster at start (`left`/`bottom`): `w_k = w_0 r^k`; at end
  (`right`/`top`): the mirror `w_k = w_0 r^(n−1−k)`;
* `both`: `w_k = w_0 r^min(k, n−1−k)` — even n: two equal largest centre
  cells; odd n: one largest centre cell;
* `w_0` such that `Σ w_k = L`.

Nodes in closed form (no width accumulation):
`x_k = L·expm1(k l)/expm1(n l)`, `l = log1p(r − 1)` (start); end =
`L − x'_{n−k}`; both = lower half `x_k = L·expm1(k l)/T'` with
`T' = 2 expm1(m l) (+ e^{m l}(r−1) for odd n)`, `m = ⌊n/2⌋`, upper half
mirrored (`x_k + x_{n−k} = L` exactly). The ratios of `expm1` values make
r → 1 well conditioned; `r = 1` exactly returns the uniform distribution
bit for bit. Invariants verified (unit tests, 7 cell counts × 4 lengths ×
5 ratios × 3 clusterings): `x_0 = 0` and `x_n = L` exactly; widths > 0 and
finite; `Σ w = L` to 1e-13; every adjacent ratio = r^(e(k+1)−e(k)) to
1e-9; two-sided symmetry to 1e-14; hand-computable cases exact
(`r=2`: 0,1,3,7,15 / 0,8,12,14,15; `both` 0,1,3,7,11,13,14 and
0,1,4,13,16,17); deterministic.

Refinement under one law: keep `r^m` constant (m = cells in the clustered
direction), `r_new = r_old^(m_old/m_new)` — the nodes then sample the same
mapping `x(ξ) = (K^ξ − 1)/(K − 1)`.

## 5. Case schema

```json
{"type": "structured_cartesian", "nx": 48, "ny": 24,
 "grading": {"x": {"type": "uniform"},
             "y": {"type": "geometric", "ratio": 1.2, "cluster": "both"}}}
```

`grading` optional; `x`/`y` optional (absent = uniform); `type` ∈
{uniform, geometric}; geometric requires `ratio` (number ≥ 1) and `cluster`
∈ {left, right, both} (x) / {bottom, top, both} (y). `MeshConfig` gained
`std::optional<MeshGradingConfig> grading`; CaseWriter writes it iff present
(both axes explicitly), so ungraded cases are written exactly as before.
Documented in `docs/user_guide/case_format.md` (*Graded meshes*).

## 6. Validation / error handling

Rejected before any mesh is built, each with the offending field
(`grading.<axis>.<key>`) and an actionable message:

| input | where | tested |
|---|---|---|
| ratio 0, −1.2, 0.5, 0.999999, NaN, ±Inf | library (`finite and >= 1`) | unit |
| ratio 0 / −1.2 / 0.5 / string / bool | parser (`grading.y.ratio`) | io |
| ratio literal `1e400` | JSON reader (IOError "number overflow") | io |
| unknown type / cluster / key / axis; x-cluster name on y; missing ratio or cluster; ratio on uniform; non-object | parser | io |
| `grading` on structured_quad | parser | io |
| zero / negative / non-finite length | geometry.json + library | io, unit |
| zero cells | mesh.json + library | io, unit |
| overflow (r = 10 over 400 cells) | CaseReader vs geometry (`reduce the ratio or the cell count`) | io, unit |
| vanishing cells (smallest < 1e-8·L, e.g. r = 2 over 40 cells) | CaseReader (`smallest cell …`) | io, unit |
| non-positive / non-finite generated widths | library | unit (overflow path) |
| degenerate cells after construction | MeshQuality gate (MESH-001), always applied | io, mesh |

The 1e-8·L floor keeps every width resolved to ~2e-8 relative in double
precision (node coordinates are O(L)). Nothing is silently repaired.

## 7. Backward compatibility

* Builder refactor: bit-identical mesh data (960 meshes, §3, `logs/01`).
* Unit: uniform grading (and `ratio: 1`) builds the Cartesian mesh bit for
  bit (4 meshes, every cell/face/vertex/fingerprint); an ungraded case is
  written as `{type, nx, ny}`; a uniform-only grading block builds the
  Cartesian mesh.
* CLI (`logs/05`): every committed Cartesian case (10 in `cases/` + 3
  `tests/data` fixtures) run by the pre-MESH-002 snapshot, by HEAD
  `b66310c`, and by the new CLI — **26 / 26 comparisons byte-identical**
  (fields.csv, solution.vtk, residuals.csv, metadata.json, stdout, exit
  codes); `cases/poiseuille_distorted` (MESH-001) byte-identical to the
  snapshot. The new graded case is cleanly rejected by the old CLI
  ("grading" unknown field) and converges with the new one.
* Existing cases need no change.

## 8. GUI / CLI integration

* CLI: the committed graded case loads, validates, runs (Converged, 1563
  iterations, mass imbalance 3.2e-12, `logs/11`) and exports (VTK points = graded
  nodes, e.g. y1 = 0.012632, y2 = 0.027791 = 1.2·Δ0 added, centre 0.5
  exactly).
* Production tests (§10–13) all go through CaseWriter/CaseReader/
  ProjectRunner; grading round-trips through CaseWriter.
* GUI: the mesh editor already authors mesh settings, so grading was added
  there (per axis: type, ratio, cluster; the Derived panel shows the
  smallest/largest cell; the preview draws the graded lines from
  `meshCellInfo`, which calls the same grading function). CaseModelAdapter
  flattens grading into the editor map; a map without grading keys keeps the
  previous grading; an ungraded case stays ungraded. Controller tests (3
  new): graded case edit → save → reopen preserves both axes; an invalid
  ratio is a Mesh-section error on `grading.y.ratio`; `meshCellInfo`
  returns graded nodes / min-max widths / `gradingError`; an ungraded case
  saves `{type, nx, ny}`. `qmllint` (Qt 6.2.4): 0 errors, only the
  file's existing "unqualified access" style warnings (`logs/07`). **The QML
  page was not visually inspected** (no human GUI session; NOT VERIFIED
  visually).

## 9. Deterministic geometry tests

`GradedMesh.GradedMeshesHaveTheRequestedCellsAndAreValid` (24 × 16 on
4 × 1), per axis:

| variant | axis | width min | width max | max/min | requested r | adjacent ratios | closure |
|---|---|---|---|---|---|---|---|
| y both 1.2 | y | 0.030305 | 0.108587 | 3.583 | 1.2 | 1.000..1.200 | 0.0 |
| y bottom 1.15 | y | 0.017948 | 0.146041 | 8.137 | 1.15 | 1.150..1.150 | 0.0 |
| y top 1.15 | y | 0.017948 | 0.146041 | 8.137 | 1.15 | 1.150..1.150 | 0.0 |
| x left 1.1 | x | 0.045199 | 0.404726 | 8.954 | 1.1 | 1.100..1.100 | 0.0 |
| x right 1.1 | x | 0.045199 | 0.404726 | 8.954 | 1.1 | 1.100..1.100 | 0.0 |
| x both 1.1 + y both 1.2 | x / y | 0.093527 / 0.030305 | 0.266842 / 0.108587 | 2.853 / 3.583 | 1.1 / 1.2 | ..1.1 / ..1.2 | 0.0 |

Every cell equals the requested rectangle exactly; cells tile the domain
(area to 1e-13); MeshQuality valid with non-orthogonality and skewness
**exactly 0**; positive areas; the vertex grid (VTK) is exactly the graded
nodes; construction deterministic (fingerprints).

## 10. Wall-gradient validation

**Why not plain Poiseuille** (preliminary, `logs/02`, before the gate): at
64 × 16, y-clustering *worsens* the solver's integral wall shear (dp/dx
error 0.76% uniform → 0.78 / 0.83 / 0.92 / 1.06 / 1.45 / 1.98% for
r = 1.05…1.4) while the velocity L2 improves only for mild r (0.71× at
1.1) and the reconstructed near-wall gradient improves. Analysis: on a
y-graded mesh the scheme's face fluxes are exact at the faces and the
profile error accumulates to ~G Δ_max²/8 at the centre — for a
constant-curvature parabola the largest cells control the error and a
uniform grid is already optimal. Poiseuille is not a wall-boundary-layer
problem. (Recorded as a finding, §16.)

**Case used** (`cases/channel_transpiration_graded`, committed): channel
L = 4, H = 1, ρ = 1, μ = 0.1, inlet U = 1, uniform wall transpiration
V = 2 (injection through the bottom wall, suction through the top — inlet-
type velocity patches with u = 0), outlet with p = 0; Re_w = VH/ν = 20.
Exact fully developed NS solution (v = V, p = p(x)):
`u = C[H(e^{λy}−1)/(e^{λH}−1) − y]`, `λ = V/ν`,
`C = U/(1/λ − H/(e^{λH}−1) − H/2)`, `dp/dx = ρCV`,
`τ_top = μC(λH e^{λH}/(e^{λH}−1) − 1)` — boundary layer 0.05 H at the
suction wall. Case validity: uniform refinement converges to it (dp/dx
error 4.7, 2.1, 0.72, 0.20% at ny = 16…128, `logs/03`).

Solver (identical for every run): SIMPLE, 0.7/0.3, gates u 2e-5 / p 5e-4 /
continuity 1e-6, `linear_upwind`, momentum BiCGSTAB 1e-10/1e-8, pressure CG
1e-10/1e-8. Errors over 0.50 L ≤ x_c ≤ 0.85 L. Metrics: velocity L1/L2/Linf
(vs exact u and v = V); dp/dx relative error = the **integral wall-shear
error** (force balance dp/dx·H = μ(u′(H) − u′(0)), no wall momentum flux
since u = 0 there); **suction-wall shear** from the near-wall solution
(quadratic through the wall and the two top cells, mean over the developed
columns); mass flow at every face column.

The **acceptance gate was fixed before the final comparison**:
`acceptance_gate.md` (design: 48 × 24, both walls, r = 1.2 — the common
growth limit — chosen before knowing the outcome; thresholds ≤ 0.75× on
both wall metrics, ≤ 1.0× velocity L2, convergence + conservation).

## 11. Uniform vs graded (equal cell count 48 × 24) — THE GATE (`logs/06`)

| quantity | uniform 48×24 | graded 48×24 (both, 1.2) | graded / uniform | gate |
|---|---|---|---|---|
| first-cell centroid distance y1 | 0.02083 | 0.00632 | 0.30 | — |
| dp/dx (integral wall shear) rel. error | 3.2287e-2 | 9.4032e-3 | **0.291** | ≤ 0.75 ✓ |
| suction-wall shear rel. error | 1.7452e-1 | 6.6716e-2 | **0.382** | ≤ 0.75 ✓ |
| velocity L2 | 3.1002e-2 | 6.8090e-3 | **0.220** | ≤ 1.0 ✓ |
| velocity L1 / Linf | 2.073e-2 / 9.615e-2 | 5.026e-3 / 1.830e-2 | 0.242 / 0.190 | — |
| converged / finite | yes / yes | yes / yes | | ✓ |
| global mass imbalance | 7.46e-14 | 3.19e-12 | | ≤ 1e-6 ✓ |
| max face-column flow error | 3.10e-11 | 4.18e-11 | | ≤ 1e-6 ✓ |

Also verified (`logs/06`): x + y grading (x left 1.05 + y both 1.2):
converged, column error 5.0e-11, dp/dx error 0.24%, velocity L2 6.8e-3.
Graded + **non-orthogonal structured_quad** (graded node grid + smooth
distortion, max non-orthogonality ~15°, `non_orthogonal_corrections 1`):
dp/dx 1.81% vs 3.73%, wall shear 6.68% vs 17.4%, velocity L2 7.8e-3 vs
3.1e-2 against the same distortion of the uniform grid (0.49× / 0.38× /
0.25×); both converged and conservative.

## 12. Accuracy vs cost

| mesh | cells | iterations | wall time (s, Release) | dp/dx err | wall-shear err | velocity L2 | dp/dx err × cells |
|---|---|---|---|---|---|---|---|
| uniform 48×24 | 1152 | 2047 | 12.8 | 3.23e-2 | 0.175 | 3.10e-2 | 37.2 |
| **graded 48×24** | 1152 | 1563 | 10.3 | 9.40e-3 | 0.067 | 6.81e-3 | 10.8 |
| uniform 48×36 | 1728 | 2134 | 26.6 | 1.94e-2 | 0.125 | 1.83e-2 | 33.5 |
| uniform 48×48 | 2304 | 1619 | 31.9 | 1.29e-2 | 0.098 | 1.20e-2 | 29.7 |
| uniform 48×72 | 3456 | 1548 | 49.0 | 6.26e-3 | 0.070 | 6.29e-3 | 21.6 |

(The two 48×24 rows are the gate run, `logs/06`; the others `logs/04`;
wall times are single measurements under parallel load — indicative only.)
Answer: **yes** — at equal cell count the graded mesh needed fewer outer
iterations (0.76×) and less time (0.80×) and cut the wall errors 2.6–3.4×;
a uniform mesh needs ~3× the cells (48 × 72) to match its wall-shear and
velocity errors, at ~4.8× the wall time. (x-resolution is not limiting:
uniform 72 × 36 ≈ 48 × 36.)

## 13. Conservation

Every graded validation run: inlet = outlet = 1.000000000 (U H), max
face-column error ≤ 9.9e-11, global mass imbalance ≤ 1.4e-11 (grid study,
gate, x+y, structured_quad) — the production tolerance is 1e-6. Grading does
not degrade conservation.

## 14. Grid convergence (`logs/06`)

One grading law (both walls, r^m = 1.2^12): 32×16 (r = 1.31453),
48×24 (1.2), 72×36 (1.12924); refinement ratio 1.5; NUM-005
`runGridConvergenceStudy` (report:
`results/validation/production/channel_transpiration_graded_grid_convergence.json`).

| grid | h | dp/dx err | wall-shear err | velocity L2 | iterations |
|---|---|---|---|---|---|
| 32×16 | 0.0884 | 1.7890e-2 | 9.641e-2 | 1.3758e-2 | 1538 |
| 48×24 | 0.0589 | 9.4032e-3 | 6.672e-2 | 6.8090e-3 | 1563 |
| 72×36 | 0.0393 | 4.2114e-3 | 4.560e-2 | 3.1302e-3 | 1452 |

* Monotone decrease of every error (asserted).
* Observed order vs the exact solution, per pair: dp/dx 1.59, 1.98;
  velocity L2 1.74, 1.92; wall shear 0.91, 0.94.
* NUM-005 triplet (from the differences): dp/dx p = 1.21, velocity
  p = 1.57, wall shear p = 0.84 — all **monotonic_not_asymptotic** (ratio
  outside 1 ± 0.1). The Richardson extrapolates are negative (e.g.
  −3.97e-3 for an error whose limit is 0), i.e. **not valid** here; GCIs
  (1.4–2.4) are reported but not meaningful outside the asymptotic range.
  Not forced: the solution is monotonically convergent, approaching the
  formal second order for dp/dx and velocity, while the near-wall
  reconstruction converges at first order.

## 15. Regression results

New tests: 45 — grading mathematics 18 (`MeshGrading` 14, `AxisSpacing` 4),
graded mesh 5 (`GradedMesh`), case format 14 (`GradedMeshCase`),
production path 5 (`GradedMeshProductionCase`), GUI 3
(`CaseEditingTest.Grading*` / `UngradedCase*`). All P12-MESH-001 tests
preserved and passing.

Focused, in the required order (`logs/08`; Release unless noted):

| stage | tests |
|---|---|
| 1 grading mathematics | 18 / 18 |
| 2 mesh construction / quality | 82 / 82 |
| 3 case reader / builder / writer / export | 145 / 145 |
| 4 CLI / production path (incl. graded + quad production cases, ProjectRunner) | 20 / 20 |
| 5 GUI / controller (Debug, Qt) | 45 / 45 |
| 6 Poiseuille / channel validation | 22 / 22 (7 pre-existing `DISABLED_` not run) |
| 7 grid convergence | 23 / 23 |
| 8 SIMPLE / physics | 618 / 618 (14 pre-existing `DISABLED_` not run) |

Full regression:

* Release (`build/release`, GUI off): **1712 / 1712 passed**, 1737 listed,
  25 disabled (the pre-existing `DISABLED_` set), 134 s (`logs/09`).
* Debug + GUI (`build/debug`): **1757 / 1757 passed**, 1782 listed, 25
  disabled, 895 s (`logs/10`). Build warnings: only the three pre-existing
  `-Wconversion` warnings in `SimulationControllerEditing.cpp` (lines
  86–89; not MESH-002 lines — shifted by the added includes).
* `clang-format-18 --dry-run --Werror` on every changed/new C++ file: clean.
* Backward-compatibility CLI comparison: 26 / 26 + 1 byte-identical (§7).
* Regenerated `results/validation/**` files: runtime-only differences
  reverted; the two MESH-001 distorted MMS reports keep the MESH-001 numbers
  (orders 2.141 / 2.278 / 1.678 unchanged). New report:
  `results/validation/production/channel_transpiration_graded_grid_convergence.json`.
* NOT run here: ASan, clang-tidy and CI (no commit authorized, so no CI run
  exists for this work).

## 16. Failed experiments / findings

1. **Plain Poiseuille does not benefit from wall clustering** (§10,
   `logs/02`) — integral wall-shear error grows with r at equal cell count;
   explained analytically. Not a mesh or scheme defect; documented.
2. **One-sided clustering at the boundary-layer wall** (top only,
   preliminary 32×16, `logs/03`): wall shear improves strongly (0.27–0.22×
   at r = 1.2–1.3) but velocity L2 gets worse (1.05–1.8×) because the
   opposite-wall cells become large (max/min up to 51) — the reason the gate
   design clusters both walls.
3. **x-grading changes dp/dx noticeably** (x left 1.05: 0.24%, x both 1.05:
   3.0% vs 0.94% y-only): the developed-region fit sees x-cell sizes 2–10×
   different; recorded, not investigated further (x-grading is verified for
   execution/conservation/accuracy bounds only).
4. Test-authoring slips caught by the tests themselves (fixed in the tests,
   not the code): expected volumes for a uniform axis taken from node
   differences instead of `L/n`; an invariant sweep including a
   distribution the library correctly rejects (r = 1.5 over 64 cells);
   a GUI test ratio (50 over 4 cells) that is still valid.

## 17. Limitations

* Geometric grading only (no tanh/spline laws, no per-block grading); one
  law per axis; `structured_cartesian` only (structured_quad: grade the
  explicit vertices — verified compatible, §11).
* The wall-shear metric is a post-processing reconstruction (first order
  under refinement); dp/dx carries the solver-consistent integral wall shear.
* Validation is laminar (Re_w = 20); turbulent wall-resolved grading
  (y+ targets) is not part of this task.
* GUI grading controls are verified at controller level and by qmllint;
  not visually inspected.
* Wall times are single, parallel-load measurements.

## 18. Final acceptance-gate decision

**P12-MESH-002 — COMPLETE.** Every element of the final gate is met by
evidence:

| requirement | evidence |
|---|---|
| production-case grading works | committed graded case: CaseReader → CaseBuilder → ProjectRunner → export (§8, §11) |
| x and y grading work | unit/mesh tests (§4, §9); x+y production run (§11) |
| wall/boundary clustering works | left/right/bottom/top/both (§9); gate case (§11) |
| invalid grids rejected | §6 (library, parser, CaseReader, MeshQuality) |
| uniform defaults preserved | bit-identical builder (960 meshes), 26/26 + 1 CLI byte-identical (§7) |
| CLI production path works | §8 |
| GUI supported | mesh editor + 3 controller tests + qmllint (§8; visual inspection NOT VERIFIED) |
| graded wall accuracy better than uniform at comparable cost | gate (fixed in advance): 0.291× / 0.382× / 0.220× at equal cells, 0.76× iterations, 0.80× time (§11, §12) |
| conservation within tolerance | ≤ 9.9e-11 face-column error, ≤ 1.4e-11 imbalance vs 1e-6 (§13) |
| grid-convergence evidence | §14 (monotone; not asymptotic — reported as such) |
| focused tests pass | §15 |
| full regression passes | 1712 / 1712 Release, 1757 / 1757 Debug + GUI (§15) |
