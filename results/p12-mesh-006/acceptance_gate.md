# P12-MESH-006 — acceptance gate (fixed BEFORE implementation and before any verification run)

Written after the architecture audit (summary.md §3) and the scope decision on pressure–velocity
stabilization (summary.md §1), **before any MESH-006 production code change or verification run**.
Expected orders follow from the implemented discretization (derivation given per item). Accuracy
tolerances for the benchmarks are derived from this code's own measured 2D behaviour on the same
discretization (cited per item), never from a 3D result. **Nothing here is changed after a
verification run.** Items marked *reported* are run and reported in full but are not pass/fail.
Test names are the planned gtest names; summary.md maps each item to the test that checks it.

Common conventions: velocities normalized by the reference speed U (lid or bulk speed), ρ = 1.
"From rest" = zero initial velocity and pressure (a non-exact initial state). All 3D meshes are
`MeshGeometry::createCartesian3D` (the only 3D production geometry). "RC" = the Rhie–Chow face
flux (opt-in, automatic for 3D — summary.md §1, §7).

## G1 — w-momentum (unit/operator)

| id | criterion |
| --- | --- |
| G1.1 permutation symmetry | Problem A on a 3×4×5 box (Lx, Ly, Lz = 1.5, 0.7, 2.3; non-cubic cells), a smooth non-uniform velocity/pressure/flux field and mixed velocity BCs (inlet, wall, moving wall, outlet, symmetry on different patches); problem B = A under the cyclic axis permutation (x, y, z) → (y, z, x) (mesh 5×3×4, fields, flux and BCs permuted). For each scheme (upwind, central, linear_upwind, quick): the relaxed momentum systems of A's U, V, W equations equal B's V, W, U systems after the cell permutation — every matrix coefficient, RHS value and diagonal within 1e-13 relative to the row's largest coefficient. |
| G1.2 W residual gate | `OuterIterationMonitor`: a sample with u, v, p, continuity below tolerance and w above is not Converged; the same sample with w below tolerance is Converged; a sample without w (2D) behaves exactly as before (existing monitor tests unchanged). SIMPLE on a 3D mesh reports W residual histories of the same length as U/V. |
| G1.3 2D-only momentum paths | the momentum/pressure-correction components accept 3D meshes (MESH-005 guards removed only where the component is now 3D-capable); the remaining 2D-only components (turbulence, wall distance, vorticity, 2D writers, restart, compressible) still refuse a 3D mesh with an error naming the component. |

## G2 — 3D face flux and continuity

| id | criterion |
| --- | --- |
| G2.1 uniform flow | uniform U = (0.3, −0.7, 1.1), ρ = 1.3, on the 3×4×5 translated box with the uniform Inlet value on every patch: the predictor face flux (linear and RC) on every face (±x, ±y, ±z, interior and boundary) equals ρ U·S_f within 1e-14 relative; per-cell continuity imbalance ≤ 1e-14·Σ\|F\|; mass-balance diagnostics: inflow = outflow = Σ_inflow ρ\|U·S_f\| within 1e-14 relative, net 0. |
| G2.2 RC consistency | RC with a linear pressure field p = 2 − x + 3y − 4z (exact FixedValue pressure data per boundary face) and uniform response coefficients: the RC correction vanishes on every interior face (≤ 1e-12 relative to ρ\|U\|\|S_f\|), i.e. RC flux = linear flux for smooth fields. |
| G2.3 RC sees the checkerboard | zero velocity, pressure (−1)^{i+j+k} on a 6×6×6 box, FixedGradient(0) pressure data: the linear flux is exactly 0 on every face; the RC flux on every interior face whose two cells are both not boundary-adjacent equals ±2·D_f/α (D_f the pressure-correction coupling) within 1e-14 relative. |
| G2.4 mass balance reporting | a production 3D result reports inflow, outflow, absolute and relative global imbalance, max and RMS cell imbalance (metadata.json and CLI) computed from the canonical corrected face flux. |

