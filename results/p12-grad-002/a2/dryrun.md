# P12-GRAD-002 A2 — pre-freeze dry-run (2026-09-17)

Every criterion of `../acceptance_gate_A2.md` was evaluated **before** the freeze. This covers the
amended clauses and the carried-over clauses no earlier run had measured. The only repository
change used was the candidate test file, which was compiled standalone. Neither `CMakeLists.txt`
nor any production file was changed.

Libraries:

| name | libcfdcore.a |
|---|---|
| repo | `143a1dda…` |
| cur | `143a1dda…`, bit-identical ([logs/00_build_cur.log](logs/00_build_cur.log)) |
| nograd | `c74f6ab7…` ([logs/00_build_nograd.log](logs/00_build_nograd.log): only `Gradient.cpp` differs from the repo) |
| base | `eaadaa63…` |
| grad001 | `4fa871b8…` |

## 1. Summary

| criterion | dry-run result | log |
|---|---|---|
| C2-A2(a) planar fixed point | PASS on all 8 planar meshes, all 3 libraries | `dry_3d_*.log` |
| C2-A2(b) recursion bound | 0 violations on all 21 meshes, all 3 libraries | `dry_3d_*.log` |
| C2-A2(c) warp order | 1.925, 1.982 | `dry_3d_repo.log` |
| C2-A2(d) non-vacuity | (i) PG/floor ≥ 1.6e8 at A = 0.05 and 2.6e2 at A = 1e-8; (ii) the PG := 0 check is violated on every warped mesh with A > 0 and on no planar mesh; (iii) the quadrature self-checks pass | `dry_3d_*.log` |
| C2, Q16 (unchanged) | 3.769e-10 ≤ 1e-9 | `dry_3d_repo.log` |
| C9 on deformed meshes: C3(b), C4a, C6-A1 | repo PASS. nograd and grad001 fail C3(b) (0.883 / 0.942), as the negative control requires | `dry_c9_*.log` |
| C10-A2(a) aligned bitwise | PASS | `dry_c10_repo_vs_nograd.log` |
| C10-A2(b) skewed: depth and coupling bound | PASS: 0 deep-cell differences, 0 bound violations, t in range | same |
| C10 as frozen (1e-13 on skewed meshes) | **fails on every non-trivially skewed mesh**, as derived in A2 §3 | same |
| C10-A2(c) nograd ≡ base | 0 differences, 15 meshes × 3 fields | `dry_c10_nograd_vs_base.log` |
| C10-A2(d) instrument self-test | flags on every skewed mesh | `dry_c10_selftest_mutated.log` |
| C11(a) | max 5.84e-16 ≤ 1e-13 | `dry_c10_repo_vs_nograd.log` |
| C11(b) aligned field exports | PASS; worst ratio to the allowed change 1.000 (one printed unit), largest relative change 3.2e-14 | `dry_c11b_fields.log`, `dry_c11b_outputs.log` |
| C8 categorization | 55 changed tests: 29 A, 25 B, 1 T; 0 unexplained | `dry_c8_categories.log`, `dry_c8_numbers.log` |
| C13 new tests, standalone | repo **9/9**; nograd **7/9**, failing exactly the two pre-registered tests (rerun on the final candidate, 0 compile warnings) | `dry_c13_*_1.log`, `dry_c13_*_2.log` |
| cur / nograd full suites (without the new file) | cur 1923/1923; nograd 1921/1923, failing exactly `GridRefinementTest…ReflectsBoundaryTreatment` and `AlePisoGalilean.TranslatingCavity…` | `suite_*.log` |

## 2. C2-A2 in detail

All errors are relative to |∇φ|. "trunc" is the four-sweep truncation |e_4 − e_64|.

