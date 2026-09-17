# P12-MESH-005 — 3D Foundation

Status: **`[x]` COMPLETE (2026-09-15)**. The unchanged pre-registered gate passes on the final
sources (30/30 order items of the 8³–64³ MMS study). The full regression passes: Release
1825/1825, Debug + GUI 1875/1875. Every 2D case is byte-identical to the pre-MESH-005 tree (§29).
Not committed, not pushed.

Pre-registered gate: [acceptance_gate.md](acceptance_gate.md), written before implementation and
before any verification run. It has one clarification, also written before any run (C3 zero-flux
clause, see §16). No threshold, level, norm or mask was changed after a run.

---

## 1. Authorization

P12-MESH-005 "3D Foundation only" was authorized on 2026-09-15, subject to a mandatory prerequisite
(§2). Scope: extend the existing face-based production mesh/numerical infrastructure from 2D to a
rigorous Cartesian-hexahedral 3D foundation, with independently verified 3D operators. The
authorization explicitly excludes:

- 3D SIMPLE, w-momentum coupling, 3D Rhie–Chow, 3D cavity;
- 3D turbulence, and 3D thermal-fluid integration beyond operator verification;
- polyhedra, tetrahedra, unstructured meshes, CAD, AMR, moving meshes;
- P12-MESH-006 and later work;
- any commit or push.

None of these was started.

## 2. Prerequisite verification ([logs/01](logs/01_prerequisite_verification.log))

P12-MESH-001–004 are `[x]` COMPLETE in TODO.md. The MESH-004 evidence was checked item by item
before any MESH-005 change:

| MESH-004 condition | evidence | result |
| --- | --- | --- |
| unchanged acceptance gate PASS | `results/p12-mesh-004/logs/29_gate_evaluation_rerun.log`: "every gate criterion PASSES … acceptance_gate.md unchanged" | PASS |
| focused tests PASS | `logs/26`: ran 831, passed 831, failed 0 | PASS |
| backward compatibility PASS | `logs/25`: NEW vs PREFIX identical on all 22 cases; the only 4 DIFFERENT lines are HEAD vs working tree on `heated_cavity` / `heated_species_diffusion` — MESH-003's documented thermal fix | PASS |
| full regression PASS | Release 1787/1787, Debug + GUI 1837/1837 (`logs/27`, `logs/28`) | PASS |
| real evidence | 33 logs, solver-robustness 7 entries, data 12 files | PASS |
| TODO accurately `[x]` | MESH-004 block: 17 `[x]`, 0 `[ ]` | PASS |

All six hold, so MESH-005 started.

## 3. Starting tree ([logs/00](logs/00_baseline_working_tree.log))

- HEAD `b66310c` (P12-NUM closeout). MESH-001–004 are complete but uncommitted.
- 159 status entries (119 modified, 40 untracked); `git diff --stat`: 119 files changed, +6949/−2917.
- Pre-MESH-005 reference: the whole working tree (tracked and untracked, `build/` excluded) was
  copied to `$HOME/m5ref/base` and built there in Release. Its 287 `src`/`include` files are
  hashed in `$HOME/m5ref/base.src.sha256`. This build is the BASE of every 2D comparison below.
  The hashes were re-checked when the work resumed: all 287 still match (`sha256sum -c`).
- **Resumed in a second session** ([logs/05](logs/05_session_resume_state.log)). The first session
  ended after the focused tests and before the full regression. The resume found one source edit
  after the last verification run: a comment-only change in `src/discretization/Gradient.cpp`
  documenting the 3×3 least-squares singularity guard. Both builds were redone on the final sources
  (Release: 0 warnings). Every check whose binary predated that edit was then repeated on the
  final binaries: the focused stages and the gate study (logs/17), the 2D bit identity (logs/20)
  and the CLI comparison (logs/21).

## 4. Architecture decision

**One mesh, one field system, one operator set, for both dimensions. No parallel `Mesh3D` stack.**

The audit (Mesh, Cell, Face, BoundaryPatch, the fields, MeshGeometry, MeshQuality, interpolation,
gradients, diffusion, convection, the energy/sparse assembly, VTK, CaseReader/CaseBuilder/ProjectRunner)
found that the numerical infrastructure is already face-based and dimension-independent in
structure:

- cells list face ids; faces carry owner/neighbour, centroid and area vector;
- every operator loops over faces and uses distances, dot products, areas and volumes.

The only thing fixing it to 2D was the two-component geometry vector, plus a handful of places
that use it in a genuinely two-dimensional way:

- the 2D "cross product" parallelism tests;
- the 2×2 least-squares solve;
- skew-vector zero tests on x and y only;
- the 2D-only algorithms: momentum (u, v), pressure correction, velocity gradient, wall distance,
  vorticity, and the 2D writers.

Generalizing the primitive therefore generalizes every dimension-independent operator. A separate
3D stack would have duplicated all of them, and nothing in the audit made one necessary.

What was **not** generalized (it is 2D by construction, and is guarded instead, §5):

- the incompressible/compressible flow path (momentum, pressure correction, SIMPLE/PISO);
- turbulence (velocity-gradient tensor, wall distance);
- vorticity;
- the 2D result writers (CSV, JSON metadata, `writeSolution`) and restart.

These are P12-MESH-006 scope.

## 5. Dimension-generalization strategy