## G3 — reduced / canonical pressure–velocity coupling (hand-derived)

| id | criterion |
| --- | --- |
| G3.1 hand-derived systems | meshes 2×1×1, 1×1×2 and 2×2×2 with hand-chosen distinct momentum diagonals A_u, A_v, A_w per cell, ρ = 1.2, hand-chosen predictor face fluxes, one FixedValue(0) pressure patch and FixedGradient elsewhere. Written out by hand in the test (never by a helper mirroring the code): d_c = V/A_c; D_f = ρ A_f d_{c,f}/\|d\| (c = the face's normal axis, interior d_f linearly interpolated, boundary owner value); the p′ matrix and RHS = −Σ s_f F*_f; p′ (solved by hand); velocity correction u = u* − d_u ∂p′/∂x etc.; corrected face flux F* + D_f(p′_P − p′_N). Every coefficient, RHS value, p′, corrected velocity and corrected flux within 1e-13 relative; the corrected fluxes satisfy continuity in every cell within 1e-13·Σ\|F\|. The 1×1×2 and 2×2×2 cases carry z-normal interior faces. |
| G3.2 pressure-reference invariance | closed 8³ lid-driven cube, Re = 100, RC, converged to 1e-10 (all outer tolerances) with reference cell 0 and with the last cell: max \|Δu\| ≤ 1e-7 U, max \|ΔF\| ≤ 1e-7 max\|F\|, gauge-removed (zero-mean) pressure difference ≤ 1e-6 (max p − min p). |
| G3.3 checkerboard regression | closed 8³ box, all stationary walls, from zero velocity with initial pressure (−1)^{i+j+k}: with RC the converged pressure has checkerboard amplitude \|Σ(−1)^{i+j+k} p_P V_P\|/Σ V_P ≤ 1e-6 and max\|u\| ≤ 1e-8. The explicit-linear-flux run is *reported* (it cannot see the mode). |
| G3.4 non-orthogonal Cartesian limit | 3D SIMPLE (RC) with non_orthogonal_corrections = 2 vs 0 on an 8³ lid-driven cube: velocity, pressure and flux agree within 1e-12 relative; every 3D face's non-orthogonal area-vector part is exactly 0. |

## G4 — genuinely 3D MMS (production SIMPLE, API level, from rest)

Unit cube, closed. U = ∇×A with A = (0, −F, G), G = sin πx sin πy e^{z/2}, F = sin πx sin πz e^{−y/2}, scaled by 1/π:

- u = sin πx (cos πy e^{z/2} + cos πz e^{−y/2}), v = −cos πx sin πy e^{z/2}, w = −cos πx sin πz e^{−y/2}
  (div U = 0 exactly, U·n = 0 on all six faces, every component depends on x, y and z);
- p = cos πx cos πy cos πz + xyz/2;
- ρ = 1, μ = 0.1 (as the 2D SIMPLE MMS); forcing f = ρ(U·∇)U + ∇p − μ∇²U, analytical.

Setup as the 2D SIMPLE MMS (test_mms_simple.cpp): exact Dirichlet velocity per boundary face, exact
FixedGradient(∇p·n) per face, reference cell 0, relaxation 0.8/0.4, outer tolerance 1e-8 (absolute,
all residuals), BiCGSTAB inner rel 1e-3 (pressure Jacobi), central convection, Green–Gauss, RC
(automatic). Levels **n = 8, 16, 32**. p compared modulo gauge (zero volume-weighted mean).

| id | criterion |
| --- | --- |
| G4.0 forcing | at 20 fixed pseudo-random points: ∇·U (≤ 1e-7), and each forcing component against 4th-order central differences (step 1e-3) of the closed forms within 1e-7 relative to max(1, \|value\|). |
| G4.1 velocity | u, v, w: L1, L2, L∞ decrease at every refinement; observed order of the finest pair (16→32) in [1.6, 2.4] for L1 and L2 (central convection and the O(h²) RC term are second order; band = the 2D SIMPLE MMS band). |
| G4.2 pressure | p: L1, L2 decrease at every refinement; finest-pair order in [1.5, 2.4] (2D band). L∞ decreasing only (boundary-ring Neumann reconstruction, as 2D). |
| G4.3 continuity | per-volume discrete continuity of the converged flux: L∞ ≤ 1e-5 on every level; global mass imbalance ≤ 1e-14 (closed box). |
| G4.4 solve | every level Converged from rest, finite. |

*Reported:* orders of all norms and pairs; iterations; face-flux error.

## G5 — analytical square duct (production case path)

Square duct, side a = 1, length L = 6, **Re = ρ U a/μ = 10** (U = 1, μ = 0.1); xmin uniform inlet
(U, 0, 0), xmax outlet with fixed pressure 0, the four lateral faces no-slip walls; RC, central
convection, relaxation 0.7/0.3, outer tolerances 1e-9, from rest. Grids n × n cross-section, cubic
cells: **n = 8, 16, 24** (nx = 6n). Reference: the fully developed Navier–Stokes solution
u(y, z) = (16 G a²/(μπ⁴)) Σ_{m,n odd} sin(mπy/a) sin(nπz/a)/(mn(m²+n²)), with G = −dp/dx fixed by
the flow rate Q = U a² = (64 G a⁴/(μπ⁶)) Σ_{m,n odd} 1/(m²n²(m²+n²)) (series truncated at
m, n ≤ 399; independent check: the constants K = Q μ/(G a⁴) = 0.035144 and u_max/U = 2.0962 of the
literature for a square duct, to 4 digits). Measurement plane: the cell-centre plane nearest
x = 4a; pressure gradient: least-squares slope of the area-averaged pressure over the cell planes
in x ∈ [2.5a, 4.5a].

| id | criterion |
| --- | --- |
| G5.1 accuracy | at n = 24: cross-section L∞(u − u_exact)/U ≤ 0.010, area-weighted RMS ≤ 0.005, \|dp/dx error\|/G ≤ 0.015, \|u_max error\|/u_max ≤ 0.010; at n = 16: L∞ ≤ 0.020, \|dp/dx error\|/G ≤ 0.030. Derivation: the 2D production Poiseuille (MESH-001) measured dp/dx within 0.585 % with 18 cells across; second order ⇒ ≈ 0.74 % at 16 and 0.33 % at 24 cells — thresholds allow ≈ 4× that. |
| G5.2 convergence | RMS profile error and \|dp/dx error\| decrease monotonically 8 → 16 → 24. |
| G5.3 conservation | every section's mass flow equals the inflow within 1e-6 relative; global \|m_in − m_out\|/m_in ≤ 1e-6. |
| G5.4 solve | every grid Converged from rest through CaseReader → CaseBuilder → ProjectRunner → export. |

*Reported:* the axial variation of the centreline velocity over x ∈ [3a, 4.5a] (full development).

## G6 — directional symmetry (production case path)

The n = 16 duct of G5 aligned with x, with y and with z (streamwise extent 6 along the flow axis,
the same n × n cross-section, inlet on the min face of the flow axis, outlet on its max face).
Fields mapped through the axis permutation.

| id | criterion |
| --- | --- |
| G6.1 | for the x-vs-y and x-vs-z pairs: max over cells of \|Δ(streamwise velocity)\|/U, \|Δ(each cross-flow component)\|/U ≤ 1e-6; \|Δp\|max ≤ 1e-6·(p_max − p_min); \|Δ(dp/dx)\|/G ≤ 1e-6. (1e-6 is far below the ≈1e-3 discretization error: any hidden x/y assumption produces O(h²) or O(1) differences; round-off / Krylov-path differences at outer tolerance 1e-9 are ≪ 1e-6.) |
| G6.2 | the z-directed duct also meets G5.1's n = 16 thresholds (it exercises w as the streamwise component). |

## G7 — 3D benchmark: lid-driven cube, Re = 1000 (production case path)

Unit cube; lid = ymax moving with (U, 0, 0), U = 1; all other faces stationary walls; FixedGradient(0)
pressure everywhere (reference cell); ρ = 1, μ = 1e-3 (**Re = 1000**, steady: below the cube's first
instability, Re_c ≈ 1914). QUICK convection (the 2D Re = 1000 configuration of record), RC,
relaxation 0.7/0.3, outer tolerances 1e-7, momentum BiCGSTAB rel 1e-6, pressure BiCGSTAB rel 1e-4
with Jacobi, from rest. **Grids 32³, 48³, 64³.**

**Reference:** Albensoeder & Kuhlmann (2005), *Accurate three-dimensional lid-driven cavity flow*,
J. Comput. Phys. 206, 536–558, Table 5 (lid-parallel velocity along the lid-normal centreline) and
Table 6 (lid-normal velocity along the lid-parallel centreline), cube (Λ = 1, rigid end walls),
Re = 1000, 17 stations each (the Ghia stations). The original is not openly accessible; the values
are taken from the open-source transcription in tum-pbs/PICT, `tests/validations.py`, commit
a95d7f9d0713262a1bff2bd9e2be5a203ee69208, function `lid_driven_cavity_3D`, keys (1000, 1, 1, False),
reproduced in full in summary.md §19 with the coordinate mapping. Their convention: cavity
[−½, ½]³, lid at x = −½ moving in +y. Mapping to CFDApp (lid ymax moving +x): x_AK = ½ − y,
y_AK = x − ½, z_AK = z − ½; v_AK = u, u_AK = −v.

Stations: the 15 interior stations of each table (the wall stations are boundary values);
CFDApp values sampled on the centreline through the cube centre by trilinear interpolation of
cell-centred velocity (wall values from the boundary conditions between the last cell centre and
the wall).

| id | criterion |
| --- | --- |
| G7.1 accuracy (64³) | over the 30 interior stations: max \|Δ\| ≤ 0.06 U and RMS ≤ 0.03 U; each of the three extrema of the reference profiles (Table 5 min −0.27293; Table 6 min −0.24407 and max 0.43423) reproduced within 0.03 U at its station. Derivation: this code's 2D Re = 1000 QUICK validation measured L∞ 0.113 / 0.020 and L2 0.054 / 0.0096 at 40² / 80² (results/validation/production/cavity_re1000.md; observed rate ≈ 2.5), i.e. ≈ 0.035 L∞ and ≈ 0.017 L2 at 64 cells per side. |
| G7.2 convergence | max \|Δ\| and RMS decrease monotonically 32³ → 48³ → 64³. |
| G7.3 symmetry | spanwise mirror symmetry about z = ½ of the converged solution (64³): max \|u(x,y,z) − u(x,y,1−z)\|, \|v(…) − v(…)\|, \|w(x,y,z) + w(x,y,1−z)\| ≤ 1e-5 U. |
| G7.4 solve / conservation | every grid Converged from rest; closed cavity net boundary mass flux \|Σ F_b\| ≤ 1e-12. |

*Reported:* primary-vortex centre location on the midplane z = ½; iterations; runtime.

## G8 — global mass conservation (every production validation case)

| id | criterion |
| --- | --- |
| G8.1 | open cases (G5, G6): \|m_in − m_out\|/m_in ≤ 1e-6 at convergence, reported in metadata.json. |
| G8.2 | closed cases (G3.2, G3.3, G4, G7): \|net boundary flux\| ≤ 1e-12 (G4: ≤ 1e-14). |
| G8.3 | max and RMS cell continuity imbalance reported for every case. |

## G9 — production case path, exports, CLI, GUI

| id | criterion |
| --- | --- |
| G9.1 case format | geometry.json `"box"` (length, height, depth), mesh.json `structured_cartesian` with `nz`, boundaries.json on patches xmin, xmax, ymin, ymax, zmin, zmax with 3-component velocities, case.json 3-component initial velocity, solver.json `face_flux` (automatic \| linear \| rhie_chow). Round-trip through CaseWriter/CaseReader is lossless. |
| G9.2 rejection | each of: box without nz; nz with a rectangle; nz = 0; depth missing or ≤ 0; 2-component velocity in 3D; 3-component velocity in 2D; 2D patch names in 3D; thermal / turbulence / species / multiphase / compressible / buoyancy physics in 3D; grading, structured_quad or multiblock with a box; 2-component initial velocity in 3D → rejected before any solver runs (CLI exit 2) with a message naming the file and field. |
| G9.3 exports | a 3D CLI run writes: fields.csv (x, y, z, velocity_x/y/z, magnitude, pressure; one row per cell), residuals.csv (with w_residual; one row per iteration), solution.vtk (parsed back: (nx+1)(ny+1)(nz+1) points, n VTK_HEXAHEDRON cells, pressure and 3-component velocity cell data, all finite, values equal to the solution), metadata.json (dimension 3, nx, ny, nz, lengths, face_flux, residuals incl. w, mass balance). The CLI prints the W residual and the mass balance. |
| G9.4 GUI | the GUI controller opens a 3D case without error, validation identifies it as 3D, runs it through ProjectRunner to completion, exposes a "w" residual series, returns empty contours (no exception) and preserves nz, depth and w through a mesh/boundary editor commit and a save round-trip. |

## G10 — two-dimensional backward compatibility (mandatory)

BASE = the pre-MESH-006 working tree (MESH-005 end state), copied to `$HOME/m6ref/base` and built
in Release (its `cfdapp` is byte-identical to MESH-005's final binary).

| id | criterion |
| --- | --- |
| G10.1 bit identity | the MESH-005 probe (tools/bitprobe.cpp) extended with 2D momentum assembly (U and V systems, every scheme), pressure-correction assembly, velocity/flux correction and complete SIMPLE solves (velocity, pressure, flux, residual histories): BASE vs NEW **bitwise identical** on every mesh of the probe. |
| G10.2 CLI | every committed 2D case and every CLI fixture of BASE: fields.csv, solution.vtk, residuals.csv byte-identical, metadata.json equal as parsed JSON, stdout identical, exit codes equal. |
| G10.3 suites | the MESH-001–005 test suites pass by name. |
| G10.4 inputs | no existing 2D case file, fixture or test input is modified; 2D fingerprints unchanged. |
| G10.5 outputs | tracked outputs regenerated by the runs differ from BASE only in timing fields (classified file by file, tools of MESH-005). |

## G11 — full regression

Release (-O3) and Debug + GUI (offscreen): 100 % pass, exact counts recorded.

## CPU baseline (reported, required to be recorded)

Lid-driven cube Re = 100 through the CLI (Release, 1 thread) at 16³, 32³, 64³: cells, outer
iterations, wall time, time per iteration, peak resident memory, cell-iterations per second;
hardware recorded. No optimization.

## Amendment A1 (written before any verification run of a gate item)

**G3.4 is replaced by G3.4′.** While writing its test I found that the end-to-end criterion as first
written ("agree within 1e-12 relative") cannot hold for a reason unrelated to the Cartesian limit.
BiCGSTAB's relative stopping test is relative to the *initial* residual (`src/algebra/BiCGSTAB.cpp`,
`absRes / b0`). The extra non-orthogonal corrector passes re-solve the *identical* momentum and
pressure-correction systems warm-started from pass 1, so they perform inner iterations. Those move
the iterate at the inner-tolerance level, and the two runs then converge along different paths. They
agree to the outer-iteration tolerance, not to round-off. G3.4′ tests the Cartesian limit where it
is exact and bounds the end-to-end difference by the iterative tolerance:

| id | criterion |
| --- | --- |
| G3.4′ (a) exact | on an 8³ lid-cube state (a non-trivial velocity, pressure and flux field), the relaxed U, V, W momentum systems assembled with the non-orthogonal correction on (least squares) equal those assembled with it off, entry for entry (`==`: every matrix value, RHS value, diagonal). The pressure-correction assembly with `nonOrthogonal` and a previous p′ has the same matrix and RHS as without, and its explicit face flux is exactly 0 on every face. Every face of the mesh has an exactly-zero non-orthogonal area-vector part. |
| G3.4′ (b) end to end | SIMPLE (RC) on the 8³ lid cube, Re = 100, non_orthogonal_corrections = 2 vs 0, both converged to outer tolerance 1e-11: max \|Δu\| ≤ 1e-8 U, gauge-removed \|Δp\| ≤ 1e-8·(p_max − p_min), \|ΔF\| ≤ 1e-8·max\|F\|. |

For transparency, the only runs made before this amendment were two non-gate smoke checks of
the 3D production path, on a 12³ Re = 100 lid cube:

- the iterative error against the outer tolerance: max \|Δu\| from a 1e-11 run of 1.4e-4 / 1.5e-5 /
  1.4e-6 at tolerances 1e-6 / 1e-7 / 1e-8;
- the time per iteration from 16³ to 64³ (6 iterations each).

Neither measures any gate quantity. No other item, threshold, grid or reference is changed.

## Amendment A2 (written before any G5, G6 or G7 run)

Written while preparing the G5–G7 case files, before any duct or cavity run of any size (the
only 3D runs so far are the A1 smoke checks, the G1–G3 unit tests and the G4 MMS study). No
threshold, grid, tolerance, norm, station set or reference value changes.

1. **G7 pressure preconditioner.** The production case format (`solver.json`
   `pressure_linear_solver`: type, backend, absolute/relative tolerance, max_iterations) has no
   preconditioner key. Through the case path the pressure BiCGSTAB therefore runs with the
   case-format default (no preconditioner), not "with Jacobi". Adding a case-format key is outside
   MESH-006. The converged discrete solution does not depend on it: the outer residuals are the
   initial residuals of freshly assembled systems. On a uniform Cartesian mesh, Jacobi scales the
   pressure Laplacian by an almost constant diagonal. Every other G7 setting stays as written.
2. **G5 measurement plane.** With nx = 6n cells on L = 6a, the cell-centre planes are at
   x = (i + ½)/n. The two planes nearest x = 4a, at 4a ± h/2, are equidistant. The measurement
   plane is the upstream one, x = 4a − h/2. The downstream plane's errors are *reported*.
3. **G5 u_max.** For even n, the duct axis y = z = a/2 is a cell-vertex line. The numerical u_max
   is the bilinear interpolant of the cell-centre velocity at the axis on the measurement plane
   (the mean of the four axis-adjacent cells). It is compared with the exact axis value
   u(a/2, a/2), which is the maximum of the exact profile. This includes the interpolation's
   O(h²) bias, so it makes the check stricter, not looser.
4. **G5/G6 inner solvers** (not specified in G5):
   - momentum: BiCGSTAB, relative 1e-6, absolute 1e-13, at most 1000 iterations;
   - pressure: BiCGSTAB, relative 1e-4, absolute 1e-13, at most 5000 iterations.

   The absolute values sit far below the 1e-9 outer tolerance. G7 specifies only the relative
   tolerances (1e-6 / 1e-4); its absolute tolerances and inner iteration limits are the same as
   G5's. The outer iteration limit is 20000 for G5/G6 and 50000 for G7. It is only a cap: a run
   that reaches it is not Converged and fails its gate.

## Stop rules

- A failed item stops the phase: recorded as evidence, MESH-006 reported BLOCKED / FAILED GATE.
- No threshold, grid, norm, station set, tolerance or reference value is changed after a run.
- TODO items are marked `[x]` only after this gate and the full regression pass.
