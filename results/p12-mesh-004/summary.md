# P12-MESH-004 — Mesh Quality & Validation: evidence summary

Status: **`[x]` COMPLETE (2026-09-15)** — the unchanged pre-registered gate passes with the final code, and the full regression passes: Release 1787/1787, Debug + GUI 1837/1837 (§19).

The pre-registered gate ([acceptance_gate.md](acceptance_gate.md)) was **unchanged** from before the
first campaign run. The first gate run failed R3, M1 and GC1. All three failures came from one
pre-existing defect: BiCGSTAB's absolute breakdown threshold near round-off. The work stopped there
([logs/13](logs/13_gate_evaluation.log)).

A narrow solver-robustness fix was then separately authorized and made, with its own reproducer
and regression tests (§23). The unchanged gate was rerun and passes
([logs/29](logs/29_gate_evaluation_rerun.log)).

Nothing is committed or pushed, and P12-MESH-005 is not started.

Evidence logs are in [logs/](logs/). The campaign data is in [data/](data/) (the rerun) and
[data_first_run/](data_first_run/) (the failed first run). The solver-robustness evidence is in
[solver-robustness/](solver-robustness/).

| log | content |
| --- | --- |
| 00 | starting state (HEAD, `git status`, `git diff --stat`) |
| 01 | audit: the pre-MESH-004 aspect ratio declared a valid 45°-rotated mesh invalid |
| 02 | preliminary feasibility probe (before the gate was written) |
| 03–11 | **first** gate run: regular tests, D1–D4, MMS (Cartesian, smooth corrected/uncorrected), irregular grid convergence |
| 12, 13 | first-run failure diagnosis (thermal replay), first-run gate evaluation: FAILED (R3, M1, GC1) |
| 14 | tree consistency at the stop |
| solver-robustness/00–05 | capture of the failing systems, tests before/after the fix, criterion comparison, thermal reproducers rerun, diagnosis of the failed first regression and of the fallback-test fixture |
| 15–23 | **unchanged-gate rerun** with the final fix (same tests, same order) |
| 24 | qmllint of the changed QML pages |
| 25 | backward compatibility (every committed case: NEW vs pre-fix vs HEAD) |
| 26 | ordered focused tests |
| 27a / 28a | FAILED / aborted full regression of the superseded first version of the fix (kept) |
| 27 / 28 | full regression, Release / Debug + GUI (final code) |
| 29 | final gate evaluation: PASS |
| 30 | generated outputs rewritten by the final regressions: classification, quantification and resolution |

[superseded_rerun_norm_criterion/](superseded_rerun_norm_criterion/) keeps the gate rerun, compatibility
and focused-test logs made with the first (superseded) version of the fix.

## 1. Authorization and scope

- **P12-MESH-004 only** (authorized 2026-09-15). Out of scope: P12-MESH-005, 3D,
  moving/deforming meshes, P12-TURB, P12-SPECIES, P12-MULTI, additional compressible scope, P13.
- **Separately authorized afterwards:** a narrow BiCGSTAB robustness fix to unblock the failed gate,
  with no gate changes and no thermal/MESH-004/mesh special cases (§23).
- **Explicitly not in scope:** the irregular-mesh velocity/pressure finding (§21). It is documented,
  not fixed.
- Nothing is committed or pushed.

## 2. Starting tree

- **Start:** HEAD `b66310ca871811c4c7671056beec291a451af55c` plus the verified, uncommitted
  P12-MESH-001/002/003 tree. That was 107 status entries (78 modified, 29 untracked);
  `git diff --stat` 78 files, +4623/−1956 ([logs/00](logs/00_baseline_working_tree.log)).
- **Last regression before MESH-004:** Release 1743/1743, Debug + GUI 1789/1789.
- **Pre-MESH-004 snapshot:** a copy was made at `/tmp/cfd_mesh003`. A WSL `/tmp` cleanup deleted it
  during the session.
  - logs/01 used it before it was lost.
  - The before-fix test run (solver-robustness/01) was regenerated with HEAD's `BiCGSTAB.cpp`,
    which is byte-identical (sha `d33521c5…`).
  - §17 explains how the comparison references were rebuilt.

## 3. Architecture

`cfd::mesh::MeshQuality::evaluate` is the one authoritative report. Non-orthogonality and skewness
are the P12-NUM-003 `MeshGeometry` definitions, aggregated and not redefined. Its consumers:

- **`CaseBuilder`** evaluates every built mesh once, stores the report in
  `SimulationSetup::meshQuality`, and rejects an `invalid` mesh before any solver object exists.
