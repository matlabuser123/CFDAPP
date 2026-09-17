# P12-MESH-006 — 3D Incompressible Solver

Status: **COMPLETE under Amendment A3** (2026-09-15). The original pre-registered gate failed and
that failure stays on record. Chronology:

1. **Original gate: FAILED.** The status at the stop, as first written: "Status: **BLOCKED / FAILED
   GATE**. G5.1 (analytical square duct: velocity L∞ at n = 16 and 24, RMS at n = 24) and G6.2 (the
   n = 16 L∞ threshold on the z-directed duct) failed. The decision and its diagnosis are in §30."
   (§17, §18, §30: the historical record, unchanged.)
2. Implementation stopped. An independent investigation classified the failure **D — pre-registered
   threshold design defect** (§31).
3. The user authorized Amendment A3, which replaces G5.1 and G6.2 only. It was frozen before a fresh
   run (§31).
4. The fresh acceptance run: **A3 G5 PASS, A3 G6 PASS** (§31).
5. The remaining work and its verification: CLI fixtures, GUI, formatting, CPU baseline, G10,
   focused tests, G11 (§32).
6. Final G1–G11 table and decision: §33, §34.

Pre-registered gate: [acceptance_gate.md](acceptance_gate.md), written after the audit (§3) and
the stabilization decision (§1), before any MESH-006 code change or verification run. Amendment A1
(G3.4 → G3.4′) was written before any gate run. Amendment A2 was written before any G5, G6 or G7
run; it fixes the G7 preconditioner and some G5 measurement definitions, and no threshold.

---

## 1. Authorization and scope

P12-MESH-006 (3D incompressible solver) was authorized on 2026-09-15, for MESH-006 only (no
MESH-007 or later P12 scope), with no commit or push. Final gate: production 3D SIMPLE must
converge from a non-exact initial state and agree quantitatively with independent
analytical/benchmark solutions while preserving global mass conservation, through the real
production path (case → CaseReader → CaseBuilder → ProjectRunner → 3D SIMPLE → results → VTK).

**Scope decision (user, 2026-09-15): opt-in Rhie–Chow.** The audit (§3) found that the production
collocated SIMPLE has **no Rhie–Chow interpolation**. This is a documented limitation in README,
ROADMAP, P12-NUM-003 (§7) and P12-NUM-007: pressure fields carry an undamped odd-even mode on open
domains. The brief's §6 assumed an existing stabilization to generalize. Asked to decide, the user
chose:

- add Rhie–Chow momentum interpolation to the shared SIMPLE as a **dimension-independent,
  case-selectable face-flux option**;
- **every existing case keeps the current linear face flux**, so 2D results stay bitwise identical;
- **3D cases use Rhie–Chow** (automatic default for 3D meshes).

Not in scope: MESH-007+, 3D turbulence/thermal/species/multiphase/compressible, non-Cartesian 3D
geometry, GPU 3D SIMPLE, optimization, and the unrelated known issues (MESH-004 irregular-mesh
convergence, CG breakdown threshold; MESH-005 explicit diffusion with two cells). None of these
was touched.

## 2. Baseline ([logs/00](logs/00_baseline_and_snapshot.log))

- HEAD `b66310c`; the working tree holds the uncommitted, verified P12-MESH-001–005 work:
  191 status entries (143 modified, 48 untracked), `git diff --stat` 143 files, +7590/−3050.
- MESH-005 is `[x]` COMPLETE; its evidence (23 logs) is intact.
- Pre-MESH-006 reference: the whole working tree (tracked + untracked-not-ignored, 1453 files)
  copied to `$HOME/m6ref/base`, its 288 src/include files hashed, built in Release with the same
  options as build/release: 0 warnings. Its `cfdapp` sha256 `bb4beb91ffce66b0` is identical to
  MESH-005's final binary, so the snapshot reproduces the MESH-005 end state exactly. It is the BASE
  of every 2D comparison. The snapshot's source hashes were re-verified at the stop point
  (`tools/m6files.sh`: `sha256sum -c` rc = 0).

## 3. Architecture audit (before any code change)

What the MESH-005 end state had, and what a 3D incompressible solver needed:

| area | MESH-005 state | needed |
| --- | --- | --- |
| momentum | `VelocityComponent {U, V}`; every assembly guarded 2D-only | a W component through the same assembly |
| velocity gradient | `gradU`, `gradV` only | `gradW` (3D only) |
| pressure correction | coupling picks d_u or d_v from the 2D face normal; velocity correction u, v | d_w on z-faces; w correction |
| face flux | linear interpolation of u* (no Rhie–Chow) | a pressure–velocity stabilization for 3D (§1) |
| SIMPLE loop | u, v predictor; monitor on u, v, p, continuity | w predictor, W residual in the monitor |
| non-orthogonal passes | u, v | u, v, w |
| case format | rectangle / structured_quad / multiblock, 2-component vectors, 4 patches | box + nz, 3-component vectors, 6 patches |
| exports | 2D writers (they refuse 3D) | 3D fields.csv / VTK hexahedra / metadata |
| mesh | `createCartesian3D` (MESH-005, verified operators) | reused unchanged |

Decision: **one shared SIMPLE**, with the dimension taken from `Mesh::dimension()`. No
`SimpleSolver3D` clone and no test-only solver. Every 2D code path keeps its exact floating-point
expression, which the 2D bit-identity probe checks (§23).

## 4. Case-path architecture

```text
case dir ─ CaseReader ─ CaseDefinition ─ CaseBuilder ─ SimulationSetup ─ ProjectRunner ─ SIMPLE ─ ResultExporter
  box/nz/3-vectors   dimension checks     createCartesian3D     same runner     u,v,w,p    3D CSV/VTK/JSON
```

- **geometry.json** `"type": "box"` with length, height, **depth** (> 0, required for a box, refused
  otherwise). `geometryDimension()` = 3 for a box.
- **mesh.json** `structured_cartesian` with **nz** (> 0; allowed only for structured_cartesian; no
  grading with nz). A box requires nz and structured_cartesian; nz without a box is refused.
- **boundaries.json**: the six patches `xmin xmax ymin ymax zmin zmax` (from `meshPatchNames`);
  velocity values are `[x, y, z]` in 3D and `[x, y]` in 2D. The component count is checked against
  the case dimension.
- **case.json** `initial_conditions.velocity`: 3 numbers in 3D, 2 in 2D.
- **solver.json** `face_flux`: `automatic` (default) | `linear` | `rhie_chow`.
- 3D physics: laminar incompressible only. Thermal, turbulence (unless `laminar`), buoyancy,
  species, multiphase and compressible are refused by CaseReader before anything is built.
- **CaseBuilder**: `MeshGeometry::createCartesian3D(nx, ny, nz, length, height, depth)`,
  `settings.faceFlux = parseFaceFluxScheme(...)`.
- **ProjectRunner**: unchanged apart from a finite check that now covers w. It runs 3D through the
  same `SIMPLE::solve`.
- **CaseWriter** writes depth, nz, 3-vectors and a non-default face_flux; a 2D case is written
  exactly as before.

## 5. w-momentum

`VelocityComponent::W` goes through the existing momentum assembly. Diffusion (both overloads),
convection (all four schemes, deferred-correction paths), pressure source, buoyancy and momentum
source use `velocityComponentValue()` and `componentGradient()` (gradW). The MESH-005 2D guards
were removed from exactly these functions. The two-component wrapper `assembleMomentum` keeps its
guard. w is a third equation of the same assembly with the same relaxation and the same linear
solver, not an auxiliary equation. Tested by G1.1 (§13): the U, V, W systems are equal under the
cyclic axis permutation for every scheme.

## 6. 3D SIMPLE coupling

One `SIMPLE::solve`. In 3D, each outer iteration:

1. Assembles and solves U, V, **W**.
2. Runs the non-orthogonal passes with `previousW`, returning `passes.w`.
3. Computes d_u, d_v, **d_w** = V/A_P (before the predictor flux).
4. Forms the predictor flux: Rhie–Chow (3D default) or linear.
5. Assembles and solves the p′ equation with d_w on z-faces, repeating over the correction passes.
6. Updates p = p + α_p p′.
7. Corrects u, v, **w** with d_c ∂p′/∂x_c.
8. Corrects the flux with F = F* + D_f(p′_P − p′_N).
9. Records the residuals (§10).

A 2D mesh takes exactly the old path (no W, linear flux under `automatic`).

## 7. Rhie–Chow / face flux