| mesh | e_4, repo | e_64, all libraries | trunc, repo | trunc, nograd | PG | worst bound ratio, repo | PG := 0 control, violations |
|---|---|---|---|---|---|---|---|
| Q16 (2D) | 3.769e-10 | 2.6e-14 | 3.8e-10 | 1.1e-10 | 0 | 0.946 | 0 |
| NUM-003 10×10 at 0.45 h | 2.738e-8 | 7.9e-15 | 2.7e-8 | 1.4e-8 | 0 | 0.889 | 0 |
| NUM-003 20×20 at 0.45 h | 5.089e-11 | 4.8e-14 | 5.1e-11 | 2.3e-11 | 0 | 0.968 | 0 |
| planar s=1, 8³ | 3.278e-4 | 6.3e-15 | 3.3e-4 | 8.1e-5 | ~1e-14 | 0.636 | 0 |
| planar s=1, 16³ | 3.169e-4 | 1.5e-14 | 3.2e-4 | 7.6e-5 | ~2e-14 | 0.767 | 0 |
| planar s=1, 32³ | 3.124e-4 | 3.1e-14 | 3.1e-4 | 7.4e-5 | ~5e-14 | 0.798 | 0 |
| warped A=0.05, 8³ | 1.244e-3 | 1.244e-3 | 5.0e-9 | 2.3e-9 | 1.242e-3 | 0.863 | 3 713 |
| warped A=0.05, 16³ | 3.275e-4 | 3.275e-4 | 4.4e-10 | 1.9e-10 | 3.273e-4 | 0.964 | 30 582 |
| warped A=0.05, 32³ | 8.292e-5 | 8.292e-5 | 3.0e-11 | 1.3e-11 | 8.291e-5 | 0.992 | 251 023 |

The planar s-sweep and the warped A-sweep are in the logs. The warped A-sweep:

- PG is linear in A: 6.5e-11 at 1e-8, up to 1.6e-4 at 0.025.
- \|e − PG\| grows as A², i.e. the feedback term.
- At A = 0 the error is 7.6e-15.

**Four-sweep truncation on the committed production meshes** (`a2_prodmesh`,
[logs/dry_prodmesh_repo.log](logs/dry_prodmesh_repo.log) and `_nograd.log`): max |g_4 − g_64| / max|∇φ|,
linear field, Neumann boundaries.

| mesh | repo | nograd |
|---|---|---|
| poiseuille_distorted 64×8 | 6.4e-6 | 3.4e-6 |
| poiseuille_distorted 144×18 | 1.6e-7 | 6.9e-8 |
| poiseuille_distorted 216×27 | 2.7e-8 | 1.1e-8 |
| poiseuille_distorted 512×64 | 7.2e-10 | 3.0e-10 |
| curved_channel, all grids | ≤ 4.4e-14 | ≤ 6.0e-15 |

With Dirichlet boundaries both libraries are ≤ 1.2e-6 and equal to within 30 %. The effect is
negligible on committed cases and falls under refinement. The non-vanishing case is the 3D mesh
with uniformly tilted patches. It is recorded as debt (A2 §6).

## 3. C10 per-layer maxima (repo against nograd, relative to the gradient scale; maximum over the three fields unless a field is named)

| mesh | interior non-bitwise cells | L2 | L3 | L4 | L5 | layer ≥ 6 | worst bound ratio |
|---|---|---|---|---|---|---|---|
| Q16 (linear) | 124 | 4.4e-7 | 4.4e-11 | 1.2e-14 | 0 | 0 of 36 differ | 0.109 |
| C5 shear 5e-3 | 28 | 2.2e-7 | 0 | 0 | 0 | 0 of 36 | 0.003 |
| C5 shear 5e-9 | 0 | 0 | 0 | 0 | 0 | 0 of 36 | 0 |
| NUM-003 20×20 at 0.45 h | 131 | 8.1e-8 | 9.2e-13 | 4.5e-17 | 0 | 0 of 100 | 0.117 |
| production quad 64×8 | 372 | 1.1e-4 | 7.5e-7 | 1.8e-9 | — | — (max layer 4) | 0.458 |
| deformed 3D 8³ | 216 | 1.4e-6 | 1.2e-10 | 3.7e-15 | — | — | 0.056 |
| translated quad 16 and 64, cube 8³ (round-off skew) | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| translated quad 16 LARGE (reported only) | 26 | 4.1e-9 | 1.1e-16 | 0 | 0 | 0 of 36 | 0.001 |