- **`ProjectRunner`** returns it (`ProjectRunResult::meshQuality`) and exports it
  (`metadata.json` `"mesh_quality"`).
- **The CLI** prints it.
- **The GUI** (`SimulationController::meshQuality`) shows it on the Mesh page, and its warnings in
  the validation panel.
- **The structured builders** (`createStructuredQuad2D`, `createMultiBlock2D`) now name the specific
  defect of a rejected cell: zero-length edge, zero area, inverted, or non-convex corner. This is a
  message change only.

## 4. Metric definitions

Defined in `MeshQuality.hpp`:

| metric | definition | per |
| --- | --- | --- |
| cell area | A_c | cell |
| face length | \|S_f\| | face |
| aspect ratio | longest / shortest face of the cell | cell |
| non-orthogonality | ∠(S_f, d), d = x_N − x_P | internal face |
| skewness | \|x_f − x_f′\| / \|d\| | internal face |
| expansion ratio | max(A_P, A_N) / min(A_P, A_N) | internal face |

The aspect ratio is coordinate-free. The pre-MESH-004 axis-sorted version declared a valid grid
rotated by 45° invalid ([logs/01](logs/01_audit_aspect_ratio_defect.log)); the regression test
`MeshQualityReport.RotatingAValidMeshChangesNoMetric` covers it.

Each metric reports count, min, max, mean, RMS, the worst entity (id and centroid; lowest id on
ties) and the count above the warning threshold. The report also gives degenerate-cell and
invalid-face counts and the number of connected components.

## 5. Warning thresholds

A warning gives status `valid_with_warnings`; the mesh is never rejected for it.

| metric | threshold | rationale |
| --- | --- | --- |
| non-orthogonality | 70° | tan 70° = 2.7: the deferred correction dominates the implicit flux |
| skewness | 0.5 | each skewness-correction sweep removes at most half the error |
| aspect ratio | 100 | coefficient anisotropy AR² = 1e4 |
| expansion ratio | 2 | interpolation weight 1/3; the first-order term reaches 1/3 of the second-order one |

- An information item recommends `non_orthogonal_corrections ≥ 1` above 10°.
- **The campaign shows these are not "safe below" limits.** The velocity error grows 20–40× well
  below every threshold (§10). They are reported as warnings because no universal failure value
  exists.

## 6. Fatal thresholds

These are not configurable. A mesh is invalid if any of the following holds:

- a cell with non-finite or non-positive area;
- a non-finite centroid;
- a cell with fewer than 3 faces, or listing a face it neither owns nor neighbours;
- an open cell (\|Σ S_f\| > 1e-10 Σ\|S_f\|);
- a face with non-finite or zero length;
- an invalid owner/neighbour id, or owner == neighbour;
- reversed orientation;
- a decomposition that is not well-posed (≥ 89.9999°);
- a boundary face not in exactly one patch;
- more than one connected region.

At most 20 fatal issues are listed, then a count of the rest. Several of these conditions are
already rejected by the `Cell`/`Face` constructors; the unit tests pin both lines of defence.

## 7. CLI

- **What it prints:** a `Mesh quality:` block after the `Mesh:` line — status, cells, area range,
  the maximum of every metric with its worst entity and location, and one line per issue.
- **Invalid mesh:** exit code 2 with the named defect.
- **Dedicated ctest entries** (all pass; 13/13 CLI ctests):
  - `CFDAppCli_mesh_quality_invalid_cli` and `CFDAppCli_mesh_quality_disconnected_cli` (non-zero
    exit, `WILL_FAIL`);
  - `CFDAppCliMeshQualityInvalidNamesTheCell` (`cell (1,0) … not convex at corner`);
  - `CFDAppCliMeshQualityDisconnectedIsFatal` (the summary plus
    `fatal connected_components … 16, 16`);
  - `CFDAppCliMeshQualityWarning` (`valid_with_warnings`, the aggregated expansion warning, and
    `Converged: yes`).
- **Fixtures:** `tests/data/cases/mesh_quality_{warning,invalid,disconnected}_cli`.

## 8. GUI

`SimulationController::meshQuality` (a `QVariantMap`: status, summary, key maxima, issues) is
refreshed on open and on validate, and cleared by a new case. Warnings are listed in
`validationIssues()` (severity "Warning", section "Mesh"; amber in `ValidationPanel.qml`). The Mesh
page shows a *Mesh quality* box.

