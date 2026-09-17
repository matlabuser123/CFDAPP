# P12-GRAD-002 — Amendment A2 (pre-registered)

Authorized on 2026-09-17 by the user's instruction to resolve the P12-DIFF-002 → P12-GRAD-002 →
P12-MESH-007 blocker chain: "create justified gate amendments (derive, dry-run, prove non-vacuity,
freeze, hash, then edit tests, execute fresh)".

## 0. What stands, and what A2 changes

**Everything recorded before A2 stands unchanged:**

- the original gate (`acceptance_gate.md`, `a46973ed…`) and its C1 failure;
- Amendment A1 (`acceptance_gate_A1.md`, `353b72ef…`) and its failure on C2/C9's "3D deformed"
  clause (`a1/summary_a1.md`);
- INV-001's two production-case degradations (`investigation/summary.md`).

**A2 amends two clauses and nothing else:**

1. **C2's "3D deformed" clause**, including C9's use of it. It becomes C2-A2 (§2).
2. **C10's skewed-mesh sentence.** It becomes C10-A2(b) (§3).

**A2 changes no production source.** The library under test is `143a1dda0680bc1d…`, the
authoritative library after DIFF-002 and FORMAT-001. The only repository changes A2 authorizes:

- the new test file `tests/unit/discretization/test_gradient_boundary_consistency.cpp`
  (candidate hash in `a2/logs/00_freeze.log`);
- one line adding it to `tests/unit/discretization/CMakeLists.txt`;
- `clang-format-18` layout of that test file.

**Carried over in their A1 form:** C1-A1, C3, C4a/C4b, C5, C6-A1, C7, C8, C9 (with C2 read as
C2-A2), C11, C12, C13 and C14. §4 defines how C8, C9, C11 and C13 are measured. It sets no new
thresholds.

**Why the failures are resolved and not bypassed.** INV-001 attributed the two production-case
degradations to two mechanisms, both outside the gradient formulation:

- a first-order Dirichlet wall flux (fixed by P12-DIFF-002);
- an undamped odd-even pressure mode of the 2D linear face flux, which made the solution depend on
  where SIMPLE stopped (P12-GRAD-002-DRIFT-001; the two validation cases now select Rhie–Chow).

§6 re-measures GRAD-002's effect on both cases with those mechanisms removed.

**Procedural requirement (the A1 lesson).** Every remaining criterion was dry-run before this
freeze, including every clause of the carried-over criteria that no earlier run had measured. The
dry-run is recorded in [a2/dryrun.md](a2/dryrun.md) and `a2/logs/dry_*`.

**Stop rule.** Stop at the first failed A2 criterion, record `P12-GRAD-002 BLOCKED / FAILED GATE`,
and never amend A2 after seeing a fresh result.

## 1. Libraries and instruments

| name | what it is | libcfdcore.a |
|---|---|---|
| repo | `build/release`, the authoritative tree | `143a1dda…` |
| cur | isolated copy of the repository sources (`a2/tools/build_cur.sh`) | `143a1dda…`, bit-identical to repo |
| nograd | the same copy with **only** `src/discretization/Gradient.cpp` replaced by the pre-GRAD-002 version (`a2/tools/build_nograd.sh`) | `c74f6ab7…` |
| base | `/root/m7ref/base`, pre-MESH-007 and pre-GRAD-002 (no motion API) | `eaadaa63…` |
| grad001 | `/root/m7ref/grad001`, pre-GRAD-002 with the motion API | `4fa871b8…` |

**nograd is the attribution control** for C8, C10, C11 and §6:

- it differs from cur in one file, so every difference between them is GRAD-002's;
- its fidelity to the literal pre-GRAD-002 operator is itself a criterion: C10-A2(c).

**Instruments** (in `a2/tools/`; their source hashes are frozen with this gate):

| file | role |
|---|---|
| `a2_3d.cpp` | C2-A2 |
| `a2_c9.cpp` | C9 on deformed meshes |
| `a2_c10.cpp` + `compare_c10.py` | C10, C11(a) |
| `run_suite.sh`, `compare_ctest_numbers.py`, `compare_outputs.py` | C8, C11(b) |
| `a2_prodmesh.cpp` | diagnostic |
| `prod_accuracy.sh` | §6 |
| `run_a2.sh` | builds a probe binary keyed by the source and library hashes |

