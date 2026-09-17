# CFDApp — TODO

**Released:** v0.2.0 (`1e960c7`, tag `v0.2.0`)

**Current development state:** P12-COMP-001/002, P12-NUM-001–007 and
P12-MESH-001–006 complete (P12-MESH-001–006 verified, working tree not yet
committed — no commit/push authorized; P12-MESH-006 under gate Amendment A3).
P12-MESH-007 (moving mesh) is **BLOCKED / FAILED GATE** (G6.3), awaiting a user
decision; see P12-MESH-007 below. Both separately authorized gradient phases
also **FAILED their own gates** — P12-GRAD-001 (GR1, GR6) and P12-GRAD-002
(C1, C4) — in every case through frozen-gate threshold-derivation errors, not
implementation defects; see those sections below. GRAD-002's continuity
criterion (C5), the central test, **passes**.
**Before any push:** CI's sanitizer job fails on the P12-MESH-004 test
`MeshQualityReport.DisconnectedMeshIsFatal` (a pre-existing use-after-free
in the test code, found by the P12-MESH-006 regression; see P12-MESH-006
below). Fixing it needs its own authorization.

**Evidence rule:** `[x]` means implemented **and verified with real evidence**.
Never mark an item complete from implementation alone.

See:

* `CLAUDE.md` — workflow/evidence rules
* `ROADMAP.md` — high-level development history and direction
* `results/` — detailed verification evidence

---

# Current

## P12-NUM Closeout — `[x]` COMPLETE (2026-09-15)

* [x] Review the complete P12-NUM working-tree diff.
* [x] Remove/revert generated files, runtime-only changes and accidental artifacts.
* [x] Confirm `results/p12-num-001/` through `results/p12-num-007/` contain the intended evidence.
* [x] Confirm `TODO.md`, `ROADMAP.md` and validation documentation agree with the final implementation.
* [x] Run final focused smoke checks if required by `CLAUDE.md`.
* [x] Commit P12-NUM-001 through P12-NUM-007.
* [x] Push the commit.
* [x] Confirm CI passes on the exact pushed commit.
* [x] Record the final commit SHA and CI run in the closeout evidence.

Evidence: `results/p12-num-closeout/summary.md` (§1 audit, §2 cleanup, §3
evidence, §4 docs, §5 verification: Release 1614/1614 + GUI 40/40, ASan 0
diagnostics; §6 pre-existing CI failures and their fixes; §7 commits/CI).

* P12-NUM commit: `105383d1c026f2cad1b19753250e4a931065cca0`; CI-fix commit:
  `44b996a39f3884bd64937c732a4c8c6e61723dd6` (pushed, = `origin/main`).
* CI run `34839669398` on exact SHA `44b996a…`: success, all 7 jobs green
  (format, python, clang-tidy, gcc-release, clang-debug, gcc-debug,
  sanitizers; each test job 1614/1614, 25 disabled, 0 sanitizer diagnostics).

**The closeout is complete.** (The scope decision it called for was made on
2026-09-15: P12-MESH-001 only — see below.)

---

# P12-MESH — authorized 2026-09-15 (MESH-001 through MESH-007)

## P12-MESH — Production Mesh Capability

**Status:** P12-MESH authorized 2026-09-15 for **MESH-001 through MESH-007
only** (MESH-005 authorized after the MESH-004 prerequisite was verified complete;
MESH-006 authorized after MESH-005 was complete; MESH-007 authorized after
MESH-006 was complete).
P12-MESH-001 — `[x]` COMPLETE (2026-09-15).
P12-MESH-002 — `[x]` COMPLETE (2026-09-15).
P12-MESH-003 — `[x]` COMPLETE (2026-09-15).
P12-MESH-004 — `[x]` COMPLETE (2026-09-15).
P12-MESH-005 — `[x]` COMPLETE (2026-09-15).
P12-MESH-006 — `[x]` COMPLETE (2026-09-15, under gate Amendment A3). The original gate failed
(G5.1 / G6.2 duct velocity thresholds; results/p12-mesh-006/summary.md §30), and that failure stays
on record. After the investigation (§31) and A3, every gate passed (§33, §34).
P12-MESH-007 — `[ ]` **BLOCKED / FAILED GATE** (2026-09-15: G6.3 Galilean invariance; the cause is a
pre-existing Green–Gauss boundary-gradient branch, not the ALE code; results/p12-mesh-007/summary.md
§6, §9). The separate fixes P12-GRAD-001 and P12-GRAD-002 (both authorized 2026-09-16) each failed
their own frozen gate, so G6.3 was never rerun and stays unmodified
(results/p12-grad-001/summary.md, results/p12-grad-002/summary.md).
Later P12 phases — **NOT AUTHORIZED**.

Goal: remove the current uniform-Cartesian production-mesh limitation and make the numerical methods developed in P12-NUM usable on realistic production geometries.

### P12-MESH-001 — Production Non-Orthogonal Structured Meshes — `[x]` COMPLETE (2026-09-15)

* [x] Define production mesh representation requirements — `mesh.json`
  `"structured_quad"` (explicit `(nx+1)x(ny+1)` vertex grid, Cartesian
  topology/patches), built into the existing `Mesh` (§2, §4).
* [x] Support non-orthogonal quadrilateral meshes through the production case path
  (CaseReader → CaseBuilder → ProjectRunner → export; CLI and GUI).
* [x] Preserve existing Cartesian case behavior — 13/13 committed Cartesian
  cases byte-identical to HEAD `b66310c` (§6.6).
* [x] Connect production meshes to existing (§6.4, production path):

  * [x] Green–Gauss gradients
  * [x] least-squares gradients
  * [x] skewness correction
  * [x] non-orthogonal diffusion
  * [x] geometric pressure correction
  * [x] SIMPLE
  * [x] CompressibleSIMPLE (Arkilic lubrication reference)
  * [x] thermal transport (analytical linear profile)
  * [x] species transport (analytical linear profile)
  * [x] turbulence transport (k-ε/SST, consistency vs Cartesian; SST wall
    distance fixed)
* [x] Add mesh validity checks (builder + `MeshQuality` gate, §4).
* [x] Add deterministic mesh regression tests (§9).
* [x] Add end-to-end distorted production case — `cases/poiseuille_distorted`.
* [x] Verify conservation and convergence (§6.2, §6.3, §6.5).
* [x] Verify existing Cartesian results are not regressed (§6.6, §9).
* [x] Add evidence under `results/p12-mesh-001/`.

**Gate:** a non-orthogonal mesh loaded through the normal production case path must run a physically validated CFD case successfully.
**Met:** distorted Poiseuille (max non-orthogonality 44.8°) through
ProjectRunner: converged, conservative (column flow error ≤ 3.4e-10),
velocity error ≤ 1.15× the Cartesian scheme's, dp/dx within 0.585% at 144x18,
second-order velocity convergence (p = 2.19, asymptotic). Section numbers
refer to `results/p12-mesh-001/summary.md`.

---

### P12-MESH-002 — Stretched / Graded Meshes — `[x]` COMPLETE (2026-09-15)

* [x] Add x-direction grading.
* [x] Add y-direction grading.
* [x] Add boundary clustering.
* [x] Support geometric progression controls.
* [x] Validate cell sizes and grading ratios.
* [x] Prevent zero/negative/degenerate cells.
* [x] Expose grading in the case format.
* [x] Preserve uniform-grid defaults.
* [x] Add CLI/case round-trip tests.
* [x] Add GUI support if required for production usability.
* [x] Validate wall-resolved Poiseuille/channel case.
* [x] Compare accuracy/cost against uniform meshes.
* [x] Add grid-convergence evidence.
* [x] Add evidence under `results/p12-mesh-002/`.

**Gate:** demonstrate quantitatively that mesh clustering improves resolution of a wall-gradient problem without violating conservation.
**Met** (gate fixed in advance, `results/p12-mesh-002/acceptance_gate.md`):
wall-transpiration channel (exact NS solution, 0.05 H suction-wall boundary
layer), equal cell count 48x24, uniform vs y-graded (both walls, ratio 1.2)
through the production path: integral wall-shear (dp/dx) error 0.291x,
suction-wall shear error 0.382x, velocity L2 0.220x the uniform mesh's; both
converged, face-column mass-flow error <= 4.2e-11. Plain Poiseuille does not
benefit from clustering (uniform grid optimal for constant curvature) —
recorded as a finding. Evidence: `results/p12-mesh-002/summary.md`.

---

### P12-MESH-003 — General 2D Geometry — `[x]` COMPLETE (2026-09-15)

Section numbers refer to `results/p12-mesh-003/summary.md`.

* [x] Define the minimum supported general-geometry architecture — conformal
  multi-block structured quadrilaterals in the existing face-based `Mesh`
  (`MultiBlockSpec`, `MeshGeometry::createMultiBlock2D`; §2).
* [x] Support multiple connected blocks and/or body-fitted structured regions —
  `mesh.json` `"multiblock"` (blocks, aligned/reversed interfaces, named
  patches) + `geometry.json` `"mesh_defined"`, production path CLI and GUI (§3, §4).
* [x] Support non-rectangular fluid domains — 270° curved channel, L-shaped
  step channel, annular sector, channel with obstacle (§5).
* [x] Support internal solid/inactive regions if compatible with the architecture —
  implemented as holes bounded by a wall patch (no masked cells):
  `cases/obstacle_channel_multiblock` (§6).
* [x] Generate correct face connectivity — solver-independent invariants incl.
  Euler characteristic; two-block mesh = single-block mesh renumbered (§7).
* [x] Generate boundary patches from geometry — named patches from whole block
  sides, `boundaries.json` must match them exactly (§8).
* [x] Validate face orientation and ownership — owner→neighbour normals,
  interface faces owned by the `first` block, rotated/reversed blocks (§9).
* [x] Detect invalid/overlapping cells — invalid cells, non-coincident or
  mis-oriented interfaces, undeclared contacts, hanging vertices, overlaps,
  nested blocks (boundary-curve + winding checks) rejected with names (§10).
* [x] Detect disconnected fluid regions — `MeshQuality` connected components,
  rejected by the validity gate (§11).
* [x] Preserve conservation across block interfaces — one internal face per
  interface pair; mass (≤ 8e-10), 1-block vs 3-block transparency (4e-10) and
  heat flow (≤ 4.8e-12 Q) through interfaces (§12, §17).
* [x] Export geometry correctly to VTK — block vertex grids + per-cell quads,
  metadata `"blocks"`; single-grid exports unchanged (§13, §18).
* [x] Add representative production cases — four committed multiblock cases
  and their generator (§5).
* [x] Add evidence under `results/p12-mesh-003/`.

**Do not silently turn this into a full unstructured-meshing project.**

**Gate:** solve and quantitatively validate at least one genuinely non-rectangular 2D production case.
**Met** (gate fixed in advance, `results/p12-mesh-003/acceptance_gate.md`,
thresholds unchanged): 270° three-block curved channel against the exact
fully developed Navier–Stokes solution through the production path —
conservative (≤ 8e-10), interfaces transparent (1 vs 3 blocks 4e-10),
velocity and dp/dθ second order (1.9–2.0), GCI brackets the exact values;
sector conduction heat flow through both interfaces ≤ 4.8e-12 Q, temperature
order 3.0. The first gate run failed G5 (thermal solver: lagged
adiabatic/heat-flux boundaries left a converged field inconsistent with its
own boundary values); fixed in the thermal solver (exact gradient-type boundary
assembly, attained-breakdown rule) with a regression test that fails on the
pre-fix solver (§17). Regression: Release 1743/1743, Debug+GUI 1789/1789.
Known limitation: the species solver has the same lagged-boundary pattern (not
changed here, §21).

---

### P12-MESH-004 — Mesh Quality & Validation — `[x]` COMPLETE (2026-09-15)

Section numbers refer to `results/p12-mesh-004/summary.md`.

* [x] Consolidate production mesh-quality diagnostics — one authoritative
  `MeshQuality::evaluate` report, stored by `CaseBuilder`, read by the CLI, GUI and
  results JSON (§3).
* [x] Report:

  * [x] minimum cell volume/area
  * [x] aspect ratio (coordinate-free; fixes a false "invalid" on a 45°-rotated mesh, §4)
  * [x] non-orthogonality
  * [x] skewness
  * [x] grading/expansion ratio
  * [x] degenerate cells
* [x] Define warning thresholds — 70° / 0.5 / 100 / 2, with rationale; warnings never reject (§5).
* [x] Define fatal-invalid thresholds (§6).
* [x] Surface diagnostics through CLI — `Mesh quality:` block; 5 dedicated ctests (§7).
* [x] Surface diagnostics through GUI where appropriate — Mesh page box and validation-panel
  warnings; 4 GUI tests (§8).
* [x] Export diagnostics to results JSON — `metadata.json` `"mesh_quality"` (§9).
* [x] Verify numerical behavior across controlled mesh-quality degradation — four families,
  Q0–Q4 each (§10).
* [x] Run MMS on production non-orthogonal meshes (§12, §13).
* [x] Run grid-convergence studies (§14).
* [x] Add evidence under `results/p12-mesh-004/`.

**Gate:** mesh-quality degradation must be measurable and its numerical effect quantitatively demonstrated.

**Met** — the gate was fixed in advance (`results/p12-mesh-004/acceptance_gate.md`) and never
changed:

- **Measurable degradation.** Every family's targeted metric rises monotonically:
  - non-orthogonality 0 → 63.9°;
  - skewness 0 → 0.27;
  - aspect ratio 1 → 16;
  - expansion ratio 1 → 1.4.
- **Numerical effect, measured.** At Q4 the velocity L2 error is 2.7× / 32× / 27× / 21× the
  orthogonal mesh's, all well below every warning threshold. Every valid level converged, and
  mass and energy were conserved (≤ 2.9e-11).
- **MMS through the production path.** Velocity and pressure are second order on the 35°
  non-orthogonal family (u 2.03, v 2.05, p 2.06). Temperature is first order (0.95), consistent
  with first-order upwind energy convection.
- **Invalid meshes** (11 kinds) are rejected before any solver runs, each error naming the defect.
- **No solver behaviour change** on any committed case (byte-identical outputs). Some existing
  validation studies change at solver-tolerance level (≤ 2e-5 relative); three of them no longer
  need the linear-solver fallback HEAD needed.

**First run failed** (R3, M1, GC1) on a pre-existing BiCGSTAB defect: an absolute 1e-30 breakdown
threshold that misreads small-scale healthy iterations. It was fixed in a separately authorized
change: a scale-invariant breakdown test (|(x, y)| ≤ ε Σ|xᵢyᵢ|) plus a bounded, progress-guarded
restart, with a stored-system reproducer (§23). The first version of that fix failed the full
regression (16 tests, kept as evidence) and was replaced. Two P12-NUM-004 fallback tests, whose
fixture was the same false breakdown, were rewritten.