Four new GUI tests: the report on open (`poiseuille_distorted`, 44.76° plus the info advice),
warnings in the validation panel, an invalid mesh with its reason, and a new case clearing the
report. GUI suite 50/50. qmllint shows 0 errors on both changed QML files
([logs/24](logs/24_qmllint_gui.log)); the "Unqualified access" warnings are the existing
project-wide pattern for C++ context properties.

## 9. JSON schema

`metadata.json` `"mesh_quality"` holds:

- status and the cell/face counts;
- one object per metric: count, min, max, mean, rms, worst_id, worst_location, above_warning,
  warning_threshold;
- degenerate_cells, invalid_faces, connected_components;
- `issues` [severity, metric, entity, id, location, value, threshold, count, message].

Non-finite numbers, and the statistics of a metric with no entities, are `null`. The key is absent
when no report is given, and existing consumers are unaffected: every other metadata key is
unchanged (§17). Tests: `JSONWriterTest.MeshQuality*` (3), `ProjectRunnerTest.MeshQuality*` (2) and
`MeshQualityCase.*` (4). Documented in `docs/user_guide/case_format.md` (*Mesh quality*).

## 10. Degradation campaign (rerun, [logs/16–19](logs/))

n = 32-class meshes, corrected recipe. Each entry is L2 error ÷ Q0's L2 error; the full metrics,
norms and runtimes are in the CSVs.

| family, Q1 / Q2 / Q3 / Q4 | targeted metric | u | v | p | T | SIMPLE it ÷ Q0 |
| --- | --- | --- | --- | --- | --- | --- |
| D1 smooth non-orthogonality | 17.7 / 34.6 / 50.1 / 63.9° | 1.11 / 1.36 / 1.81 / 2.72 | 1.02 / 1.28 / 1.90 / 3.28 | 0.72 / 0.81 / 1.02 / 1.37 | 1.09 / 1.35 / 1.73 / 2.19 | 1.2 / 3.3 / 4.7 / 6.0 |
| D2 cell-scale irregularity | skewness 0.064 / 0.130 / 0.199 / 0.272 | 5.7 / 14.0 / 22.8 / 32.1 | 7.2 / 17.3 / 27.9 / 39.7 | 58.8 / 76.9 / 79.2 / 78.6 | 1.00 / 1.03 / 1.06 / 1.09 | 7.0 / 8.2 / 8.2 / 7.6 |
| D3 aspect ratio | AR 1.96 / 4 / 8.27 / 16 | 2.2 / 5.3 / 12.6 / 27.1 | 2.3 / 4.6 / 9.2 / 17.4 | 1.25 / 3.1 / 5.7 / 12.2 | 1.04 / 1.23 / 1.54 / 1.91 | 1.3 / 2.4 / 1.6 / 1.6 |
| D4 expansion | 1.1 / 1.2 / 1.3 / 1.4 | 2.5 / 6.8 / 13.4 / 21.3 | 1.7 / 3.5 / 6.6 / 10.9 | 1.9 / 4.3 / 7.8 / 12.6 | 1.01 / 1.07 / 1.09 / 1.06 | 1.2 / 1.3 / 1.3 / 1.4 |

- **D1 Q5** (73.6°, reported): converged. u L2 4.24×, v 5.64×, p 1.96×, T 2.61× Q0.
- **Uncorrected recipe** (reported): diverges (`MomentumFailure`) at every non-zero D1 amplitude. On
  D2 it converges, with u/v errors 1.04–1.12×, p 0.96–1.00× and T 1.04–1.71× the corrected recipe's.

Each ratio comes from runs that change only that family's parameter, with everything else fixed.
No causal claim beyond that is made. D2 moves skewness, non-orthogonality and expansion ratio
together, and D3/D4 change cell sizes where the manufactured velocity has its largest gradients.

## 11. Pre-registered gate

[acceptance_gate.md](acceptance_gate.md), written before the first final run and never changed.

- **First run** ([logs/13](logs/13_gate_evaluation.log)): FAILED R3, M1, GC1 (thermal
  `LinearSolveFailure`).