`src/pressure_velocity/RhieChow.cpp` (new):

  F*_f = F_lin − (D_f/α_u)·[(p_N − p_P) − (∇p)_f·d],  d = x_N − x_P,

- D_f is the face's two-point pressure-correction coupling (`pressureCorrectionFaceCoupling`,
  N = 0).
- (∇p)_f is the linear interpolation of the cell pressure gradient (the SIMPLE gradient scheme).
- D_f/α_u is the unrelaxed coupling. This is the converged form of Majumdar's
  relaxation-independent correction.
- Boundary faces keep the boundary-condition flux (correction 0).

The correction vanishes for linear pressure and sees the checkerboard (G2.2, G2.3, G3.3).
`FaceFluxScheme {Automatic, Linear, RhieChow}`: Automatic resolves to Linear in 2D and RhieChow
in 3D. Every existing case (no `face_flux` key) keeps its linear flux, so 2D stays identical.

## 8. Reduced pressure-correction system

- The coupling selects d_c by the face-normal axis: x → d_u, y → d_v, z → d_w.
- `isAxisAligned` counts exactly one nonzero area-vector component.
- `exactlyParallel` compares cross products with 0.
- The response vector keeps the 2D expression when S_z = 0, so 2D is bitwise unchanged.
- The w response coefficient is a trailing optional parameter of the pressure-correction assembly
  and of `correctVelocity`. It is required on a 3D mesh and refused as missing with an error
  naming 3D.
- Hand-derived systems on 2×1×1, 1×1×2 and 2×2×2 (G3.1) check every coefficient, the RHS, p′, the
  corrected velocities and the corrected fluxes against numbers written out by hand.

## 9. Continuity and mass-balance diagnostics

`computeMassBalance(mesh, flux)` (new, `ContinuityEquation`) computes:

- inflow and outflow (boundary fluxes by sign) and the net boundary flux;
- relative imbalance = |net|/max(in, out);
- max and RMS cell imbalance;
- a flux scale (max of the through-flow and the mean internal |F|) and the normalized continuity.

metadata.json `conservation` carries all of them for 3D results, and the CLI prints the mass
balance and the cell continuity. The existing global imbalance and continuity residual are
unchanged.

## 10. Residual reporting

- `SIMPLEResult` has `wResidualHistory` and `finalWResidual`. The W residual is the initial
  residual of the freshly assembled W system, the same definition as U and V.
- `OuterResidualSample` has an optional w: when present, `isConverged` requires it (both
  criteria), and distance, divergence, non-finite and relaxation control include it. A 2D sample
  has no w and behaves exactly as before.
- Progress callbacks carry `wResidual` and `threeDimensional`.
- residuals.csv gains a `w_residual` column in 3D. metadata `residuals.w` and
  `normalized_residuals.w` are written in 3D, and the CLI prints "W residual:".

## 11. Initialization

Deterministic and non-exact: zero velocity and pressure by default ("from rest"), or the case.json
`initial_conditions` (3 components in 3D) applied uniformly by CaseBuilder. Every gate study starts
from rest (G4 through the API, G5–G7 through the case path).

## 12. Boundary conditions on all six orientations

inlet, outlet, wall, moving_wall, symmetry and fixed_value / fixed_gradient pressure work on every
patch xmin … zmax (the existing BC classes are vector-valued and orientation-independent; the
face normal drives them). Tests:

- `SIMPLE3D.PlugFlowIsExactInAllSixDirections`: uniform flow into each of the six faces.
- `SIMPLE3D.CouetteFlowIsExactForMovingWallsOnThreeAxes`.
- G2.1: exact uniform flux on every face orientation.
- G6: the whole duct aligned with x, y and z.

## 13. G1 — w-momentum ([logs/02](logs/02_focused_3d_tests.log))

| item | test | result |
| --- | --- | --- |
| G1.1 permutation symmetry | `SIMPLE3D.MomentumSystemsArePermutationSymmetric` | PASS: 3×4×5 box and its cyclic permutation. 4 schemes × U, V, W give 24 system comparisons, every coefficient, RHS and diagonal within 1e-13 relative |
| G1.2 W residual gate | `SIMPLE3D.MonitorNeverConvergesWhileWIsUnconverged`; W history length in `Duct3DProductionCase.CommittedDuctRunsConservesAndExports` | PASS |
| G1.3 2D-only components | `Operators3DTest.ThreeDimensionalCapableComponentsAcceptA3DMesh`, `…TwoDimensionalOnlyComponentsRefuseA3DMesh` | PASS: the 3D-capable components accept a 3D mesh and refuse it without their W input. assembleMomentum, wall distance, k-ε, k-ω, SST, vorticity, transient/PISO, the compressible solver and its parts, the 2D writers and restart refuse it with an error naming the component |

## 14. G2 — face flux and continuity ([logs/02](logs/02_focused_3d_tests.log))

| item | test | result |
| --- | --- | --- |
| G2.1 uniform flow | `SIMPLE3D.UniformFlowFluxIsExactOnEveryFaceOrientation` | PASS: linear and RC, every face orientation, ≤ 1e-14 relative. Mass balance: inflow = outflow, net 0 |
| G2.2 RC consistency | `SIMPLE3D.RhieChowVanishesForALinearPressureField` | PASS |
| G2.3 checkerboard | `SIMPLE3D.RhieChowSeesTheCheckerboardTheLinearFluxCannot` | PASS: 144 interior faces give ±2D_f/α; the linear flux is exactly 0 |
| G2.4 reporting | metadata.json `conservation` keys (`Duct3DProductionCase.CommittedDuctRunsConservesAndExports`); CLI lines (§21) | PASS (metadata). CLI printing is implemented. No ctest CLI fixture was added before the stop (§29) |

## 15. G3 — reduced / canonical coupling ([logs/02](logs/02_focused_3d_tests.log))

| item | result |
| --- | --- |
| G3.1 hand-derived 2×1×1, 1×1×2, 2×2×2 | PASS. For example, 2×1×1 gives p′ = (1.515, 0.875) and corrected fluxes 0.86, 0.9; 1×1×2 (through z) gives p′ = (47/24, 31/24) and corrected fluxes 0.59, 0.61 |
| G3.2 reference invariance (8³ cube, Re = 100, tolerance 1e-10) | PASS: max \|Δu\| 2.088e-14, \|ΔF\|/max\|F\| 3.225e-14, gauge-removed \|Δp\|/range 2.143e-14 (77 / 77 iterations) |
| G3.3 checkerboard regression (8³) | PASS: RC amplitude 3.411e-14 (≤ 1e-6), max\|u\| 4.050e-13 (≤ 1e-8), 60 iterations. *Reported*: the linear flux leaves amplitude 6.220e-12 after 3702 iterations |
| G3.4′(a) exact Cartesian limit | PASS (`…NonOrthogonalCorrectionIsExactlyInertOnCartesianHexahedra`): U, V, W systems equal with the correction on and off (`==`), explicit p′ flux exactly 0, every non-orthogonal area part 0 |
| G3.4′(b) N = 2 vs N = 0, tolerance 1e-11 | PASS: max \|Δu\| 2.220e-16, \|ΔF\|/max\|F\| 3.359e-16, \|Δp\|/range 1.948e-16 (≤ 1e-8) |

## 16. G4 — genuinely 3D MMS ([logs/04](logs/04_g4_mms_simple3d.log), [data/mms_simple3d.txt](data/mms_simple3d.txt))

Setup:

- Production `SIMPLE` with RC (automatic), from rest.
- Levels 8³, 16³ and 32³ took 43, 113 and 368 iterations (0.2, 6.3 and 236 s).
- Forcing check G4.0: worst deviation from 4th-order central differences 1.774e-10; max \|∇·U\|
  1.965e-13.

| quantity | L2 error 8 / 16 / 32 | L2 order 8→16, 16→32 | finest-pair gate |
| --- | --- | --- | --- |
| u | 1.216e-2 / 2.357e-3 / 6.141e-4 | 2.367, 1.940 | L1 1.849, L2 1.940 in [1.6, 2.4] PASS |
| v | 8.531e-3 / 1.830e-3 / 5.158e-4 | 2.221, 1.827 | L1 1.813, L2 1.827 PASS |
| w | 4.479e-3 / 1.122e-3 / 3.209e-4 | 1.997, 1.806 | L1 1.811, L2 1.806 PASS |
| p (gauge-free) | 2.613e-2 / 7.620e-3 / 1.741e-3 | 1.778, 2.130 | L1 2.391, L2 2.130 in [1.5, 2.4] PASS |
| face flux (reported) | 7.476e-3 / 1.003e-3 / 1.933e-4 | 2.898, 2.376 | — |

