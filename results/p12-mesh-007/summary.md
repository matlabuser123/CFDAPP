# P12-MESH-007 — Moving / Deforming Mesh Foundation

## Final status (2026-09-17): **COMPLETE**

Every item of the frozen gate `acceptance_gate.md` (`5b45fed9…`, amendments A1 and A2) is resolved.
Amendment A3 (`acceptance_gate_A3.md`, `ba60e4f7…`) changes G9's reference only; it was dry-run,
frozen and executed fresh.

- **The G6.3 threshold and wording are unchanged.** G6.3 and G7.3 passed when rerun unchanged, after
  the separately gated fixes of the cause (P12-GRAD-002) and of what that fix exposed
  (P12-DIFF-002).
- **Nothing is committed or pushed.**
- **The first-run report further down is kept unchanged.** Its sha256 was `59412e0e…` (Git Bash
  hash, with `*` marker) before this section was prepended.

### Chronology

```text
2026-09-15  gate frozen, A1, A2 → stages 1-3: G6.3 / G7.3 FAILED (2.491e-02 against 1e-8), logs 10-12
            diagnosis: the pre-existing Green–Gauss exact-alignment branch, not the ALE code (§6)
2026-09-16  P12-GRAD-001 (predicate tolerance)        FAILED its own gate
            P12-GRAD-002 (branch removed)             original gate and A1 FAILED
2026-09-17  P12-DIFF-002 (second-order wall flux)     COMPLETE
            P12-GRAD-002 (A2 FAILED at C10-A2(d); A3) COMPLETE, full regression PASS
            G6.3 / G7.3 rerun unchanged               PASS (logs 20-22)
            G2.3                                      PASS (log 26)
            A3 (G9 reference) dry-run → freeze → fresh PASS (logs 30-34, 39, 40-45)
            G10 (G10.1 fresh; G10.2-4 from the GRAD-002 A3 regression, same build inputs) PASS
            performance baseline                      recorded (log 50)
```

### Gate items, final

| item | final outcome | evidence |
| --- | --- | --- |
| G1–G5, G6.1, G6.4, G6.5, G7.1, G7.2, G7.4, G8 | PASS in the original run (§5 below), and again in the unchanged rerun: stages 17/17, 8/8, 13/13 | logs 10–12, 20–22 |
| **G6.3 Galilean invariance** | **original FAILED: 2.491e-02** (gtest: 0.024912309424299468) against 1e-8. **Rerun unchanged: PASS**, max \|u_B − b − u_A\| **1.900e-13** (≤ 1e-8), pressure 4.671e-12 (≤ 1e-8), flux 1.147e-14 (≤ 6.25e-10). The largest step value is at step 15; every step is ≤ 1.9e-13 | logs 12, 22 |
| **G7.3 moving walls** | the G6.3 run: **original FAILED, rerun PASS** | logs 12, 22 |
| G6.4 discrimination | 1.174e-01 ≥ 1e-4 (original 1.159e-01; the static path changed through GRAD-002 and DIFF-002) | log 22 |
| G2.3 independent Python geometry | **PASS** on C16, G16, Q16 and MB2 (SN2) and H8 (SN3); worst ratio 0.18 of the bound (H8 face centroid). The instrument self-test fails as required | logs 19 (dry), 26 |
| G9.1–G9.5 (A3: reference = the current tree minus MESH-007) | **PASS**. G9.1 probes bitwise identical; G9.2 35/35 identical, 0 invalid; G9.3 no input changed by MESH-007; G9.4 0 value changes; G9.5 by G10. Details below | logs 39–45, a3/dryrun.md |
| G10.1 focused and dependent suites | **PASS**. The MESH-007 suites pass 17, 8 and 13 tests. The dependent suites pass: PISO 81, Solver 90, Mesh 156 (1 disabled: the G2.3 dump), Discretization 178, and PISO/transient/restart tests elsewhere 2 + 3 + 16. 0 failed, 0 stale | log 61 |
| G10.2–G10.4 full regression, sanitizers, format | **PASS**: Release 1932/1932, Debug + GUI 1984/1984, ASan + UBSan 1932/1932, 0 ASan / UBSan / LSan diagnostics, 0 timeouts; clang-format 0 of 568 (reuse disclosed below) | `results/p12-grad-002/a2/logs/regr_*`, log 60 |
| Performance | recorded (measurement only) | log 50 |
| Documentation | README (Features and Known limitations), `docs/user_guide/case_format.md`, ROADMAP, TODO | — |