- **Rerun with the final §23 fix** ([logs/29](logs/29_gate_evaluation_rerun.log)): R1–R4, R6, R7,
  M1–M3 and GC1 **PASS**. R5 and M4 are reported. GC2 is not applicable (no quantity classified
  asymptotic, the gate's pre-registered rule).
- A rerun with the first, superseded version of the fix also passed. Its full regression failed
  (§20), so it is not the evidence; 42 of its 45 runs are bit-identical to the final rerun.

## 12. MMS configuration

- **Solution:** wall-compatible, through the production path (as in the gate).
- **Forcing check:** matches finite differences independently to 1.1e-7 (momentum) and 3.4e-7
  (heat) relative.
- **Families:** Cartesian and the smooth A = 0.05 family (≈ 35°), n = 8, 16, 32, 64. The smooth
  family runs with both the corrected and the uncorrected recipe.
- **Correction paths exercised:** least-squares gradients, non-orthogonal diffusion correction
  (momentum and energy) and the geometric pressure-correction passes (`non_orthogonal_corrections`
  2). The Green–Gauss skewness correction is not exercised by the MMS, because least squares needs
  none; its own P12-NUM-002/003 verification stands.

## 13. MMS results (rerun, [logs/20–22](logs/))

| family | pair | u | v | p | T |
| --- | --- | --- | --- | --- | --- |
| Cartesian | 8→16 / 16→32 / **32→64** | 2.49 / 2.16 / **1.93** | 2.61 / 2.26 / **1.89** | 2.09 / 1.88 / **1.96** | 0.63 / 0.87 / **0.94** |
| smooth 35°, corrected | 8→16 / 16→32 / **32→64** | 2.20 / 2.16 / **2.03** | 2.41 / 2.37 / **2.05** | 2.27 / 2.01 / **2.06** | 0.62 / 0.86 / **0.95** |

- **Velocity and pressure:** second order on both the orthogonal and the 35° non-orthogonal family
  (measured, finest pair ≥ 1.89).
- **Temperature:** approaches first order (0.94–0.95), consistent with the production energy
  equation's first-order upwind convection. It is not second order, and is not claimed as such.
- **Uncorrected recipe:** converged at n = 8 only; at n = 16, 32 and 64 it diverged (M4, reported).

## 14. Grid convergence (P12-NUM-005)

Grids 16/32/64. Every report passes `validateGridConvergenceReport`:
[data/grid_convergence_*.json](data/).

| family | kinetic energy KE | mean temperature ⟨T⟩ |
| --- | --- | --- |
| Cartesian | oscillatory: no order or GCI computed | monotonic, not asymptotic (p = 0.29) |
| smooth | monotonic, not asymptotic (p = 4.1) | monotonic, not asymptotic (p = 0.41) |

- **Smooth KE:** the fine-grid uncertainty U21 = 1.6e-5 does not bracket the actual error of
  8.0e-5, consistent with the observed order being far outside the asymptotic range. It is reported;
  GC2 applies only to asymptotic quantities.
- **Irregular family (reported):** rejected. At n = 64, SIMPLE stops at `MaxIterations` (§21). Its
  thermal solve now converges.

## 15. Conservation

On all 38 accepted rerun runs, |global mass imbalance| = 0.0 (limit 1e-10) and the energy imbalance
is ≤ 2.9e-11 (limit 1e-8). R7 passes. The first run gave the same result on its 34 accepted runs
(≤ 9.6e-12).

- **Inlet/outlet flows:** the MMS cavity is closed (all walls), so inlet and outlet mass flow are
  zero by construction, and the global imbalance is the check.
- **Cases with through-flow** (MESH-001 distorted Poiseuille, MESH-002 transpiration channel,
  MESH-003 curved channel and sector conduction) keep their own conservation gates. Their
  production tests pass unchanged (focused step 13: 20/20).

## 16. Invalid-mesh campaign

R6 passes ([logs/15](logs/15_rerun_regular_tests.log)). All 11 cases give `InvalidCase` (exit 2),
with no SIMPLE result, zero progress callbacks and no `results/` directory. Each error names the
defect and where it is:

- zero-area cell: `zero area`;
- vertex moved through its neighbour: `not convex at corner`;
- mirrored block: `inverted`;
- degenerate face: `degenerate edge … zero-length face`;
- non-finite coordinate in memory: `non-finite coordinate`;
- non-finite coordinate in mesh.json: `mesh.json … number overflow`;
- broken interface: `does not coincide`;
- disconnected domain: `2 disconnected cell regions`;
- both beyond-range levels: `is not a strictly convex counter-clockwise quadrilateral`.

## 17. Backward compatibility ([logs/25](logs/25_backward_compat_cli.log))

The `/tmp` snapshots were lost, so the references were rebuilt outside `/tmp`:

- **HEAD** `b66310c`, from a git worktree;
- **PREFIX:** the current tree with `src/algebra/BiCGSTAB.cpp` replaced by HEAD's version. That file
  is sha `d33521c5…`, identical to the pre-fix solver, and it is MESH-004's only numerical change.

All 22 cases were run: every committed case, the three existing CLI fixtures, the three new fixtures
and the six MESH-001–003 cases.

- **NEW vs PREFIX:** every case is **identical**. Fields, VTK and residuals match byte for byte;
  metadata matches apart from the new `mesh_quality` key; stdout matches apart from the new
  mesh-quality block. The BiCGSTAB fix changes nothing on any committed case, and the invalid
  fixtures are rejected the same way (exit 2, nothing written).
- **PREFIX vs HEAD and NEW vs HEAD:** identical on the 13 Cartesian cases and fixtures, except
  `heated_cavity` and `heated_species_diffusion`. Those are MESH-003's thermal fix: the numbers
  (temperature 4.66e-8 / 9.91e-8, thermal iterations 112 → 2 / 201 → 2) reproduce
  `results/p12-mesh-003/logs/08` exactly. So the quality reporting changed no solver behaviour on
  any valid existing mesh.
- **Mesh quality of the committed cases:** every one is `valid`. Only `poiseuille_distorted`
  (44.8°) carries the info item.

## 18. Focused tests ([logs/26](logs/26_ordered_focused_tests.log))

**831/831** across 25 ordered steps (suites overlap, so some tests count twice).

New tests:

| suite | new tests |
| --- | --- |
| `BiCGSTABBreakdown.*` | 7 |
| `MeshQualityReport.*` | 18 |
| `MeshQualityCase.*` | 4 |
| `JSONWriterTest.MeshQuality*` | 3 |
| `ProjectRunnerTest.MeshQuality*` | 2 |
| `CaseEditingTest.*MeshQuality*` | 4 (GUI) |
| CLI ctests | 5 |
| `MeshQualityCampaign.*` | 5 regular + 8 explicit `DISABLED_` campaign tests |

Whole suites: algebra 97, mesh 127, I/O 266, thermal 87, app layer 8 (ProjectRunner), GUI 50,
CLI 13; `SIMPLERobustnessTest.*` 10 and `CompressibleSIMPLERobustnessTest.*` 3 (two of them rewritten,
§23); `LinearFallbackTest.*` 13.

## 19. Full regression

Final code (the gate rerun, compatibility and focused-test code; `clang-format` afterwards changed 3
lines of `BiCGSTAB.cpp`, whitespace only):

| build | result | listed | disabled / explicit-only | log |
| --- | --- | --- | --- | --- |
| Release (-O3, GUI off) | **1787/1787 passed**, 0 failed | 1820 | 33 (25 before + 8 `DISABLED_` campaign tests) | [logs/27](logs/27_full_regression_release.log) |
| Debug + GUI (Qt 6.2.4, offscreen) | **1837/1837 passed**, 0 failed | — | — | [logs/28](logs/28_full_regression_debug_gui.log) |

Against the end of MESH-003 (1743 / 1789):
- **Release:** +44 new tests — 18 mesh, 7 algebra, 7 I/O, 2 app, 5 campaign regular, 5 CLI.
- **Debug:** the same +44, plus 4 GUI tests (+48).

**Generated outputs** ([logs/30](logs/30_generated_outputs.log)). The regression rewrote 33 tracked
outputs.
- **Restored to HEAD:** 13 files whose only changes are runtime fields.
- **Kept:** 20 files whose values changed.
  - Two fixture `metadata.json` files gained the `mesh_quality` key.
  - Three production validations (cavity QUICK, Poiseuille, scheme comparison) no longer need the
    linear-solver fallback HEAD needed (1 / 2 / 1 → 0). HEAD's fallbacks were triggered by
    BiCGSTAB breakdowns under the old absolute test.
  - Their solutions, and those of the momentum MMS, Poiseuille grid convergence and turbulence
    channel outputs, change at solver-tolerance level (≤ 2e-5 relative on derived error
    quantities). No pass/fail flag changed.

The earlier FAILED regression of the superseded first version is kept as
[logs/27a](logs/27a_FAILED_full_regression_release_norm_criterion.log) (16 failures, §20.4).

## 20. Failed experiments

1. **The first gate run** failed R3, M1 and GC1 ([logs/13](logs/13_gate_evaluation.log)). It is
   kept, with its data in `data_first_run/`.
2. **A restart-on-breakdown change to the thermal Picard loop**, drafted during the probe. It was
   reverted before any campaign run: it is a thermal-specific workaround, not the root cause.
3. **The n·ε relative breakdown criterion** (the worst-case dot-product rounding bound, relative to
   the norms). It was rejected on evidence: it spuriously breaks down on the healthy 4096-unknown
   thermal system and on a 200-unknown system that the old code solves
   ([solver-robustness/03](solver-robustness/03_criterion_comparison.log)).
4. **The first version of the fix: norm-relative test |(x, y)| ≤ ε‖x‖‖y‖, without restart.** It
   passed the gate rerun, but the full Release regression then **failed 16 tests**
   ([logs/27a](logs/27a_FAILED_full_regression_release_norm_criterion.log), kept):
   - species/alpha advection, species coupling and the turbulence channel;
   - the two P12-NUM-004 fallback tests.

   Causes ([solver-robustness/05](solver-robustness/05_fallback_fixture_and_regression_diagnosis.log)):
   - an inner product without cancellation is accurate however small it is relative to ‖x‖‖y‖, so
     the norm test wrongly called it zero;
   - genuine rounding-level cancellation does occur mid-solve at normal scales, and the old code
     only survived it by continuing.

   Both were fixed by the final design (§23). The superseded rerun is kept in
   `superseded_rerun_norm_criterion/`.
5. **The R5 "continuity contraction" metric** is uninformative (≈ 1.000 everywhere, because the zero
   initial field makes the first residual tiny); iteration counts are used instead.

## 21. Limitations and findings

- **Irregular (cell-scale random) meshes: velocity and pressure do not converge.** This was NOT
  modified here and needs separate scoping.
  - On f = 0.15, u L2 is 2.19e-2 / 2.15e-2 / 1.39e-2 and p L2 0.24 / 0.43 / 0.51 for n = 16 / 32 /
    64; n = 64 stops at SIMPLE `MaxIterations`.
  - The pressure error is 59× Q0's already at skewness 0.064.
  - Temperature on the same meshes converges (order 0.92), and the uncorrected recipe gives nearly
    the same u/v/p errors.
  - A pressure–velocity coupling or gradient-treatment defect on skewed meshes is likely, but it is
    not diagnosed.
- **The uncorrected recipe on smooth non-orthogonal meshes** diverges from 17.7° upward at
  relaxation 0.8/0.4. Lower relaxation was not tested.
- **Two existing tests were rewritten** — `SIMPLERobustnessTest.LinearSolverFallbackRecovery` and
  `CompressibleSIMPLERobustnessTest.FallbackRecovery`. Their fixture, the "P12-COMP-002 BiCGSTAB
  pressure breakdown", was this same false breakdown: t·t < 1e-30 only because ‖t‖/‖s‖ ≈ 4e-8 (the
  matrix's scale) times ‖s‖ ≈ 2e-8 (the residual's).
  - They now assert the corrected behaviour: the run reaches its budget, the fallback is never
    needed, and results are bit-identical with and without it.
  - The fallback on a genuine breakdown stays covered by the 13 `LinearFallbackTest` unit tests.
  - There is no longer a natural SIMPLE-level trigger. Restart-capable BiCGSTAB cannot report
    `Breakdown` on a symmetric positive-definite pressure system.