**Regression:** Release 1787/1787, Debug+GUI 1837/1837.

**Found and documented, not fixed (separate scoping):**

- velocity/pressure do not converge on cell-scale irregular meshes (§21);
- the uncorrected recipe diverges on smooth non-orthogonal meshes;
- CG keeps an absolute breakdown threshold.

---

### P12-MESH-005 — 3D Foundation — `[x]` COMPLETE (2026-09-15)

**Do not start until P12-MESH-001–004 are complete.** (Verified complete 2026-09-15,
before any MESH-005 change: `results/p12-mesh-005/logs/01`.)

Section numbers refer to `results/p12-mesh-005/summary.md`.

* [x] Generalize mesh primitives from 2D to 3D — one `Vector3` geometry/field
  vector (`Vector2` is an alias; 2D results bitwise unchanged), `Mesh::dimension()`,
  3D `StructuredGrid`; no parallel 3D stack (§4, §5).
* [x] Add 3D cell volumes — V = dx·dy·dz; sum = domain volume (§7, §24a).
* [x] Add 3D face-area vectors — magnitude, orientation, per-cell closure exactly 0 (§8).
* [x] Add 3D cell/face centroids (§9).
* [x] Add six-face Cartesian hexahedral connectivity — `MeshGeometry::createCartesian3D`,
  canonical west/east/south/north/bottom/top order, counts vs independent formulas and
  the Euler characteristic (§6, §10).
* [x] Generalize scalar fields to 3D meshes — the same `ScalarField` (§12).
* [x] Generalize vector fields to three components — `VectorField` is always (u, v, w) (§13).
* [x] Generalize boundary patches — `xmin`, `xmax`, `ymin`, `ymax`, `zmin`, `zmax`,
  outward normals (§11).
* [x] Generalize interpolation (§14).
* [x] Generalize gradients — Green–Gauss and least-squares (3×3 system) (§15).
* [x] Generalize diffusion (§16).
* [x] Generalize convection — the four existing schemes, no new scheme (§17).
* [x] Generalize sparse-system assembly — hand-derived 1×1×1 … 2×2×2 systems (§18).
* [x] Add 3D VTK export — `VTKWriter::writeCellFields`, VTK_HEXAHEDRON, parsed back (§19).
* [x] Add unit/operator MMS tests — genuinely 3D MMS (every field varies in z), 8³–64³ (§20–§23).
* [x] Add evidence under `results/p12-mesh-005/`.

**Gate:** 3D operators reproduce analytical fields at their expected spatial order.
**Met** — gate fixed in advance (`results/p12-mesh-005/acceptance_gate.md`), never changed:

- **Expected orders met:** all 30 gated (quantity, norm) items meet their preregistered order at
  the finest pair 32³→64³, and every error decreases at every refinement:
  - Green–Gauss gradient: 1.96–1.98;
  - least-squares gradient: 1.97–1.98 interior; globally, L∞ 0.99 against an expected 1;
  - explicit diffusion: 1.99–2.00;
  - Poisson solve: 2.00;
  - upwind convection: 0.98–0.99;
  - central / linear_upwind / quick convection, interior: 1.89–1.99;
  - convection–diffusion solve: 0.90–0.91.
- **Exactness:** geometry, linear-field operators and assembly are exact to ≤ 8.8e-12 (§24a).
  The assembled sparse systems equal the hand-derived ones exactly.
- **2D unchanged:**
  - every touched 2D quantity is bitwise identical on 18 meshes;
  - all 27 CLI cases and fixtures are byte-identical;
  - regenerated outputs changed in timing only.
- **Regression:** Release 1825/1825, Debug+GUI 1875/1875.

**Scope boundary:** 3D is a verified foundation (C++ API), not a solver:

- case files, CLI and GUI remain 2D;
- the flow/turbulence components refuse a 3D mesh explicitly (P12-MESH-006).

**Found, not fixed (pre-existing, 2D and 3D alike, no production caller):** the explicit
`diffusion()` operator is wrong with exactly two cells along a boundary normal (§28, logs/04).

---

### P12-MESH-006 — 3D Incompressible Solver — `[x]` COMPLETE (2026-09-15, under gate Amendment A3)

Section numbers refer to `results/p12-mesh-006/summary.md`.

**Chronology:**

1. The original pre-registered gate's G5.1 and G6.2 **FAILED**, and implementation stopped.
2. An independent investigation classified the failure as **D, a threshold design defect**.
3. The user authorized Amendment A3, which was frozen before a fresh run.
4. The fresh run passed: **A3 G5 PASS, A3 G6 PASS**.
5. The remaining work was completed and verified, and the phase is **COMPLETE**.

The original failure stays on record (§17, §18, §30). Its status block, as first written:

> **Status (2026-09-15): BLOCKED / FAILED GATE.**
>
> - The first failed pre-registered item is **G5.1**, the analytical square duct through the
>   production case path:
>   - n = 16: velocity L∞ 0.02336 U, limit 0.020;
>   - n = 24: L∞ 0.01058 U (limit 0.010) and RMS 0.00520 U (limit 0.005).
> - **G6.2** fails on the same n = 16 quantity.
> - Diagnosis: not an implementation defect. CFDApp's 3D SIMPLE reproduces an independent
>   computation of the discrete fully developed solution of the same second-order scheme to 4–5
>   digits. The gate's velocity thresholds sit below that scheme's discretization error
>   (summary.md §17, logs/05e).
> - Thresholds unchanged (stop rules). Amending the gate is the user's decision.
> - Checkboxes stay `[ ]`: the pre-registered rule marks `[x]` only after the gate and the full
>   regression pass.
> - Evidence: `results/p12-mesh-006/`.
>
> Not done because the phase stopped: GUI (G9.4), raw CLI 3D fixtures, CPU baseline, final G10
> 2D compatibility, G11 full regression, and documentation.

**After the original decision** (§31–§34):

- **Investigation** (`g5-investigation/`): CFDApp equals an independent exact solution of the same
  discrete equations to ≤ 8.1e-8 U. The original limits sit below the scheme's own discretization
  error.
- **A3** (`acceptance_gate_A3.md`, frozen 2026-09-15T12:36:10Z) replaces G5.1 and G6.2 only. It
  requires:
  - agreement with the independent discrete solution ≤ 1e-5 U;
  - monotone convergence;
  - observed order in [1.8, 2.2];
  - an n = 24 physical-error envelope of 1.25 × the frozen prediction;
  - a GCI check.
- **Fresh run:** 46/46 items pass.
  - Discrete agreement ≤ 7.08e-8 U; order 1.954.
  - n = 24: dp/dx 0.663 %, u_max 0.799 %, L∞ 1.058e-2 ≤ 1.322e-2, RMS 5.202e-3 ≤ 6.503e-3.
  - GCI indicator 1.008 / 1.010.
  - G6.2-A3 6.38e-8.
- The checkboxes below were marked only after every gate and the full regression passed.

* [x] Add w-momentum equation — G1.1 permutation symmetry (4 schemes), G1.3 (§13).
* [x] Extend SIMPLE pressure–velocity coupling to 3D — one shared SIMPLE; G3.1–G3.3 (§15).
* [x] Extend face-flux correction to 3D — opt-in Rhie–Chow (automatic for 3D); G2.1–G2.3, G3.3
  (§14, §15).
* [x] Extend continuity diagnostics to 3D — mass balance in metadata and CLI; G2.4, G8.3 (§14, §20).
* [x] Extend non-orthogonal correction to 3D — w pass; G3.4′ (Amendment A1) (§15).
* [x] Extend solver residual reporting — W residual: monitor, CSV, JSON, CLI, GUI; G1.2 (§13, §32).
* [x] Add 3D manufactured solution — G4 PASS (8³/16³/32³, orders 1.8–2.4) (§16).
* [x] Add 3D Poiseuille/channel validation — analytical square duct: the original G5.1 FAILED
  (§17). **A3 G5 PASS** (§31).
* [x] Add 3D lid-driven-cavity or equivalent benchmark — Re = 1000 cube vs Albensoeder & Kuhlmann
  (2005), 32³/48³/64³: G7 PASS 14/14 (§19).
* [x] Verify global mass conservation — G8 PASS (open ≤ 3.52e-12, closed 0) (§20).
* [x] Add CPU performance baseline — lid cube Re = 100, one thread: 16³ 0.043 s/iteration,
  32³ 0.495 s, 64³ 7.41 s (774 iterations, 96 min, 761 MB) (§32).
* [x] Add evidence under `results/p12-mesh-006/` — summary.md, acceptance_gate.md,
  acceptance_gate_A3.md, g5-investigation/, a3/, logs, data, tools.

Also done after A3 (§32):

- raw CLI 3D fixtures: 2 valid (exit 0), 3 invalid (exit 2);
- GUI: 3D cases edited, validated and run, w residual series, empty 3D contours, nz/depth/w
  preserved;
- clang-format clean (0 of 555 files);
- the user guide, README, ROADMAP.

**Gate:** production 3D SIMPLE must converge from a non-exact initial state and agree quantitatively with an independent analytical/benchmark solution.

**Met under Amendment A3** — G1–G11 pass (§33), with G5.1/G6.2 as amended. The final `cfdapp` is
byte-identical to the binary of the A3 acceptance run.

- **2D unchanged (G10):**
  - bitwise-identical 2D momentum, pressure-correction and SIMPLE probes;
  - 27/27 2D CLI cases and fixtures byte-identical;
  - MESH-001–005 suites pass by name;
  - 0 of 161 inputs changed;
  - regenerated outputs changed in timing only.
- **Focused:** gtest stages 1229/1229, GUI 52/52, CLI 18/18.
- **Regression (G11):** Release 1862/1862, Debug + GUI 1914/1914 (44 disabled, not run).
- **Supplementary ASan + UBSan** (not part of G11):
  - 1858/1862 at a 1800 s timeout;
  - the 3 timed-out tests pass at CI's 7200 s timeout with 0 diagnostics;
  - 1 pre-existing test defect (below).

**Found, not fixed:**

- **Pre-existing, P12-MESH-004 test code:** `MeshQualityReport.DisconnectedMeshIsFatal` reads a
  destroyed temporary (`tests/unit/mesh/test_mesh_quality_report.cpp:487-489`). ASan reports a
  heap-use-after-free, reproduced identically on the pre-MESH-006 tree (§32, a3/logs/18a).
  **CI's sanitizer job will fail until it is fixed.** The fix (one test statement) needs its own
  authorization before any push.
- Unchanged from earlier phases:
  - MESH-004 irregular-mesh convergence;
  - the CG absolute breakdown threshold;
  - MESH-005 explicit `diffusion()` with two cells along a boundary normal;
  - six `-Wconversion` warnings at `apps/gui/SimulationControllerEditing.cpp:86-89`.

**Limitations** (§34):

- 3D scope:
  - uniform Cartesian boxes, laminar steady SIMPLE only;
  - PISO/transient, turbulence, thermal, species, multiphase and compressible refuse 3D;
  - no GPU 3D SIMPLE;
  - single-threaded.
- The GUI has no 3D field view (use ParaView), and its pages were not visually inspected by a human.
- The O(boundary faces) boundary-condition lookup costs about 25–30 % at 64³.

---

### P12-MESH-007 — Moving / Deforming Mesh Foundation — `[ ]` BLOCKED / FAILED GATE (authorized 2026-09-15)

**Do not start until the static 3D foundation is stable.** (Verified stable 2026-09-15, before any
MESH-007 change: `results/p12-mesh-007/logs/01`.)

Section numbers refer to `results/p12-mesh-007/summary.md`.

**Status (2026-09-15): BLOCKED / FAILED GATE.**

- The first failed pre-registered item is **G6.3**, Galilean invariance of the translating lid
  cavity on the Cartesian 16×16 mesh: max |u_B − b − u_A| = 2.49e-2, limit 1e-8. G7.3 is the same
  run.
- **Diagnosis** (§6): not an ALE or GCL implementation defect.
  - The pre-existing Green–Gauss gradient (`Gradient.cpp`, `tryPairedBoundaryContribution`) uses a
    second-order boundary treatment only when `cross(d, S_f)` is exactly zero. A translated mesh
    loses that by round-off.
  - The static PISO on a translated copy of the Cartesian mesh reproduces the same discrepancy on the
    pre-MESH-007 library (5.4e-3 → 2.5e-2).
  - On a mesh where both runs use the same branch (the distorted Q16), the ALE runs are Galilean to
    1.5e-13.
- The primary gate G5 (uniform flow) **passes**: seven moving-mesh runs keep uniform flow to
  ≤ 1.1e-13 (limit 1e-9).
- Thresholds are unchanged (stop rules). The user decides between a disclosed G6.3 amendment, a
  separately authorized gradient fix, or staying blocked (§9).
- **2026-09-16:** option (b) was authorized and attempted twice — as **P12-GRAD-001** (a tolerance
  on the predicate) and then **P12-GRAD-002** (removing the branch itself). Both failed their own
  frozen gates, so **G6.3 was never rerun** and is unmodified, and MESH-007 stays blocked.
- G2.3's Python check, G9 compatibility, G10 regression, the performance baseline and docs were
  **not run** (§7).
- Library API only (user decision): no case-format, CLI or GUI change. 3D is verified at the
  geometry and operator level; AlePISO is 2D.
- Checkboxes stay `[ ]`. The phase gate and the full regression have not both passed.

* [ ] Define mesh-motion architecture. — `results/p12-mesh-007/architecture.md`, frozen with the gate.
* [ ] Preserve topology for deformation-only motion. — `Mesh::setGeometry` changes geometry only;
  G2.7 topology identical after every advance.
* [ ] Update cell volumes and face geometry. — 2D (builder formulas) and 3D (trilinear hexahedra):
  G1.3 and G2 pass; G2.3's independent Python check not run.
* [ ] Implement mesh velocity. — `MeshMotionStep::vertexVelocities`; G3 passes.
* [ ] Implement ALE transport foundation. — `aleImplicitEulerTimeDerivative`, `relativeMassFlux`,
  `AlePISO`: G1.2, G1.4, G6.1, G6.5 and G6.4 pass; **G6.3 FAILS** (see above).
* [ ] Enforce the geometric conservation law. — exact swept volumes; G4 per-cell ≤ 4.7e-3 of the
  round-off bound.
* [ ] Add moving-wall support. — `checkBoundaryMotion` plus `MovingWall`: G7.1, G7.2 and G7.4 pass;
  G7.3 is the failed G6.3 run.
* [ ] Add prescribed mesh-motion test. — U1–U7, the piston and translating Couette (2D);
  operator-level 3D.
* [ ] Add conservation regression. — G8 passes in every run (ALE mass residual ≤ 3.6e-13 of the
  reference flux).