Boundary cells (layer 1) change by up to 0.36 of the gradient scale on every mesh with a skewed or
non-representable boundary: the intended first-to-second-order change.

## 4. C11(b)

There are 183 generated files. 114 are IDENTICAL, 11 RUNTIME-ONLY and 58 VALUES.

**Category A (6 files, quantified):**

- `mms/distorted_mesh_mms.{json,md}`: extrapolated-error changes ≤ 4.4e-5 relative;
- `mms/distorted_mesh_momentum_mms.{json,md}`: the distorted MMS momentum errors move 6.65e-3 →
  6.72e-3 at 16² (+1.0 %), 3.167e-3 → 3.171e-3 at 32² (+0.15 %), and are
  unchanged to 4 digits at 128²;
- `production/curved_channel_multiblock_grid_convergence.json`;
- `production/poiseuille_distorted_grid_convergence.json`: the W8 families, as in §6 of the gate.

**Category B field exports (13 files), all PASS:**

| file | worst ratio \|d\|/allowed |
|---|---|
| `valid_cavity_cli_smoke` fields / VTK | 4.6e-10 (relative 3.2e-14) |
| `does_not_converge` | 2.8e-10 |
| `mesh_quality_warning_cli` (graded rectilinear) | 2.2e-8 |
| natural-convection centerlines | 6.7e-6 |
| Nusselt profiles | 0.66, below one printed unit |
| transient-cavity centerlines | 1.0, exactly one printed unit |

**Category B diagnostics (39 files).** These are residual histories, final residuals, imbalances,
wall-shear asymmetries (1e-13 to 1e-10), iteration counts and inner-solver totals. On the aligned
cases they change at round-off or stopping-point level. Examples:

| quantity | nograd → cur |
|---|---|
| cavity final continuity | 9.2e-11 → 8.4e-11 |
| natural-convection flow iterations in the last coupling pass | 1 → 10 |
| natural-convection heat imbalance | 1.06e-7 → 1.04e-7 |
| Nu_avg | changes 8e-16 relative |
| Cartesian production Poiseuille, deviation from the discrete-exact profile | 4.9e-9 → 5.5e-9 (bound 1e-4) |
| same case, dp/dx deviation | 2.2e-8 → 1.8e-7 (bound 1e-3) |

## 5. C8

In the verbose ctest output, 1 913 tests print identical numbers, 53 differ and 2 differ in text.
The two text changes are the tests nograd fails.

[logs/dry_c8_categories.log](logs/dry_c8_categories.log) assigns every changed test:

| category | count | what changes |
|---|---|---|
| **A** | 29 | ALE, distorted refinement and MMS families, curved multi-block, oblique-Neumann, skewness, structured_quad production, non-orthogonal thermal |
| **B** | 25 | stopping-point magnitudes; examples below |
| **T** | 1 | a timing ratio |

Examples of category B changes:

| quantity | nograd → cur |
|---|---|
| duct 3D relative imbalance | 2.7e-12 → 1.3e-12 |
| light cube symmetry defect | 2.8e-8 → 6.0e-8 |
| SIMPLE3D gauge checks | ~1e-14 |
| low-Mach global imbalance | 4.5095e-5 → 4.5086e-5 |
| MMS continuity L∞ | 3.2e-7 → 2.9e-7 |
| momentum-MMS outer iterations at 128² | 7 → 8 |
| deliberately diverging robustness run, final continuity | 1.4e-5 → 2.1e-5 |

Examples of category A changes, the intended ones:

| quantity | nograd → cur |
|---|---|
| structured_quad lubrication channel, iterations | 3668 → 2472 |
| same case, pressure L2 error | 9.9e-3 → 5.8e-3 |
| graded structured_quad channel, iterations | 1405 → 1890 and 1588 → 2246 |

The graded structured_quad case uses the 2D default linear flux, so its convergence rate is governed
by the undamped odd-even mode (DRIFT-001), which GRAD-002 couples more strongly. Its mass imbalance
improves: 1.3e-10 → 1.1e-11 on the uniform variant.

## 6. C9 on the deformed 3D family