- **CG** (`src/algebra/CG.cpp`) keeps the same kind of absolute test (`|pAp| < 1e-30`). It is not
  changed (out of the authorized BiCGSTAB-only scope) and is recorded as a latent sibling.
- **MMS correction paths:** the Green–Gauss skewness correction is not exercised by the MMS (§12).
- **Species solver:** the lagged-boundary pattern from P12-MESH-003 remains.
- **Runtimes** are wall-clock, with 8 campaign processes running concurrently on 32 cores.
- **Lost snapshots:** the pre-MESH-004 snapshot was lost to a WSL `/tmp` cleanup, and the
  compatibility references were rebuilt (§17).

## 22. Final acceptance decision

**ACCEPTED — P12-MESH-004 `[x]` COMPLETE.** The final acceptance gate's evidence is all present:

| requirement | evidence |
| --- | --- |
| authoritative production quality metrics | §3–4 |
| warning/fatal classification | §5–6 |
| CLI visibility | §7 |
| GUI visibility | §8 |
| results JSON | §9 |
| controlled, measurable degradation with quantified numerical consequences | §10 |
| production non-orthogonal MMS (second-order velocity/pressure, first-order temperature) | §12–13 |
| grid convergence | §14 |
| conservation | §15 |
| invalid-mesh rejection | §16 |
| backward compatibility (byte-identical committed cases) | §17 |
| focused tests 831/831 | §18 |
| full regression Release 1787/1787, Debug + GUI 1837/1837 | §19 |