| change | file(s) | 2D behaviour |
| --- | --- | --- |
| `Vector3 {x, y, z}` is the one geometry/field vector; `using Vector2 = Vector3;` keeps every 2D caller compiling; `cross()` and `isFinite()` added | `core/Vector3.hpp`, `core/Vector2.hpp` | `Vector2{x, y}` has z = 0. `dot` skips an exactly-zero z product, so a 2D dot product is bitwise the former two-term formula, including a −0.0 result that IEEE `+0.0` addition would turn into +0.0. Tested over 6561 combinations with signed zeros (`Vector3Test`). |
| `Mesh::dimension()` (3 iff some face area vector has z ≠ 0, computed at construction); `requireTwoDimensional()` | `mesh/Mesh.{hpp,cpp}` | every 2D builder: 2 (A8) |
| `StructuredGrid::nz` (0 = 2D), `vertex(i, j, k)`, `cellCount()`, `vertexCount()`; Mesh validates grid dimension against the geometry | `mesh/StructuredGrid.hpp`, `Mesh.cpp` | `StructuredGrid{nx, ny, v, name}` unchanged |
| parallelism tests `cross(d, Sf) == 0` (all components) | `MeshGeometry.cpp` (decomposition, crossing), `Gradient.cpp` (paired boundary fit, oblique Neumann) | for 2D vectors the x, y components are ±0 and z is the former 2D formula, so every decision is unchanged |
| skew-vector tests include z | `Gradient.cpp`, `Interpolation.cpp`, `VectorGradient.cpp` | z = 0 |
| least-squares: a 3×3 weighted normal-equation solve (adjugate; guard det < 1e-10·trace³) when any displacement has z ≠ 0, else the unchanged 2×2 path | `Gradient.{hpp,cpp}` | planar displacements take the old code |
| finiteness checks include z | `Cell.cpp`, `Face.cpp`, `MeshQuality.cpp` | z = 0 is finite |
| `MeshQuality`: a cell needs ≥ dimension + 1 faces; locations print z when it is non-zero | `MeshQuality.{hpp,cpp}` | threshold 3 and messages unchanged |
| fingerprint hashes z for 3D meshes only | `MeshFingerprint.cpp` | 2D fingerprints unchanged (golden values in `Cartesian3DMeshTest.FingerprintSeesZIn3DAndKeeps2DValues`); restart files stay valid |
| `VectorErrorNorms::z`; the magnitude includes e_z | `validation/ErrorNorms.{hpp,cpp}` | e_z² = +0 adds nothing: identical |
| `VTKWriter::writeCellFields` — generic structured export: VTK_HEXAHEDRON (12) for 3D, VTK_QUAD for single-grid 2D | `io/VTKWriter.{hpp,cpp}` | `writeSolution` untouched |
| **guards** (`requireTwoDimensional`, `InvalidArgumentError` naming the component) | the 7 public momentum assemblers; pressure correction (5); `computeVelocityGradient`; the (u, v) skew-corrected interpolation; `computeWallDistance`; `vorticity2D`; `CSVWriter::writeFields`; `VTKWriter::writeSolution`; `inferStructuredMeshInfo` (also covers the JSON metadata); restart snapshot make/validate | never throw on a 2D mesh |
| `createCartesian3D` | `MeshGeometry.{hpp,cpp}` | new builder |

2D identity was checked bit for bit, not to round-off (§24).

## 6. 3D mesh representation

`MeshGeometry::createCartesian3D(nx, ny, nz, Lx, Ly, Lz, origin = 0)` builds the ordinary `Mesh`
(cells, faces, patches) plus its `StructuredGrid` (nz > 0):