L∞ (u, v, w, p) also decreases at every refinement (u 6.46e-2 / 1.34e-2 / 2.08e-3; p 9.50e-2 /
3.37e-2 / 1.15e-2). All norms and orders are in [data/mms_simple3d.txt](data/mms_simple3d.txt).

- Every L1, L2 and L∞ decreases.
- Continuity L∞ per volume: 3.147e-9, 1.842e-7, 4.614e-6 (≤ 1e-5) PASS. It grows as h⁻³ × the
  outer residual: an iterative quantity, not a discretization one.
- Global mass imbalance ≤ 3.1e-32 (≤ 1e-14) PASS.
- **DECISION G4: every item PASSES** (29 of 29 gate lines).
- Note: the pressure L1 finest-pair order 2.391 is inside the band but close to its upper edge.

## 17. G5 — analytical square duct (production case path) — **FAILED** (original gate; G5.1 superseded by A3, PASS: §31)

Configuration:

- Committed case `cases/duct_3d`: 48×8×8, Re = 10, central, RC, relaxation 0.7/0.3, tolerances
  1e-9.
- Variants n = 16, 24 are written by CaseWriter. The solver configuration is asserted identical to
  the committed case before each run.
- Each level ran through CaseReader → CaseBuilder → ProjectRunner → export.
- Logs: [05a](logs/05a_g5_duct_n8.log), [05b](logs/05b_g5_duct_n16.log),
  [05c](logs/05c_g5_duct_n24.log), gate [05d](logs/05d_g5_duct_gate.log),
  [data/duct3d_gate.txt](data/duct3d_gate.txt).

Exact reference (series m, n ≤ 399): K = 0.0351443 (f Re = 56.908), u_max/U = 2.096256. The
literature values are 0.035144 and 2.0962 (`Duct3DProductionCase.SeriesReproducesTheSquareDuctConstants`).

| n | cells | iterations | L∞/U | RMS/U | \|dp/dx err\|/G | \|u_max err\|/u_max | section / global mass |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 8 | 3 072 | 67 | 8.4997e-2 | 4.1910e-2 | 5.551e-2 | 6.691e-2 | 1.98e-12 / 1.93e-12 |
| 16 | 24 576 | 183 | **2.3357e-2** | 1.1483e-2 | 1.474e-2 | 1.776e-2 | 7.98e-12 / 3.39e-12 |
| 24 | 82 944 | 362 | **1.0577e-2** | **5.2021e-3** | 6.632e-3 | 7.990e-3 | 1.44e-11 / 3.52e-12 |

The official gate ([logs/05d](logs/05d_g5_duct_gate.log)) fails three items and passes the other fifteen:

| item | measured | limit | result |
| --- | --- | --- | --- |
| G5.1 n = 16 L∞ | 2.3357e-2 | 0.020 | **FAIL** |
| G5.1 n = 16 \|dp/dx err\|/G | 1.4742e-2 | 0.030 | PASS |
| G5.1 n = 24 L∞ | 1.0577e-2 | 0.010 | **FAIL** |
| G5.1 n = 24 RMS | 5.2021e-3 | 0.005 | **FAIL** |
| G5.1 n = 24 \|dp/dx err\|/G | 6.6323e-3 | 0.015 | PASS |
| G5.1 n = 24 \|u_max err\|/u_max | 7.9896e-3 | 0.010 | PASS |
| G5.2 RMS, dp/dx decreasing 8 → 16 → 24 | 4.19e-2 → 1.15e-2 → 5.20e-3; 5.55e-2 → 1.47e-2 → 6.63e-3 | monotone | PASS |
| G5.3 sections / global, n = 8, 16, 24 | ≤ 1.44e-11 / ≤ 3.52e-12 | 1e-6 | PASS |
| G5.4 Converged from rest, finite, exported, RC | 67 / 183 / 362 iterations | — | PASS |


Cross-flow velocity on the measurement plane: 1.5e-8. Axis-velocity variation over x ∈ [3a, 4.5a]:
1.6e-6 relative (fully developed). The downstream plane has the same errors to 6 digits.

**Diagnosis ([logs/05e](logs/05e_diag_duct_fv_reference.log), [tools/duct_fv_reference.py](tools/duct_fv_reference.py)).**
An independent numpy computation, not using CFDApp, solves the fully developed duct problem. That
is the 2D Poisson equation for u with the flow rate fixed. It is discretized exactly like CFDApp's
momentum diffusion (two-point face gradients, half-cell wall distance). It reproduces CFDApp's 3D
production results to 4–5 significant digits:

| n | L∞ (numpy FV) | CFDApp | RMS (numpy FV) | CFDApp | \|G err\|/G (FV) | CFDApp dp/dx | u_max err (FV) | CFDApp |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8 | 8.4997e-2 | 8.4997e-2 | 4.1910e-2 | 4.1910e-2 | 5.5513e-2 | 5.5512e-2 | 6.6907e-2 | 6.6907e-2 |
| 16 | 2.3357e-2 | 2.3357e-2 | 1.1483e-2 | 1.1483e-2 | 1.4744e-2 | 1.4742e-2 | 1.7760e-2 | 1.7760e-2 |
| 24 | 1.0577e-2 | 1.0577e-2 | 5.2021e-3 | 5.2021e-3 | 6.6339e-3 | 6.6323e-3 | 7.9896e-3 | 7.9896e-3 |

So the 3D SIMPLE converges to the exact discrete solution of its own second-order cell-centred
scheme. The developing 3D flow from the inlet, the RC flux, the w equation and the pressure
coupling add nothing measurable. The failure is in the **gate's thresholds**. The dp/dx and u_max
thresholds were derived from the 2D Poiseuille dp/dx error (0.585 % at 18 cells). The velocity L∞
and RMS thresholds were set to comparable values without deriving them from the scheme's velocity
error. The duct's corners make that error about 2× the 2D channel's. The scheme converges at
second order: numpy FV at n = 8, 16, 24, 32, 48 gives L∞ 8.50e-2, 2.34e-2, 1.06e-2, 5.99e-3 and
2.68e-3. It meets L∞ ≤ 0.020 at n = 18 (1.86e-2). It meets L∞ ≤ 0.010 and RMS ≤ 0.005 at n = 26
(9.03e-3 / 4.44e-3; logs/05e). These are observations; no threshold or grid has been changed. The
n = 24 prediction was logged at 10:48Z, before CFDApp's n = 24 run finished at 10:56Z. CFDApp
matched it to every printed digit.

## 18. G6 — directional symmetry — G6.1 PASS, **G6.2 FAILED** (original gate; G6.2 superseded by A3, PASS: §31) ([logs/06](logs/06_g6_directional_symmetry.log), [data/duct3d_symmetry_gate.txt](data/duct3d_symmetry_gate.txt))

The n = 16 duct along x, y and z: 183 iterations each, identical errors.

| pair | streamwise | cross 1 | cross 2 | pressure / range | d(dp/ds)/G |
| --- | --- | --- | --- | --- | --- |
| x vs y | 2.346e-12 | 2.106e-13 | 2.085e-13 | 1.736e-11 | 2.252e-11 |
| x vs z | 3.436e-12 | 1.156e-13 | 1.156e-13 | 2.127e-11 | 6.692e-11 |

- G6.1 (≤ 1e-6): **PASS** with margins of 10⁵–10⁷. There is no hidden x/y/z assumption anywhere in
  the 3D path, including w as the streamwise component.