The pre-registered gate is unchanged, and every gated criterion passes on the final code
([logs/29](logs/29_gate_evaluation_rerun.log)).

**Open findings for separate scoping (§21):**
- velocity/pressure on cell-scale irregular meshes;
- the uncorrected recipe on non-orthogonal meshes;
- CG's absolute breakdown threshold;
- the rewritten fallback tests.

Nothing is committed or pushed, and P12-MESH-005 is not started.

## 23. Solver robustness — BiCGSTAB scale-invariant breakdown detection with bounded restart (authorized fix)

**Root cause** ([solver-robustness/00](solver-robustness/00_capture_failing_systems.log)).
`BiCGSTAB::solve` declared a breakdown when a recurrence scalar fell below the absolute constant
`constants::tiny` = 1e-30:

- |ρ| = \|(r̂, r)\| < 1e-30;
- \|(r̂, v)\| < 1e-30;
- t·t < 1e-30;
- \|ω\| < 1e-30.

These quantities scale with the residual (ρ, (r̂, v)), with the square of residual × matrix scale
(t·t), or with 1/‖A‖ (ω). The thermal system of a converging Picard loop starts each linear solve at
~1e-8 and must reach 1e-12. At that scale a perfectly healthy iteration produces inner products
below 1e-30.

The three captured failures:

| case | failing test | absolute value | relative value \|(x,y)\|/(‖x‖‖y‖) |
| --- | --- | --- | --- |
| smooth n = 32 (and D1 Q2) | (r̂, v), iteration 36 | 2.3e-31 | 3.4e-10 |
| smooth n = 64 | (r̂, v), iteration 34 | 6.6e-31 | 8.7e-12 |
| D2 Q4 | ρ, iteration 38 | 7.4e-31 | 7.5e-12 |

Each relative value is 10⁴–10⁶ × machine epsilon, yet `runPicardLoop` reported
`LinearSolveFailure` although the field was within \|ΔT\| ≈ 3e-8 of convergence. The instrumented
copy of the algorithm was bit-identical to production on every outer solve.

The P12-COMP-002 "breakdown" used by the fallback tests was the same defect
(solver-robustness/05): t·t = 5.6e-31 < 1e-30, with ‖t‖/‖s‖ = 3.9e-8 and ‖s‖ = 1.9e-8.

**Old criterion:** the absolute `< 1e-30` tests above.

**New criterion** (`src/algebra/BiCGSTAB.cpp`):

1. **An inner product of the recurrence is numerically zero when it has cancelled to the rounding
   level of its own terms:**

       |(x, y)| ≤ ε Σᵢ |xᵢ yᵢ|,   ε = std::numeric_limits<double>::epsilon()

   applied to ρ = (r̂, r), to (r̂, v), and to (t, s), which replaces the \|ω\| test.
2. **t·t** is a sum of squares, with no cancellation, so it breaks down only if it underflows below
   the normal range (`numeric_limits::min()`). The non-finite checks stay.
3. **Bounded restart.** On a numerical breakdown (1), if the residual has decreased strictly since
   the current Krylov sequence started:
   - the sequence restarts from the current iterate, using its **recomputed true residual**
     b − Ax as the new r̂ and resetting the coefficients;
   - if that true residual already meets the tolerance, the result is `Converged`;
   - otherwise, including a breakdown in a sequence's first iteration, `Breakdown` is reported.
   - Every iteration counts against `maxIterations`, and restarts need progress, so the number of
     restarts is bounded by the number of iterations. `SolverResult::restarts` counts them.

**Mathematical justification:**

- **Why these three products.** BiCGSTAB divides by ρ_{k−1} (through β), by (r̂, v) (α) and by
  ω = (t, s)/(t, t). Each vanishes at a genuine breakdown because its vectors are orthogonal, not
  because they are small.
- **Why Σ|xᵢyᵢ| and not 1e-30 or ‖x‖‖y‖.** Whether a computed inner product can be distinguished
  from zero is decided by the rounding error of its own sum, bounded by γₙ Σᵢ\|xᵢyᵢ\| (Higham,
  eq. 3.5):
  - not by its size against a fixed constant, which is scale-dependent;
  - not by ‖x‖‖y‖: a sum without cancellation is accurate however small it is relative to ‖x‖‖y‖,
    which was the first version's failure (§20.4).
- **Why the threshold is ε.** It flags only products cancelled to the level of a single rounding of
  their terms, so it never rejects a healthy step. The stricter bound n·ε was rejected (§20.3).
- **Scale invariance.** The test is invariant under (A, b) → (αA, βb); power-of-two scaling gives
  bit-identical decisions and an exactly scaled residual history (`ScalingTheSystemByAPowerOfTwoChangesNothing`;
  the old code broke down at iteration 1 of the 2^-100 system). Since Σ\|xᵢyᵢ\| ≤ ‖x‖‖y‖, the norm
  test is a free pre-filter, and the extra pass over the terms runs only in the near-zero case.