## 2. C2-A2 — replaces C2's "3D deformed" clause

**Why the clause is invalid.** C2's derivation ("the correction vanishes identically for a linear
field once the lagged gradient is converged, leaving plain Green–Gauss with exact face values") has
two premises:

- **P1: the sweeps converge.** Production stops after four sweeps.
- **P2: plain Green–Gauss with exact face values, (1/V) Σ φ(x_f) S_f, is exact for a linear φ.**

P2 is true for planar faces, where x_f is the area centroid. It is **false for a warped (bilinear)
face**: no single point represents ∫_f φ n dS there. An independent 4×4 Gauss quadrature of every
bilinear face (exact for this integrand) gives the defect PG_P of the face representation itself:

- **PG = 1.242e-3, 3.273e-4, 8.291e-5** (relative) on the deformed 8³/16³/32³ meshes;
- the converged gradient error is **1.244e-3, 3.275e-4, 8.292e-5**;
- the same values appear in repo, nograd and grad001.

No version of the code can meet 1e-9 there. P1 fails too: the four-sweep truncation alone is
5.0e-9 on the deformed 8³ mesh with the current library (2.3e-9 before GRAD-002).

The clause is re-derived from the two premises separately. **Q16's C2 clause (≤ 1e-9 at the
production sweep count) is unchanged.**

**Definitions:**

- e_K is the linear-field gradient error after K sweeps, relative to |∇φ|. The field is
  φ = (1.3, −0.9, 0.4)·x + 0.7, with exact Neumann conditions on flat patches.
- The floor is A1's face-rounding term ε·8·max|φ|·max(Σ|S_f|/V), relative.
- The feedback bound: for K ≥ 1 the lagged sweeps obey **e_{K+1} = PG + A e_K** exactly. A
  collects every term that uses the previous gradient:
  - skew-corrected interior faces;
  - oblique Neumann transfer;
  - the GRAD-002 opposite-face correction.

  With every skew weight t ∈ [0, 1], the triangle inequality bounds |e_{K+1,P} − PG_P| cell by cell
  by bound_P(e_K), as defined in `a2_3d.cpp` and in the new test. This is an inequality, not a fit.
- Mesh families:
  - **planar**: faces all planar; graded, skewed, and non-orthogonal at every boundary, with tilted
    patches. Built at s = 1 on 8³, 16³ and 32³, and at s ∈ {1e-6, 1e-3, 0.25, 0.5, 2} on 16³.
  - **warped**: A1's sinusoidal box, A = 0.05 on 8³, 16³ and 32³, and A ∈ {0, 1e-8, 1e-6, 1e-4,
    1e-3, 1e-2, 0.025} on 16³.
  - **2D references**: Q16, and NUM-003's 0.45 h meshes at 10×10 and 20×20.

| id | criterion | dry-run (repo; nograd and grad001 identical where not stated) |
|---|---|---|
| **C2-A2(a)** | **Planar faces: fixed-point exactness.** On every planar mesh, the converged error e_64 is at most the floor. | 6.3e-15 / 1.5e-14 / 3.1e-14 against floors 1.3e-13 / 2.7e-13 / 5.4e-13. The s-sweep: ≤ 2.0e-14 against ≥ 2.5e-13. **PASS**, all libraries. |
| **C2-A2(b)** | **Recursion bound (warp attribution).** On every mesh of all three families, in every cell, for every K = 1..8: \|e_K − PG\| ≤ bound(e_{K−1}) + floor. | **0 violations** on every mesh. Worst ratio 0.992 (repo, warped 32³) and 0.821 (nograd and grad001, planar s = 2). |
| **C2-A2(c)** | **The warp error is second order.** Converged-error order on warped A = 0.05, 8→16 and 16→32: ≥ **1.8**, the second-order threshold C3(b) froze. PG = O(h²) is derived in INV-001 §6. | **1.925, 1.982** |
| **C2-A2(d)** | **Non-vacuity.** (i) max PG exceeds the floor on every warped mesh with A > 0, i.e. the defect is resolved above round-off (the ratio is reported). (ii) The same check with PG := 0, i.e. claiming exact linearity, is **violated** on every warped mesh with A > 0 and **holds** on every planar mesh and at A = 0. (iii) The independent quadrature is self-consistent: \|∫n dS − S_f\|/A ≤ 1e-14, \|V_exact/V − 1\| ≤ 1e-13, and the planar family's warp is ≤ 1e-15. | (i) PG/floor = 9.6e9 / 1.2e9 / 1.6e8 at A = 0.05; 2.6e2 at A = 1e-8, the smallest amplitude. (ii) 3 713 / 30 582 / 251 023 cell-sweeps violated, 31 078–32 768 across the A-sweep, 0 on planar and at A = 0. (iii) ≤ 6.5e-16, ≤ 1.2e-14, ≤ 1.6e-16. |
| C2 (Q16, unchanged) | ≤ 1e-9 at the production sweep count | 3.769e-10 (nograd 1.091e-10). **PASS** |
| *C2-A2(diag)* | *Reported, no pass/fail:* the four-sweep truncation \|e_4 − e_64\| per mesh and library, and on the committed production meshes (`a2_prodmesh`). | See `a2/dryrun.md` §2. GRAD-002 enlarges the four-sweep truncation by up to 4.2× (1.0–4.2× across the meshes). On committed meshes this is ≤ 6.4e-6 and falls with refinement. On a 3D mesh with uniformly tilted patches it does **not** fall with refinement (3.1e-4 against 7.4e-5 before). Recorded as debt, §6. |