**Precision of the new G6.3 value.** The frozen test prints the maxima with `%.3e`, so the gate
record's value is 1.900e-13, to four significant digits.

- **Full precision.** A separate NOT-gate diagnostic changes only that `printf` format, in a copy
  outside the repository, and links it to the same library
  ([logs/23](logs/23_g63_full_precision_NOT_gate.log)). It gives **1.89974564178970561e-13**, with
  pressure 4.67050009778091635e-12 and flux 1.14724717356282091e-14. Rounded, these reproduce log
  22, and so do its 20 per-step lines.
- **A consistency-check bug, preserved.** Log 23's own consistency verdict ("NOT REPRODUCED") was
  an instrument bug: it counted the printed threshold 6.25e-10 as a fourth number. Log 23 is kept;
  the fixed check is [23b](logs/23b_g63_consistency_recheck_NOT_gate.log).

**The rerun used the original instrument** ([logs/25](logs/25_instrument_identity_NOT_gate.log),
NOT a gate item). The four ALE test sources are untracked, and the original run did not hash them.
Their identity rests on three links:

- (A) log 12's failure records (`test_ale_piso.cpp:491–493` with their asserted expressions) are
  found at exactly those lines in FORMAT-001's pre-format copies;
- (B) those copies equal FORMAT-001's recorded "before" hashes, the current files equal its "after"
  hashes, and its token proof says the code is unchanged;
- (C) logs 20–22 run the same tests in the same order as logs 10–12, with identical number-masked
  printed lines and identical printed thresholds. Stage 1 prints no "(<= x)" thresholds, so there
  only the skeletons are compared.

**G2.3 supplementary controls** ([logs/27](logs/27_g23_channel_controls_NOT_gate.log), NOT a gate
item). The frozen self-test perturbs only the volume. Copies of the gate's dumps were perturbed in
the centroid, face-centroid and area-vector channels, and by an area-vector sign flip; each is
reported FAIL on all 5 meshes, and the unperturbed copy reproduces log 26. Two failed attempts of
this control are kept:

- `…CRASHED-flat-indexing`;
- `…MISCOUNTED-overall-line`.

### G9 under Amendment A3

- **Why A3.** G9's frozen reference, the pre-MESH-007 tree, no longer isolates MESH-007, because
  DIFF-002 and GRAD-002 changed the static numerics on purpose.
- **The reference.** A3 compares the current tree (`143a1dda…`) with **nom7**: the current tree with
  every MESH-007 change removed and every later phase kept (`4421c826…`).
- **Fidelity.** F1 (the difference set equals MESH-007's 29 files), F2 (all 42 differences from
  BASE are attributed to later phases), F3 (only the GRAD-002 / DIFF-002 insertions in
  `MeshGeometry`) and F5 (nom7 rebuilt from empty reproduces the frozen hash) are checked on every
  build.