- G6.2 (the z-duct meets G5.1's n = 16 thresholds): dp/dz 1.474e-2 ≤ 0.030 passes, but
  L∞ 2.3357e-2 > 0.020 **FAILS**. This is the same quantity and the same cause as G5.1 (§17).

## 19. G7 — lid-driven cube, Re = 1000 (production case path)

**Reference.** Albensoeder & Kuhlmann (2005), J. Comput. Phys. 206, 536–558, Tables 5 and 6 (cube,
rigid end walls, Re = 1000). The values come from the open transcription in tum-pbs/PICT
`tests/validations.py`, commit a95d7f9d0713262a1bff2bd9e2be5a203ee69208, function
`lid_driven_cavity_3D`, keys (1000, 1, 1, False).

- Their cavity is [−½, ½]³ with the lid at x = −½ moving +y.
- Mapping to CFDApp (lid ymax moving +x): x_AK = ½ − y, y_AK = x − ½, z_AK = z − ½;
  v_AK = u, u_AK = −v.

| # | Table 5 x_AK | v_AK | Table 6 y_AK | u_AK |
| --- | --- | --- | --- | --- |
| 1 | −0.5 | 1.0 | −0.5 | 0.0 |
| 2 | −0.4766 | 0.58964 | −0.4375 | −0.21738 |
| 3 | −0.4688 | 0.48443 | −0.4297 | −0.22746 |
| 4 | −0.4609 | 0.39821 | −0.4219 | −0.23503 |
| 5 | −0.4531 | 0.33171 | −0.4062 | −0.24407 |
| 6 | −0.3516 | 0.12183 | −0.3437 | −0.22924 |
| 7 | −0.2344 | 0.07334 | −0.2734 | −0.17580 |
| 8 | −0.1172 | 0.03905 | −0.2656 | −0.16987 |
| 9 | 0.0 | 0.00802 | 0.0 | −0.03674 |
| 10 | 0.0469 | −0.00612 | 0.3047 | 0.15223 |
| 11 | 0.2187 | −0.10999 | 0.3594 | 0.31117 |
| 12 | 0.3281 | −0.25160 | 0.4063 | 0.43423 |
| 13 | 0.3984 | −0.27293 | 0.4453 | 0.33511 |
| 14 | 0.4297 | −0.23696 | 0.4531 | 0.29032 |
| 15 | 0.4375 | −0.22283 | 0.4609 | 0.24095 |
| 16 | 0.4453 | −0.20623 | 0.4688 | 0.18864 |
| 17 | 0.5 | 0.0 | 0.5 | 0.0 |

**Configuration.**

- Committed case `cases/lid_driven_cavity_3d_re1000` (32³): QUICK, RC, relaxation 0.7/0.3,
  tolerances 1e-7, momentum BiCGSTAB rel 1e-6, pressure BiCGSTAB rel 1e-4, no preconditioner
  (Amendment A2.1).
- The 48³ and 64³ variants differ only in nx, ny and nz, which is asserted before each run.
- Centreline sampling: trilinear, on the cell-centre lattice extended by the wall values. The
  sampler is exact for linear fields (`LidDrivenCube3DProductionCase.CentrelineSamplerIsExactForLinearFields`).
- The three levels ran concurrently with G5/G6 (one process each). They were launched before G5
  failed, and were allowed to finish and preserved as evidence after the failure. The run-level
  times below are wall-clock under that load.
- Logs: [07a](logs/07a_g7_cube_32.log), [07b](logs/07b_g7_cube_48.log),
  [07c](logs/07c_g7_cube_64.log), gate [07d](logs/07d_g7_cube_gate.log),
  [data/cavity3d_re1000_gate.txt](data/cavity3d_re1000_gate.txt).

Results: the levels finished and the gate test ran after G5 had failed (12:23Z). They were preserved
and recorded here, and they do not change the G5 decision.

| grid | status | iterations | wall time (under load) | max \|Δ\| | RMS | extrema \|Δ\| (T5 min / T6 min / T6 max) | spanwise symmetry | net boundary flux |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 32³ | Converged | 249 | 195 s | 0.1040 | 0.0486 | 0.0843 / 0.0428 / 0.1040 | 9.2e-8 | 0 |
| 48³ | Converged | 441 | 1525 s | 0.0938 | 0.0370 | 0.0394 / 0.0158 / 0.0502 | 4.8e-7 | 0 |
| 64³ | Converged | 680 | 6175 s | 0.0532 | 0.0210 | 0.0176 / 0.0088 / 0.0289 | 5.6e-7 | 0 |

**G7 gate: every item passes (14/14).**

- G7.1 at 64³: max \|Δ\| 0.0532 ≤ 0.06; RMS 0.0210 ≤ 0.03; extrema within 0.03 (the Table 6
  maximum, 0.0289, is the closest).
- G7.2: monotone decrease.
- G7.3: symmetry 5.6e-7 ≤ 1e-5.
- G7.4: all Converged, net flux 0.

Station-by-station values for all three grids are in [logs/07d](logs/07d_g7_cube_gate.log).

## 20. G8 — global mass conservation

| case | quantity | value | criterion |
| --- | --- | --- | --- |
| G5 duct n = 8 / 16 | \|m_in − m_out\|/m_in | 1.93e-12 / 3.39e-12 | ≤ 1e-6 (G8.1) PASS |
| G5 duct n = 24 | same | 3.52e-12 — PASS | ≤ 1e-6 |
| G6 ducts x / y / z | same | 3.39e-12 / 2.65e-12 / 2.53e-12 | ≤ 1e-6 PASS |
| G3.2, G3.3 (8³ closed) | \|net boundary flux\| | 0 (closed walls: boundary fluxes are exactly 0) | ≤ 1e-12 PASS |
| G4 (closed MMS) | global mass imbalance | ≤ 3.1e-32 | ≤ 1e-14 PASS |
| G7 cube 32³ | \|Σ F_b\| | 0.0 | ≤ 1e-12 PASS |
| G7 cube 48³ / 64³ | same | 0.0 / 0.0 | ≤ 1e-12 PASS |

G8.3 (max and RMS cell imbalance reported): metadata.json `conservation.max_cell_imbalance` and
`rms_cell_imbalance` are present for every 3D result (asserted in
`Duct3DProductionCase.CommittedDuctRunsConservesAndExports`).

## 21. G9 — case format, rejection, exports, CLI, GUI

- **G9.1 case format** — PASS (`Case3DTest`, 8 tests, [logs/02](logs/02_focused_3d_tests.log)).
  Parsing, CaseBuilder (24 hexahedra, six patches, RC setting, 3-component initial state), and the
  CaseWriter round trip (lossless; writing the reread definition again reproduces every file byte
  for byte). A 2D case writes no depth, nz or face_flux, and 2-component vectors.
- **G9.2 rejection** — PASS. Every listed malformed case is rejected by CaseReader with a message
  naming the file and the field:
  - box without nz; nz with a rectangle; nz = 0, −2, 1.5, "2";
  - depth missing, 0, −0.5, "0.5"; depth with a rectangle;
  - grading, structured_quad or multiblock with a box;
  - a 2-component velocity in 3D; a 3-component velocity in 2D;
  - 2D patch names on a box; a missing 3D patch;
  - a 2- or 3-component initial velocity of the wrong dimension;
  - thermal, thermal + buoyancy (named "buoyancy"), k-ε, SST, species, multiphase and
    compressible in 3D;
  - an invalid `face_flux`.

  ProjectRunner returns InvalidCase, `exitCodeFor` = 2, with no mesh and no solver result. No raw
  CLI fixture (tests/data/cases) for 3D was added before the stop (§29).
- **G9.3 exports** — PASS through ProjectRunner, the CLI's own run function
  (`Duct3DProductionCase.CommittedDuctRunsConservesAndExports`):
  - fields.csv has header `cell_id,x,y,z,velocity_x,velocity_y,velocity_z,velocity_magnitude,pressure`,
    3072 rows, and values equal to the solution.
  - residuals.csv has the `w_residual` column, one row per iteration.
  - solution.vtk was parsed back: 49·9·9 points, 3072 `8 …` hexahedra of type 12, pressure and the
    3-component velocity equal to the solution **bit for bit** (17-digit output).
  - metadata.json has dimension 3, nx/ny/nz 48/8/8, lx 6, lz 1, face_flux rhie_chow, residuals.w
    and the full conservation block.
  - The CLI prints the mesh as "16 x 16 x 16 (3D)", plus "W residual", "Face flux: rhie_chow",
    "Mass balance: inflow …, outflow …, relative imbalance …" and "Cell continuity: max …, rms …,
    normalized …" ([logs/03b](logs/03b_cli_stdout_3d_light_cube.log), the stdout of a diagnostic
    CLI run of the light cube). No ctest CLI 3D fixture checks it (§29).
- **G9.4 GUI — NOT DONE** (§29). The GUI was not changed in MESH-006. Opening a 3D case in the GUI
  has not been verified: the 2D contour code may throw on a 3D mesh, and the mesh and boundary
  editors have no nz, depth or w fields.

## 22. CPU baseline — NOT RUN at the stop (§29; recorded later: §32)

The pre-registered CLI baseline (lid cube Re = 100 at 16³, 32³, 64³, 1 thread, with nothing else
running) was not run because the phase stopped at G5. The only timing data are informal:

- G4 MMS at 32³: 236 s for 368 iterations, 0.64 s per iteration, 32 768 cells.
- The G7 runs (under concurrent load): 32³ took 195 s for 249 iterations (0.79 s per iteration);
  48³ took 1525 s for 441 iterations (3.5 s); 64³ took 6175 s for 680 iterations (9.1 s).
- The A1 smoke check (acceptance_gate.md A1): 64³ at about 6 s per iteration.

The boundary-condition lookup `boundaryPatchNameForFace` is linear in the number of boundary
faces per call. It was measured before the gate at about 25–30 % of the time at 64³ and was
deliberately not optimized (§28).

## 23. G10 — 2D compatibility — **early probe PASS; final G10 NOT RUN** at the stop (§29; final G10 PASS: §32)

- **Early bit identity** ([logs/01](logs/01_early_2d_bit_identity_core.log)), after the core
  numerics changes (momentum W, gradients, interpolation, pressure correction, relaxed momentum,
  monitor). The MESH-005 probe and the MESH-006 probe (`tools/bitprobe6.cpp`) were each compiled
  against BASE and against the working tree. The MESH-006 probe covers:
  - 2D momentum systems for 4 schemes, with and without the non-orthogonal correction;
  - velocity gradients and response coefficients;
  - pressure-correction systems and flux/velocity correction;
  - SIMPLE solves (capped at 25 iterations, 4 variants).

  Both probes are **BITWISE IDENTICAL** (37 and 82 output lines).
- Changes made after that probe that reach 2D code:
  - SIMPLE.cpp: the face-flux branch (Linear for every 2D case), W bookkeeping (3D only), progress
    fields, and the pressure gradient computed only for RC;
  - exports (3D branches);
  - the CLI (3D lines);
  - case parsing (3D keys);
  - the CaseReader 3D refusal order (3D only).

  By construction none of them changes a 2D value. **This was not re-verified**: the final
  bitprobe6 run, the CLI BASE-vs-NEW comparison of every 2D case and fixture, the MESH-001–005
  suites by name and the output classification (G10.1–10.5) were not run before the stop.

## 24. G11 — full regression — NOT RUN at the stop (§29; PASS: §32)

Release and Debug + GUI full ctest were not run at the stop point. Focused counts are in §25.

## 25. Focused test counts ([logs/02](logs/02_focused_3d_tests.log), Release, 0 warnings)

| suite | tests | result |
| --- | --- | --- |
| `SIMPLE3D.*` (G1.1, G1.2, G2.1–2.3, G3.1–3.4′, BCs) | 14 | 14 PASS |
| `Operators3DTest.*` (MESH-005 suite + G1.3) | 10 | 10 PASS |
| `MMSSimple3DTest.*` regular (forcing check, 6³/12³ guard) | 2 | 2 PASS (+ gate study DISABLED, run in logs/04) |
| `Case3DTest.*` (G9.1, G9.2) | 8 | 8 PASS |
| `Duct3DProductionCase.*` / `LidDrivenCube3DProductionCase.*` regular | 4 | 4 PASS (+ 9 DISABLED gate tests, run in logs/05–07) |
| **total regular** | **38** | **38 PASS** |

Gate studies: G4 3/3 passed ([logs/04](logs/04_g4_mms_simple3d.log)). G5 levels 3/3 ran;
**G5 gate FAILED**. G6 **FAILED** (G6.2). G7: 3/3 levels Converged, gate 14/14 PASS (completed after the stop; §19).

## 26. Files changed (vs the pre-MESH-006 snapshot; `tools/m6files.sh`)

New:

- `include/cfd/pressure_velocity/RhieChow.hpp`, `src/pressure_velocity/RhieChow.cpp`
- `tests/solver/simple/test_simple3d.cpp`, `tests/integration/mms/test_mms_simple3d.cpp`,
  `tests/integration/case/test_3d_production_cases.cpp`, `tests/unit/io/test_case3d.cpp`
- `cases/duct_3d`, `cases/lid_driven_cavity_3d`, `cases/lid_driven_cavity_3d_re1000` (6 files each)

Modified (lines + / −):

- numerics:
  - `MomentumEquation` (+21/−4, +22/−20); `VectorGradient` (+12/−4, +24/−6);
    `Interpolation` (+8/−5, +18/−6);
  - `PressureCorrectionEquation` (+17/−5, +77/−39); `RelaxedMomentum` (+7/−1, +33/−4);
  - `SIMPLE.cpp` (+105/−21); `SIMPLESettings` (+28, +25); `SIMPLEResult.hpp` (+10);
    `SIMPLEProgress.hpp` (+5);
  - `SolverRobustness` (+11, +29/−7); `ContinuityEquation` (+26, +36);
- 2D-only guards:
  - `CompressibleMassFlux`, `CompressibleMomentum`, `CompressiblePressureCorrection`,
    `CompressibleRelaxedMomentum` (+1 each); `CompressibleSIMPLE` (+4);
  - `PISO` (+3); `TransientMomentum` (+4); `KEpsilonModel`, `KOmegaModel` (+2 each);
- case path:
  - `GeometryConfig.hpp` (+11), `MeshConfig.hpp` (+7/−2), `InitialConditions.hpp` (+4),
    `SolverConfig.hpp` (+4);
  - `GeometryConfigParser` (+15/−4), `MeshConfigParser` (+22/−1), `CaseConfigParser` (+15/−2),
    `BoundaryConfigParser` (+10/−5), `SolverConfigParser` (+10/−1);
  - `JsonUtil` (+25, +6), `Parsers.hpp` (+3/−1);
  - `CaseReader` (+51/−1), `CaseBuilder` (+11), `CaseWriter` (+18/−3);
- exports / CLI / runner: `CSVWriter` (+9, +43), `JSONWriter` (+45/−4), `ResultExporter` (+21/−1),
  `apps/cli/main.cpp` (+23/−4), `ProjectRunner.cpp` (+1/−1);
- build / tests:
  - `src/CMakeLists.txt` (+1);
  - the CMakeLists of tests/solver/simple, tests/integration/mms, tests/integration/case and
    tests/unit/io;
  - `tests/unit/discretization/test_operators3d.cpp` (+127/−47: the MESH-005 guard test becomes
    G1.3).

No existing 2D case, fixture or test input was modified. GUI sources, documentation (README,
ROADMAP, user guide) and TODO.md checkboxes were not changed in MESH-006. TODO.md's status lines
were updated as recorded in §30.

Note on the gate binary: G5–G7 ran on a frozen copy of the Release `CFDCaseIntegrationTests`
(`tools/gate_freeze.sh`, sha256 `50b675bb…0f45a`, recorded in each log). Two source changes came
after the freeze:

- the CaseReader refusal order (buoyancy is now named before thermal; this affects only invalid
  3D cases);
- `tests/unit/io/test_case3d.cpp` (a different test target).

Neither touches any code path of a valid case.

## 27. Failed experiments and diagnostics (all preserved)

1. **G3.4 as first written was infeasible.** Agreement to 1e-12 end to end cannot hold because
   BiCGSTAB's relative criterion is relative to the initial residual. It was replaced by G3.4′
   (Amendment A1) before any gate run, disclosing the two smoke checks made before it.
2. **G5.1 / G6.2 FAILED**: velocity L∞ 0.023357 > 0.020 at n = 16. The independent FV reference
   (§17) shows it is exactly the scheme's discretization error; the threshold was too tight.
   Preserved: logs 05a–05e and 06, data/duct3d_*.json, and the tool.
3. **Spanwise asymmetry of the light cube** (7.8e-8 against an ad-hoc 1e-8 sanity bound in a
   regular test, not a gate). Diagnosed in [logs/03](logs/03_diag_spanwise_asymmetry_vs_tolerance.log):
   the asymmetry scales linearly with the outer tolerance (6.1e-8 / 6.9e-10 / 6.7e-12 / 2.4e-13 at
   1e-6 / 1e-8 / 1e-10 / 1e-12), so it is iterative, not discretization. The regular test's bound
   became the case's outer tolerance, with the log cited. The source of the iterative asymmetry is
   not established; the pinned corner reference cell is a candidate that was not tested.
4. Test-input corrections during development (not solver changes): an incomplete multiblock
   mesh.json and a multiphase physics block with the wrong baseline viscosity in the new G9.2 test.

## 28. Limitations and known issues