| clause | repo | nograd = grad001 |
|---|---|---|
| C3(b), ∇(x³) L∞ at 8³/16³/32³ | 2.00e-2 / 5.69e-3 / 1.49e-3 | 7.98e-2 / 4.33e-2 / 2.25e-2 |
| C3(b) order, L∞ | **1.814, 1.935** | 0.883, 0.942 |
| C3(b) order, L2 | **1.905, 1.953** | 1.485, 1.491 |
| C4a | all ρ ≤ 0.069; geometry valid (centroid ≤ 2.3e-14 h) | PASS |
| C6-A1 | ρ spread ≤ 1.51 at 8³ and 16³ | PASS |

The pre-GRAD-002 operator is first order in L∞ at the boundary of a deformed 3D mesh. GRAD-002 is
second order there.

## 7. C13 dry-run (standalone build with the build's own gtest)

- **repo:** all 9 tests pass. One `-Wconversion` warning in the test file was fixed before the
  freeze; the rerun on the final candidate (`dry_c13_*_2.log`) compiles with 0 warnings and gives
  the same 9/9 and 7/9.
- **nograd:** 7 pass. `TranslatedMeshGivesTheSameGradient` fails on exactly the quadratic rows: Δ =
  7.8125e-3, 3.906e-3 and 1.953e-3 at 16/32/64, which reproduces A1's baseline. The dyadic rows and
  the constant and linear rows pass. `GradientErrorIsContinuousInTheBoundaryMisalignment` fails at
  the first step: a jump of 7.8125e-3 at m_f = 5.1e-13, which reproduces A1's log 04.

## 8. Production accuracy

These are the cur and nograd numbers quoted in the gate's §6. Logs: `prod_cur_*`, `prod_nograd_*`.

## 9. Instrument defects found and corrected during this dry-run (disclosed)

1. **`a2_3d.cpp`, first version.** It compared 2D gradients with the 3D field's gradient, so the
   2D reference rows read 0.245 (the missing z-component). This was fixed and rerun. The first
   run's log was overwritten by the rerun and is not preserved. Its planar and warped rows were
   unaffected: they are identical in the rerun.
2. **`compare_ctest_numbers.py`, three corrections:**
   - the two trees' paths made every test "TEXT";
   - gtest's "(N ms total)" was not masked;
   - a Markdown timing column was not masked, because its header row was dropped before header
     detection.

   One regex edit also briefly contained a literal backspace character from shell escaping. That
   version masked nothing and was replaced.
3. **`c11b_fields.py`.** A one-printed-unit difference evaluated in binary floating point came out
   at 1.00000000003 units, so a pass was reported as FAIL. The comparison now uses exact decimal
   arithmetic on the printed digits. A self-test pins both sides: a 1e-5 change must fail, and one
   printed unit must pass.
4. **Several heredoc escaping errors** made edits fail their own assertions. Each failed edit
   changed nothing; the edits were redone with the file tools.

5. **Final instrument adjustments before the freeze**, each followed by a rerun of its dry-run on
   the final version:
   - `a2_3d.cpp` prints the C2-A2(d)(i) ratio PG/floor with a verdict (the gate's (i) was
     re-worded from a "≥ 1e6 × floor" factor, chosen after seeing the data, to the derived "PG
     exceeds the floor");
   - `c8_categorize.py` gained a category for the new `GradientBoundaryConsistency.*` tests, whose
     output necessarily differs between cur and nograd;
   - `run_suite.sh`, `prod_accuracy.sh`: a log-name prefix, so fresh runs cannot overwrite this
     dry-run's logs; `fresh.sh` (new) drives the fresh execution.

6. **Line endings.** Windows Python's text mode had written CRLF into eight of these files
   (`a2_3d.cpp`, `a2_prodmesh.cpp`, `c8_categorize.py`, `c11b_fields.py`, `freeze.sh`, this
   record, the gate and `ROADMAP.md`). They were normalized to LF before the freeze. The code is
   unchanged; the dry-run logs record the CRLF-era probe source hashes.

None of these touched a threshold. Each was found because a result was implausible, and each
correction is covered by a self-test or by a rerun of the same data.