- **Why a restart is needed.** Genuine rounding-level cancellation occurs mid-solve at normal
  scales, e.g. a species-coupling pressure solve with ρ = −2.2e-26 against ε Σ\|xᵢyᵢ\| = 3.9e-26.
  The old code survived it only by continuing with a noise-level scalar. The restart is the
  standard remedy: a fresh shadow residual, exact from the true residual, re-establishes the
  biorthogonality. The progress guard guarantees termination, and a first-iteration breakdown,
  which a restart cannot cure, is still reported.

**Convergence-versus-breakdown ordering.** This was audited and kept. Every residual — r₀, the
half-step s, the updated r, and a restart's recomputed true residual — is tested for convergence
immediately after it is formed, before any breakdown test that uses it
(`ExactTerminationIsConvergedNotBreakdown`: a 1×1 system solved by the half-step, where t·t would
be 0).

**No silent acceptance.**

- A restart never declares success; only a residual meeting the tolerance does, and after a
  restart that is the recomputed true residual.
- A breakdown without progress is `Breakdown`.
- The reproducer and restart tests check the recomputed residual ‖b − Ax‖, not only the recursive
  one.

**Before/after reproducer** ([solver-robustness/01](solver-robustness/01_tests_before_fix.log)
pre-fix = HEAD's `BiCGSTAB.cpp`, [02](solver-robustness/02_tests_after_fix.log) final). The two
captured 1024-unknown thermal systems are stored in `tests/data/linear_systems/`, reals written with
`%.17g` for an exact round trip.

| | smooth system | irregular system | `BiCGSTABBreakdown` tests |
| --- | --- | --- | --- |
| pre-fix solver | `Breakdown` after 35 it (residual 4.19e-11) | `Breakdown` after 37 it (9.43e-12) | 3/7 (4 fail: reproducers, scale invariance, 2⁻⁴⁰ solve, restart recovery) |
| final solver | `Converged` in 44 it, **0 restarts**, true residual 8.95e-13 | `Converged` in 53 it, 0 restarts, true residual 8.85e-13 | 7/7 |

The scale-invariant test alone fixes the blockers; the restart is not involved.

**True-breakdown regression.** Nonsingular 3×3 integer systems with exactly dyadic recurrences
(exact rational search, `solver-robustness/tools/find_breakdowns.py`), so the breaking product is
exactly zero:

- **Reported as `Breakdown`, 0 restarts** (a breakdown without progress, which a restart cannot
  cure):
  - the 2×2 rotation, (r̂, v) = 0 at iteration 1;
  - (t, s) = 0 at iteration 1;
  - ρ = 0 at iteration 2 after a first iteration with ‖r₁‖ = ‖r₀‖.
- **Recovered by 1 restart:** (r̂, v) = 0 at iteration 2, after the first iteration made progress.
  It is `Converged` in 4 iterations, true residual 7.2e-16, and deterministic.
- The unit-level genuine fallback trigger (`diag(1, −1)`, `LinearFallbackTest`) still breaks down.

**Thermal reproducers** ([solver-robustness/04](solver-robustness/04_thermal_reproducer_rerun.log),
final library). Rerun unchanged, all thermal `Converged`:

| case | outer iterations | last linear solve | energy imbalance |
| --- | --- | --- | --- |
| smooth n = 32 | 18 | 3.5e-9 → 3.4e-13 | 4.6e-12 |
| smooth n = 64 | 19 | 3.1e-9 → 9.0e-13 | 2.2e-11 |
| D1 Q2 | 18 (identical to n = 32) | 3.5e-9 → 3.4e-13 | 4.6e-12 |
| D2 Q4 | 10 | 1.0e-8 → 8.4e-13 | 2.9e-11 |

**Unchanged gate rerun:** PASS (§11, [logs/29](logs/29_gate_evaluation_rerun.log)).

**Scope of the behaviour change:**

- Every committed case gives byte-identical outputs with and without the fix (§17).
- Converged campaign runs are unchanged.
- Tests the first version broke — species/alpha transport, species coupling, the turbulence channel
  — pass again.
- Three existing production validations no longer need the linear-solver fallback HEAD needed
  (1 / 2 / 1 → 0). Some existing validation outputs change at solver-tolerance level
  (≤ 2e-5 relative); no pass/fail flag changed (§19, [logs/30](logs/30_generated_outputs.log)).
- Only already-failing runs changed: diverging uncorrected-recipe runs run a few iterations longer
  before failing the same way.
- The two fallback tests are rewritten (§21).