- **G5/G6.2 thresholds** (§17, §30): the cell-centred scheme's duct velocity error is
  2.34 % U at n = 16 and about 1.06 % U at n = 24.
- The boundary-condition lookup is O(boundary faces) per face. It costs about 25–30 % of the
  runtime at 64³ and was not optimized (no optimization in MESH-006).
- The 3D case format is uniform Cartesian boxes only. 3D is laminar incompressible steady SIMPLE
  only; PISO/transient, turbulence, thermal, species, multiphase and compressible refuse 3D.
- No GPU 3D SIMPLE (none was intended).
- The G7 inner pressure solver has no preconditioner, because the case format has no
  preconditioner key (A2.1).
- Unrelated known issues, preserved untouched:
  - MESH-004 irregular-mesh convergence;
  - the MESH-004 CG breakdown threshold;
  - MESH-005 explicit `diffusion()` with two cells along a boundary normal.

## 29. Not done because the phase stopped at the first failed gate (each done later: §32)

- **G9.4 GUI**: CaseModelAdapter (nz, depth, w), mesh and boundary editor fields, empty contours for
  3D, the "w" residual series, and a GUI test.
- **Raw CLI fixtures** for 3D (a valid smoke run with the W residual and mass-balance lines; exit
  code 2 fixtures).
- **CPU baseline** (§22).
- **G10 final** (§23) and **G11 full regression** (§24).
- **Documentation**: README, ROADMAP and the user guide for the 3D case format.
- **clang-format**: 14 of the 56 new or modified C++ files are not clang-format-18 clean
  ([logs/08](logs/08_clang_format_dry_run.log), dry run only). They were not reformatted, and the
  CI format job would fail until they are.
- **TODO checkboxes**: all stay `[ ]` (pre-registered rule: `[x]` only after the gate and the full
  regression pass).

## 30. Decision at the original gate (historical record, unchanged; final decision: §34)

**P12-MESH-006 BLOCKED / FAILED GATE.**

- First failed item: **G5.1**. At n = 16 the cross-section velocity L∞ is 0.023357 U against the
  pre-registered limit of 0.020 U. At n = 24 the L∞ is 0.010577 (limit 0.010) and the RMS is
  0.0052021 (limit 0.005). G6.2 fails on the n = 16 quantity. Every other G5 item passes:
  dp/dx, u_max, monotone convergence, section mass flow and the solve.
- Passed before the failure: G1, G2, G3 (with G3.4′), G4, G9.1, G9.2, G9.3 (through ProjectRunner),
  G6.1, and the G8 items measured.
- The failure is not an implementation defect. CFDApp's 3D production SIMPLE reproduces, to 4–5
  digits, an independent computation of the discrete fully developed solution of the same
  second-order scheme (§17). The G5.1 and G6.2 velocity thresholds were set below that scheme's
  discretization error. They are not changed here (stop rules). Whether to amend the gate (for
  example, deriving the thresholds from the scheme's discretization error, or using finer grids)
  is the user's decision.
- MESH-007 was not started. Nothing was committed or pushed.

---

# After the original decision: investigation, Amendment A3, completion

Everything above (§1–§30) is the historical record of the original pre-registered gate and its
failure, kept as written. The only later edits are:

- forward references added to section headings, with §30 retitled "Decision at the original gate
  (historical record, unchanged …)";
- the four pending G7 placeholders, filled when those runs finished;
- the status line at the top, which now gives the chronology and quotes the original status in
  full.

## 31. Investigation, Amendment A3 and the fresh acceptance run

**Investigation** ([g5-investigation/README.md](g5-investigation/README.md), evidence only, preserved
unchanged).

- Classification: **D — PRE-REGISTERED THRESHOLD DESIGN DEFECT**. A (implementation), B
  (reference/metric), C (configuration) and E (inconclusive) were each excluded by direct evidence.
- CFDApp equals an independent exact solution of the same discrete equations to ≤ 8.1e-8 U
  (n = 8…32), with identical observed orders (→ 2.000).
- The analytical reference and the norm arithmetic were verified independently.
- The limits sit below the scheme's own discretization error. The derivation carried the 2D channel
  error over to the duct without computing the duct's velocity error, which is 4.1× the channel's.
- The original gate and its evidence were hash-checked unchanged before and after.

**Authorization.** The user explicitly authorized amendment A3 after the investigation
(2026-09-15).

**A3** ([acceptance_gate_A3.md](acceptance_gate_A3.md)) replaces G5.1 and G6.2 only.

| id | criterion |
| --- | --- |
| G5.1-A | agreement with the independent discrete solution (\|u − u_disc\| ≤ 1e-5 U, cross-flow ≤ 1e-5 U, dp/dx ≤ 1e-4) at n = 8, 16, 24 |
| G5.1-B | monotone convergence of L∞, RMS, dp/dx and u_max |
| G5.1-C | velocity L∞ order in [1.8, 2.2] on 16 → 24 |
| G5.1-D | at n = 24: dp/dx ≤ 1.5 %, u_max ≤ 1 %, L∞ and RMS ≤ 1.25 × the frozen independent prediction |
| G5.1-E | GCI asymptotic indicator in [0.95, 1.05] for G and u_max |
| G6.2-A3 | the z-duct's w against the discrete solution ≤ 1e-5 U |

The metric and problem are the same. G6.1 and every other gate are unchanged.

**Frozen before the rerun** ([a3/logs/00_freeze.log](a3/logs/00_freeze.log), 2026-09-15T12:36:10Z):

- the A3 document, sha256 `ad19946c…`;
- the independent expected values [a3/data/frozen_expected.json](a3/data/frozen_expected.json),
  sha256 `b0b68b6d…`, computed without CFDApp;
- the evaluator [a3/tools/a3_gate.py](a3/tools/a3_gate.py), sha256 `5de2f9e5…`;
- the run script.

No fresh-run output existed at that time.

**Fresh acceptance run** ([a3/logs/04_a3_gate.log](a3/logs/04_a3_gate.log)):

- rebuilt and frozen Release binaries;
- five new production CLI runs: x-duct n = 8, 16, 24; y- and z-duct n = 16;
- three new ProjectRunner level runs of the unchanged G5 test code, in a relocated working
  directory.

**46/46 items pass.**

| item | result |
| --- | --- |
| G5.1-A | max \|u − u_disc\|/U 3.55e-8 / 6.38e-8 / 7.08e-8 (n = 8 / 16 / 24; limit 1e-5); cross-flow ≤ 1.53e-8; \|dp/dx − G_disc\|/G_disc ≤ 1.54e-6 (limit 1e-4) |
| G5.1-B | L∞ 8.50e-2 → 2.34e-2 → 1.06e-2; RMS 4.19e-2 → 1.15e-2 → 5.20e-3; dp/dx 5.55e-2 → 1.47e-2 → 6.63e-3; u_max 6.69e-2 → 1.78e-2 → 7.99e-3 |
| G5.1-C | p(L∞, 16 → 24) = 1.954 (reported: RMS 1.953, L1 1.929, dp/dx 1.970, u_max 1.970; 8 → 16: 1.86–1.91; each equal to the independent prediction) |
| G5.1-D | dp/dx 0.663 % ≤ 1.5 %; u_max 0.799 % ≤ 1 %; L∞ 1.05773e-2 ≤ 1.32216e-2 (predicted 1.05772e-2; margin 20.0 %); RMS 5.20207e-3 ≤ 6.50257e-3 (predicted 5.20205e-3; margin 20.0 %) |
| G5.1-E | GCI indicator 1.0082 (G) and 1.0099 (u_max), p 1.88; Richardson G 2.84672 vs exact 2.84542, u_max 2.09741 vs exact 2.09626; exact inside the fine-grid GCI band for both |
| G5.2–G5.4 (unchanged) | RMS and dp/dx decreasing; sections ≤ 1.44e-11, global ≤ 3.52e-12; all Converged from rest, finite, exported, Rhie–Chow |
| G6.1 (unchanged) | x vs y and x vs z: ≤ 3.44e-12 (velocity), ≤ 2.13e-11 (pressure/range), ≤ 6.69e-11 (dp/ds) |
| G6.2-A3 | z-duct max \|w − u_disc\|/U 6.38e-8; cross-flow 1.53e-8 |

- **Decision: A3 G5 PASS, A3 G6 PASS.**
- The fresh fields are byte-identical to the investigation's runs (determinism;
  [a3/logs/05](a3/logs/05_fresh_vs_investigation_fields_sha256.log)).