| | dry-run (logs 30–34) | fresh (logs 40–44) |
| --- | --- | --- |
| G9.1 bitprobe7, MESH-005 probe, MESH-006 probe (16 2D cases) | bitwise identical; N1 35/18/51 differing lines on cur vs nograd; N2 identical | same, output line-identical to the dry-run |
| G9.2 CLI, 19 cases + 16 fixtures | C1 35/35; N3 21 different; nom7 vs NEW 35/35, 0 invalid | same |
| G9.3 inputs | PASS (4 BASE-vs-current changes, all DRIFT-001's); self-test PASS | same |
| G9.4 suites + outputs | nom7 1885/1885, NEW 1932/1932; 270 files, 0 VALUES / NEW / GONE | same (222 identical, 48 timing-only) |

**Freeze and verification.** The freeze ([logs/39](logs/39_a3_freeze.log), 17:19:31Z, sha256
`40608bed…`) hashes 39 artifacts and the two libraries. The fresh run re-verified them before its
first stage, and [logs/45](logs/45_a3_post_execution_verification.log) re-verified them afterwards,
with none changed.

**Instrument defects found by the dry-run, fixed before the freeze, and preserved**
([a3/dryrun.md](a3/dryrun.md) §2):

- The MESH-005 and MESH-006 probes were first given 3D cases. They aborted identically, a vacuous
  "IDENTICAL".
  - Fix (user-authorized): 2D cases only, every run must exit 0 and be complete, and N1 is checked
    for every probe.
- The inherited CLI comparison was not fail-closed, and its verdict could hide a metadata-only or
  stdout-only difference.
  - Fix: INVALID unless each side shows the exit code and exports the code and test suite require;
    differences are collected before the verdict.
  - The read-only audit found 0 masked lines in MESH-005's and MESH-006's recorded runs.
- `build_nom7.sh`'s listing included generated outputs.
- An edit to `g9_a3.sh` while its G9.2 stage ran made bash execute a harmless fragment after the
  stage loop. The logs were unaffected; this is disclosed.

### G10: reuse of the GRAD-002 A3 regression (disclosed)

The user authorized G10.2–G10.4 to reuse the GRAD-002 A3 full regression instead of a second
identical rebuild, on three conditions. All three hold:

1. **Identical build inputs.** The hash covers every file under src, include, tests, apps, cmake and
   cuda (generated test results and `*.pyc` excluded), plus `CMakeLists.txt` and
   `CMakePresets.json`. It was `34cd655d…` over 733 files at 16:55Z and again at 17:54:26Z,
   immediately before G10 ([logs/60](logs/60_g10_build_input_hash.log)). The read-only audit session
   recomputed it independently at 17:54:37Z. The newest input, 13:50:45Z, predates the regression's
   clean-first builds: Release 14:11:59Z, Debug + GUI 14:18:19Z, ASan 14:43:51Z.
2. **G10.1 ran fresh** (log 61).
3. **This disclosure.** The criteria and counts are unchanged. G10.3 is evaluated with **zero**
   permitted failures (A3), and there were none.

**Build warnings (G10.4).** Release and ASan built with 0 warnings. Debug + GUI has the 6 known
`-Wconversion` warnings at `apps/gui/SimulationControllerEditing.cpp:86-89`. Those lines are
unchanged since HEAD, and MESH-007 changed no file under `apps/`. The debt is already recorded.

### Performance baseline ([logs/50](logs/50_performance_baseline.log), measurement only)

**Conditions.**

- Release `143a1dda…`, one thread (OpenMP off), pinned to one core, on an Intel i9-14900HX
  (32 threads) under WSL2.
- Nothing else ran; the 1-minute load average was 0.47 before and 0.79 after, the run itself.
- One warm-up run, then 5 measured runs. Each figure is the median of the per-run medians, with the
  spread of those medians.

| item | ms per step | spread |
| --- | --- | --- |
| `MeshMotion::advance` (geometry, swept volumes, GCL), 2D 128² | 1.665 | 1.579–1.760 |
| same, 2D 256² | 9.463 | 8.800–9.803 |
| same, 3D 32³ | 13.005 | 12.836–13.153 |
| same, 3D 64³ (262 144 cells) | 110.0 | 109.3–112.9 |
| static PISO, 2D 128² cavity | 291.4 | 288.5–300.1 |
| AlePISO, SN2, 2D 128² cavity | 337.6 | 331.1–341.9 |

AlePISO costs about 1.16× the static PISO step. The geometry update is a small part of that
difference, 1.7 ms of 46 ms.

**Peak memory** ([logs/51](logs/51_performance_memory.log)). Each item ran once in its own process
under `/usr/bin/time -v`, separately from the timed runs; the timings printed in log 51 are not
baseline figures.

| item | peak RSS |
| --- | --- |
| empty process (floor) | 3.6 MB |
| geometry, 2D 128² | 24.5 MB |
| geometry, 2D 256² | 83.8 MB |
| geometry, 3D 32³ | 56.2 MB |
| geometry, 3D 64³ | 422 MB, about 1.6 kB per cell |
| static PISO and AlePISO, 128² (the larger of the two) | 45.5 MB |

**Iterations.** Each PISO step performs two pressure corrections by construction. The step result
(`TransientStepResult`) exposes no linear-solver iteration counts, so none are reported.

**Disclosures.**

- WSL reports a constant 2419 MHz and no temperature sensor, so frequency scaling and thermal
  throttling cannot be ruled out directly. The run-to-run spread, 2–11 %, is the available evidence.
- The GPU is not involved. The WSL CUDA toolchain (nvcc 11.5, architecture 52) predates the Ada GPU
  and is recorded as debt.
- The first attempt failed to compile (a duplicated helper) before measuring anything. It is kept as
  `50_…BUILD-FAILED-duplicate-helpers.log`.
- No optimization was done.

### Component checklist

Each item was marked `[ ]` in the pre-restructure TODO archive. Every item now has verified evidence:

- [x] **Mesh-motion architecture:** `architecture.md` (`d06f67c8…`), frozen with the gate.
- [x] **Topology preserved under deformation:** G2.7.
- [x] **Cell volumes and face geometry:** 2D and 3D; G1.3, G2, and the independent check G2.3.
- [x] **Mesh velocity:** G3.
- [x] **ALE transport foundation:** G1.2, G1.4, G6.1, G6.3 (rerun), G6.4, G6.5.
- [x] **Geometric conservation law:** G4.
- [x] **Moving walls:** G7.1–G7.4.
- [x] **Prescribed mesh-motion tests:** U1–U7, the piston and translating Couette; operator-level 3D.
- [x] **Conservation regression:** G8.
- [x] **Analytical validation where possible:** affine geometry, piston, Couette, uniform flow and
  Galilean invariance. No moving-mesh MMS, a disclosed limitation.
- [x] **Evidence:** this directory, logs 00–61.
- [x] **Compatibility (G9 A3), regression (G10), performance baseline, documentation.**

### Preserved failures and invalid runs

- The original G6.3 / G7.3 failure (log 12, and §5–§9 below). P12-GRAD-001's failure.
- GRAD-002's original, A1 and A2 failures (`results/p12-grad-002/`).
- Log 23's instrument-bug verdict.
- The two failed G2.3 control attempts (log 27).
- The G9 dry-run attempts, all hashed in log 39:
  - `30_…run1`;
  - `31_…run1` (vacuous), `run2`, `run3`;
  - `32_…` and `33_…pre-final-script`, `33_…run1`;
  - `31_…selftest.run1`.
- The failed performance build (log 50).

### What was delivered, and its limits (unchanged scope; user decision of 2026-09-15)

- **2D ALE/PISO is a C++ API** (library level). It is not in the case format, the CLI or the GUI,
  which stay steady-SIMPLE only. It is not a production capability in the sense of CLAUDE.md §9.
- **3D is a geometry, swept-volume, GCL and ALE-operator foundation only.** There is no 3D ALE flow
  solver.
- **AlePISO is laminar** and uses PISO's discretization (implicit Euler, upwind convection). Its
  accuracy on deforming meshes is not claimed, and there is no moving-mesh MMS.
- **Motion is prescribed.** There is no fluid–structure coupling or remeshing, `MovingWall`
  velocities are constant, and a moving-mesh run cannot be restarted.

---

# Historical first-run report (2026-09-15 / 2026-09-16), unchanged

# P12-MESH-007 — Moving / deforming mesh foundation

Status: **BLOCKED / FAILED GATE**. The first failed pre-registered item is **G6.3**, Galilean
invariance of the translating lid cavity: max |u_B − b − u_A| = 2.49e-2 against a limit of 1e-8.
G7.3 is the same run.

- The cause is not the ALE/GCL implementation. It is a pre-existing property of the static
  Green–Gauss gradient, reproduced on the pre-MESH-007 library with no MESH-007 code (§6).
- The primary phase gate G5, uniform-flow preservation, **passes** on all seven moving-mesh runs, at
  about 1e-14 against 1e-9.
- The work stopped at the failure. G2.3's independent Python check, G9, G10, the performance
  baseline and the documentation were not run (§7).

## 1. Authorization and scope

P12-MESH-007 was authorized by the user on 2026-09-15, for MESH-007 only, with no commit or push.

Scope decisions made by the user after the audit, before the gate was written (architecture §1):

- **Library-level ALE only.** The production case format, CLI and GUI are steady SIMPLE only (the
  `"type": "PISO"` rejection is pinned by a test), and transient PISO exists only in the C++ API.
  Mesh motion therefore goes into that API. The case format, CLI and GUI are unchanged.
- **3D at the geometry, GCL and ALE-operator level.** PISO has been 2D-only since MESH-006, so the
  uniform-flow gate for flow runs in 2D.

The known pre-existing sanitizer defect `MeshQualityReport.DisconnectedMeshIsFatal` (P12-MESH-004 test
code) was reproduced at the baseline ([logs/01](logs/01_baseline_mesh006_stability.log)) and not
touched.

## 2. Baseline ([logs/00](logs/00_baseline_git.log), [01](logs/01_baseline_mesh006_stability.log), [02](logs/02_baseline_reference_tree.log))

- HEAD `b66310ca871811c4c7671056beec291a451af55c`. The working tree holds the uncommitted
  P12-MESH-001–006 work: 243 status entries (178 modified, 65 untracked).
- MESH-006 is stable:
  - `build/release` is up to date (0 units rebuilt), and `cfdapp` is byte-identical to MESH-006's
    final binary (`df4ee6d0…`);
  - the MESH-006 3D suites pass (SIMPLE3D 14, Case3D + VTK3D 11, Operators3D 10, Cartesian3DMesh 12,
    duct/cube 4, 3D MMS 4);
  - the existing PISO/transient suites pass (99), and the CLI process tests pass (18/18).
- BASE, the pre-MESH-007 reference tree, is at `$HOME/m7ref/base`: 1647 files, 719 source/test files
  hashed, built in Release, and its `cfdapp` is byte-identical.

## 3. Architecture and implementation ([architecture.md](architecture.md), sha256 `d06f67c8…`)

Written before any source change.

**Mesh and geometry.**

- **`Mesh::setGeometry` / `Mesh::geometry()`** are the only geometry mutation path. The setters on
  `Cell` and `Face` are private, with `Mesh` as a friend; topology has no setter. `setGeometry`
  validates everything before committing (strong guarantee).
- **`MeshGeometry::structuredTopology / computeGeometry / sweptVolumes`**:
  - a welded structured vertex map, verified against the stored geometry;
  - 2D geometry from the builders' own helpers, so an unmoved mesh gives bit-for-bit the builder
    geometry;
  - 3D trilinear hexahedra (volume and centroid exact by 2×2×2 Gauss, bilinear faces);
  - exact swept volumes: the 2D midpoint formula, and 3D 2×2×2 Gauss in (s, r, t);
  - invalid cells rejected with a message naming the block, (i, j[, k]) and the defect.
- **`MeshMotion` + `PrescribedMotion`** (`StationaryMotion`, `AffineMotion`, `SinusoidalMotion`):
  - reference, current and previous vertices;
  - `advance()` with a strong guarantee, and an idempotent `revert()`;
  - `MeshMotionStep` carries V^n, δV_f, φ_m, vertex velocities, per-cell and global GCL residuals,
    and volume and displacement summaries;
  - a step that moves no vertex is an exact no-op.

**ALE operators and solver.**

- **ALE operators:** `aleImplicitEulerTimeDerivative` (V^n, V^{n+1}), `relativeMassFlux`
  (F − ρφ_m) and `assembleAleTransientMomentumComponent`. All reuse the shared convection assembly.
- **`AlePISO`** (2D, laminar): moves the mesh, checks the boundary motion against the velocity
  conditions (`checkBoundaryMotion`), and runs the one shared PISO algorithm with the ALE terms.
  It reverts on any failure, and on `TransientSolver` rejection through a new no-op-by-default hook,
  `TransientStepSolver::onStepRejected`.
- **PISO refactor:** `PISO::solveTimeStep` delegates to the library-internal
  `detail::solvePisoStep`, which is the former body unchanged plus two ALE branches.
- **`evaluateAleConservation`:** the local and global ALE mass residual.

**Files.**

- New:
  - `include/cfd/mesh/MeshMotion.hpp`, `src/mesh/MeshMotion.cpp`;
  - `include/cfd/pressure_velocity/AlePISO.hpp`, `src/pressure_velocity/AlePISO.cpp`,
    `src/pressure_velocity/PisoStep.hpp`;
  - tests: `tests/support/MeshMotionCases.hpp`, `tests/unit/mesh/test_mesh_motion.cpp`,
    `tests/unit/discretization/test_ale_operators.cpp`, `tests/solver/piso/test_ale_piso.cpp`.
- Changed: `Cell.hpp`, `Face.hpp`, `Mesh.hpp/.cpp`, `MeshGeometry.hpp/.cpp`,
  `TimeDerivative.hpp/.cpp`, `MassFlux.hpp/.cpp`, `TransientMomentum.hpp/.cpp`, `PISO.cpp`,
  `TransientSolver.hpp/.cpp`, `src/CMakeLists.txt`, and three test CMakeLists.
- Not changed: the case format, CLI, GUI and docs.

## 4. Acceptance gate ([acceptance_gate.md](acceptance_gate.md))

- **Frozen at 2026-09-15T20:47:58Z**, before any source change: sha256 `275eb19a…`
  ([logs/05](logs/05_gate_freeze.log)); all 719 source files were identical to BASE.
- **Pre-freeze observations**, disclosed and not gate runs:
  - an independent numpy round-off experiment ([logs/03](logs/03_prefreeze_roundoff_NOT_gate.log))
    measured ≤ 0.11 of every bound;
  - a solver-settings feasibility check on the existing static PISO
    ([logs/04](logs/04_prefreeze_solver_feasibility_NOT_gate.log)).
- **Amendment A1** (G1.4 only), before any gate run ([logs/06](logs/06_gate_amendment_A1.log),
  sha256 `4792ceed…`). Both momentum-component assemblies are 2D-only, and the constant-viscosity
  overload is not the ALE counterpart. The 3D static limit is checked on the constituent operators
  instead.
- **Amendment A2** (the discrimination controls of G5.4 and G6.5), before any gate run
  ([logs/07](logs/07_gate_amendment_A2.log), sha256 `5b45fed9…`).
  - The frozen text used "the static formulation" as the negative control. That was a design error:
    the static formulation preserves uniform flow by construction.
  - The controls became two GCL-inconsistent variants: N1, no mesh flux; N2, V^{n+1} at both time
    levels.
- Both amendment logs verify that the frozen text is an unchanged prefix of the amended file.

## 5. Gate results ([logs/10](logs/10_gate_stage1_G1.1_G1.3_G2_G3_G4.log), [11](logs/11_gate_stage2_G1.4_G6.1_G6.5.log), [12](logs/12_gate_stage3_G1.2_G5_G6_G7_G8.log))

Three stages ran on Release binaries (sha256 in each log). Stage 1 passed 17/17 and stage 2 passed
8/8. Stage 3 passed 12 of 13; the failure is G6.3.

| item | result | measured (worst over runs and steps) |
| --- | --- | --- |
| G1.1 static identity, motion | PASS | STAT on C16, G16, Q16, MB2 and H8: geometry bitwise unchanged; δV = v = 0.0 exactly |
| G1.2 AlePISO(STAT) = PISO | PASS | C16 cavity, Q16 channel, MB2 channel, 10 steps: every field and diagnostic **bitwise** identical |
| G1.3 kernel vs builder | PASS | Q16, MB2: **bitwise**; C16: exact (0 of bound); G16 0.034 of bound; H8 0.016 of bound |
| G1.4 static-limit operators (A1) | PASS | time derivative, relative flux, ALE momentum (C16, Q16; U and V), 3D convection + time term (U, V, W): **bitwise**; both assemblies refuse H8 |
| G2.1–G2.2, G2.4–G2.7 geometry | PASS | affine images within ≤ 0.137 of the bounds; closure ≤ 0.0076; total volume ≤ 0.10; orientation valid; topology identical; min V > 0 |
| G2.3 independent Python geometry | **NOT RUN** | the dump test and `tools/independent_geometry.py` were not executed before the stop |
| G2.8 invalid motion | PASS | SN2 A = 0.4: "cell (15,7) … not convex at corner (i+1,j+1)"; SN3 A = 0.4: "cell (5,4,1) … corner Jacobian … at corner (0,0,1) is −8.4e-5"; axial collapse: "cell (0,0) … inverted"; out-of-plane motion refused; mesh bitwise unchanged each time |
| G3.1–G3.4 mesh velocity | PASS | translation and affine: ≤ 0.112 of 8εX/dt; sinusoid: ≤ 0.067 against the mean velocity, 0.987 of the (dt²/24)Aω³ Taylor bound against the midpoint velocity |
| G4.1–G4.4 GCL | PASS | per-cell ≤ 4.7e-3 of the bound (2D) and 1.5e-3 (3D); global ≤ 1.0e-2; swept volumes vs the independent formula ≤ 2.8e-3 (2D) and 1.5e-5 (3D); translation volumes unchanged |
| **G5.1–G5.3 uniform flow (primary)** | **PASS** | U1–U7 (C16/SN2, TR2, EX2, SH2; Q16/SN2; MB2/SN2; G16/EX2), 20 steps each: max \|u − u0\|/‖u0‖ ≤ **1.1e-13**, \|p − p0\|/(ρ‖u0‖²) ≤ **6.3e-14**; per-step \|ΔV\|/V up to 5.1 %; displacement up to 0.20; corner-angle change 21.8° (U4); CFL up to 1.29 |
| G5.4 discrimination (A2) | PASS | N1 5.2e-2 and N2 5.3e-2 (U1), 1.0e-2 and 9.5e-3 (U3), each ≥ 1e-5; static formulation 0.0 (recorded) |
| G6.1 hand-derived | PASS | unit cell: swept 0.1, F_rel (−1, 0.8, 0, 0), row 3 = 3; two cells, including the upwind reversal (F_rel = −0.3): every coefficient ≤ 1e-14 |
| **G6.3 Galilean invariance** | **FAIL** | C16 translating cavity vs fixed: max \|u_B − b − u_A\| **2.49e-2** (limit 1e-8); pressure 9.3e-2 (limit 1e-8); flux 6.3e-4 (limit 6.25e-10) |
| G6.4 discrimination | PASS | static formulation on the translating mesh: 1.16e-1 ≥ 1e-4 |
| G6.5 3D operator level (A2) | PASS | H8/SN3 and H8/EX3: ALE residual ≤ 8.3e-16 (limit 1e-10); N1 and N2 7.4e-3 and 2.4e-2 (each ≥ 1e-4); static 2.4e-16 (recorded) |
| G7.1 piston | PASS | \|u + Vp\|/Vp 3.0e-14, \|v\| 3.7e-15, \|p\| 3.8e-14; outlet flux error 2.2e-16; channel compressed from 2 to 1.5 |
| G7.2 translating Couette | PASS | \|u − y_c\|, \|v\| ≤ 1.2e-14; with the stationary bottom wall given the mesh velocity: 0.27 (≥ 1e-3) |
| G7.3 walls with MovingWall(b) | **FAIL** | the G6.3 run |
| G7.4 refusals | PASS | stationary walls, symmetry planes and a mismatched piston are refused, naming the patch; mesh bitwise reverted |
| G8.1–G8.4 conservation | PASS | R_P/F_ref ≤ 3.6e-13, global ≤ 3.8e-12; STAT: R_P bitwise equal to the continuity imbalance; domain volume ≤ 3.8e-3 of the bound; the G6.3-B run is conservative (≤ 3.4e-14) |
| extra tests | PASS | TransientSolver runs AlePISO (the final mesh is a bitwise replay; a CFL-rejected step reverts the mesh bitwise); a numerical failure reverts; 3D refused; VTK POINTS are the moved vertices bitwise (2D and 3D) |
| G9 compatibility, G10 regression | **NOT RUN** | the phase stopped at G6.3 |

## 6. G6.3 failure: diagnosis ([logs/13](logs/13_diag_g63_step1.log), [14](logs/14_diag_g63_step1_stages.log), [15](logs/15_diag_g63_static_translation_BASE_and_NEW.log), [16](logs/16_diag_g63_galilean_q16_NOT_gate.log); evidence only)

**1. Where the difference is** (logs/13):

- Only in wall-adjacent cells, and only in the **wall-normal** velocity component. It is largest at
  the lid corners (3.9e-3 at step 1).
- Giving the fixed cavity's walls as `MovingWall(0)` instead of `Wall` changes nothing, so the
  boundary-condition type is not involved.

**2. Step 1, stage by stage** (logs/14). Every ALE ingredient matches:

| quantity | max difference |
| --- | --- |
| convecting flux F_rel,B vs F_A | 6.8e-16 |
| predictor u*_B − b vs u*_A | 2.6e-14 |
| momentum diagonal | 1.2e-14 |
| predictor-flux imbalance | 8.3e-16 |
| p′₁ | 3.0e-12 |

The first stage that differs is the **velocity correction**, u₁ − b vs u₁ (5.4e-3). The pressure
gradient of the same p′ field differs between the fixed mesh A and the translated mesh B: at cell
(0,15) it is (−0.892, 0.876) on A and (−1.338, 1.314) on B.

**3. Mechanism.** `src/discretization/Gradient.cpp`, `tryPairedBoundaryContribution` (pre-existing,
P12-NUM / P12-MESH-001):

- The Green–Gauss gradient gives a boundary cell a second-order "paired" quadratic fit (boundary,
  owner, opposite neighbour) only when `cross(d, S_f) == Vector3{}` holds **exactly, bit for bit**.
- That holds for every boundary face of a Cartesian mesh from the builder.
- On a Cartesian mesh translated by a non-dyadic offset, the centroids carry round-off. The test
  fails for 54 of 64 boundary faces, and the operator falls back to plain Green–Gauss.
- The result is an O(1) change in the boundary-cell gradient caused by an O(1e-17) change in the
  geometry.
- The frozen G6.3 derivation assumed the two runs "solve the same discrete systems up to
  round-off". For C16 that is false.

**4. It predates MESH-007** (logs/15). Using pre-MESH-007 APIs only, compiled against BASE and
against NEW (identical output):

- the static PISO cavity on a copy of C16 translated by (0.005, 0.0025), built with the pre-existing
  `createStructuredQuad2D`, differs from the untranslated cavity by 5.418e-3 at step 1 and 2.487e-2
  at step 20;
- G6.3 measured 5.418e-3 and 2.491e-2.

The static solver is not invariant under a pure translation of a Cartesian mesh.

**5. The ALE formulation is Galilean-invariant** (logs/16, a diagnostic, not a gate run). The same
comparison on the distorted Q16 mesh, where no boundary face is exactly parallel in either run at
any step, gives max |u_B − b − u_A| = **1.5e-13** over 20 steps. That is five orders below the frozen
1e-8.

**Classification.** This is not an ALE or GCL implementation defect. It is a real, pre-existing
discontinuity of the static Green–Gauss boundary gradient with respect to round-off-level geometry.
The gate's choice of the Cartesian C16 reference exposed it. On a moving mesh it means that a
Cartesian mesh changes its boundary-gradient discretization at the first motion step.

## 7. Not done because the phase stopped at the first failed gate

- G2.3: the independent Python check of the non-affine geometry (the dump test exists and is
  `DISABLED_`).
- G9: the BASE-vs-NEW bit-identity probes, CLI comparison, inputs and output classification.
- G10: full regression in Release, Debug + GUI, and ASan + UBSan.
- The performance baseline.
- Documentation (README, ROADMAP, user guide).
- clang-format of the ten new or changed files that the dry run flags
  ([logs/17](logs/17_stop_state_existing_suites_NOT_gate.log)); none was reformatted.

**State at the stop, as information only** (logs/17). The existing suites of every touched module
pass at their pre-MESH-007 counts:

| suite | result |
| --- | --- |
| CFDPisoTests (non-ALE) | 68/68 |
| CFDSolverTests | 87/87 |
| CFDMeshTests (non-motion) | 139/139 |
| CFDDiscretizationTests (non-ALE) | 148/148 |
| PISO, transient and restart validations | 21/21 |

## 8. Limitations and findings

- **New finding (pre-existing, not fixed):** the Green–Gauss boundary gradient's exact-orthogonality
  test (§6) makes the static discretization discontinuous under round-off perturbations of the
  geometry. That includes a pure translation of a Cartesian mesh.
- Scope:
  - library API only (no case format, CLI or GUI);
  - AlePISO is 2D and laminar, and uses PISO's discretization (first-order implicit Euler, upwind
    convection, uncorrected diffusion); its accuracy on deforming meshes is not claimed and no
    moving-mesh MMS was run;
  - 3D is verified at the geometry and operator level only;
  - `MovingWall` has a constant velocity;
  - no restart of a moving-mesh run (the geometry fingerprint refuses it).