## 3. C10-A2 — replaces C10's skewed-mesh sentence

**The first sentence is unchanged:** on meshes with no skewed faces, every interior cell is
bitwise identical to pre-GRAD-002.

**Why the second sentence is invalid.** It reads "interior differences are ≤ 1e-13 relative and
attributable to the existing sweep coupling through boundary cells". The coupling it names makes
the bound impossible:

- On a skewed mesh, an interior face's value after sweep k uses the gradients of its two cells
  after sweep k−1.
- GRAD-002 changes the boundary cells' gradients by O(h). That is the intended change: first to
  second order.
- The next ring therefore changes by the skew weight times that difference, then the next.
- Measured: up to 4.5e-7 (Q16), 8.1e-8 (NUM-003 20×20), 1.4e-6 (deformed 3D) and 1.1e-4 (the
  production 64×8 mesh), confined to layers 2–4.

The derivation also gives the exact replacement:

- The sweep loop runs K = 4 times after the first sum.
- A difference that starts in layer 1 reaches layer k + 1 after sweep k.
- **Every cell in layer ≥ K + 2 = 6 is bitwise identical.**
- Every interior cell's difference is bounded by the coupling of its skewed faces.

| id | criterion | dry-run (repo against nograd) |
|---|---|---|
| **C10-A2(a)** | *(unchanged)* Aligned meshes: every interior cell bitwise identical. | cartesian 16/64, quad plain/dyadic 16 and 64, graded 1.2, cube 8³: **0** non-bitwise cells |
| **C10-A2(b)** | Skewed meshes: (i) every cell of layer ≥ 6 bitwise identical; (ii) every interior cell obeys \|Δg4_P\| ≤ (1/V_P) Σ_f \|S_f\|\|skew_f\| max(\|Δg3_P\|, \|Δg3_N\|) + floor, where Δg is the repo−nograd difference after 3 and 4 sweeps; (iii) t ∈ [0, 1] on every skewed face. Meshes: Q16, the C5 shear at 5e-3 and 5e-9, NUM-003 20×20 at 0.45 h, the production quad 64×8, deformed 3D 8³, and the translated quads 16 and 64 and cube 8³ (round-off skew). The LARGE-offset quad is reported only (outside the valid-geometry domain, C4b). | (i) 0 differing deep cells on every mesh with a layer 6. (ii) **0 violations**, worst ratio 0.458. (iii) 0 out of range. Per-layer maxima are in `a2/dryrun.md` §3. |
| **C10-A2(c)** | **Control fidelity.** nograd against base: every cell of every mesh base can build is **bitwise identical**. | 0 differences on all 15 meshes × 3 fields |
| **C10-A2(d)** | **Instrument non-vacuity.** In a copy of the repo dump, one cell of layer ≥ 6 is moved by one ulp and one layer-2 cell by 1e-3 of the gradient scale. `compare_c10.py --mutate-new` must flag both on every skewed mesh. | Flagged on every skewed mesh: "1/N differ", "violations 1" |

## 4. How the carried-over criteria are measured (definitions, no new thresholds)