- By construction, an implementation that reproduces the scheme has a margin of about 20 % against
  the G5.1-D velocity envelope. Discriminating power lies in G5.1-A (1e-5 U).

## 32. Completion work after A3 PASS (evidence: [a3/README.md](a3/README.md))

**G9.3 CLI fixtures and G9.2 exit codes** (§8 of the authorization).

- Five raw `cfdapp` process tests, `CFDAppCli3D_*` in `tests/CMakeLists.txt`, run through
  `tests/cli_expect.cmake`. It runs the CLI on a copy of the fixture in the build tree, so nothing is
  written into tests/data, and checks the exact exit code and every expected line.
- 3D duct (`tests/data/cases/valid_duct3d_cli_smoke`, 12×4×4) and 3D cavity
  (`valid_cube3d_cli_smoke`, 6³): exit 0, and the report has `Mesh: … (3D)`, `Mesh quality: valid`,
  `Converged: yes`, U, V, W and P residuals, continuity, mass imbalance, `Face flux: rhie_chow`, the
  mass balance and the cell continuity.
- Box without nz, a 2-component velocity, thermal physics in 3D: exit 2, with a message naming the
  file and field.
- The committed 3D cases run through the CLI:
  - `cases/duct_3d` (A3 runs);
  - `cases/lid_driven_cavity_3d` (CPU baseline);
  - `cases/lid_driven_cavity_3d_re1000` (G7).

**G9.4 GUI** (§9).

- **`CaseModelAdapter`:** geometry depth, mesh nz and the velocity `valueZ` / `velocityZ` in both
  directions. An editor map without them keeps the previous values, so a page that does not show
  them cannot drop them. 2D maps only gain zero-valued keys.
- **`SimulationController`:**
  - `resultsThreeDimensional`;
  - the field map, contours, vectors, probe and line sampler return nothing for a 3D result instead
    of throwing;
  - `meshCellInfo` has nz and dz;
  - validation records the built mesh's `dimension`.
- **`VisualizationSnapshot` (app layer):**
  - nz, the w velocity and the W residual series;
  - results reload with CSV columns located by header name (the fixed column positions would have
    misread 3D files);
  - a trailing CR is stripped. Some committed 2D results have CRLF line endings; this keeps them
    loading.
- **QML:**
  - mesh page: depth and nz for a box, with grading hidden for 3D;
  - boundary page: the six patches in order and a Z velocity field;
  - results page: a "w" series colour and a 3D notice.
- **Tests:**
  - `CaseEditingTest.ThreeDimensionalCaseLoadsValidatesPreservesZAndRunsFromTheGui` and
    `CaseEditingTest.TwoDimensionalResultsHaveNoWSeries` (GUI);
  - `VisualizationSnapshotTest.ThreeDimensionalResultLiveAndReloaded`,
    `…TwoDimensionalResultHasNoThirdComponent` and
    `LoadSnapshotFromResultsTest.CrlfResultsFilesReload` (app layer).
- **qmllint** of the three changed pages: 0 errors; only the existing "Unqualified access" pattern
  grew ([a3/logs/10](a3/logs/10_qmllint_gui.log)).
- The pages have not been visually inspected by a human. The evidence is controller-level tests and
  qmllint.

**clang-format** (§10).

- clang-format-18 on the 16 MESH-006 files the dry run flagged. Two of them come from the GUI work.
- The change is layout only ([a3/logs/11](a3/logs/11_clang_format_apply.log)):
  - whitespace;
  - comment re-wrapping and string-literal splits in three test files;
  - one include moved by SortIncludes in each of `apps/cli/main.cpp` and `SIMPLE.cpp`.
- Afterwards the repository-wide dry run over include/src/apps/tests (CI's scope) reports **0 of 555
  files would change** ([a3/logs/11b](a3/logs/11b_clang_format_check_after.log)).
- The final builds have 0 warnings ([a3/logs/12](a3/logs/12_final_builds.log)).
- The final Release `cfdapp` is **byte-identical** (sha256 `df4ee6d0…`) to the frozen binary of the
  A3 acceptance run. The A3 result therefore applies to the final CLI binary itself.

**CPU baseline** (§11; [a3/logs/14](a3/logs/14_cpu_baseline.log); measurement only).

- Lid-driven cube Re = 100 (`cases/lid_driven_cavity_3d`, QUICK, Rhie–Chow, tolerances 1e-6)
  through the Release CLI.
- Intel Core i9-14900HX (WSL2), GCC 11.4, `-O3 -DNDEBUG`.
- **Single thread** (`CFDAPP_ENABLE_OPENMP=OFF`), nothing else running.

| grid | cells | SIMPLE iterations | wall / CPU time | time per iteration | cell-iterations/s (CPU) | peak RSS |
| --- | --- | --- | --- | --- | --- | --- |
| 16³ | 4 096 | 85 | 3.61 s / 3.66 s | 0.043 s | 95 126 | 16 MB |
| 32³ | 32 768 | 251 | 124.5 s / 124.2 s | 0.495 s | 66 211 | 93 MB |
| 64³ | 262 144 | 774 | 5739 s / 5737 s | 7.41 s | 35 367 | 761 MB |

- Linear-solver iteration counts are not exported by the CLI (not available).
- Throughput per cell falls with size; the O(boundary faces) boundary-condition lookup (§28) is part
  of that. No optimization in MESH-006.

**G10 — 2D compatibility on the final sources** ([a3/logs/13_*](a3/logs/)).

- **G10.1:** the MESH-005 probe (37 lines) and the MESH-006 probe (82 lines: 2D momentum systems for
  4 schemes, pressure correction, velocity/flux correction, SIMPLE solves) are **BITWISE IDENTICAL**
  between BASE (the pre-MESH-006 tree) and the final build, on every 2D committed case.
- **G10.2:** **27 of 27** 2D committed cases and CLI fixtures are **IDENTICAL**, BASE vs final:
  - fields.csv, solution.vtk, residuals.csv byte for byte;
  - metadata.json as parsed JSON;
  - stdout and exit codes.

  The eight 3D cases and fixtures are new and BASE rejects them by design, so they are excluded.
- **G10.3:** the MESH-001–005 suites by name all pass on the final binaries
  ([a3/logs/15](a3/logs/15_focused_tests_final.log), stages 11a–11o):
  - MESH-001: 54 tests;
  - MESH-002: 42;
  - MESH-003: 27;
  - MESH-004: 36;
  - MESH-005: 7, plus `Cartesian3DMeshTest` 12 and `Operators3DTest` 10 in stages 1 and 3.

  The whole mesh, io, discretization, SIMPLE, MMS, case, core, fields and validation suites pass as
  well (stages 12a–12i).
- **G10.4:** 161 existing case/fixture input files compared, **0 different**.
- **G10.5** ([a3/logs/19](a3/logs/19_g10_5_generated_outputs.log), [19b](a3/logs/19b_g10_5_restore.log),
  [19c](a3/logs/19c_g10_5_after_restore.log)), after every test run of the phase: 580 regenerated
  outputs under results/, cases/\*/results/ and tests/data/cases/\*/results/ compared with BASE.
  - 532 identical, 48 runtime-only, **0 values, 0 new**.
  - The 48 runtime-only files were restored from BASE (not `git checkout`). Afterwards 580/580 are
    identical.
  - No working-tree entry appeared outside this evidence directory during the verification runs
    (`a3/tools/status_check.sh`).

**Focused tests on the final binaries** ([a3/logs/15](a3/logs/15_focused_tests_final.log)).

- Staged in the authorized order, stopping at the first failure: 3D mesh, case parsing, 2D-only
  guards, w-momentum, face flux / RC, 3D SIMPLE, MMS, duct / cavity / export, VTK / JSON / CSV,
  app layer, then G10.3 and the whole suites.
- gtest stages: **1229 run, 1229 passed, 0 failed** (stages overlap: a test can count in two).
- GUI (Debug, offscreen): **52/52**.
- CLI process tests (`ctest -R "CFDAppCli|CFDAppSmokeTest"`): **18/18**.

**G11 — full regression** ([a3/logs/16](a3/logs/16_full_regression_release.log),
[17](a3/logs/17_full_regression_debug_gui.log); sequential, nothing else running).

| build | result | listed | disabled (not run) | time |
| --- | --- | --- | --- | --- |
| Release (-O3, GUI off) | **1862/1862 passed** | 1906 | 44 | 164 s |
| Debug + GUI (offscreen) | **1914/1914 passed** | 1958 | 44 | 1000 s |

MESH-005 ended at 1825 and 1875. Of the 44 disabled tests, 10 are this phase's gate studies (G4
study, G5 levels and gate, G6, G7 levels and gate), each run in its own logged gate run.