* [ ] Add manufactured/analytical validation where possible. — analytical: affine geometry, the
  piston, Couette, uniform flow. No moving-mesh MMS (not practical with PISO's discretization).
* [ ] Add evidence under `results/p12-mesh-007/`. — present (summary.md, acceptance_gate.md,
  architecture.md, logs 00–17, tools).

**Gate:** uniform flow must remain uniform under prescribed mesh motion to numerical tolerance.

### P12-GRAD-001 — Green–Gauss Boundary-Gradient Predicate — `[ ]` FAILED GATE (authorized 2026-09-16)

Separately authorized numerical-correctness fix for the pre-existing defect found by MESH-007 G6.3.
Section numbers refer to `results/p12-grad-001/summary.md`.

**Status: FAILED GATE.** Two frozen items fail, both because of errors in the gate I froze, not in
the implemented fix:

- **GR1** — translation invariance 2.414e-9 against 1e-9 (2D 64², quadratic field). The bound was
  derived from the misalignment round-off (≤ 5e-13) instead of the boundary gradient's response to
  it. The true floor is ≈ 0.6 ε (X/h)⁴ = 2.7e-9 at 64²; exactly representable (dyadic) offsets give
  **exactly 0** at every resolution, with 0 faces misclassified (§4).
- **GR6** — 10 of 64 faces not aligned at the large translation (1234.5678, 987.6543), where
  round-off genuinely gives m_f = 7.9e-4. **GR6 and GR3 are mutually unsatisfiable** there: GR6
  wants that aligned, GR3 forbids aligning anything with m_f ≥ 1e-5. No tolerance passes both (§5).

- The fix itself works: the quadratic-field boundary difference falls 5.06 → 8.6e-12 (16²),
  20.2 → 2.414e-9 (64²), 3.38 → 4.6e-14 (3D), and every genuinely non-orthogonal case (m_f = 1e-5 …
  0.19, Q16, 3D deformed) is correctly rejected.
- Thresholds are unchanged and **MESH-007 G6.3 was not rerun** and is unmodified (stop rules).
- **NOT RUN:** GR5 (cavity reproducer rerun), GR7 (regression, sanitizers, CLI/case comparison),
  GR8 (bit-identity). GR2, GR3 and GR4 pass; GR1 and GR6 fail (§7).
- The fix is left in the tree, uncommitted: `MeshGeometry::boundaryFaceAlignment` (new) plus the
  predicate call in `Gradient.cpp`.
- Checkboxes stay `[ ]`: the gate did not pass.

* [ ] Freeze a separate gradient-fix gate before any change. — `results/p12-grad-001/acceptance_gate.md`,
  sha256 `2c45e438…`, frozen before `Gradient.cpp` was touched (logs/03). Internally inconsistent
  (GR6 vs GR3) — found only when GR6 ran.
* [ ] Fix the predicate with a scale-aware criterion. — `MeshGeometry::boundaryFaceAlignment`,
  dimensionless, derived from the measured m ≈ 0.33 ε (X/h)³ law, not from the G6.3 result.
* [ ] Verify predicate behaviour. — GR6 run: 18 of 19 geometries correct, the large translation fails.
* [ ] Verify analytical gradients. — GR4 passes, nothing degraded (§6).
* [ ] Reproduce the static defect before and after. — before: 2.487e-2 (MESH-007 logs/15).
  GR5's post-fix rerun **not run** (stopped at the first failure).
* [ ] Confirm existing numerical-method compatibility with exact counts. — GR7 **not run**.
* [ ] Rerun the original frozen MESH-007 G6.3 and G7.3 unchanged. — **not run**; the gate failed.

**Gate:** translation and scale invariance of the Green–Gauss gradient, with genuine
non-orthogonality never treated as aligned.

**Superseded:** the tolerance predicate GRAD-001 added is no longer in the tree — P12-GRAD-002
removed the branch entirely. GRAD-001's evidence stays as historical record.

### P12-GRAD-002 — Continuous Green–Gauss Boundary Treatment — `[ ]` BLOCKED / FAILED GATE (authorized 2026-09-16)

Removes the numerical discontinuity itself rather than tuning GRAD-001's cutoff. Section numbers
refer to `results/p12-grad-002/summary.md`; the derivation is `results/p12-grad-002/formulation.md`.

**Status: BLOCKED / FAILED GATE.** First failed criterion **C1** (constant field on the L = 1e-3
mesh: 6.939e-12 against a frozen 3.6e-13); **C4** also fails at the large-coordinate offsets. Both
are mis-derived thresholds in the gate, and both are reproduced **identically by the unmodified
pre-GRAD-002 library** — the proof that neither is a property of the new formulation (§4, §5).

The formulation is verified and is a large improvement:

- translated-mesh boundary gradient, quadratic field: **8.065e-03 → 3.027e-14** at 16²
  (1.969e-03 → 4.620e-13 at 64²); dyadic offsets exactly 0.
- quadratic field vs analytic, **all cells** including boundary-adjacent, exact Cartesian 2D and 3D:
  **0.000e+00** — the Cartesian second-order treatment is retained exactly (§7).
- **continuity (C5) PASSES**: d(error)/d(m_f) is constant at **1.563e-02 across nine decades**
  (m_f = 5e-13 … 5e-3), no jump. The negative control (pre-GRAD-002) jumps by 7.813e-03 at
  m_f = 5e-13 and then plateaus — the MESH-007 G6.3 defect in one measurement (§6).
- C2, C3(a) pass; C3(b), C6 (partly), C7–C11, C13 **NOT RUN** (stopped at the first failure);
  C12 audit complete, C14 done (clang-format clean, binary byte-identical).

- Thresholds unchanged and **MESH-007 G6.3 was not rerun** (stop rule). G6.3 unmodified.
- Implementation left in the tree, uncommitted: `boundaryConsistentFaceValue` in `Gradient.cpp`,
  `MeshGeometry::boundaryLineIntersection`; `FaceAlignment`/`boundaryFaceAlignment` deleted.
- Checkboxes stay `[ ]`: the gate did not pass.

* [ ] Preserve GRAD-001 and record the chronology. — `results/p12-grad-001/` unchanged and hashed in
  logs/00; chronology in §1.
* [ ] Freeze the gate before implementation. — `acceptance_gate.md` sha256 `a46973ed…`,
  `formulation.md` sha256 `5ca61d23…`, frozen before any production source change (logs/00).
  C1/C4/C6 were frozen with mis-derived floors — found only when they ran.
* [ ] Investigate candidate formulations and document the derivation. — six candidates evaluated,
  selection justified (`formulation.md` §4, §5).
* [ ] Replace the branch with a continuous boundary reconstruction. — boundary-consistent face value;
  the same linear functional as the old paired fit in the aligned limit, by uniqueness.
* [ ] Constant / linear / quadratic analytical results. — C2 and C3(a) pass; C1's bound unachievable.
* [ ] Translation study incl. large coordinates. — passes to X/h ~ 2.6e2 at the derived law; fails at
  X/h >= 2e4, where the generator's own centroid error reaches 5e-4 h (4.1 h at 256²) — a separate,
  pre-existing mesh-generator limitation (§5).
* [ ] Continuity sweep with a quantitative criterion. — **PASSES** with a constant Lipschitz
  quotient; negative control fails as required (§6).
* [ ] Cartesian convergence / non-orthogonal accuracy / static cavity reproducer / 3D /
  previous-phase compatibility / regression. — **NOT RUN** (stopped at the first failed criterion).
* [ ] Rerun the original frozen MESH-007 G6.3 and G7.3 unchanged. — **not run**; the gate failed.

**Gate:** the boundary-gradient result must vary continuously with the geometry, with no O(1) jump at
any alignment threshold, while retaining the Cartesian second-order accuracy.

#### Amendment A1 (authorized 2026-09-16) — `[ ]` BLOCKED / FAILED GATE

`results/p12-grad-002/acceptance_gate_A1.md` (sha256 `353b72ef…`), audit `a1/audit.md`, pre-freeze
baseline dry-run `a1/dryrun.md`, results `a1/summary_a1.md`. Frozen after the dry-run and before
every fresh run; production formulation unchanged by A1; binary `51e82ee8…`.

A1 amended **only** the three criteria the investigation proved invalid — C1 and C6 (absolute bounds
below the dimensional ε·|φ|/h floor) and C4 (split into C4a, restricted to a verified valid-geometry
domain, and C4b, an extreme-coordinate diagnostic). Everything else was carried over verbatim.

**Every criterion A1 amended passes, as do C5, C7 and C3(b):**

- **C1-A1** 22/22 rows pass; the constant field is now exactly 0.000e+00 on plain, dyadic and
  L = 1e3 meshes.
- **C4a** passes on every valid-geometry mesh: the quadratic-field translation difference falls
  7.813e-03 → **2.932e-14** (16²) and 4.883e-04 → 7.262e-12 (256²); dyadic offsets exactly 0.
- **C4b** (diagnostic): `createStructuredQuad2D` geometry leaves the 1e-6 validity domain between
  X/h ≈ 1.0e3 and 9.9e3 — a separately scoped pre-existing defect, not fixed here.
- **C6-A1** passes; ρ spreads across L = 1e-3/1/1e3 are 2.05, 25.6, 1.10 against 100×.
- **C5** (unchanged, rerun fresh) passes with a constant quotient of 1.563e-02 over nine decades;
  the negative control fails at the first step (quotient 1.527e+10).
- **C7 static cavity reproducer**: 5.418e-03/2.487e-02 → **6.136e-14/6.452e-14** at 16² (worst
  9.835e-14) and 4.889e-13 at 32², both inside 1e-9. The baseline reproduces the original numbers
  exactly.
- **C3(b)**: order 2.000 on Cartesian plain **and translated** (baseline **0.985** translated — first
  order), 2.000 in 3D; the distorted family improves from 0.97 to 1.74 overall (L2 1.98).
- **C9** 3D translation passes at every offset including X = 1234.5678.

**Failed criterion: C9/C2's "3D deformed" clause.** A linear field's gradient on a sinusoidally
deformed 3D mesh measures **2.029e-03** against C2's frozen 1e-9; the pre-GRAD-002 library measures
**2.028e-03**, so it is pre-existing and unchanged by this phase. It converges at order 1.93, and the
likely cause is that a warped (bilinear) 3D face cannot reproduce ∫φ n dS exactly from one stored
centroid — a MESH-005 face-representation limitation. C2 was carried over as "passing" although this
clause had **never been measured** in the original run: an audit error of the same species as C1/C4/C6.

- **NOT RUN** (stop rule): C8 previous-phase verification, C10 interior bit-identity, C11 case/CLI
  output comparison, C13's full regression, GUI suite and sanitizers. **MESH-007 G6.3 was not rerun**
  and is unmodified.
- **Focused suites** (in flight when the failure surfaced, reported for information): 155/156
  discretization, 156/156 mesh, 81/81 PISO, 30/30 core, 40/40 fields, 97/97 algebra, 92/92 thermal,
  136/136 turbulence, 19/19 MMS, 61/63 case integration. The 3 failures are all tests that pin the
  **old** boundary behaviour — `GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`
  asserts the distorted-mesh order is **below 1.9** and now measures 1.94/1.97/1.98, i.e. it fails
  because the scheme became second order. No golden or reference output was updated.
- Checkboxes stay `[ ]`: the gate did not pass.

#### P12-GRAD-002-INV-001 — Production Accuracy Regression Investigation (authorized 2026-09-16)

Investigation only — no production source, test, threshold, golden output or prior evidence changed.
Evidence: `results/p12-grad-002/investigation/` (plan.md, summary.md, logs 00–22, tools).

**Root cause found and classified: C + B.** The Dirichlet (wall) boundary diffusive flux is **first
order** on non-orthogonal meshes — `MomentumEquation.cpp:112` passes the straight-line
owner→face-centroid distance with no tangential correction of the prescribed value — measured at
6.2574 % / 4.1691 % / 2.7785 % wall-force error (observed order **1.00**), **identical in both
libraries**. The pre-GRAD-002 Green–Gauss boundary gradient was also first-order biased at the same
cells (wall L∞ 9.68e-02), and the two errors partially cancelled. GRAD-002 makes the gradient second
order (wall L∞ **4.95e-03**, 20× better, monotone in α with both endpoints validated against the
libraries), which **removes the cancellation and exposes the flux error**.

- **GRAD-002 is not mathematically incorrect**: interior unchanged, every region's gradient error
  improves, mass conservation improves 10–200×.
- Anti-correlation proving cancellation: least squares is the *least* accurate at the wall for this
  field (1.02e-01) yet converges in 675 iterations, like the old code (682); GRAD-002 is 20× the most
  accurate and takes 2789.
- The effect switches on with the first non-zero misalignment (distortion sweep: iterations
  643→797 at 0°, 522→2158 at 13°, 682→2789 at 48°), i.e. with the tangential-transfer term.
- dp/dx error is flat at 0.58–0.68 % in both libraries, and the baseline's own value spans
  0.5846–0.6135 % across distortions — the size of the entire "regression".
- At 20000 iterations the baseline is stationary (3.2782e-03) while GRAD-002 drifts to 4.5991e-03
  with an 18× larger U residual, so at full convergence its error on this case is genuinely ~1.4×.
- MESH-003: same mechanism; velocity and dp/dθ keep second order (1.92/1.98, 1.83/1.90), the rise
  error is *smaller than the baseline's at all three grids*, and only its monotonicity fails — on a
  metric that is a difference of two ≈0.83 values whose reference moves with the grid (D).
- Deformed-3D (A1's failing clause) **characterized only, not fixed**: an independent 4×4 Gauss
  quadrature of ∫φ n dS shows the single stored face centroid is exact on planar faces (4.99e-15)
  and wrong on warped ones, converging at order 1.92/1.98 — a separate MESH-005 face-representation
  limitation (**E**).

**Recommendation (not implemented, needs authorization):** make the Dirichlet boundary diffusive flux
second-order consistent — normal distance d_n plus the tangential transfer of the prescribed value,
the analogue of MESH-001's oblique-Neumann treatment — in `boundaryFaceDiffusionTerms` and its
callers, **not** in GRAD-002.

**Superseded by P12-DIFF-001 (below): this recommendation was wrong.** The coefficient is already
exactly Γ|S|/d_n (verified to 4.5e-16), so there is no straight-line-distance defect, and the
tangential transfer changes nothing. The wall flux's first-order character is the standard half-cell
one-sided difference, equal to 0.5 h on a perfectly orthogonal mesh. INV-001's measurements stand;
its attribution did not.

### P12-DIFF-001 — Second-Order Dirichlet Boundary Diffusion — `[ ]` BLOCKED / FAILED GATE (authorized 2026-09-16)

Stopped at workflow step 2, the mandatory pre-freeze dry-run, **before freezing a gate and before
any production change**. Evidence: `results/p12-diff-001/` (acceptance_gate.md — recorded as NOT
frozen and why, summary.md, logs 01–03, tools).

**The authorized root cause does not hold**, on three measurements against the unchanged library:

- `boundaryFaceDiffusionTerms`'s coefficient is **already exactly Γ|S|/d_n** — `S_orth = (S·S)/(d·S)d`
  and `d·S = |S| d_n`, so `|S_orth|/|d| = |S|/d_n`. Measured agreement **2.8e-16 / 4.0e-16 /
  4.5e-16** at distortion 0 / 0.5 / 1.0. There is no straight-line-distance defect, and the
  expression is algebraically exact for a linear field.
- The remaining first-order error is the **half-cell one-sided difference**: on a *perfectly
  orthogonal* Cartesian mesh the wall-force error is 6.2500e-02 / 4.1667e-02 / 2.7778e-02 = **0.5 h
  to five significant figures**, order **1.000**; 48° non-orthogonality adds 0.1 %. Analytically
  (0 − φ_P)/(h/2) = −6 + 3h against −6 for φ = 6y(1−y), i.e. exactly h/2.
- The authorized correction `φ_b ± ∇φ_P·d_t` moves the wall force by ≤ 0.05 % and leaves the order at
  **1.00** in both signs — not because the geometry is benign (max |d_t|/d_n = **0.724**) but because
  a fully developed profile's wall-cell gradient is nearly wall-normal, so ∇φ_P·d_t is ≤ 4.8e-04 of
  the wall jump, ~58× below the error.

Sign convention, derived as instructed: transferring a *known* boundary value inward to the normal
foot is `φ_b − ∇φ_P·d_t` (**minus**); the `+ d_t` form is the correct sign for MESH-001's oblique
**Neumann** transfer in the opposite direction. Both were measured.

Stop rule 2 ("the proposed formulation does not restore the expected boundary-flux convergence")
applies, and a genuinely second-order wall flux needs a different reconstruction across momentum,
thermal and species — substantial new scope (stop rule 6). GRAD-002 is intact and untouched; steps
3–15 were not reached; no sanitizer run this phase. Checkboxes stay `[ ]`.

#### P12-DIFF-002-INV-001 — Second-Order Dirichlet Wall-Flux Reconstruction Investigation (authorized 2026-09-16)

Investigation only — no production source, test, threshold or prior evidence changed. Evidence:
`results/p12-diff-002/investigation/` (plan.md with the derivation, summary.md, logs 01–05, tools).

**Classification: B** — works mathematically, but needs operator/architecture changes and its
benefit to the failing quantities is partly unproven.

- **It works.** A one-sided quadratic fit through (boundary value, owner, opposite neighbour), with
  the cell values transferred onto the wall-normal ray, takes the **per-face wall flux from order
  1.000 to 2.000** and is **exact for quadratic fields** (so DIFF-001's 2.78 % becomes 0). Verified
  on 2D orthogonal, 2D translated, 2D distorted 48°, curved annular (270°) and 3D Cartesian, with
  the computed GRAD-002 gradient as well as an exact one. Derived from the geometry, not assumed;
  reduces to `[−8φ_b + 9φ_P − φ_F]/(3h)` on uniform 1D spacing.
- **Stencil availability is complete in production**: 100 % of boundary faces on all 19 buildable
  committed cases, including multi-block (204/204, 248/248), graded, distorted (144/144, min
  anti-parallel 0.994) and 3D (6144/6144). The only fallback condition is topological — no interior
  face across the cell — which is empty in production and 80–100 % on 1-cell-thick meshes, where
  today's behaviour is retained exactly.
- **But the production benefit is mesh-dependent and changes no order.** The discrete
  fully-developed channel (which reproduces MESH-001's dp/dx error exactly, 2/(ny²+2)) improves
  **4.0×** in dp/dx and 1.85× in velocity on **uniform** spacing — but only **1.16×** and 1.04× on a
  wall-clustered graded mesh, and both schemes are already second order in the solution.
- **Whether MESH-001's/MESH-003's failing assertions would pass is undetermined**: they test observed
  order, a GCI band and monotonicity, and the non-1D residual in MESH-001's dp/dx error changes sign
  across the grid family (−0.750, −0.274, +0.065 percentage points), so removing 3/4 of the clean
  second-order term could improve or worsen the order estimate. Not resolvable by probe.
- **Architecture cost**: the stencil couples the boundary cell to its **far** cell, which
  `FaceDiffusionTerms` cannot express (one coefficient + one explicit term) and which corresponds to
  no face of that cell in the sparsity graph; the alternative lagged correction risks GRAD-002's
  3–4× slowdown. Six call sites (momentum ×2, thermal ×2, species, k-ε) plus a second independent
  implementation in `Diffusion.cpp:196-216`. The new form must **supersede**, not augment, the
  existing non-orthogonal boundary correction or the tangential term is double-counted.

A smallest production scope and a proposed 10-criterion **P12-DIFF-002 acceptance gate** (including
an explicit iteration-count guard against the lagged-correction failure mode) are in summary.md
§6–§7. **Not implemented, not frozen.** Checkboxes stay `[ ]`.

### P12-DIFF-002 — Second-Order Dirichlet Boundary Diffusion — `[ ]` BLOCKED / FAILED GATE (authorized 2026-09-16)

Gate frozen before any numerical change: `results/p12-diff-002/acceptance_gate.md`, sha256
`51079f6da5dd0a417b39fe332f92eccf32a9877a96035168bc566ed1d3a9e46b`. Full report:
`results/p12-diff-002/summary.md`.

Implemented (DIFF-002-A architecture, DIFF-002-B reconstruction) and **measured to pass W1a, W1b,
W1c, W1d, W2, W3a and W4**: quadratic exactness 6.25e-02 → 1.5902e-16, cubic observed order
1.000 → 1.963–2.000, curved multi-block 1.8616e-02 → 3.3578e-04 (55×), 100 % stencil availability on
all 19 committed cases, and 13/13 hand-derived matrix tests including the cross-check that the new
coefficients reproduce `Diffusion.cpp`'s general 3-point formula to ≤ 1e-14 on a **graded** mesh.
No new sparse-matrix mechanism was needed: the far cell is always reached through a face of the owner,
so the assembled sparsity pattern is verified identical to the pre-change one.

**Stopped at W3b**, the first failed criterion in the frozen gate's order. W5–W10 not run. The failure
is a **mis-derived criterion, not an implementation defect**: W3b demands 100 % fallback on 2D 8×1,
1×8 and 3D 8×8×1, but those meshes are one-cell-thick in only *one* direction and legitimately have a
valid interior stencil across the others (measured: higher-order on exactly the patches normal to a
thick direction — 2/18, 2/18 and 32/160 faces). The meshes that are thin in *every* direction, 2D 1×1
and 3D 1×1×1, pass completely with bitwise identity on every face. The pre-freeze dry-run could not
catch this because "100 % fallback" is vacuously true on a baseline that has no higher-order path at
all. Per the authorization, the result is preserved and **W3b was not amended** — a separate decision
is requested.

Nothing is marked complete: not DIFF-002, not GRAD-002, not MESH-007. The original MESH-007 G6.3
failure stands as recorded.

#### P12-DIFF-002 Amendment A1 — `[ ]` W3b-A1 PASS, then BLOCKED / FAILED GATE at W7 (authorized 2026-09-16)

Gate frozen before the fresh run: `results/p12-diff-002/acceptance_gate_A1.md`, sha256
`c4b08824cf98e95f6bf3d424e7e0fd006231f1ed955cbfb9f6f641d45faae7d0`. Report:
`results/p12-diff-002/a1/summary_a1.md`. The original W3b failure and all its evidence are preserved
unchanged; W1, W2, W3a, W4 and W5–W10 were not altered, and no unrelated threshold was weakened.

```text
Original W3b     — FAILED
Amendment A1 W3b — PASS
W4               — PASS
W5               — PASS
W6               — PASS
W7               — FAILED -> STOP (first failed criterion in gate order)
W8               — FAILED (both historical tests)
W9, W10          — not run
```

**W3b-A1 PASS.** Replaced the mis-derived "100 % fallback on degenerate meshes" with per-face
classification agreement against an **independent topology oracle** — two of them, neither derived
from production's own answer: Oracle-T (integer comparison on the generator's `nx, ny, nz` plus the
patch name; no floating point at all) and Oracle-G (connectivity walk selecting by normal **depth**,
where production selects by normal **alignment**). Fresh post-freeze run over 12 920 boundary faces
on all 12 required structured meshes plus graded, distorted and every buildable committed case:
**0 classification mismatches, 0 fallback bitwise mismatches, 0 silently-degraded higher-order faces,
0 oracle disagreements** (12 366 HIGHER_ORDER_REQUIRED, 554 FALLBACK_REQUIRED — both classes present).
Two negative controls, frozen into the gate, both FAIL as required: "fallback everywhere" (the
pre-DIFF-002 library, 12 366 mismatches) and "reconstruct everywhere" (554). The first of those is
exactly the state on which the original W3b passed **vacuously** — the recorded methodological lesson.

**W5 PASS** — the mandatory convergence guard. SIMPLE outer iterations worst ratio **1.1205×**
(`poiseuille_distorted` 1983 → 2222) against the 1.25× bound; `curved_channel_multiblock` 1400 → 1405.
Thermal worst 1.143×, species worst 1.077×, measured with a link-substituted pre-change library so
the comparison isolates DIFF-002 on non-orthogonal meshes. **No GRAD-002-style slowdown** (that was
4.1×). Thermal *linear* iterations consistently fall (47 → 22, 23 → 18): the implicit far-cell
coupling improves conditioning.

**W6 PASS** — 144×18 dp/dx error **0.207 %** against the frozen ≤ 0.747 % bound, a **3.28×**
improvement on 0.6788 %. Velocity L2 improves 1.4–1.6× and is now better than the Cartesian reference
at all three resolutions.

**W7 FAILED — one NEW regression.**
`StructuredQuadProductionCase.KEpsilonChannelMatchesCartesianOnTiltedMesh` passed pre-DIFF-002 and now
fails: k-ε Re_τ Cartesian 447.902 vs distorted 439.024, a **1.98 %** gap against a 1 % threshold
(historically 0.06 %). Cause diagnosed: the test enables `nonOrthogonalCorrections = 1` on the
distorted arm only, so that arm now gets the second-order wall flux while the Cartesian arm keeps the
two-point one — the comparison measures two different wall discretizations. Every other focused suite
is unchanged (CFDMesh/Piso/Solver/Core/Field/Algebra/Thermal/Turbulence/MMS all green).

**W8 FAILED** — both historical tests, as W8 anticipated ("reported, not presupposed"). Both were
already failing pre-DIFF-002, but the failing assertion changed: DIFF-002 **fixed** the structured-quad
GCI assertion and the multi-block radial-pressure-rise monotonicity failure, while order assertions
now fail (dp/dx order −5.012; multi-block dp/dθ order 1.050). The dp/dx error sequence is
0.174 % → 0.027 % → 0.207 %: non-monotonic because the medium grid lands almost exactly on the exact
value, so an "observed order" across a near-zero error is an artifact, not a divergence. Historical
gates **not** amended.

**Open finding (no production change made).** The reconstruction is gated behind
`solver.json: non_orthogonal_corrections`, which defaults to 0. On a case with it at 0 the
reconstruction never runs, so DIFF-001's half-cell first-order wall-flux error — present on
**orthogonal** meshes too — remains there. Faithful to the DIFF-002 authorization (the new form
replaces the existing value-boundary treatment, which lives in that branch), but the second-order wall
flux is not achieved on Cartesian cases as shipped, and it is the direct cause of the W7 regression.
Widening activation is a scope decision, deliberately not taken here.

**Two separate authorizations requested:** (1) the new k-ε regression and the activation-gating
question; (2) whether the historical validation gates should be amended.

#### P12-DIFF-002 Amendment A3 — `[ ]` W7 validation reconciliation — BLOCKED / FAILED A3 GATE at A3-5 (authorized 2026-09-16)

Gate frozen before either test was edited: `results/p12-diff-002/acceptance_gate_A3.md`, sha256
`f6dbdd06032c333a4347735abd81d6450ba9a0442879eb4eaa9edb1adaae0e95`. Report: `a3/summary_a3.md`.
Full chronology preserved: original W3b FAIL, A1 correction, A2 W7 FAIL.

```text
A3-P production freeze   PASS   (14 files byte-identical; A3 changed only test code)
A3-1 activation test     PASS   (obsolete instrument replaced, now passes)
A3-2 sector conservation PASS   (obsolete instrument replaced, now passes)
A3-5 focused rerun       FAILED -> STOP
A3-6/7/8                 not run
```

**Both reconciled tests were test defects, not production defects, and both are now stronger.**
A3-1: the indirect "the N = 0 solution must be inaccurate" proxy (broken because A2 improved that
solution from 3.31e-2 to 1.65e-2) is replaced by a direct operator-level activation test — interior-only
rows of the production momentum assembly are bitwise identical with the correction off and differ with
it on (372/372 on the committed case), bitwise identical between 1 and 2 passes, plus the propagation
chain and the original 1.5x solution check (ratio 1.528). A3-2: the boundary-flux estimator that
explicitly requested the pre-A2 two-point path is replaced by the documented DIFF-002 assembled-flux
convention with independently summed conservation; all four cross-sections now agree to 8e-14..2e-12
(was a spurious 8.6e-05 spread), the threshold is **100x tighter** (derived as the solver's own
relative tolerance times the flux scale, 1e-8 x Q), and a new heat-flow convergence assertion was
added (observed order 1.945/1.968). `CFDCaseIntegrationTests` improves from 59/63 to **61/63**.

**A3-5 revealed 14 further obsolete instruments.** Focused counts with every target rebuilt:
CFDCaseIntegrationTests 63/61/2, CFDDiscretizationTests 169/167/2, CFDThermalTests 92/**79**/**13**,
CFDTurbulenceTests 136/136/0 — total 460 run, 443 passed, 17 failed. Three are pre-existing (W8's two
tests and the GRAD-002 gradient-order assertion). The other **14** — 13 in `CFDThermalTests` plus
`BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne` (this phase's own W4 test) —
all fail for one uniform reason: hand-derived assertions encoding the **pre-A2 two-point Dirichlet
wall coefficient at `non_orthogonal_corrections = 0`** (verified: expected diagonal 21 vs measured 24;
W4's baseline block 5 -> 6, -1 -> -4/3, 6 -> 8). None indicates a production defect. Amending fourteen
more tests is outside A3's scope, so the phase stopped.

**Instrument defect disclosed:** `results/p12-diff-002/tools/run_w7.sh` rebuilt a suite only when its
binary was *missing*, so A1's and A2's W7 runs used stale binaries. The library has been unchanged
since A2, yet a forced rebuild of the same sources turns CFDThermalTests 92/92 into 79/92. **A2's
reported W7 figure of 970 run / 966 passed / 4 failed is unreliable and understated and must not be
used.** No reliable complete W7 count exists yet (A3-6 was not reached).

Files modified by A3: `tests/integration/case/test_structured_quad_production_case.cpp`,
`tests/integration/case/test_multiblock_production_case.cpp`. No production numerics, no W8
amendment, no threshold weakened, no coverage deleted.

**Decision requested** on the 14 newly revealed obsolete instruments before W7 can be green.

#### P12-DIFF-002 Amendment A4 — `[ ]` legacy Dirichlet validation migration — BLOCKED, ADDITIONAL LEGACY VALIDATION FOUND (authorized 2026-09-16)

Report: `a4/inventory.md`, `a4/derivations.md`, `a4/negative_controls.md`, `a4/harness_validation.md`.
**No A4 gate was frozen and no test was amended** — the inventory step (A4-2) came first by design and
showed the scope is far larger than A4 was authorized for. Production numerics verified
**byte-identical before == after** (`a4/production_freeze.txt` vs `a4/logs/03`; 22 files + library).

**A4-1 harness defect fixed and self-tested.** `tools/run_w7.sh` rebuilt a target only when its
binary was *missing*, so A1's and A2's W7 counts came from stale binaries. The new harness
(`a4/tools/w7_harness.sh`) configures, builds, verifies, records provenance (source HEAD, library
hash, test-binary hash, build timestamp, build and test commands) and **fails closed**. Self-test:
appending a temporary always-passing TEST moves the binary bc698f84 → f44e2670 and the count 169 →
170; reverting restores both exactly; the library is unchanged throughout.

**Mark as non-authoritative:** the A2 W7 aggregate `970 run / 966 passed / 4 failed` is
**INVALID AS AUTHORITATIVE W7 EVIDENCE — STALE TEST BINARIES**. A3-5's `460/443/17` covered only four
suites.

**A4-2 complete clean-build inventory (1913 tests, 1877 passed, 36 failed, 45 disabled):** A3 saw 14;
the full build shows **36**, of which 33 are A2-related, in six sub-classes — (A) 18 hand-derived
boundary coefficient/RHS constants, (B) 2 flux estimators calling the pre-A2 path, (C) 3 Poiseuille
validations whose reference *is* the superseded scheme's own closed form `ny²/(ny²+2)`, (D) 4 MMS
observed-order **upper** bands exceeded because the boundary ring now converges at ≈3rd order, (E) 2
recorded iteration baselines, (F) **4 conservation/benchmark tolerances genuinely exceeded**.

Sub-class A/B are mechanical re-derivations (worked end to end in `a4/derivations.md`: thermal
two-cell diagonal 21 → **24** and rhs 180 → **200**; 3D two-cell 20 → **24**, −4 → **−16/3**; the W4
baseline 5 → 6, −1 → −4/3, 6 → 8 — each derived from `cP/cF/cB`, matching production exactly).
Sub-class C shows accuracy *improved* (Cartesian centerline 1.45455 → **1.46512** against the exact
1.5, 1.30× better) but needs a genuinely new discrete-exact reference. Sub-class D needs a policy
decision on the band's upper limit.

**Sub-class F is `PRODUCTION_DEFECT_SUSPECTED` and was not amended**, per A4-3:
`SpeciesConservationTest.OpenChannelWithVolumetricSource...` 1.587e-03 vs 1e-3;
`LowMachRegressionTest.GlobalMassImbalanceIsSmall` 1.034e-04 vs 1e-4;
`NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3` and its constant-property twin, v_max
error 0.1303 vs 0.12. These protect species conservation, mass conservation and an independent
literature benchmark — references that are *not* the superseded discretization — so the
obsolete-instrument explanation does not cover them. Overshoots are modest (1.03×–1.59×). Not proven
to be a defect; not excludable either without authorized production-level investigation.

Pre-existing and unamended: W8's two tests, and the GRAD-002 gradient-order assertion
(1.93558/1.96913/1.98396, `PRE-EXISTING / GRAD-002-ERA`).

**Decisions requested:** (1) scope for migrating sub-classes A–E (29 tests); (2) an investigation
authorization for sub-class F.

#### P12-DIFF-002-INV-002 + validation migration Steps 1–3 — `[ ]` BLOCKED AT STEP 3, PRODUCTION DEFECT FOUND (authorized 2026-09-16)

Evidence: `results/p12-diff-002/investigation-f/` (plan.md, summary.md incl. the Step 1 factorial,
logs 00–12, 12 tools) and `results/p12-diff-002/validation-migration/` (acceptance_gate.md sha256
`833ad5608a265ecc75b8df32e5a37242a68773f04155ba7f8101cf92150ba219`, frozen before any test change;
summary.md; logs/00_freeze.log). Production numerics **byte-identical throughout** (verified against
`a4/production_freeze.txt`).

**INV-002 resolved all four sub-class F failures.** All four pass on an isolated pre-DIFF-002 baseline
build (source copied outside the repo, one block removed from the copy — the authoritative tree never
written to), so all four are DIFF-002-caused, not pre-existing.

- The assembled DIFF-002 boundary stencil is **conservative**: constant-field net flux ≤ 2.13e-14,
  telescoping mismatch ≤ 1.10e-15 relative, on 12 mesh families including all-fallback, mixed,
  sheared, graded and 3D. A broken conservation identity is ruled out.
- **Species (F-C):** the test's own inline `ρD|S|/d·(Y_P − Y_b)` estimator. The consistent operator
  gives **3.48e-08** where the test reports 1.587e-03 (45 600×); an independent Σ(AY−b) route agrees
  at −3.5e-09; the estimator's error converges at first order and passes its own bound by 120×8.
- **Low-Mach (F-C):** the metric **grows** under refinement in the pre-DIFF-002 build too
  (4.80e-05 → 1.03e-04 → 1.07e-04 at 32×6 → 64×12 → 128×24) and already exceeds 1e-4 there at 64×12;
  tightening solver tolerances 10³× does not reduce it. Its formulation needs review, not its value.
- **Natural convection (F-F → F-C, Step 1):** a 2×2 factorial of isolated builds shows the **momentum
  wall shear alone** causes the velocity-extrema degradation (momentum-only 0.13764/0.13089 vs
  "both" 0.13703/0.13026) while the **thermal** reconstruction improves Nu_avg and leaves u_max/v_max
  unchanged (0.10365/0.06631 vs neither's 0.10442/0.06714). θ range, buoyancy coupling, the gradient
  scheme and convergence are excluded by the same data.

**Step 3 found a genuine production defect and stopped.** `src/thermal/ThermalInterface.cpp:60-73`
hard-codes the boundary coefficient as `conductivity * face.area() / distance` — the **pre-A2
two-point** wall flux — with no `boundaryValueCoefficient` and no far-cell entry, while its own comment
claims "Same boundary treatment as the single-material assembly". A2 made that untrue by making
`EnergyEquation.cpp`'s Dirichlet wall flux unconditional. For a **single region**, where the two
production paths must be identical, they now differ by O(1) physical temperature:

```text
Cartesian 2x1      2/2   rows  max |d diag| 2.500  max |d rhs| 533   solved max |dT| 3.024 K
Cartesian 10x10   36/100 rows  max |d diag| 5.000  max |d rhs| 1133  solved max |dT| 0.661 K
Cartesian 20x20   76/400 rows  max |d diag| 5.000  max |d rhs| 1133  solved max |dT| 0.657 K
Cartesian 3D 8^3 296/512 rows  max |d diag| 0.938  max |d rhs| 225   solved max |dT| 1.431 K
```

`ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo` correctly detects this and **was
not amended**. Classification **F-A**. A smallest correction is proposed in
`validation-migration/summary.md` §3 (the same six-line pattern A2 applied to the other five call
sites; no new sparsity mechanism needed) and was **deliberately not implemented**.

**Migration actually completed:** `tests/support/BoundaryFluxProbe.hpp` (new) is now the single shared
"flux production actually assembles" diagnostic, and `ThermalBoundaryConsistency`'s helpers use it —
`ConvergedFieldIsConsistentWithItsBoundaryValues` **now passes** (was 9.83e-02 vs 5.75e-10), with its
`1e-9 × throughput` tolerance unchanged. The other 20 authorized migrations were not started.

**Recorded before the work began** (gate §3): leaving the 13 **U** tests unchanged means Step 6's W7
cannot pass — four U-D MMS upper-order-band tests sit inside W7's own suite list, and A4 established
those need a numerical-**policy** decision rather than a derivation.

Steps 4–10 not reached. GRAD-002, MESH-007 G6.3, W8 and the GRAD-002 gradient-order test untouched.

#### P12-DIFF-002 ThermalInterface conjugate boundary-flux fix — `[ ]` accepted T0–T12 after A5 (authorized 2026-09-16)

Evidence: `results/p12-diff-002/thermal-interface-fix/` (acceptance_gate.md sha256
`9944d066659d4127cbfdba5246f67b023461738fb9e9f5cd96d0d289258e8870`, frozen before any production
edit; summary.md; logs 00–13; 4 probes; data/). The F-A defect above is **fixed**: the boundary-face
contribution `EnergyEquation.cpp` already performed is now **one shared exported helper**
(`assembleThermalBoundaryFaceContribution`) called by both the single-material and the region-aware
assembly, so the two paths cannot drift apart again. Three production files changed; the other 20
byte-identical.

The pre-registered O(1) divergences (3.024 K, 0.661 K, 0.657 K, 1.431 K) are **exactly zero** — the
assemblies are bitwise identical. T1 proved the single-material path unchanged **bitwise** over 64
configurations (68 962 `%a`-hex lines) against a baseline reconstructed by reverse-applying the
recorded edits and then *proved* authentic: its rebuilt `libcfdcore.a` hashes to `58be6b75…`, the
pre-fix value frozen before the work. T5/T6: 812 boundary faces, 0 classification mismatches against
the frozen A1 oracles, 174 fallback faces bitwise historical, identity `cP−cF=cB` ≤ 3.4e-16. T7:
constant/linear/quadratic exact, cubic order 2.000, in one- and two-region assemblies. T8–T10:
analytical two-layer slab T ≤ 8.5e-15, interface antisymmetry 0.000e+00, hand-derived 4-cell
multi-region matrix 0.000e+00.

**Initially `BLOCKED AT T11`** (preserved, not rewritten): 14 failures, all frozen inventory items
(11 M-A + 3 U-H), with **0 new**. T11 was not weakened and not declared passed. A5 resolved it.

**Disclosed side effect, attributed:** on a 35 %-sheared mesh the 1D-analytic error goes 8.5e-15 →
1.228e-03 — *exactly* the single-material path's own pre-existing value, unchanged to the last digit;
the conjugate path's former apparent exactness came from the superseded two-point wall flux. On the
same mesh the fix **repaired** conservation (per-cell 1.77e-03 → 4.8e-14; Neumann global imbalance
2.43e-02 → 2.2e-14). The corrected-wall / uncorrected-internal asymmetry remains out of scope.

#### P12-DIFF-002 Amendment A5 — M-A reclassification + validation migration — `[x]` COMPLETE (authorized 2026-09-17)

Evidence: `results/p12-diff-002/validation-migration/acceptance_gate_A5.md` sha256
`fbc927a22cf474b97d7a1cd3aa69f4646ca9c1a7691299fd9fd2f7fca66f40cb` (frozen before any authoritative
test was edited), `a5/summary.md`, `a5/logs/00–17`, `a5/tools/derive_expected.py`. **A5 changed no
production file** (0 of 23, verified start and end).

The frozen M-A inventory was **not authoritative**: `RegionAwareThermalDiffusionTest.`
`EqualConductivityMatchesSingleMaterialPathExactly` encoded no constant — it asserted a genuine
production invariant, was detecting the ThermalInterface defect, and **passes untouched** now. Kept
**KEEP**, its file byte-identical. Count correction recorded: the inventory lists **18** M-A entries,
not 12 (six live in suites T11 does not run); all 18 audited → **1 KEEP, 17 MIGRATE, 0 DEFECT, 0
UNCERTAIN**.

The audit also found a second distinction the inventory missed. Six entries are **contaminated
instruments**, not stale constants: they assert an internal face's equal/opposite contribution — still
exactly true (interpolated coefficients unchanged: 4.0, 51.5, 0.011, 0.00865) — but read a matrix
entry that now also carries DIFF-002's deliberately **one-sided far-cell coupling**. Each was amended
to pin the far-cell term and assert the asymmetry *equals* it, and to re-test the property where
nothing can contaminate it (two interior cells of a 5×5; and every internal face at once under
gradient-type boundaries). Coverage strictly increased (assertions 63→70, 57→71, 26→32, 20→25, 13→17,
47→54); nothing disabled, no threshold weakened.

Expected values came from `a5/tools/derive_expected.py` — from-scratch **Python**, no CFDApp code,
computing `cP = Γ|S|h₂/(h₁(h₂−h₁))`, `cF = Γ|S|h₁/(h₂(h₂−h₁))`, `cB = Γ|S|(1/h₁+1/h₂)` from the mesh
generator arguments, asserting `cP−cF=cB` for every case. Derived == measured everywhere.
Non-vacuity: all **17** amended tests FAIL against a library with the reconstruction removed
(`90473bfb…`), while `SparseAssembly3DTest.OneCell` (all six walls one-cell-thick) and entry 10's
`FixedGradient` row correctly still pass — proving the values are per-face, not a blanket factor.

**T11 re-run exactly as frozen: PASS.** 467 run, 464 pass, **3 fail**, 18 disabled — the three
remaining failures are precisely the permitted **U-H** tests; all 11 M-A failures resolved, 0 new.
`ThermalInterface fix acceptance: PASS T0–T12`. M-B also completed (species conservation estimator →
`tests/support/BoundaryFluxProbe.hpp`, threshold unchanged, mutation-controlled). There is no M-C
class. **M-E deferred**: recorded iteration/status baselines are not mathematically derivable.

#### P12-DIFF-002 Amendment A6 Step 1 — M-E investigation — `[ ]` BLOCKED, PRODUCTION DEFECT FOUND (authorized 2026-09-17)

Evidence: `results/p12-diff-002/validation-migration/a6/` (logs 01–03, 2 probes). **No A6 gate was
frozen and no test was amended** — Step 1 is investigation-only and its stop rule fired. Production
read but not modified.

`SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline` is **not** an obsolete baseline: it is
detecting a genuine defect in the divergence detector. On `cavity(4, 0.01)`, α = 0.7/0.3, tol 1e-6 the
reference run **Converges** in 406 iterations, but merely *enabling* the detectors aborts the same
solve at iteration 112 with status **Diverging** and an unconverged field (u off by 7.98e-04).
Pre-DIFF-002 the two runs were bit-identical (field difference exactly 0.000e+00).

Cause, localized and independently replayed: `src/solver/SolverRobustness.cpp::`
`OuterIterationMonitor::diverging` applies two rules. Rule 1 (lines 389–400) floors its base —
`base = max(tracker.floor(), bestBeforeWindow)`, and `floor()` *is* the convergence tolerance
(line 356) — so it does **not** fire. Rule 2 (lines 401–415) uses `oldest` **raw, with no floor**, and
fires on a monotone **round-off** drift of the continuity residual, `1.958e-14 → 3.100e-13`: values
5.1e+07× and 3.2e+06× *below* the 1e-6 tolerance, i.e. already converged by the detector's own
definition. An independent replay reproduces production's own message exactly, and the same rule with
rule 1's floor does **not** fire. DIFF-002 did not create the defect; it exposed it by changing the
trajectory.

`SIMPLERobustnessTest.DivergenceStatus` classifies **MIGRATE_POLICY** (not amended): the run is a
genuine finite runaway (u 7.5e-02 → 1.26e+13, growth 1.7e+14, fields finite, status `Diverging`), but
the recorded `EXPECT_EQ(result.iterations, 20u)` is trajectory- and mesh-specific — measured 20/20/21
across 4×4/6×6/8×8 now and 25/21/20 pre-DIFF-002. `startIteration + window = 20` is a *lower bound* on
when the detector can fire, not a contract value.

Steps 2–10 not reached: no U-D/MMS work, no A6 gate, no W7/W8/W9/W10, GRAD-002 and MESH-007
untouched.

#### P12-DIFF-002-ROB-001 — divergence detector floor consistency — `[x]` COMPLETE (authorized 2026-09-17)

Gate frozen before any production modification: `results/p12-diff-002/rob-001/acceptance_gate.md`
sha256 `1e2c98684b62b14418dd745ae7c5b8397b0547fb073212196c6fdc64753d3a2d`. Report
`rob-001/summary.md`, logs 00–11, 2 probes. The A6 stop above is **preserved verbatim** — it was
correct when made, and ROB-001 fixes what it found.

```text
R1 reproducer fails first     PASS      R6 rest of SolverRobustness  PASS   CFDSolverTests 90/90
R2 minimal change             PASS      R7 sensitivity               PASS   0 of 12 healthy altered
R3 baseline invariance        PASS      R8 fresh regression          PASS   680/676/4, 0 new
R4 genuine divergence fires   PASS      R9 scope                     PASS
R5 rule-2 non-vacuity         PASS
```

**Root cause:** `OuterIterationMonitor::diverging` applied two growth rules with **inconsistent
floors**. Rule 1 (persistent excursion) bases its comparison on
`max(tracker.floor(), bestBeforeWindow)`; rule 2 (sustained runaway) used `oldest` raw. Since
`tracker.floor()` *is* the residual's convergence tolerance, rule 1 could never fire on an
already-converged residual while rule 2 could — and did, on a continuity residual drifting
`1.958e-14 → 3.100e-13` (ratio 15.8) against a 1e-6 tolerance, every one of the ten window values
already converged, aborting a **converging** 4×4 cavity at iteration 112 as `Diverging` with an
unconverged field (u off by 7.98e-04).

**Fix:** two functional lines — `meaningfulOldest = std::max(tracker.floor(), oldest)` — with
`oldest` kept raw for the diagnostic. Completeness proven rather than asserted: reverse-applying the
edit reproduces the frozen pre-fix file byte-for-byte (`d210001a…`), so no other production edit is
hiding in it. Growth factors, windows, start iterations, tolerances, stagnation, fallback, the
adaptive controller and the non-finite path are untouched; nothing special-cased, rule 2 not disabled.

**Baseline restored:** `DefaultRobustnessPreservesBaseline` passes with its file byte-identical —
detectors OFF and ON now both Converge in 406 iterations with field difference **exactly 0.0000e+00**.
**Genuine divergence intact:** the runaway still reports `Diverging` with finite fields and
1.664e+14 growth. **Non-vacuity**, three new sequences in `tests/unit/solver/test_solver_robustness.cpp`
on the continuity channel: healthy decreasing → no fire; sub-floor round-off growth → no fire (this
one *failed* before the fix); above-floor runaway → still fires at 15, through rule 2. A vacuity trap
was caught by the gate's own dry-run column — (b)'s first draft grew only 2.68× across the 5-wide
window, so it passed pre-fix; the sequence was corrected, not the criterion.

**Sensitivity** (verification, not tuning): 12 healthy configurations across 4×4/6×6/8×8, tolerances
1e-5…1e-8 and windows 5/10/20 are all observationally inert (field difference 0.000e+00); 11 genuine
runaways across the same spread are all detected, firing at 10–42 iterations with growth
4.7e+03…1.1e+20.

**Regression** (A4 harness, library `143a1dda…`): `CFDSolverTests` 90/90, `CFDSimpleTests` 123/122/1,
`CFDThermalTests` 99/99, `CFDDiscretizationTests` 169/168/1, `CFDCaseIntegrationTests` 63/61/2 (18
disabled), `CFDTurbulenceTests` 136/136 — **680 run, 676 pass, 4 fail, 0 new**. All four failures were
pre-registered: `DivergenceStatus`'s `iterations == 20` assertion (A6 **MIGRATE_POLICY**, deliberately
not migrated here) and the three frozen **U-H** tests. The four-suite A5/A6 checkpoint is unchanged at
467/464/3.

Production: `src/solver/SolverRobustness.cpp` `d210001a…` → `2b97f94a…`. `SolverRobustness.hpp`,
`test_simple_robustness.cpp`, all 15 other production numerical files, every frozen gate and all A6
evidence byte-identical. Next authorized decision: **RESUME A6 FROM STEP 1**, re-evaluating both M-E
tests on the corrected library.

#### P12-DIFF-002 Amendment A6, resumed after ROB-001 — `[ ]` BLOCKED / FAILED A6 GATE at A6-j (authorized 2026-09-17)

Gate frozen before any authoritative test was modified:
`results/p12-diff-002/validation-migration/acceptance_gate_A6.md` sha256
`ba33ced0073fb0e1026fda63771a0a07ea73620a79e2a140bfd5525f33d3393d`. Report `a6/resumed/summary.md`,
logs 01–15, 3 probes. The **first** A6 attempt's `BLOCKED — PRODUCTION DEFECT FOUND` is preserved
verbatim (`a6/summary.md`, `a6/logs/01–04`) and was correct. **A6 changed no production file** (0 of
22, verified start and end).

```text
Steps 1-8  PASS      Step 9 fresh W7  FAILED -> STOP     Step 10 W8  not reached
```

**M-E resolved.** `DefaultRobustnessPreservesBaseline` → **KEEP**: it passes unchanged on the
ROB-001-corrected library, file byte-identical, confirming the first attempt's PRODUCTION_DEFECT call.
`DivergenceStatus` → **MIGRATE_POLICY**: its `EXPECT_EQ(iterations, 20u)` was the *earliest possible*
trigger `max(start,1)+window` asserted as a prediction; replaced by that derived lower bound plus the
derived guarantee `max(start,1)+2*window`, computed from the settings. Measured 11–41 across 18
configurations with rule 1 and rule 2 alternating; both bounds held everywhere. Non-vacuity: the new
criterion rejects detector-disabled, fires-too-early (caught by the lower bound), runaway-recovered
and healthy-converging. `SIMPLERobustnessTest` **10/10**.

**U-D resolved.** Eleven failing criteria, all gating a **three-level Richardson** order that the
framework itself labelled `monotonic_not_asymptotic`. Derivation: DIFF-002 makes the boundary ring
converge at ~3 while the interior stays ~2, so `E(h) = A h² + B h³`, whose pairwise order lies in
(2,3) and decays to 2 while the triplet estimator overshoots past 3. Verified by refining **beyond**
the tests' range to 256²: u l2 2.382→2.017, u linf 2.374→**1.988**, v linf →**1.978**, while
u_boundary_ring is asymptotic at 3.017/2.932/2.948/**2.947**. Pre-DIFF-002 all 19 MMS tests pass with
a boundary ring of order **1.976** — the bands were calibrated on a single-rate error. Amendment:
gate the finest **pairwise** order with upper limit `formal+1` (the faster component's own order,
measured), every lower bound and decrease gate unchanged, **plus a new boundary-ring order gate
[2.50, 3.50]** pinning DIFF-002's wall flux. Non-vacuity, executed: on the pre-DIFF-002 library the
order bands still pass (1.981/1.976) while the new gate **fails** (1.949/1.937), i.e. it detects a
disabled DIFF-002 reconstruction — which the bands alone cannot. MMS suite **19/19**.

**U-C/U-F/U-H audited individually, no PRODUCTION_DEFECT.** U-C (3 Poiseuille) are
**VALIDATION_DEFECT**: they assert production reproduces the *superseded* scheme's own closed form
(centerline 1.45454545 = 16/11; dp/dx error = 2/66 = 3.03 %), while production is measurably closer to
the exact physics (1.30× on centerline, **3.5×** on dp/dx — 0.86 %). U-F natural convection ×2 are
**EXPECTED_DIFFT002_CHANGE** (v_max error 0.13026 vs 0.12, attributed by INV-002's factorial to the
momentum wall shear alone); U-F low-Mach is **PRE_EXISTING** (1.03433e-04 vs 1e-4; the metric grows
under refinement pre-DIFF-002 too). U-H ×3 are **PRE_EXISTING** (W8 ×2, GRAD-002 ×1), unamended.

**Fresh authoritative W7** (configure → build all → verify hashes → ctest; no A1/A2 count reused):
**1923 run, 1914 passed, 9 failed, 90 disabled**, against A4's 1913/1877/**36**. The accounting is
exact: +7 ConjugateBoundaryEquivalence +3 ROB-001 sequences; −17 A5 M-A, −1 A5 M-B, −1 RegionAware,
−1 ConjugateConductionPath, −1 ConvergedField, −1 ROB-001, −4 A6 U-D, −1 DivergenceStatus.

**W7 does not pass, so A6 stops at A6-j.** The 9 remaining failures are exactly the 9 audited tests —
none unexplained, none new — but **five are DIFF-002-related and unresolved**: the 3 U-C need a
genuinely new discrete-exact reference for the three-point wall flux (a numerical derivation; Step 7
authorizes only U-D, and writing the current output in would be the prohibited "adopt production
output" move and would discard a real 3.5× accuracy gain), and the 2 natural-convection U-F need a
decision on an independent literature benchmark's bound, which the authorization forbids redefining.
**Step 10's W8 decision point was therefore not reached**, W8 was not run as a gate and remains
unamended, W9/W10 not reached, and DIFF-002 is **not** marked complete.

**Two separate authorizations requested:** (1) derive a new discrete-exact Poiseuille reference for
the three-point wall flux (U-C); (2) a decision on the de Vahl Davis coarse-grid velocity-extrema
bound (U-F).

#### P12-DIFF-002-UC-001 — Poiseuille discrete-exact reference derivation — `[x]` COMPLETE — U-C RESOLVED (authorized 2026-09-17)

Evidence `results/p12-diff-002/uc-001/`: `plan.md`, `summary.md`, frozen
`acceptance_gate.md` sha256 `bed6b8941a7e011212231f0e4585ff0164fa3e7ff5cfb87e3be009a3ed090d06`,
logs 01–11, 4 tools (2 exact-rational Python references, 2 C++ probes). **UC-001 changed no
production file** — the clean rebuild reproduces `libcfdcore.a` `143a1dda…` bit-identically.

**The reference was stale, not the discretisation.** The tests' "discrete exact" solution still
encoded the superseded two-point wall gradient over the half cell. Derived independently from the
stencil coefficients and solved in exact `Fraction` arithmetic: wall row `−4u₀+(4/3)u₁ = Gh²`, the
discrete solution equals the continuum parabola **exactly at the cell centres** (no `+Gh²/8`
offset), `dp/dx = dp/dx_exact·2ny²/(2ny²+1)`, relative error `1/(2ny²+1)`, drop ratio
`2ny²/(2ny²+1)`. At `ny=8`: `dp/dx = −256/215`, profile `15/43, 39/43, 55/43, 63/43`, centreline
`63/43`. The superseded form gives centreline `16/11` and ratio `64/66` — **the exact constants
the tests asserted**. Step 2 first reproduced that historical reference independently, confirming
the diagnosis rather than assuming it.

**Step 5 — production matches the derived system at machine precision.** Five levels, worst
deviation **2.2e-16**: assembled momentum row entry-by-entry (8 rows, RHS 0.00e+00), every cell of
the solved system, matrix structure (far-cell column at the derived index), `cB` against a
**nonzero** wall value — added because the `u_b = 0` RHS check is vacuous whatever `cB` holds —
and wall flux with the global momentum balance closing to 9.3e-16. The solved 2D profile tracks
the reference to 4.07e-06 (64x8), 3.59e-08, 5.48e-09, 8.23e-09 (216x27).

**Step 6 — order established; the A6 observation checked, and partly corrected.** Both operators
are second order (→2.000); DIFF-002 changes only the error constant: `dp/dx` **3.909 at ny=8 →
4.000**, centreline **1.303 → 4/3**. A6's "≈1.30× centreline" is **confirmed**; its "≈3.5× dp/dx"
is **corrected** — 3.5 came from production's *measured* `dp/dx`, inflated by that estimator's
8.3e-04 relative bias. Improvement is recorded as a consequence, never as an acceptance criterion.

**Step 7 — non-vacuity.** Every criterion fails for all four wrong controls (two-point, far-cell
×2, far-cell sign flipped, far-cell dropped); weakest margin 4.6×. The two order gates are
explicitly recorded as **not** operator-discriminating (two-point is also second order), so the
operator claim rests entirely on the six value criteria.

**Two findings beyond the brief, both proved from exact rationals with no solver involved.**
(1) `pressure_gradient_asymptotic`'s observed order 2.2652 is an **instrument-resolution limit**:
adding production's measured pressure-extraction offsets (+9.93e-04, +1.06e-04, +2.13e-07) to the
*exact* reference reproduces 2.2652 / 1.1135 to all printed digits, while the exact DIFF-002
sequence is asymptotic (1.9846, ratio 0.9938). The same offsets leave the two-point sequence
asymptotic (1.0073) — which is why the gate passed before DIFF-002: the offset is 19.3 % of the
grid-to-grid difference Richardson consumes for DIFF-002 against 5.0 % for two-point, because
DIFF-002 shrank the error 4× while the extraction bias stayed. The declared `absoluteNoise = 1e-5`
is wrong for this quantity by up to 99×, but raising it cannot rescue the gate (only
`InsufficientSeparation`, also rejected). The claim was kept at full strength and **moved** to
`ny = 12/18/27`, where the offset is ≤ 4.6 %. (2) The centreline sampling deficit is
`4.5/(2ny²+1)` for even `ny` and `1.5/(2ny²+1)` for odd — **exactly 3× apart** — so an order
triplet must be **parity-consistent**: the mixed `12/18/27` yields p = 0.9376, ratio 0.65, *from
the reference values themselves*, and production reproduces 0.9376 exactly.

**Classification (Step 8):** `Profile`, `PressureDrop` → `MIGRATE_DERIVED_REFERENCE`;
`ProductionGridConvergence` → that **+ `REDESIGN_VALIDATION`** for the one order gate's triplet;
`MassFlow` → `KEEP` (passed throughout). **No `PRODUCTION_DEFECT`, nothing `UNCERTAIN`.**

**Migration (gate frozen first).** `PoiseuilleValidationUtils.{hpp,cpp}` reference formulas;
`test_poiseuille_production_validation.cpp` — `2.0/66.0` and `64.0/66.0` replaced by
`discreteGradientRelativeError(ny)` / `discreteDropRatio(ny)` and the centreline identity by
`centerlineSamplingDeficit(ny)`, all computed from `ny`; added the `216x27` grid; split the study
into one triplet per quantity with the reason recorded; two new `limitations`. **Every bound is
unchanged** (1e-4, 1e-3, band 1±0.1); no production output was copied into a test.

**Step 10 — fresh `--clean-first` rebuild (431/431, exit 0):** Poiseuille **10/10**, MMS
**19/19**, plus Physics 98/98, Piso 81/81, SIMPLE 123/123, Mesh 156/156, Solver 90/90, Core 30/30,
Field 40/40, Algebra 97/97, Thermal 99/99, Turbulence 136/136. Discretization 168/169 and Case
61/63; those 3 failures are all on the recorded pre-existing list and all out of scope
(GRAD-002 ×1, W8 ×2 — `a4/inventory.md` `KEEP_UNCHANGED`). **No new failures.** Of the 9
pre-existing W7 failures the **3 U-C are resolved**; 2 are the U-F natural-convection tests
(Part B), and 4 remain out of scope. W8 not run, no commit, no push.

#### P12-DIFF-002-UF-001 — Natural-convection benchmark policy investigation — `[x]` COMPLETE — U-F RESOLVED (authorized 2026-09-17)

Evidence `results/p12-diff-002/uf-001/`: `investigation_plan.md`, `summary.md`, frozen
`acceptance_gate.md` sha256 `558dad1a4fba6f8d27a8dd61b033bff9f68a2f65355e9933dd6ed09a144c9559`,
logs 00–17, 8 tools, `data/wan_patnaik_wei_2001.pdf` + extracted text. **UF-001 changed no
production file** (0 of 23) and left `DeVahlDavis1983.hpp` (`35cbb3a5…`) and
`validation/literature/natural_convection/**` **byte-identical**. The de Vahl Davis values were
never changed, redefined or replaced.

**Classification: both tests `REDESIGN_GRID_CONVERGENCE`** — the user's category **C**, with **B**
as the mechanism and **D** as a quantified contributor. Not `PRODUCTION_DEFECT`, not
`BENCHMARK_DEFINITION_MISMATCH`, not `UNCERTAIN`, not `KEEP_BOUND`.

**Benchmark definition established from the source, not assumed.** Re-fetched the open PDF the
repo's own provenance names (Wan/Patnaik/Wei 2001, sha256 `8a7ca469…`): Table 2 is "maximum
vertical velocity (v) at the **mid-height (y = 0.5)**" and Table 3 "maximum horizontal velocity
(u) at the **mid-width (x = 0.5)**" — so CFDApp's sampling *locations* match — with `Nu` =
∫₀¹ Nu_local dy (eq. 55). Independently established from arXiv `physics/0305049` that de Vahl
Davis's tabulated values are **h→0 Richardson-extrapolated** ("the 'grid-independent' values …
obtained by Richardson extrapolation"), i.e. not values on any mesh.

**Nondimensional equivalence derived and then verified.** `Ra = 1·(Ra·Pr)·1·1/(Pr·1) = Ra`
exactly, `Pr = ν/α`, velocity scale `α/L = 1`, `θ = T`; the case's buoyancy differs from the
benchmark's only by the constant `−Ra·Pr·0.5`. Re-solving with the benchmark's own reference moved
velocity by **2.7e-13** and temperature by **7.3e-15**. Disclosed: the secondary prediction that
the *pressure* difference is exactly the continuum linear field does not hold discretely
(residual 71 of a 355 range) — a collocated-coupling property, not an equivalence failure.

**The momentum wall change is correct — `CORRECTED_DISCRETIZATION`.** Manufactured analytic
wall-shear study against the production `boundaryFaceDiffusionTerms`, no CFD solve, run on both
libraries: two-point is **first order** (quadratic 7.06e-02, order 1.000) while DIFF-002 is
**exact for quadratics** (~1e-15) with cubic order **2.000** and sine **1.995** — 4.2× more
accurate at n = 10. `cP−cF=cB` 4.44e-16, constant-field flux 5.00e-16. Established from the
analytic shear and the order study, **not** inferred from conservation improving.

**No continuum-limit regression.** Five grids (10/15/20/30/40) on the authoritative library and
on an isolated pre-DIFF-002 baseline (`719d0fc7…`, one block removed, every other production file
byte-identical); mass imbalance and wall flux 0.0e+00 everywhere. DIFF-002 is better for `Nu_avg`
at every grid (ratio 0.463→0.800) and worse for the velocity extrema at every grid (`v_max`
1.940→1.499→1.248) — **both ratios moving toward 1**, the signature of a common continuum limit
with different error constants. Richardson on the only constant-ratio triplet, 10/20/40:
`v_max` extrapolates to 3.6642 (GCI 0.0188) against the literature 3.679, `Nu_avg` to 1.1154
(GCI 0.0124) against 1.12 — **every credible extrapolation within its own GCI**. One baseline
sequence (p = 0.062) is reported as *not computed* rather than printing its 5.19 artifact.
Parity rule carried from UC-001 and corroborated by this file's own `GridConvergence` comment that
`u_max` "oscillates" on the mixed 10/15/20 family.

**Why the criterion was invalid — two findings.** (1) The test recorded its bounds as "locked from
diagnosed evidence (coarsest grid, largest error): u_max 10.1 %, v_max 6.7 %, Nu_avg 4.9 %"; the
pre-DIFF-002 baseline measures **6.714 %** and **4.856 %** at 10×10 — the quoted figures to the
digit shown. The `0.12` bound was a **regression lock on old production output at one
non-asymptotic grid** (10×10 carries 13.0 % velocity error), with the literature value entering
only to compute that error. (2) The failing assertion is **anti-correlated with wall-operator
correctness** there: a deliberately far-cell-doubled operator scores `v_max` **0.01855** against
the correct code's **0.13026** — 7× "better" on the very assertion that fails. (The old test as a
whole still rejects that control, via its `Nu_avg` bound.)

**Amendment — only the two authorized tests; every bound derived, none loosened.** The three
hand-locked 10×10 bounds are replaced by `expectApproachesDeVahlDavis` on the parity-consistent
10/20 pair: **P1** each error must shrink toward the fixed literature value; **P2** by ≥1.5× per
doubling (formal order 1 — first-order upwind — predicts 2.0); **P3** the 20×20 errors must meet
**this file's own already-written** `DISABLED_Grid20x20` bounds 0.08/0.08/0.05; **P4** each
extremum's *location* within one cell of the literature's reported location (y = 0.813,
x = 0.179) — benchmark data `DeVahlDavis1983.hpp` carried that no test had ever used. Every health
assertion unchanged; the 10×10 errors preserved and printed as diagnostics. Non-vacuity dry-run
**before** editing: far-cell-doubled fails 8/13, sign-flipped 7/13, while the two-point scheme
**passes** — correctly, and recorded as such, since these criteria are deliberately *not*
wall-operator-discriminating (that claim lives in the wall-shear order study).

Measured after the change, matching the frozen gate exactly: 10×10 Nu_avg 0.02246 / u_max 0.13703
/ v_max 0.13026; 20×20 0.01218 / 0.05375 / 0.04713; reduction factors 1.844/2.550/2.764. Each
test now ~40 s (was ~4 s).

**Focused verification: 425 run, 424 passed, 1 failed** — NaturalConvection 7/7, HeatedCavity 5/5,
ConjugateHeatTransfer 6/6, Boussinesq 5/5, Thermal 99/99, Physics 98/98, Discretization 168/169,
MMS 19/19, SpeciesConservation 4/4, MultiphaseConservation 3/3, Cavity 10/10. The one failure is
the pre-existing GRAD-002-era test. **Zero new failures.**

**Fresh authoritative W7: 1923 run, 1919 passed, 4 failed** (1968 entries, 45 disabled), against
A6's 1923/1914/**9** — exactly **−5**, the 3 U-C plus the 2 U-F. **DIFF-002-related unresolved: 0.**
Remaining: GRAD-002-era `GridRefinementTest` ×1 and W8's two tests (**pre-existing historical
gates**), `LowMachRegressionTest.GlobalMassImbalanceIsSmall` (**unrelated known defect**).

**W7 does NOT legitimately pass**, applying its frozen wording exactly ("…and GRAD-002 passes,
each against its own originally frozen thresholds"): the GRAD-002-era assertion does not pass, and
being explained and `KEEP_UNCHANGED` does not make W7 a pass. **W8 was therefore NOT run.** The
exact W7 blocker is that single GRAD-002-era assertion, which pins the pre-GRAD-002 first-order
distorted-mesh behaviour; amending it is a GRAD-002 decision outside every authorization granted.
No commit, no push.

#### P12-DIFF-002 final authoritative W7 — `[ ]` BLOCKED / FAILED W7 (2026-09-17)

Evidence `results/p12-diff-002/final-w7/`: `summary.md`, logs 01–04, 2 tools. No previous W7
aggregate reused. **No production file changed**; all 11 DIFF-002-lineage gates and the
GRAD-001/GRAD-002/MESH-006/007 gate files verify intact.

**Build provenance.** configure → **clean-first rebuild of everything** (433 lines, 119 s) → all
**41** test binaries newer than the newest production source (`SolverRobustness.cpp`,
2026-09-16T16:00:47Z), **0 stale** → production hashes equal their recorded post-fix values → 0
binaries lacking the executable bit. `libcfdcore.a` is **bit-identical before and after the clean
rebuild** (`143a1dda…`): a reproducible build, and proof the binaries correspond to these sources.

**Result: 1923 run, 1919 passed, 4 failed, 45 disabled** (1968 ctest entries, 220.02 s). Against
A6's 1923/1914/**9** — the same 1923 run and exactly **−5**, the 3 U-C (UC-001) plus the 2 U-F
(UF-001). **0 DIFF-002 regressions, 0 new or unexplained failures.**

All four remaining failures reproduce their recorded values **digit-for-digit**:

| test | measured | recorded | class |
|---|---|---|---|
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | 1.93558 / 1.96913 / 1.98396 | identical | known GRAD-002 failure |
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | dp/dx order −5.012 | A1: −5.012 | known W8 historical failure |
| `MultiBlockProductionCase.CurvedChannelGridConvergence` | G-error order 1.050 | A1: 1.050 | known W8 historical failure |
| `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | 1.0343290470e-04 | A6: 1.03433e-04 | pre-existing unrelated (P12-COMP) |

A2-7's explicit condition — the GreenGauss test "stays `PRE-EXISTING` only while it reproduces the
recorded GRAD-002 values (1.936/1.969/1.984)" — was **measured, not assumed**, and is satisfied.

**W7 semantics, applied exactly.** Three wordings exist. The original gate's W7 requires
"…and **GRAD-002 passes**, each against its own originally frozen thresholds" (**not met**); A2-7
adds "a classification does not waive a frozen failure" (**not met**); A6-j requires "0 unexplained
DIFF-002-related failures" (**met**). A6-j is a row in **A6's own** gate table governing whether A6
could proceed, not a redefinition of W7 for closure — A6's verdict table still reads "W1-W10 all
legitimately pass → COMPLETE". The closure bar is therefore the original wording, and it is not met.
The two production-case failures are **not** counted against W7 (the original gate carves them into
**W8**); `LowMachRegressionTest` is P12-COMP and outside W7's named scope — reported, not excluded.

**The single W7 blocker is the GRAD-002-era assertion**, which pins *pre-GRAD-002 first-order*
distorted-mesh behaviour (`order < 1.9`) and now fails **because the scheme became second order** —
the same obsolete-instrument class as the U-C and M-A tests already migrated. **W8 was not run**
(permitted only once W7 legitimately passes). W9/W10 not reached. DIFF-002 is **not** complete.

**Circular blocker, decision required.** Amending that assertion is a **GRAD-002** decision, which
every authorization in this lineage forbids; but the plan's ordering requires DIFF-002 to close
before returning to GRAD-002. Nothing here indicates a production defect. No commit, no push.

#### P12-GRAD-002-VAL-001 — Green–Gauss distorted global-order instrument — `[x]` COMPLETE — MIGRATE_VALIDATION (authorized 2026-09-17)

Narrowly scoped to **one assertion in one test**. Evidence `results/p12-grad-002/val-001/`:
`acceptance_gate.md` sha256 `9baf33440c1fce50fc77ac6cea42b6cfe389744bde3619445baef1fbc4f69e22`
(frozen **before** the test changed), logs 00–03, 3 tools. **No production file changed**
(`libcfdcore.a` `143a1dda…` before and after); all GRAD-001/GRAD-002/DIFF-002/MESH-007 gates
byte-identical. The original failure is preserved verbatim
(`final-w7/logs/02_greengauss.log`, sha256 `5247098e…`).

**Original purpose recovered** from the test's own comment: GreenGauss on a distorted mesh "loses
its Cartesian-specific boundary advantage… its global rate **drops to ~1.58–1.72**… record its
actual performance rather than assuming second order". `[1.4, 1.9]` was a **descriptive envelope
around a measured degradation**, not a derived correctness property.

**Independent derivation, stated before measuring.** Plain Green–Gauss pairs an *exact* boundary
face value with an *interpolated* opposite-face value carrying `−½w(1−w)L²∂²φ/∂ξ² = O(h²)`. In the
interior those two errors sit on opposite area vectors and cancel (order 2); at a boundary the
cancellation breaks, so the ring degrades to O(h). The ring is ~4n cells of volume ~h², a volume
fraction ~4h, so `L2² ~ C_i²h⁴ + 4C_b²h³` ⇒ **plain global order → 1.5**. GRAD-002 removes that
interpolation bias, restoring the cancellation ⇒ ring O(h²), `L2² ~ C_i²h⁴ + 4C_b'²h⁵` ⇒
**global order → 2, approached strictly from below**.

**Measured (grids 16…256), every prediction confirmed:** plain ring **1.0036**, plain global
**1.5233**, plain global L∞ **0.9997**; GRAD-002 ring **1.9936**, global **1.9958**; interior
**2.0002 / 2.0008** — *unchanged in both*, so the whole difference is confined to the ring, exactly
where GRAD-002 acted. The plain-Green–Gauss control reproduces the comment's documented
"~1.58–1.72" (**1.5802–1.7204**), confirming the historical reconstruction is faithful.

**The old bound detects no correctness property:** evaluated on the same variants, `[1.4, 1.9]`
**ACCEPTS** plain Green–Gauss (1.7204/1.6393/1.5802) and **REJECTS** the corrected treatment
(1.9356/1.9691/1.9840). Its *lower* bound does carry content and is **kept**.

**Amendment: two numbers at one call site**, `1.4, 1.9` → **`1.875, 2.15`**, derived not widened.
The sub-leading term gives `p(n) = 2 − c/n` with `c ≈ 0.5` measured (0.494–0.538 across five
pairs); with a **2× safety factor** the coarsest pair used (n = 8) predicts `p ≥ 2 − 1.0/8 =
1.875`. Model vs measured: 1.9375/1.93558, 1.96875/1.96913, 1.984375/1.98396 — agreement ≤ 0.002.
The **lower bound is RAISED 1.4 → 1.875**, so the assertion is strictly stronger.
**Non-vacuity**, measured before the edit: rejects plain Green–Gauss, a first-order boundary value
(0.55/0.51/0.50), a wrong boundary-coefficient **sign** (negative orders, errors growing) and an
O(h) boundary perturbation (0.61/0.52/0.50).

**Step 11 verification:** named test **OK**; `GridRefinementTest` **28/28**; gradient tests
**35/35**; `CFDDiscretizationTests` **169/169** (was 168/169); MMS **19/19**; Mesh **156/156**;
Physics **98/98**. Zero failures.

#### P12-DIFF-002 final authoritative W7, round 2 (post VAL-001) — `[x]` W7 PASSES ITS NAMED SCOPE (2026-09-17)

Evidence `results/p12-diff-002/final-w7/round2/logs/`. Round 1 (1923/1919/**4**) is preserved and
**not reused**. Clean-first rebuild (433 lines, 123 s); 41 test binaries, **0** older than the
newest production source; `libcfdcore.a` **bit-identical** before and after (`143a1dda…`);
production hashes equal their recorded values; 0 binaries lacking the x bit.

**1923 run, 1920 passed, 3 failed, 45 disabled** (215.91 s). The GRAD-002-era assertion is
**resolved** by VAL-001. Remaining:

| test | class |
|---|---|
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | **W8's own test** — carved into W8 by the original gate |
| `MultiBlockProductionCase.CurvedChannelGridConvergence` | **W8's own test** |
| `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | pre-existing, **P12-COMP**, outside W7's named scope (P12-NUM / MESH-001…006 / GRAD-002) — reported, not excluded |

W7's named scope — "P12-NUM, MESH-001…006 and GRAD-002 … each against its own originally frozen
thresholds" — now passes, with W8's two tests carved out by the gate itself and no new or
unexplained failure. **W8 therefore ran.**

#### P12-DIFF-002 W8 — `[ ]` BLOCKED — HISTORICAL W8 GATE DECISION REQUIRED (2026-09-17)

Evidence `results/p12-diff-002/w8/`: `summary.md`, `logs/01_W8.log` (complete raw output),
`tools/run_w8.sh`. **Run unchanged** — same cases, meshes, quantities, references, extraction and
thresholds; test sources byte-identical; **nothing amended, nothing retuned**.

```text
StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence   FAILED
MultiBlockProductionCase.CurvedChannelGridConvergence             FAILED
```

**First failed criterion:** `test_structured_quad_production_case.cpp:406`,
`EXPECT_GE(velocityOrder, 1.5)` — actual **1.4390906187401413**.

Raw errors recorded before any order was computed. StructuredQuad `dp/dx` (exact −1.2):
**−0.002088 / +0.0003255 / +0.002484** — the error **changes sign**, so |error| falls then rises
and no observed order is defined across the crossing; the −5.012 is that artifact. The relative
sequence 0.174 % → 0.027 % → 0.207 % reproduces A1's record exactly. Velocity L2
0.010826713 / 0.0047562152 / 0.0026536902. MultiBlock G error 0.008257 / 0.005394 / 0.002817.

Orders recomputed independently and matching the test: velocity 2.0298 / **1.4389**, dp/dx
**−5.013**, MultiBlock G **1.0502** / 1.6024.

Per the frozen W8 — "If either still fails: STOP, preserve the result, and request a separate
decision on amending the historical gate. Do not retune it" — **W9 and W10 were not reached** and
DIFF-002 is **not** complete. No commit, no push.

#### P12-DIFF-002 Amendment A2 — `[ ]` Dirichlet wall-flux activation consistency — BLOCKED / FAILED A2 GATE at A2-7 (authorized 2026-09-16)

Gate frozen before any production change: `results/p12-diff-002/acceptance_gate_A2.md`, sha256
`3a268060809a4566331163f4fa582d5ba5e08398368b4814a08256a2605d8d40`. Audit:
`a2/activation_architecture.md`. Report: `a2/summary_a2.md`. All original and A1 evidence preserved.

```text
A2-1 activation invariance      PASS
A2-2 orthogonal correctness     PASS
A2-3 A1 topology compatibility  PASS
A2-4 k-epsilon regression       PASS   (1.98 % -> 0.076 %, threshold untouched)
A2-5 W5 revalidated             PASS   (worst 1.1554x vs the 1.25x guard)
A2-6 W6 complete dataset        PASS   (144x18 dp/dx error 0.2070 % vs 0.747 %)
A2-10 production consistency    PASS
A2-7 W7 focused verification    FAILED -> STOP
A2-8 W8, A2-9 W9/W10            not run
```

**The activation inconsistency is fixed.** `non_orthogonal_corrections` conflated the *Dirichlet
wall-flux spatial scheme* with the *iterative non-orthogonal correction*; the gradient is now always
built for the boundary branch (so the wall scheme is chosen by geometry alone — valid inward stencil →
DIFF-002, otherwise the historical two-point fallback) while `enabled` still gates the internal-face
correction and `N` still means the pass count. The setting, its default of 0 and its semantics are
unchanged, `boundaryFaceDiffusionTerms` is unchanged, and `Diffusion.cpp` needed no change — its
explicit operator has been unconditionally higher-order at the boundary **since P0**, so A2 removes an
accidental divergence between the implicit and explicit paths rather than introducing a policy.

Measured: pre-freeze negative control showed the defect on all 4 callers × 4 geometries (uniform-grid
jump exactly the hand-derived 4/3). After the change, orthogonal meshes are **bitwise invariant**
across N = 0, 1, 2 for thermal/species/k-ε/momentum — whole matrix and RHS; on distorted meshes the
wall coefficient, isolated from the diagonal by subtracting exactly-recomputed internal-face
coefficients, is invariant to **1–2 ulp** (≤ 1.33e-15 on a scale of ~4.0) against the negative
control's 8.8e-01. **k-ε now passes**: Re_τ Cartesian 447.902 → 438.690 (the arm that previously
missed DIFF-002), distorted unchanged at 439.024, relative difference 1.98 % → **0.076 %**.

**W7 failed with two new regressions**, both explained but neither waived (the gate states a
classification does not waive a frozen failure), and both belonging to MESH-001/MESH-003, which this
authorization forbids amending:

- `StructuredQuadProductionCase.NonOrthogonalCorrectionIsActiveOnTheProductionPath` — a **lower
  bound requiring the N = 0 run to be inaccurate** (`uncorrectedError > 1.5 × cartesian`); the N = 0
  error improved 3.31e-2 → **1.65e-2**, so the bound fails. Its other assertion (the correction still
  helps) passes.
- `MultiBlockProductionCase.SectorConductionInterfaceConservation` — the test's own boundary flux
  probe uses `boundaryFaceDiffusionTerms(..., nullptr, false)`, the **pre-A2 two-point** estimator,
  while the solver now uses DIFF-002; interior lines (which use the matching internal formula) agree
  exactly. The solver's energy balance stays exact at 7.4e-14 and temperature L2 now converges at
  observed order **≈3.0**.

Counts: 970 tests run, 966 passed, 4 failed. Pre-existing and unmodified: the GRAD-002 gradient-order
assertion (digit-identical 1.93558/1.96913/1.98396) and W8's two historical tests. Files changed:
`src/thermal/EnergyEquation.cpp`, `src/physics/MomentumEquation.cpp`, `src/species/SpeciesEquation.cpp`,
`src/turbulence/KEpsilonEquation.cpp` — no header, no signature, no test, no threshold, no golden
output.

**Decision requested** on the two new W7 failures before W8/W9/W10 can proceed.

---

# Future P12 Scope

These remain **unauthorized** until an explicit scope decision is recorded.

## P12-COMP

Completed:

* [x] COMP-001 — EOS-based boundary-density model.
* [x] COMP-002 — coupled compressible pressure–velocity solver.

Deferred:

* [ ] Advanced compressible-energy formulation.
* [ ] Pressure-work coupling.
* [ ] Viscous-dissipation coupling.
* [ ] Higher-Mach capability.
* [ ] Compressible energy validation.
* [ ] Higher-Mach benchmark validation.

---

## P12-TURB

Potential future scope:

* [ ] Establish stronger turbulence-validation baseline.
* [ ] Fully developed turbulent-channel validation against DNS profiles.
* [ ] Improve wall-treatment architecture.
* [ ] Add wall functions where justified.
* [ ] Validate k-ε.
* [ ] Validate k-ω.
* [ ] Validate SST.
* [ ] Add separated-flow turbulence validation.
* [ ] Quantify mesh/y+ sensitivity.
* [ ] Add turbulence grid-convergence studies.

Do not add new turbulence models merely to increase model count.

---

## P12-SPECIES

Potential future scope:

* [ ] General source-term framework.
* [ ] Multiple transported species.
* [ ] Species-dependent diffusivity.
* [ ] Reaction source terms.
* [ ] Coupled reactions.
* [ ] Conservation enforcement.
* [ ] Thermal/species coupling.
* [ ] Production validation cases.
* [ ] Manufactured-solution verification.

---

## P12-MULTI

Potential future scope:

* [ ] Couple mixture density consistently where physically required.
* [ ] Interface reconstruction.
* [ ] Interface compression.
* [ ] Surface tension.
* [ ] Curvature calculation.
* [ ] Contact-angle treatment.
* [ ] Bounded volume-fraction transport.
* [ ] Static-droplet validation.
* [ ] Advection validation.
* [ ] Capillary-wave validation.

Do not claim full multiphase CFD from the existing foundation alone.

---

# P13 — Production Maturity

**Status:** future; not authorized.

Potential scope:

* [ ] Structured diagnostic logging.
* [ ] Crash reporting.
* [ ] Long-duration stress testing.
* [ ] Large-case stress testing.
* [ ] Benchmark dashboard.
* [ ] Automated result comparison.
* [ ] Case-schema migration/versioning.
* [ ] Backward-compatibility testing.
* [ ] Plugin/model extension architecture.
* [ ] Linux packaging.
* [ ] Cross-platform installer testing.
* [ ] Formal release-candidate qualification.
* [ ] Performance-regression tracking.

---

# Blocked / Manual

* [ ] Native Windows + CUDA validation.

  * WSL2/Linux + CUDA is verified.
  * Requires the appropriate native Windows/CUDA build environment and hardware path.

---

# Completed

## Releases

* [x] v0.1.5
* [x] v0.2.0

## Major Phases

* [x] P0–P5 — Core CFDApp
* [x] P6 — GPU Performance
* [x] P7 — Performance Validation
* [x] P8 — Production Hardening
* [x] P9 — v0.2.0 Release
* [x] P10 — Production Physics Integration
* [x] P11 — GUI Case Authoring
* [x] P12-COMP-001
* [x] P12-COMP-002

## P12-NUM — Numerical Methods & Robustness

* [x] NUM-001 — Higher-order convection
* [x] NUM-002 — Gradient reconstruction
* [x] NUM-003 — Non-orthogonal/skewness correction
* [x] NUM-004 — Solver robustness
* [x] NUM-005 — Grid convergence
* [x] NUM-006 — Manufactured solutions
* [x] NUM-007 — Production validation

Final NUM-007 backward-facing-step verification (Gartling 1990 configuration,
Re = 800, expansion ratio 2; lengths in step heights h = H/2; cells/H = cells
per channel height; the 50 cells/H level is a pure refinement run through the
unchanged implementation — see `results/p12-num-007/summary.md` §8):

```text
20 cells/H : x_r/h = 10.085 ; upper bubble (x_rs - x_s)/h = 12.143
30 cells/H : x_r/h = 11.405 ; upper bubble (x_rs - x_s)/h = 11.863
40 cells/H : x_r/h = 11.780 ; upper bubble (x_rs - x_s)/h = 11.626
50 cells/H : x_r/h = 11.932 ; upper bubble (x_rs - x_s)/h = 11.506
```

Published 2D benchmark ranges (10 studies incl. Gartling, compiled in
arXiv:2507.16509 Table 2):

```text
lower-wall reattachment x_r/h:  11.48–12.20  -> PASS
upper-wall bubble (x_rs-x_s)/h: 10.60–11.52  -> PASS
```

P12-NUM final regression:

```text
1654 / 1654 passed
0 failed
(1679 listed, 25 disabled/explicit-only)
```

Evidence:

```text
results/p12-num-001/
results/p12-num-002/
results/p12-num-003/
results/p12-num-004/
results/p12-num-005/
results/p12-num-006/
results/p12-num-007/
```

---

# Working Rules

1. Work top-to-bottom.
2. Stop at failed acceptance gates.
3. `[x]` requires implementation **and real verification evidence**.
4. Never weaken a quantitative gate merely because an implementation fails it.
5. Preserve failed experiments and explain their cause.
6. Numerical correctness comes before optimization.
7. New physics requires independent physical validation.
8. New numerical methods require quantitative convergence verification.
9. Production capability means case format → parser → solver dispatch → results/export and, where required, GUI integration.
10. Preserve backwards compatibility unless an explicit breaking change is authorized.
11. Keep generated/runtime-only noise out of commits.
12. Record evidence under `results/<phase>/`.
13. Run focused tests before the full regression.
14. Run the full regression before closing a phase.
15. Record the exact test counts; never estimate them.
16. Do not commit or push unless explicitly authorized.
17. Do not begin another major phase without an explicit scope decision.