- The known pre-existing sanitizer defect (P12-MESH-004 test) is unchanged.

## 9. Decision

**P12-MESH-007 BLOCKED / FAILED GATE.**

The first failed pre-registered item is G6.3; G7.3 is the same run. The threshold is unchanged,
following the stop rules. How to proceed is the user's decision:

- **(a) A disclosed, post-result amendment of G6.3.** Compare the translating cavity with a static
  reference on a mesh where both runs use the same boundary-gradient branch (for example Q16), with
  the 1e-8 threshold unchanged. The original failure stays on record.
- **(b) A separately authorized fix of the Green–Gauss boundary test** (for example a relative
  tolerance instead of exact zero). This is a shared numerical operator, so it needs its own gate and
  2D-compatibility evidence. G6.3 would then be rerun as frozen.
- **(c) Leave MESH-007 blocked.**

Nothing was committed or pushed, and no later phase was started.

### 9.1 Update, 2026-09-16 — option (b) attempted and failed

The user authorized option (b) as a separately scoped phase, **P12-GRAD-001**
(`results/p12-grad-001/`), with G6.3 explicitly not amended. That phase froze its own gate before
changing `Gradient.cpp`, implemented a scale-aware predicate
(`MeshGeometry::boundaryFaceAlignment`), and **failed its own frozen gate** at GR1 (2.414e-9 against
1e-9, 2D 64² quadratic field) and GR6 (10 of 64 boundary faces misclassified at a large
translation). Both failures trace to errors in that phase's own frozen criteria — a bound derived
from the wrong quantity, and a GR6/GR3 pair that is mutually unsatisfiable — not to the fix, which
does remove the mechanism (boundary difference 20.2 → 2.414e-9 at 64², and exactly 0 for exactly
representable offsets).

Consequences for this phase, per the stop rules:

- **G6.3 was not rerun.** Its original failure above (2.49e-2 against 1e-8) remains this phase's
  first and only recorded gate result, unchanged.
- G6.3's threshold and wording are unmodified.
- MESH-007 remains **BLOCKED / FAILED GATE**, with §7's items still not run.
- Options (a) and (c) of §9 remain open, as does re-running P12-GRAD-001 under corrected criteria
  (`results/p12-grad-001/summary.md` §9), which needs explicit authorization because it would be a
  post-result amendment.