**Supplementary: ASan + UBSan** (the configuration of CI's `sanitizers` job; not part of the
pre-registered G11, which is Release and Debug + GUI).

- [a3/logs/18](a3/logs/18_full_regression_asan_ubsan.log): `--timeout 1800`, 0 build warnings, **1858
  of 1862 passed**.
  - Three tests hit the 1800 s timeout: `NaturalConvectionValidation.GridConvergence`,
    `CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic` and
    `SIMPLEMMS.DistortedMesh`.
  - One test, `MeshQualityReport.DisconnectedMeshIsFatal`, failed with an ASan heap-use-after-free.
  - That is 1 AddressSanitizer diagnostic, 0 UBSan, 0 LeakSanitizer. The log is kept as run.
- **The timeouts came from the timeout I chose, not from a defect**
  ([a3/logs/18b](a3/logs/18b_asan_rerun_timeouts_ci_timeout.log)).
  - CI's sanitizer job uses `--timeout 7200`, because these tests took 1423–2220 s under ASan at the
    P12-NUM closeout (`.github/workflows/ci.yml`).
  - Rerun with CI's own timeout and sanitizer options: **3/3 passed** in 1107 s, 1260 s and 1670 s,
    each faster than those reference times, with 0 sanitizer diagnostics.
  - Their Release and Debug times are within 8 % of MESH-005's, both measured under `-j16`
    (for example `SIMPLEMMS.DistortedMesh` 102 s vs 97 s in Release, 700 s vs 721 s in Debug).
- **The use-after-free is pre-existing, in P12-MESH-004 test code**
  ([a3/logs/18a](a3/logs/18a_asan_diag_use_after_free.log)).
  - `tests/unit/mesh/test_mesh_quality_report.cpp:487-489` takes a pointer into the `issues` of the
    temporary report returned by `MeshQuality::evaluate(mesh)`. The temporary is destroyed at the end
    of that statement, and the next line reads through the pointer.
  - The test file and the `MeshQuality` sources are byte-identical to BASE.
  - BASE, built with the same sanitizer flags, reproduces the same diagnostic at the same lines.
  - The production code is not involved. The test passes silently without ASan (logs/16, 17).
  - MESH-001–005 never ran ASan (their summaries record it as not run), so this is the first
    sanitizer run over that work.
  - Not fixed: it is outside MESH-006's scope. Until it is fixed, **CI's sanitizer job will fail** on
    any push that contains the P12-MESH-004 work.

**Documentation.**

- User guide:
  - `docs/user_guide/case_format.md`: the 3D case format, box geometry, nz, the six patches,
    3-component values, `face_flux`;
  - `cli.md`: the 3D report and the malformed-case exits;
  - `gui.md`: editing, validating and running 3D cases, and what is empty for 3D results;
  - `paraview.md`: 3D hexahedra.
- README "Known limitations": the 3D and Rhie–Chow entries.
- ROADMAP: the P12-MESH sequence.
- TODO.md: the MESH-006 section.

**Files changed** (vs BASE, [a3/logs/20](a3/logs/20_files_changed_final.log), `tools/m6files.sh`).

- §26 lists the solver, case-path and test files of the original phase. Later changes are the ones
  listed in this section:
  - GUI (`CaseModelAdapter`, `SimulationController`, `SimulationControllerEditing`, three QML pages,
    `test_case_editing.cpp`);
  - app layer (`VisualizationSnapshot.hpp/.cpp`, `test_visualization_snapshot.cpp`);
  - CLI tests (`tests/CMakeLists.txt`, `tests/cli_expect.cmake`, five fixtures);
  - clang-format (layout only);
  - the user guide, README, ROADMAP, TODO.
- The listing's other "A" entries are ignored or empty directories (caches, `cmake_test_discovery_*`
  files, build outputs) that the snapshot, a copy of tracked and untracked-not-ignored files, does
  not contain. They are not MESH-006 changes.
- No existing 2D case, fixture or test input was modified (G10.4).

## 33. Final G1–G11 table

| gate | result | evidence |
| --- | --- | --- |
| G1 w-momentum | **PASS** (G1.1 permutation symmetry, 4 schemes; G1.2 W residual gate; G1.3 2D-only components refuse 3D) | §13 |
| G2 face flux, continuity | **PASS** (G2.1–G2.4) | §14 |
| G3 reduced / canonical coupling | **PASS** (G3.1–G3.3; G3.4′ by Amendment A1, written before any gate run) | §15 |
| G4 genuinely 3D MMS | **PASS** (29/29) | §16 |
| G5 analytical square duct | original G5.1 **FAILED** (§17, kept); **A3 G5.1-A…E PASS**; G5.2–G5.4 PASS | §17, §31 |
| G6 directional symmetry | G6.1 **PASS**; original G6.2 **FAILED** (§18, kept); **G6.2-A3 PASS** | §18, §31 |
| G7 lid-driven cube Re = 1000 | **PASS** (14/14) | §19 |
| G8 global mass conservation | **PASS** (open ≤ 3.52e-12, closed 0, MMS ≤ 3.1e-32; cell imbalance reported) | §20, §31 |
| G9 case format, rejection, exports, CLI, GUI | **PASS** (G9.1–G9.3; CLI fixtures with exit 0 / 2; G9.4 GUI) | §21, §32 |
| G10 2D compatibility | **PASS** (G10.1 bitwise identical; G10.2 27/27; G10.3 by name; G10.4 0 of 161; G10.5 0 values) | §23, §32 |
| G11 full regression | **PASS** (Release 1862/1862; Debug + GUI 1914/1914) | §32 |

Also required and done:

- CLI: 3D runs, five fixtures.
- GUI: controller, adapter and QML, with tests.
- CPU baseline recorded (§32).
- clang-format clean (0 of 555 files).
- Documentation and evidence complete (this file, [a3/README.md](a3/README.md),
  [g5-investigation/README.md](g5-investigation/README.md)).

## 34. Final decision

**P12-MESH-006 COMPLETE, under Amendment A3.**

- G1–G11 pass, with G5.1 and G6.2 as amended by A3.
- **The original pre-registered G5.1 and G6.2 failed.** That failure, its diagnosis, the
  investigation (classification D) and the amendment are recorded in §17, §18, §30 and §31, and
  none of it is rewritten. The A3 expected values were frozen before the fresh run and computed
  without CFDApp.
- The final `cfdapp` is byte-identical to the binary of the A3 acceptance run.

Known limitations:

- 3D scope:
  - uniform Cartesian boxes only;
  - laminar, incompressible, steady SIMPLE only;
  - PISO / transient, turbulence, thermal, species, multiphase and compressible refuse a 3D case;
  - no GPU 3D SIMPLE.
- GUI:
  - 3D cases are edited, validated, run and their residuals shown (including w);
  - no 3D field view: contours, vectors, probes and line samples are empty for 3D, so use ParaView
    (VTK hexahedra);
  - the QML pages were not inspected visually by a human.
- 2D cases keep the linear face flux by default. Their pressure can carry the odd-even mode on open
  domains unless `face_flux: "rhie_chow"` is chosen.
- CPU cost:
  - single-threaded;
  - the O(boundary faces) boundary-condition lookup costs about 25–30 % at 64³;
  - linear-solver iteration counts are not exported.
- The case format has no preconditioner key (A2.1).
- By construction, the G5.1-D velocity envelope has a margin of about 20 % for an implementation
  that reproduces the scheme. Its discriminating power lies in G5.1-A.
- The light cube's spanwise asymmetry is iterative (it scales with the outer tolerance). Its source
  is not established (§27).
- Pre-existing, unchanged:
  - MESH-004 irregular-mesh convergence;
  - the CG absolute breakdown threshold;
  - MESH-005 explicit `diffusion()` with two cells along a boundary normal;
  - six `-Wconversion` warnings at `apps/gui/SimulationControllerEditing.cpp:86-89` (lines identical
    to BASE).
- **Found during the MESH-006 regression, pre-existing and not fixed:** the ASan heap-use-after-free
  in the P12-MESH-004 test `MeshQualityReport.DisconnectedMeshIsFatal` (§32). It makes CI's
  sanitizer job fail. Fixing it (one test statement) needs its own authorization, before any push.

P12-MESH-007 was not started. Nothing was committed or pushed.