- **C9 on the deformed family** (`a2_c9.cpp`). The clauses no earlier run measured are evaluated as
  frozen:

  | clause | frozen requirement | dry-run, repo | dry-run, nograd = grad001 |
  |---|---|---|---|
  | C1 | constant field | A1 rows | — |
  | C2 | linear field | read as C2-A2 | — |
  | **C3(b)** | ∇(x³), exact conditions, 8³→16³→32³: L∞ and L2 order ≥ 1.8, and not below pre-GRAD-002 by more than 0.1 | L∞ 1.814 / 1.935, L2 1.905 / 1.953 | 0.883 / 0.942 and 1.485 / 1.491, **a negative control** |
  | **C4a** | deformed mesh and its translated copy, valid-geometry gate, Δ ≤ E, three fields | all ρ ≤ 0.069 | PASS |
  | **C6-A1** | L = 1e-3, 1, 1e3: every ρ ≤ 1 and spread ≤ 100 | spread ≤ 1.51 | PASS |

  C3(a) does not apply: the mesh is not aligned.
- **C11(a)**, as frozen: aligned meshes, all cells, ≤ 1e-13 relative to the field's gradient scale.
  Dry-run max **5.84e-16**.
- **C11(b).** cur and nograd each run the full suite from the same generated-output state (the W10
  snapshot). `compare_outputs.py` then classifies every generated file as:
  - IDENTICAL;
  - RUNTIME-ONLY (MESH-006's classifier rules, imported unchanged);
  - VALUES.

  **"Exported field"** means a solution-field export: `fields.csv`, `solution.vtk`, and the
  profile/centerline CSVs. For such a file, "relative" means max|Δ| over a column divided by
  max|column|. A printed value cannot resolve changes below one unit of its last printed digit, so
  a difference of at most one such unit counts as within resolution. Requirements:
  - **Aligned-case field exports:** ≤ 1e-6, the frozen bound.
  - **Non-orthogonal / skewed / translated / moving-mesh files:** quantified and explained (A1 §3).
  - **Solver diagnostics** (residual histories, imbalances, iteration counts, final changes): not
    fields. Their changes are reported with absolute magnitudes and must stay at round-off or
    solver-tolerance magnitude on aligned cases.
  - **Scalar validation summaries:** listed and explained per category.

  Dry-run: 114 IDENTICAL, 11 RUNTIME-ONLY, 58 VALUES. The largest aligned field-export change is
  3.2e-14 (`valid_cavity_cli_smoke`). `a2/dryrun.md` §4 has the list.
- **C8.** Printed numbers are compared by `compare_ctest_numbers.py`:
  - cur against nograd, verbose ctest;
  - wall-clock values masked (unit-suffixed numbers, and table columns whose header names a time);
  - each changed test categorized as **A** (non-orthogonal, skewed, translated or moving mesh:
    intended) or **B** (aligned mesh: round-off or solver-tolerance magnitude, since C11(a) bounds
    the aligned boundary-gradient change at 6e-16).

  **Each phase's own frozen thresholds** are the assertions in the tree, and they must all pass. Two
  sets were re-derived by gates frozen earlier, and the re-derived versions apply:
  - MESH-001's and MESH-003's grid-convergence assertions: the DIFF-002 W8B amendment,
    `results/p12-diff-002/w8b/acceptance_gate.md`;
  - NUM-002's distorted-mesh order band: `val-001/acceptance_gate.md`.

  Dry-run: 1 913 tests SAME, 53 CHANGED, 2 TEXT (the two tests nograd fails).
- **C13.** The new test file has 9 tests; their outcomes are pre-registered in §5. Then the full
  regression: Release, Debug + GUI, and ASan + UBSan at CI settings. The P12-MESH-004
  `DisconnectedMeshIsFatal` exception no longer applies, because P12-ASAN-001 fixed that test, so
  **0 failures and 0 sanitizer diagnostics** are required.
- **C14.** `clang-format-18` over the changed files, then a rebuild. **The library must stay
  `143a1dda…`**, since A2 changes no production code. Every fresh result above comes from that
  library.

## 5. Pre-registered outcomes of the fresh execution

| run | expected |
|---|---|
| `a2_3d`, repo / nograd / grad001 | C2-A2(a)–(d) PASS for all three: they are consistency properties of the lagged scheme, not GRAD-002 detectors |
| `a2_c9`, repo | C3(b), C4a and C6-A1 PASS |
| `a2_c9`, nograd / grad001 | **C3(b) FAIL** (first-order boundary); C4a and C6-A1 PASS |
| `a2_c10`: repo vs nograd; nograd vs base; self-test | C10-A2(a), (b), (c) and (d) as in §3; C11(a) PASS |
| new test file, standalone: repo / nograd | 9/9 / 7/9. **Exactly these fail** on nograd: `TranslatedMeshGivesTheSameGradient` (quadratic rows, 7.8125e-3 at 16²) and `GradientErrorIsContinuousInTheBoundaryMisalignment` (first step) |
| cur suite (with the new file) | 100 %: the W10 count plus 9 |
| nograd suite (with the new file) | **exactly 4 failures**: `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`, `AlePisoGalilean.TranslatingCavityIsTheFixedCavityPlusTheTranslation`, and the two new tests named above |
| C8 / C11(b) comparison | as characterized in §4; every change in category A or B |
| full regression in repo | Release, Debug + GUI and ASan + UBSan all at 100 %; 0 sanitizer diagnostics; format clean |
| production accuracy (§6), cur vs nograd | reported. A change larger than the W8B iterative uncertainty (10 % of the error) must be explained, or it fails as an unexplained regression |

## 6. Production accuracy and recorded findings

**The production-accuracy obstacle** (INV-001; reported, `prod_accuracy.sh`). Dry-run with the
committed settings at the W8 grids, cur against nograd:

| quantity | change (cur against nograd) |
|---|---|
| StructuredQuad velocity L2 | −0.02 %, −0.02 %, −0.02 % |
| StructuredQuad dp/dx error | −0.09 %, +0.28 %, −1.29 % |
| MultiBlock velocity L2 | −0.64 %, −0.06 %, +0.12 % |
| MultiBlock G error | +0.04 %, +0.06 %, +0.09 % |
| iterations | +3 to +5 |
| continuity | ~1e-12 in both |

INV-001's degradations are gone: dp/dx error +16 % and velocity error +13 % against pre-GRAD-002,
iterations ×4.1, and a non-monotone radial rise.

**Recorded, not fixed (outside A2's scope; no frozen criterion or committed case depends on it).**
The production gradient stops after a fixed four sweeps (`kGreenGaussSkewCorrectionSweeps`,
chosen empirically in NUM-003). GRAD-002's opposite-face transfer adds a term to the lagged
feedback operator, which enlarges the four-sweep truncation by up to 4.2×.

- The converged fixed point stays exact (C2-A2(a)).
- On the committed 2D meshes the extra truncation is negligible and falls with refinement:
  ≤ 6.4e-6 at 64×8, 7e-10 at 512×64.
- On a 3D mesh with uniformly tilted boundary patches, the four-sweep truncation does not fall
  with refinement: 3.1e-4 (7.4e-5 before GRAD-002, already non-zero).
- The complete fix is a convergence-controlled sweep loop. That changes every skewed-mesh result
  and is a scope decision for the user.
- Separately, NUM-003's header comment ("4 keeps the worst measured linear-field error ≤ ~1e-9")
  does not hold with Neumann boundaries even before GRAD-002: 1.4e-8 at 0.45 h, 10×10.

## 7. Execution order

1. Freeze this gate, `a2/dryrun.md`, the instruments and the candidate test file
   (`a2/tools/freeze.sh` → `a2/logs/00_freeze.log`). This happens only after W10 (P12-DIFF-002)
   has finished, and it touches no build input while W10 runs.
2. Add the test file to `CMakeLists.txt`, apply `clang-format-18`, rebuild `build/release`, check
   that the library is `143a1dda…` and that no binary is stale, then run the new tests and
   `CFDDiscretizationTests`.
3. Run the fresh probes: `a2_3d`, `a2_c9`, `a2_c10` with its comparisons, `a2_prodmesh`
   (diagnostic) and `prod_accuracy` (report).
4. Copy the repository's `tests/` and `cases/` (with the test file and `CMakeLists.txt`) into cur
   and nograd, rebuild both (the two libraries must keep their hashes), run both suites, then the C8
   and C11(b) comparisons (`a2/tools/fresh.sh rebuild`, then `suites`).
5. Run the full regression in the repository.
6. Close out: `a2/summary_a2.md`, TODO, ROADMAP. Then, and only then, the original frozen
   MESH-007 G6.3.