- **cells:** `cell(i, j, k) = (k·ny + j)·nx + i`; centroid = box centre, volume = dx·dy·dz
  (dx = Lx/nx, …; per-axis arithmetic is `AxisSpacing::uniform`, the 2D builder's, plus the origin);
- **faces:** each created once. First the x-faces (k, j, i = 0..nx), then the y-faces, then the
  z-faces. An internal face's owner is the lower-index cell and its area vector points toward +axis;
  a boundary face's points out of the domain;
- **cell face lists:** in the canonical order west, east, south, north, bottom, top (this falls out
  of the creation order, as left/right/bottom/top does in 2D);
- **patches:** `xmin`, `xmax`, `ymin`, `ymax`, `zmin`, `zmax`;
- **vertex grid:** `vertex(i, j, k) = origin + (i·dx, j·dy, k·dz)`, export only (the numerics never
  read it);
- **input validation:** nx, ny, nz ≥ 1; finite positive extents; finite origin.

Only uniform Cartesian hexahedra: no graded, polyhedral or unstructured 3D meshing (out of scope).

## 7. Volume verification (A2)

`Cartesian3DMeshTest.CellVolumesAreBoxVolumesAndSumToTheDomain` covers the gate meshes:

- M1 1×1×1 unit cube;
- M2 2×1×1, M3 2×2×1, M4 2×2×2;
- M5 3×4×5 cuboid, 1.5 × 0.7 × 2.3, translated to origin (−1.2, 0.4, 3.1), non-cubic cells;
- M6 8×8×8.

Every V_P is finite, positive, and within 1e-14 relative of dx·dy·dz; the sum is within 1e-12 of
Lx·Ly·Lz. Measured maxima are in §24a.

## 8. Area-vector verification (A3)

`Cartesian3DMeshTest.AreaVectorsHaveTheRightMagnitudeDirectionAndClosure`, on M1–M6:

- every face is axis-aligned (two components exactly 0), and \|Sf\| equals its rectangle's area
  (dy·dz, dx·dz, dx·dy) to 1e-14;
- boundary Sf points out of the domain; internal Sf points owner → neighbour, toward +axis;
- per-axis face counts match A1;
- per cell, \|Σ±Sf\| ≤ 1e-13·Σ\|Sf\| (measured: exactly 0, §24a).

## 9. Centroid verification (A4)

`Cartesian3DMeshTest.CentroidsAreBoxAndRectangleCentres` checks:

- cell centroids equal the box centres to 1e-14·L and lie strictly inside the box;
- face centroids lie on their plane, half a cell from the owner, at the rectangle centre;
- for every internal face, x_N − x_P is exactly parallel to Sf (cross product exactly 0), and the
  face plane cuts P–N at t = ½ to 1e-14.

## 10. Connectivity (A1, A5, A6)

Counting formulas, derived by counting planes of faces:

- faces = (nx+1)·ny·nz + nx·(ny+1)·nz + nx·ny·(nz+1);
- interior = (nx−1)·ny·nz + nx·(ny−1)·nz + nx·ny·(nz−1);
- boundary = 2(ny·nz + nx·nz + nx·ny);
- vertices = (nx+1)(ny+1)(nz+1), cells = nx·ny·nz.

They are cross-checked by interior + boundary = faces and by the Euler characteristic of a ball,
V − E + F − C = 1.

On M1–M6:

- every cell has exactly 6 distinct valid faces in canonical order;
- each face appears in exactly 1 (boundary) or 2 (internal) face lists — exactly its
  owner/neighbour;
- the owner is the lower index; there are no duplicate (owner, neighbour) pairs;
- two builds are bitwise identical with equal fingerprints;
- the documented face numbering is checked face by face.

Tests: `TopologyCountsMatchIndependentFormulas`,
`EachCellHasSixFacesInCanonicalOrderAndEachFaceItsOwners`,
`BuildIsDeterministicAndFollowsTheDocumentedNumbering`.

## 11. Boundary patches (A7)

On M1–M6:

- six patches, in order, with counts ny·nz, ny·nz, nx·nz, nx·nz, nx·ny, nx·ny;
- every boundary face in exactly one patch, no internal face in any;
- each patch's faces lie on its plane with the outward axis normal, and the patch area equals the
  domain side to 1e-13;
- `boundaryPatchNameForFace` agrees.

`MeshQuality::evaluate` reports every 3D mesh `valid` with no warning:

- non-orthogonality 0, skewness 0, expansion ratio 1;
- aspect ratio exactly max(dx,dy,dz)/min(dx,dy,dz) (face-area ratio, §28);
- minimum volume dx·dy·dz.

The 2D patches (`left/right/bottom/top`, named multi-block patches) are unchanged (§24).

## 12. Scalar fields (B1)

`Fields3DTest.ScalarFieldsSampleLinearFieldsWithExactVolumeMeans` uses the same `ScalarField` type
as 2D; there is no 3D field class. Constant, x, y, z and x + 2y + 3z are sampled on M5. Each
volume-weighted mean equals the value at the domain centre to 1e-13 relative (the midpoint rule is
exact for linear fields). This checks centroids and volumes together on a translated, non-cubic
mesh, and z genuinely varies (range 1.84 = 2.3 − dz).

## 13. Vector fields (B2)

**Decision: always three components** (u, v, w), not a dimension-aware size. `VectorField =
Field<Vector3>`: on a 2D mesh w = 0 and every 2D result is unchanged; on a 3D mesh the same type
carries w. So no operator needs a second signature.

Cost: 24 instead of 16 bytes per vector (reported in §24). Scalar fields and matrices are
unaffected.

`Fields3DTest.VectorFieldsCarryThreeComponents` covers:

- zero and constant initialisation, access and assignment of all three components;
- magnitude √(4 + 9 + 36) = 7 exactly, sizing and checked access;
- copy and move preserve w; +, −, ×, ÷ and the compound operators act on w (size mismatch rejected);
- a 2D-constructed value has w = 0;
- the vector error norms see w (`VectorErrorNorms::z`).

## 14. Interpolation (C1)

`Operators3DTest.InterpolationIsExactForConstantAndLinearFields`, on M5 and M6:

- a constant is exact on every face;
- 1 + x − 2y + 3z is exact to 1e-12 on every internal x-, y- and z-directed face (each class
  non-empty), and equals the prescribed value on boundary faces;
- a 3-component linear vector field is exact to 1e-12 on internal faces.

## 15. Gradients (C2; order in §23)

`Operators3DTest.GradientsAreExactForLinearFieldsInEveryCell`, for the production `gradient()` with
both GreenGauss (skew-corrected, paired boundary fit) and LeastSquares (now 3×3 in 3D):

- constant → (0, 0, 0); x, y, z → unit vectors; 2x − 3y + 4z → (2, −3, 4);
- to 1e-9 per component in every cell, including boundary, edge and corner cells;
- on M1–M6 with exact Dirichlet data, and on M5/M6 also with exact Neumann (`FixedGradient`) data.

`LeastSquaresPrimitiveSolvesTheThreeByThreeSystem` covers `solveLeastSquaresGradient`: 5 non-coplanar
3D displacements recover (0.7, −1.3, 2.1) to 1e-12, and coplanar 3D displacements (rank 2) are
reported ill-conditioned with a {0,0,0} placeholder.

## 16. Diffusion (C3; order in §23)

- **Explicit operator, exactness.** `diffusion(φ, Γ = 1.7)` of φ = x² + 2y² + 3z² + xy − yz + 2xz − x + 1
  gives 1.7·12 = 20.4 in every cell to 1e-8. Meshes: 6×5×4 on M5's translated domain, and M6.
- **Telescoping** (clarified C3). Σ V_P·diffusion_P = Γ∮∂φ/∂n dA = 20.4·|Ω| to 1e-10 relative, so
  every internal flux cancels pairwise.
- **Zero flux.** A constant field with `FixedGradient(0)` on all six sides gives Σ V_P·diffusion_P = 0
  exactly.
- **Clarification** (acceptance_gate.md, written before any run). As first written, the zero-flux
  clause could not hold for a non-constant field. `diffusion()` rebuilds a Neumann face's flux from
  the 4-point cubic fit through `boundaryValue(φ_P, d)`, which is zero only for a field constant
  along the normal. This is a pre-existing property of the explicit operator, in 2D as well; the
  implicit assemblies impose prescribed fluxes exactly. The clause is therefore evaluated as the 2D
  test `ZeroFluxBoundaryConservesGlobally` evaluates it, and the stronger telescoping check was
  added.
- **Poisson solve** (implicit assembly + linear solve): §22–23.

## 17. Convection (C4; order in §23)

`DivergenceFreeMassFluxBalancesInEveryCell`: the mass flux of u = (1 + y/2, 1 + z/2, 1 + x/2),
ṁ = ρ u(x_f)·Sf (exact for linear u), balances to 1e-13 relative in every cell of M1–M6.

`ConvectionPreservesAConstantWithEveryScheme`: upwind, central, linear_upwind and quick all give
\|conv\| ≤ 1e-10 on M5 and M6. For a constant field every limited scheme falls back to the upwind
value exactly.

`HigherOrderConvectionIsExactForALinearFieldAwayFromTheBoundary`: for 1 + x − 2y + 3z,
central/linear_upwind/quick give ρ u(x_P)·∇φ to 1e-9 in the 64 cells of M6 more than 2 layers from
the boundary. The derivation is exact: each component of u is constant along its own direction, so
the face-centroid products telescope without quadrature error.

No scheme was added: the four existing dimension-independent schemes only.

## 18. Sparse assembly (D)

`SparseAssembly3DTest` covers the production `thermal::assembleEnergyEquation` (diffusion + upwind
convection + source; k = 2, cp = 1, Q = 5):

- M1: [[24]], rhs 29;
- M2: [[20, −4], [−4, 20]], rhs 18.5, 18.5;
- M2 with a +x mass flux 3·A and a `FixedGradient(0)` outlet: [[23, −4], [−7, 15]], rhs 21.5, 10.5;
- M3: diagonal 14, −2 to the x- and y-neighbour, rhs 11.25;
- M4: diagonal 9, −1 to the neighbours id^1, id^2, id^4, rhs 6.625.

Each is derived by hand in the test's comments from D_f = k·A_f/d. For each, the checks are:

- the CSR sparsity pattern equals the hand pattern exactly (sorted, no duplicate, every index valid);
- every value and rhs is within 1e-13 relative;
- two assemblies are bitwise identical.

The measured matrices are printed in §24a: they are exactly the hand values.

## 19. VTK (E)

`VTK3DTest` parses the written file back:

- header; (nx+1)(ny+1)(nz+1) points, each equal to origin + (i·dx, j·dy, k·dz) to 1e-15 relative;
- n cells of `8` + 8 ids in VTK_HEXAHEDRON order, and CELL_TYPES all 12;
- each hexahedron's volume and centroid, from a 6-tetrahedron decomposition of the written points,
  equal the mesh cell's to 1e-14;
- scalar and 3-component vector cell data round-trip bitwise;
- a hand-checked 2×1×1 connectivity;
- non-finite, wrongly sized or badly named fields, and a mesh without a single grid, are rejected
  before any file is written.

No ParaView inspection was used as evidence.

## 20. MMS definition (F)

Unit cube, per-face exact Dirichlet data.

- φ = sin(πx)·cos(πy/2)·exp(z/2) + x·y·z
  - ∇φ = (π cos πx cos(πy/2) e^{z/2} + yz, −(π/2) sin πx sin(πy/2) e^{z/2} + xz, ½ sin πx cos(πy/2) e^{z/2} + xy)
  - ∇²φ = (¼ − 5π²/4) sin πx cos(πy/2) e^{z/2} (∇²(xyz) = 0).
- u = (1 + y/2, 1 + z/2, 1 + x/2), div u = 0, every component ≥ 1.
- ψ = exp(0.3x + 0.5y + 0.7z); u·∇ψ = ψ(1.5 + 0.35x + 0.15y + 0.25z).
- Forcing: Poisson −k∇²φ = Q (k = 1); convection–diffusion ρcp u·∇φ − k∇²φ = Q (ρ = cp = 1, k = 0.1).

This is genuinely 3D, not a 2D field extruded in z: every field depends on z, and u has a z
component that varies with x.

## 21. Independent source verification (F0)

`MMS3DTest.ForcingMatchesFiniteDifferencesOfClosedForms` compares, at 20 fixed pseudo-random points
in the cube, against 4th-order central differences (step 1e-3) of the closed-form φ, ψ and u:

- ∇φ, ∇²φ, div u, u·∇ψ;
- both forcings.

Worst deviation: **4.3e-10**, against a bound of 1e-7 relative, measured against max(1, \|value\|)
([logs/10](logs/10_mms3d_refinement_study.log)). No forcing was computed with a discrete operator
of the code under test.

## 22. Refinement study ([logs/10](logs/10_mms3d_refinement_study.log), [data/](data/))

`MMS3DTest.DISABLED_OperatorRefinementStudy` ran in Release with n = 8, 16, 32, 64 (up to 262 144
cells), h = representativeGridSize(1, n³, 3) = 1/n. Every solve converged:

| level | Poisson: status, outer iterations | transport: status, outer iterations | level wall time |
| --- | --- | --- | --- |
| 8³ | Converged, 3 | Converged, 2 | 0.0 s |
| 16³ | Converged, 3 | Converged, 3 | 0.1 s |
| 32³ | Converged, 3 | Converged, 3 | 1.1 s |
| 64³ | Converged, 3 | Converged, 3 | 16.8 s |

Settings: BiCGSTAB rel 1e-10, abs 1e-14, ≤ 20000 iterations; outer tolerance 1e-10.

Reports: `data/mms3d_operator_study.{json,md,txt}` (the JSON/Markdown via the P12-NUM-006 report
writer). The staged focused run (§25) repeated the study with identical errors.

## 23. Observed spatial orders

p = ln(E_c/E_f)/ln(h_c/h_f). The gate is on the finest pair 32³→64³, threshold 0.75 × expected, with
E decreasing at every refinement.

| quantity | expected | L1 errors 8→64 | p (L1) | p (L2) | p (L∞) | finest p (L1 / L2 / L∞) | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| F1 GG gradient | 2 | 3.54e-2 → 6.72e-4 | 1.83 1.92 1.96 | 1.82 1.92 1.96 | 1.76 1.94 1.98 | 1.96 / 1.96 / 1.98 | PASS |
| F2 LSQ gradient, interior | 2 | 3.80e-2 → 6.81e-4 | 1.87 1.95 1.98 | 1.84 1.94 1.97 | 1.67 1.91 1.97 | 1.98 / 1.97 / 1.97 | PASS |
| F3 LSQ gradient, global | L1 2, L2 1.5, L∞ 1 | 4.55e-2 → 7.43e-4 | 1.98 1.98 1.98 | 1.95 1.93 1.88 | 1.94 1.05 0.99 | 1.98 / 1.88 / 0.99 | PASS |
| F4 explicit diffusion | 2 | 7.11e-2 → 1.11e-3 | 2.01 2.00 2.00 | 1.99 1.99 2.00 | 1.90 1.97 1.99 | 2.00 / 2.00 / 1.99 | PASS |
| F5 Poisson solve | 2 | 2.51e-3 → 3.77e-5 | 2.05 2.01 2.00 | 2.02 2.00 2.00 | 1.99 1.99 2.00 | 2.00 / 2.00 / 2.00 | PASS |
| F6 convection, upwind | 1 | 1.34e-1 → 1.79e-2 | 0.95 0.97 0.99 | 0.96 0.98 0.99 | 0.90 0.95 0.98 | 0.99 / 0.99 / 0.98 | PASS |
| F7 central, interior | 2 | 1.72e-3 → 2.78e-5 | 1.98 1.99 1.99 | 1.95 1.97 1.98 | 1.58 1.79 1.90 | 1.99 / 1.98 / 1.90 | PASS |
| F7 linear_upwind, interior | 2 | 6.51e-3 → 1.10e-4 | 1.94 1.97 1.98 | 1.91 1.95 1.97 | 1.54 1.77 1.89 | 1.98 / 1.97 / 1.89 | PASS |
| F7 quick, interior | 2 | 4.12e-3 → 6.91e-5 | 1.94 1.97 1.98 | 1.92 1.95 1.98 | 1.55 1.78 1.89 | 1.98 / 1.98 / 1.89 | PASS |
| F8 convection–diffusion solve (upwind) | 1 | 4.91e-2 → 7.80e-3 | 0.89 0.87 0.90 | 0.81 0.84 0.90 | 0.72 0.84 0.91 | 0.90 / 0.90 / 0.91 | PASS |

All 30 gated (quantity, norm) items pass, and every gated error decreases at every refinement.

Reported, not gated (as pre-registered):

- F7 global norms: central L1 p ≈ 0.97, L2 ≈ 0.51, L∞ ≈ −0.03 (no convergence); linear_upwind and
  quick alike. This is the documented O(1) error in the boundary band where the limited schemes
  degrade to upwind (P12-NUM-001 measured a global order of ≈ 0.5 in 2D). The 3D behaviour
  reproduces it.
- The LSQ global L2 order (1.88) is above the asymptotic 1.5. The O(h) boundary layer's share of
  the L2 norm is still growing at these resolutions, and its L∞ has already reached the predicted 1
  (0.99).
- F8 is first order, approaching 1 from below (0.81 → 0.90 in L2), as in 2D (P12-NUM-006: 0.83 →
  0.95).

## 24. Two-dimensional backward compatibility (G)

- **Bit identity of every touched 2D quantity** ([logs/02](logs/02_2d_bit_identity_probe.log);
  probe `tools/bitprobe.cpp`). The pre-MESH-005 and final libraries were probed on 18 meshes: two
  Cartesian meshes and the mesh of every committed case (Cartesian, graded, distorted
  structured_quad, 4 multi-block). Hashed quantities:
  - FNV-1a over the exact bits of all geometry: unit normals, non-orthogonal decompositions,
    angles, crossings, skewness;
  - GG/LSQ gradients, plain and corrected diffusion, interpolation, all four convection schemes,
    the assembled energy system, vector and skew-corrected interpolation, vector error norms — each
    with Dirichlet and Neumann data;
  - fingerprints and the quality summaries.

  Verdict: **BITWISE IDENTICAL**. It was also run right after the core change (before tests were
  written), with the same result. It was run a third time on the final library
  ([logs/20](logs/20_2d_bit_identity_probe_final.log)): BITWISE IDENTICAL on all 18 meshes, and the
  NEW output is byte-identical to the one recorded in logs/02.
- **Restart compatibility — 2D fingerprints** ([logs/06](logs/06_fingerprint_golden_check.log)). The
  golden values in `Cartesian3DMeshTest.FingerprintSeesZIn3DAndKeeps2DValues` (`f6fc7c6e993bc9ac`,
  `a267b75d368102a4`) were recomputed independently with the pre-MESH-005 library (tool
  `tools/fingerprint_probe.cpp`); both match. The fingerprint that restart files store is
  therefore unchanged for 2D meshes.
- **CLI, every committed case and fixture** ([logs/12](logs/12_backward_compat_cli.log)). BASE
  (pre-MESH-005 tree) vs NEW: 16 committed cases and 11 CLI fixtures — fields.csv, solution.vtk and
  residuals.csv byte-identical, metadata.json equal, stdout identical, exit codes equal. **27/27
  IDENTICAL.** Repeated with the final CLI ([logs/21](logs/21_backward_compat_cli_final.log)): **27/27
  IDENTICAL**. The final `cfdapp` has the same sha256 (`bb4beb91ffce66b0`) as the one tested in
  logs/12, so the comment edit of §3 did not change the CLI binary.
- **Existing 2D case files:** none modified by MESH-005
  ([logs/14](logs/14_g4_case_inputs_and_change_set.log)). Every file of `cases/` and `tests/data/`
  (outputs excluded) is identical to the pre-MESH-005 snapshot; `cases/` differs only by two empty
  directories that the snapshot copy could not create. The case format is unchanged.
- **MESH-001–004 suites by name:** all pass (§25, stages 17a–17n).
- **Full regression:** §26.
- **Performance of the 3-component vector** (reported, not gated; [logs/13](logs/13_performance_2d_cli.log),
  `tools/perf.sh`).
  - Setup: the same 2D committed cases were run with the BASE CLI (pre-MESH-005) and the final
    CLI, alternating, 5 repetitions each. Both Release -O3 (GCC 11.4.0), with identical CMake
    configuration and compile flags. OMP_NUM_THREADS=1, i9-14900HX laptop, WSL2.
  - Metric: CPU time (user + sys, getrusage), which is immune to WSL wall-clock jumps. Wall times
    agree within 1–2%.
  - A single-run sizing pass ([logs/13b](logs/13b_performance_sizing_single_run.log)) chose the
    cases. `heated_cavity` (≈5 ms, one SIMPLE iteration) is excluded, because process start-up
    dominates it.

  | case (2D) | BASE CPU min / median [s] | NEW CPU min / median [s] | NEW/BASE min / median |
  | --- | --- | --- | --- |
  | lid_driven_cavity_40x40 | 20.69 / 21.33 | 20.98 / 21.70 | 1.014 / 1.017 |
  | poiseuille_distorted (non-orthogonal) | 3.39 / 3.45 | 3.53 / 3.54 | 1.041 / 1.026 |
  | curved_channel_multiblock | 5.83 / 5.93 | 5.96 / 6.00 | 1.021 / 1.013 |
  | channel_transpiration_graded | 8.21 / 8.35 | 8.10 / 8.12 | 0.986 / 0.972 |
  | lid_driven_cavity_80x80 | 138.32 / 140.41 | 126.33 / 126.65 | 0.913 / 0.902 |

  The largest measured cost is on the non-orthogonal distorted Poiseuille case: +2.6% (median)
  to +4.1% (min). That case has the most per-face vector arithmetic: non-orthogonal
  decompositions, cross-product tests, and 3-component gradients.

  The 80×80 cavity is 9–10% faster with the new build, consistently in all 5 alternating pairs
  (126.3–129.3 s against 138.3–141.2 s). Both builds do identical work: the outputs, including
  `residuals.csv`, are byte-identical (§24 G3), so the iteration counts are equal. Nothing in
  MESH-005 is designed to speed up a Cartesian SIMPLE run, and the cause (possibly code layout
  or alignment in the hot loops) was not investigated. No speed-up is claimed.

  Conclusion: at this measurement's resolution, the 3-component vector costs at most about 4% on
  these 2D runs.

  One caveat: the final tree-state check (logs/22, about 1 min of file comparison) overlapped the
  first cavity 40×40 repetitions. Only the other four cases ran with nothing else on the machine.

### 24a. Measured exactness ([logs/03](logs/03_exactness_measurements.log))

The unit tests of §7–§18 are pass/fail against the pre-registered round-off bounds. The probe
`tools/exactness_probe.cpp` (production API, final Release library) records the measured worst
cases behind them:

| item | quantity | measured worst case | bound |
| --- | --- | --- | --- |
| A2 | \|V_P − dx·dy·dz\|/V, M1–M6 | 0 (every mesh) | 1e-14 |
| A2 | \|ΣV − V_domain\|/V_domain | 0; M5: 1.3e-15 | 1e-12 |
| A3 | \|\|Sf\| − A\|/A | 0 | 1e-14 |
| A3 | per-cell closure \|Σ±Sf\|/Σ\|Sf\| | 0 (exactly) | 1e-13 |
| A4 | \|x_P − box centre\|/L | 0 | 1e-14 |
| A4 | \|t − ½\| (face plane on P–N) | 0; M5: 1.0e-15 | 1e-14 |
| A7 | aspect-ratio relative error | 0 | 1e-13 |
| C1 | interpolation, constant / linear interior / linear boundary | M5: 4.4e-16 / 3.6e-15 / 0; M6: 0 / 0 / 0 | 1e-12 |
| C2 | GG gradient, max component error, Dirichlet (Neumann) | M1–M4 ≤ 1.1e-15; M5 2.8e-14 (2.8e-14); M6 4.4e-15 (4.4e-15) | 1e-9 |
| C2 | LSQ gradient, Dirichlet (Neumann) | M1–M4 0; M5 9.8e-15 (9.8e-15); M6 0 (0) | 1e-9 |
| C3 | explicit diffusion of the quadratic (exact 20.4) | 6×5×4: 8.8e-12; M6: 1.4e-14 | 1e-8 |
| C3 | telescoping Σ V_P·diffusion_P vs 20.4·\|Ω\| (relative) | 1.9e-14; 9.6e-15 | 1e-10 |
| C4 | per-cell net mass flux / Σ\|ṁ\| | 0 (exactly) | 1e-13 |
| C4 | constant field, every scheme | 0 (exactly) | 1e-10 |
| C4 | linear field, central / linear_upwind / quick, 64 inner cells | 1.0e-14 each | 1e-9 |
| D | the four assembled systems | exactly the hand values (printed row by row in the log) | 1e-13 relative |

The same probe measures the n = 2 finding of §28: explicit `diffusion()` of x² + y² (+ z²) on a
mesh with exactly two cells along x is wrong by up to 2.17. The exact values are 4 in 2D and 6 in 3D.
The same field with three cells along x is exact to 9.8e-15. [logs/04](logs/04_diffusion_two_cell_finding.log)
runs the 2D case against the pre-MESH-005 and the final library: the output is byte-identical
(2.1667 on 2×4 and 4×2; 8.0e-15 on 3×4). The defect is pre-existing, and MESH-005 leaves it unchanged.

## 25. Focused tests ([logs/11](logs/11_focused_tests_staged.log))

The stages ran in the authorized order, stopping on the first failure:

1. geometry;
2. volume;
3. area vector;
4. centroid;
5. connectivity;
6. boundary patch;
7. scalar field;
8. vector field;
9. interpolation;
10. gradient;
11. diffusion;
12. convection, plus the 2D-only guards;
13. sparse assembly;
14. VTK;
15. MMS;
16. convergence (the gate study);
17. 2D compatibility: MESH-001 distorted, MESH-002 graded, MESH-003 multi-block, MESH-004 quality
    and MMS, then whole mesh/io/discretization/thermal/field/core/validation suites.

**Ran 991, passed 991, failed 0** (stages overlap).

**Repeated on the final binaries** ([logs/17](logs/17_focused_tests_final_binaries.log), rebuilt after
the comment edit of §3): the same 17 stages in the same order, **ran 991, passed 991, failed 0**.
Stage 16 regenerated `data/mms3d_operator_study.{json,md,txt}`. Every error, order and gate line
is identical to the first run. Only the four per-level runtimes differ (JSON leaves
`runtime/levels[*]/seconds`; 64³: 16.6 s → 15.8 s).

The MESH-005 tests themselves: 39 new tests —

| suite | tests |
| --- | --- |
| Vector3Test | 5 |
| Cartesian3DMeshTest | 12 |
| Fields3DTest | 2 |
| Operators3DTest | 9 |
| SparseAssembly3DTest | 5 |
| VTK3DTest | 3 |
| MMS3DTest | 3, one of them the `DISABLED_` gate study |

## 26. Full regression (G1) and generated outputs (G5)

Both builds were redone from the final sources, then run sequentially with `ctest -j16`
(`tools/final_verification.sh`):

| build | result | disabled (not run) | wall | log |
| --- | --- | --- | --- | --- |
| Release -O3, GUI off (0 build warnings) | **100% tests passed, 0 tests failed out of 1825** | 34 | 141.6 s | [logs/15](logs/15_full_regression_release.log) |
| Debug, GUI on, Qt offscreen (6 pre-existing GUI warnings, §28) | **100% tests passed, 0 tests failed out of 1875** | 34 | 878.6 s | [logs/16](logs/16_full_regression_debug_gui.log) |

- **Counts reconcile with MESH-004** (Release 1787, Debug + GUI 1837, 33 disabled each). MESH-005
  adds 39 tests: 38 enabled (+38 in both builds) and 1 disabled (`MMS3DTest.DISABLED_OperatorRefinementStudy`,
  the gate study run explicitly, §22), hence 34 disabled. All 38 enabled MESH-005 tests pass in
  both logs.
- **G5 — generated outputs** ([logs/18](logs/18_generated_outputs_classification.log),
  `tools/classify_generated_outputs.py`). The final runs rewrite tracked validation reports. The
  baseline is the pre-MESH-005 snapshot, not HEAD, because the tree holds legitimate uncommitted
  MESH-001–004 output changes. Every output under `results/`, `cases/*/results/` and
  `tests/data/cases/*/results/` (539 files; this phase's own evidence excluded) was compared: JSON
  leaf by leaf, Markdown tables and CSV cell by cell. Only keys and columns named as timings
  (runtime, seconds, …) were excluded; physics keys such as `wall_shear_*` were always compared.
  - Result: 491 identical, **48 runtime-only, 0 with any other difference, 0 new**. Raw diffs
    were spot-checked, and an independent line-based check of the 48 found nothing else.
  - The 48 were restored from the snapshot. Afterwards all 539 are identical, and
    `git status` matches the resume listing except for 10 MMS reports, which are now clean
    (== HEAD). The first session's focused run had rewritten those 10 with new runtimes only.
- **G5 PASS**: every regenerated output differs from the pre-MESH-005 tree in timing fields only,
  and none is left changed.

## 27. Failed experiments

None numerical: every MESH-005 test and gate item passed on its first run. Recorded tooling
failures:

- The first staged focused run stopped at its last stage because my script named the validation
  binary wrongly. That stage ran 0 tests, while all 949 tests of the earlier stages had passed.
  Kept as [logs/11a](logs/11a_STOPPED_focused_tests_script_wrong_binary_name.log); the corrected
  run is logs/11.
- The first CLI-compat log printed a wrong total (the count covered only the fixtures). All 27
  per-case verdicts were IDENTICAL; the script was fixed and rerun.
- A heredoc-generated edit of `VTKWriter.cpp` turned `\n` escapes into literal newlines. This was
  caught by inspection before any build and rewritten.
- The first performance run was interrupted when the first session ended. It had written only its
  header ([logs/13a](logs/13a_INTERRUPTED_performance_header_only.log)) and was rerun (§24).
- The first session wrote the exactness probe but never ran it. Its first run is the §24a log. One
  output fix preceded it: the Neumann column was printed with `std::to_string`'s six fixed
  decimals, which would print round-off values as 0.000000; it now uses `%.3e`.

## 28. Limitations

- **3D is a foundation, not a solver.** 3D incompressible/compressible flow, w-momentum, 3D
  pressure–velocity coupling and turbulence are refused explicitly on 3D meshes (P12-MESH-006).
- **No case-file / CLI / GUI path for 3D.** `CaseReader`, `CaseBuilder` and `ProjectRunner` build
  2D meshes only. The 3D mesh, operators and VTK export are C++ API features.
- **Mesh coverage:** uniform Cartesian hexahedra only. Non-orthogonal and skew corrections are
  dimension-general in code (full cross products), but on 3D meshes they are exercised only in
  their orthogonal short-circuit; 3D non-orthogonal verification is MESH-006.
- **MeshQuality in 3D** keeps its 2D metric names (JSON/CLI keys): `cell_area` is the volume,
  `face_length` the face area, and `aspect_ratio` the face-area ratio (exactly max/min edge for a
  box).
- **Boundary-condition lookup is O(boundary faces) per face** (`boundaryPatchNameForFace`, linear
  search; pre-existing). This is harmless in 2D. In 3D it makes per-face-patch operator
  evaluations at 64³ cost seconds (16.8 s for the whole 64³ level); large 3D meshes will need an
  index.
- **Explicit diffusion, pre-existing findings** (unchanged by MESH-005, dimension-independent;
  reported, not fixed — outside this phase's scope, and a fix would change 2D behaviour):
  - A Neumann face's flux is rebuilt from the boundary value by the cubic fit, not imposed (§16).
  - With exactly two cells along a boundary normal, the far point of that fit is a transverse cell,
    because `nextInteriorFaceAwayFrom` accepts a perpendicular face. The Laplacian of x² + y² is
    then wrong by up to 2.17 (exact 4), in 2D and 3D alike. The output is byte-identical before
    and after MESH-005 (§24a, logs/04).
  - The explicit `diffusion()`/`laplacian()` operators have no production caller in `src/` or
    `apps/`: the solvers use the implicit assemblies. These defects affect verification and API
    use only.
  - All 3D exactness and order claims use ≥ 3 cells per direction (M5/M6-class meshes).
- **2D-only guards are tested per component** (C5: the momentum assemblers, pressure correction,
  velocity gradient, wall distance, vorticity, writers, restart). No solver was run end to end on
  a 3D mesh. SIMPLE, PISO and CompressibleSIMPLE each call a guarded component
  (`assembleDiffusionContribution`, `computeMomentumResponseCoefficient`, …) in their first
  iteration, so they are refused there, before any result is produced.
- **Build warnings, pre-existing:** the Debug + GUI build reports 3 `-Wconversion` warnings
  (`qsizetype` → `int`, each printed twice) in `apps/gui/SimulationControllerEditing.cpp`, lines 86–89.
  The same code is present at HEAD `b66310c`, so MESH-005 did not introduce them. The Release build
  has 0 warnings. `-Werror` is deliberately off (`cmake/CompilerOptions.cmake`).
- **`VTKWriter::writeCellFields`** needs a single structured grid: no multi-block 3D, no re-patched
  mesh.

## 29. Acceptance decision

**P12-MESH-005 COMPLETE.** Every item of the pre-registered gate ([acceptance_gate.md](acceptance_gate.md),
unchanged since it was written; its one clarification was also written before any run) passes on
the final sources:

| gate | result | evidence |
| --- | --- | --- |
| A1–A9 geometry and topology | PASS (measured deviations 0 or ≤ 1.3e-15) | §7–§11, §24a |
| B1–B2 scalar / three-component vector fields | PASS | §12, §13 |
| C1–C5 interpolation, gradients (GG + LSQ), diffusion, convection, 2D-only guards | PASS | §14–§17, §24a |
| D1–D3 sparse assembly (hand-derived) | PASS (exactly the hand values) | §18, §24a |
| E1–E4 VTK hexahedra (parsed back) | PASS | §19 |
| F0 independent forcing check | PASS (4.3e-10 ≤ 1e-7) | §21 |
| F1–F8 refinement study, 8³–64³ | **30/30 gated (quantity, norm) items PASS**; every error decreasing | §22, §23 |
| G1 full regression | Release 1825/1825, Debug + GUI 1875/1875 | §26 |
| G2 MESH-001–004 suites by name | PASS | §25 (stages 17a–17n) |
| G3 CLI, 16 cases + 11 fixtures | 27/27 byte-identical (first and final binaries) | §24 |
| G4 case inputs unchanged; 2D fingerprints and bit identity | PASS (18/18 meshes bitwise) | §24, logs/06, 14, 20 |
| G5 regenerated outputs | timing only (48), 0 value changes; restored | §26 |

Stages were not skipped and no threshold, level, norm or mask was changed. No P12-MESH-006 work
(3D SIMPLE, w-momentum, 3D Rhie–Chow, 3D benchmarks, 3D turbulence) was started, and nothing was
committed or pushed.

**Final tree** ([logs/22](logs/22_final_tree_state.log)):

- HEAD = origin/main = `b66310c`; nothing staged.
- 191 status entries (143 modified, 48 untracked).
- The MESH-005 change set against the pre-MESH-005 snapshot:
  - 18 `src` files and 13 `include` headers, 1 of them the new `Vector3.hpp`;
  - 7 new test files and 7 test `CMakeLists.txt`;
  - 2 user docs, README, TODO and ROADMAP;
  - this evidence directory.

  It is exactly the change set recorded in logs/14, plus TODO.md and ROADMAP.md (updated after
  verification). No case file and no generated output is changed.
- CI format pre-check: clang-format-18 clean on all 549 C++ files
  ([logs/19](logs/19_clang_format_check.log)).
