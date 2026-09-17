# P12-GRAD-002 — Continuous Green–Gauss Boundary Treatment

## Final status (2026-09-17): **COMPLETE**

Every criterion of the frozen gate `acceptance_gate.md` (`a46973ed…`) is resolved, as amended by
A1 (`353b72ef…`), A2 (`ea88f10f…`) and A3 (`f4d579fa…`).

- **Where a criterion was re-derived,** the amendment was dry-run, frozen and hashed before its
  fresh run, and each earlier failure is kept on record.
- **Nothing is committed or pushed.**
- **The first-run report further down is kept unchanged.** Its sha256 was `67b03afc…` (Git Bash
  hash, with `*` marker) before this section was prepended; A1's freeze log records the same value.
- **Final evidence:** [a3/summary_a3.md](a3/summary_a3.md) (A2 and A3 execution, full regression,
  C14 re-verification) and [a1/summary_a1.md](a1/summary_a1.md).
- **Supporting records:** [investigation/summary.md](investigation/summary.md) (INV-001),
  [drift-001/summary.md](drift-001/summary.md) and [val-001/](val-001/).

| criterion | final outcome | where |
| --- | --- | --- |
| C1 constant fields | **original FAILED** (this report, below). **C1-A1 PASS**, and rerun bit-identical on the final library | a1/, a3 §4b |
| C2 linear fields | Cartesian and Q16 PASS (Q16 3.769e-10 ≤ 1e-9). The "3D deformed" clause **FAILED under A1**. **C2-A2 PASS**: planar fixed point exact on all 3 libraries; recursion bound with 0 violations; warp order 1.925 / 1.982; non-vacuity shown | a1/, a3 §2 |
| C3 Cartesian second order | (a) PASS. (b) PASS: 2D, and 3D deformed L∞ 1.814 / 1.935 and L2 1.905 / 1.953. Both pre-GRAD-002 controls fail (0.883 / 0.942), as pre-registered. Rerun bit-identical on the final library | a1/, a3 §2, §4b |
| C4 translation invariance | **original FAILED** at the large offsets. **C4a PASS**. C4b is recorded as a pre-existing geometry limit | a1/, a3 §4b |
| C5 continuity sweep | PASS: Lipschitz quotient 1.563e-02, constant across nine decades. The negative control fails, as required | a1/, a3 §4b |
| C6 scale invariance | **original not satisfied as frozen** (it has the same ε·φ/h floor as C1). **C6-A1 PASS** | a1/, a3 §2, §4b |
| C7 static cavity translation | PASS: 1.425e-13 (16²) and 5.206e-13 (32²) on the final library, against 1e-9. Before GRAD-002: 2.487e-02 | a1/, a3 §4b |
| C8 previous phases | PASS: 1932/1932. Of the executed tests, 1875 print identical numbers; 53 changed and 4 text-changed tests are all categorized; 0 unexplained | a3 §2 |
| C9 3D | PASS under C2-A2 (deformed) and on translation | a1/, a3 §2 |
| C10 interior cells | (a)(b)(c) PASS. **C10-A2(d) FAILED** (self-test design error). **C10-A3(d)(e) PASS**: self-test 30/30, and the 3-sweep operator mutant is rejected where derived | a3 §2 |
| C11 aligned equivalence and outputs | (a) PASS (5.84e-16 ≤ 1e-13). (b) PASS: aligned exports within 1e-6 or one printed unit; every VALUES file categorized | a3 §2 |
| C12 no geometric threshold branch | PASS (A1 audit; `Gradient.cpp` unchanged since, `d24882a9…`) | a1/audit.md |
| C13 tests and regression | PASS. The 9 new tests pass; the controls fail exactly as pre-registered. Release 1932/1932, Debug + GUI 1984/1984, ASan + UBSan 1932/1932, with 0 ASan, 0 UBSan and 0 LSan diagnostics and 0 timeouts | a3 §4 |
| C14 format and hash discipline | PASS. clang-format 0 of 568. Release library `143a1dda…` unchanged. The A1-form criteria were rerun on the final library after DIFF-002 changed it | a3 §4, §4b |

**Production accuracy.** The obstacle INV-001 recorded is resolved. With DIFF-002's second-order
wall flux in place and the W8 cases on Rhie–Chow, GRAD-002 changes the two W8 cases by ≤ 1.3 %
(a3 §2).

### Production changes of the phase

These are in the working tree and uncommitted.

- `src/discretization/Gradient.cpp`: the continuous boundary face-value correction. It removes the
  paired treatment's alignment and equal-area tests and `obliqueNeumannFace`'s exact-zero test.
- `include/cfd/mesh/MeshGeometry.hpp` and `src/mesh/MeshGeometry.cpp`: `FaceAlignment` and
  `boundaryFaceAlignment` are deleted, and `BoundaryLineIntersection` /
  `boundaryLineIntersection` are added.
- DRIFT-001: `"face_flux": "rhie_chow"` in the `solver.json` of `poiseuille_distorted` and
  `curved_channel_multiblock`, plus one sentence in each `case.json`. This is recorded with DIFF-002
  W8B.

### Tests added or migrated

- New: `tests/unit/discretization/test_gradient_boundary_consistency.cpp`, 9 tests (C13), added to
  `tests/unit/discretization/CMakeLists.txt`.
- Migrated under VAL-001's own frozen gate: one assertion in
  `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`
  (`tests/unit/discretization/test_grid_refinement.cpp`).

### Preserved failures

- P12-GRAD-001: its gate failure (`results/p12-grad-001/`).
- The original GRAD-002 gate: C1, and C4 at the large offsets (this report, below).
- A1: C2/C9 "3D deformed" (`a1/summary_a1.md`); it reproduces bit for bit in `a3/logs/c14_a1_3d_final.log`.
- A2: C10-A2(d) (`a2/logs/fresh_c10_selftest_mutated.log`, `a3/summary_a3.md` §2).
- INV-001's production-degradation findings (`investigation/`).
- **Invalid and fail-closed runs, kept under their own names:**
  - `a2/logs/*.INVALID-*`: the stale-binary check failed closed, then the comparisons read the
    dry-run's ctest output;
  - `a2/logs/a3_suite_*.FAIL-CLOSED-*`.

### Findings carried forward (debt, not fixed here)

- **The fixed four-sweep Green–Gauss loop** (classified **B — technical debt**, a3 §5). It does not
  converge on 3D meshes with uniformly tilted boundaries.
- **Warped 3D faces.** A single stored centroid is O(h²) on a warped face (C2-A2).
- **Large-coordinate geometry.** `createStructuredQuad2D` breaks down beyond X/h ≈ 1e3 (C4b).
- **The 2D Linear face flux has an odd-even pressure mode** (DRIFT-001).

---

# Historical first-run report (2026-09-16), unchanged

# P12-GRAD-002 — Continuous Green–Gauss boundary treatment

Status: **BLOCKED / FAILED GATE**. The first failed pre-registered criterion is **C1** (constant-field
gradient on the L = 1e-3 mesh: 6.939e-12 against a frozen bound of 3.6e-13); **C4** also fails, at the
large-coordinate offsets. Both failures are **mis-derived thresholds in the gate I froze**, and both
are reproduced **identically by the unmodified pre-GRAD-002 library**, which is the proof that they
are not properties of the new formulation.

The formulation itself is verified and is a large improvement:

| quantity | pre-GRAD-002 | GRAD-002 |
| --- | --- | --- |
| translated-mesh boundary gradient, quadratic field, 2D 16² | **8.065e-03** | **3.027e-14** |
| same, 64² / 256² | 1.969e-03 / 4.892e-04 | 4.620e-13 / 7.276e-12 |
| quadratic field vs analytic, **all cells**, exact Cartesian 2D and 3D | not measurable (branch-dependent) | **0.000e+00** |
| continuity: d(error)/d(m_f) over m_f = 5e-13 … 5e-3 | **1.5e+10** at the first step, then a plateau | **1.563e-02, constant over nine decades** |

Per the authorization's stop rule the original MESH-007 G6.3 was **not** rerun, G6.3 is unmodified,
MESH-007 stays **BLOCKED / FAILED GATE**, and no threshold was changed after seeing a result.

## 1. Chronology (preserved)

```text
MESH-007 G6.3 failure
        ↓
pre-existing Green–Gauss defect identified
        ↓
GRAD-001 authorized
        ↓
hard tolerance formulation attempted
        ↓
GR1 failed
        ↓
GR3/GR6 shown mutually incompatible
        ↓
GRAD-001 stopped
        ↓
GRAD-002 separately authorized
        ↓
gate + derivation frozen before any source change
        ↓
continuous formulation implemented and verified (C2, C3a, C5 pass)
        ↓
C1 and C4 failed as frozen — both thresholds mis-derived, both reproduced on the baseline
        ↓
GRAD-002 stopped; G6.3 not rerun
```

`results/p12-grad-001/` is unchanged; its gate, its summary and its three failure logs are hashed in
[logs/00_gate_freeze.log](logs/00_gate_freeze.log). GRAD-001 is **not** re-scored or rewritten as
successful. Its *code* is superseded: the tolerance predicate it added is gone from the tree, which
is the point of this phase.

## 2. Freeze

[logs/00_gate_freeze.log](logs/00_gate_freeze.log), 2026-09-16T03:16:32Z, git HEAD `b66310ca`:

- `acceptance_gate.md` sha256 **`a46973ed5ba4190a008a1c3f3138f21612a6b1ca0f11ca32deb6d0899602d2eb`**
- `formulation.md` sha256 **`5ca61d236ff473e866b1c52cd7ee7785ee1c88427ff6af35782176d0d5e266a8`**
- production source at freeze time (the GRAD-001 state) hashed, and the pre-GRAD-002 library
  snapshotted to `$HOME/m7ref/grad001` so every comparison below is reproducible after the rebuild.

Frozen **before** `src/discretization/Gradient.cpp`, `src/mesh/MeshGeometry.cpp` or
`include/cfd/mesh/MeshGeometry.hpp` were touched.

## 3. The formulation (see `formulation.md` for the full derivation)

Green–Gauss is exact for the volume-averaged gradient whenever every face value is exact, so the
boundary defect is in the **face value**, not in the sum. The old treatment *replaced* the
{boundary face, opposite face} contribution pair with a one-dimensional fit along the boundary
normal — valid only for parallel, equal-area face pairs, which is why it had to be selected by
geometric tests whose two branches differ at **leading** order (second- versus first-order boundary
gradient). GRAD-002 instead removes the leading O(h²) interpolation bias from the opposite face's
value:

```text
t*   = ((x_P − x_B) · n̂_B) / (ξ · n̂_B),   x_B′ = x_P − t* ξ,   ξ = (x_F − x_P)/L
φ_B′ = φ_B + g_P · (x_B′ − x_B)
∂²φ/∂ξ² ≈ 2 [ (φ_B′ − φ_P)/t* + (φ_F − φ_P)/L ] / (t* + L)
φ_O(for P) = φ_O − ½ w(1−w) L² ∂²φ/∂ξ²
```

with the third fit point **on the boundary plane** rather than at the face centroid — the step that
makes it geometrically general. Properties, all derived before implementation and confirmed below:

- **the same linear functional** as the old paired fit in the aligned limit (both are linear in
  (φ_B, φ_P, φ_F) and exact on {1, x, x²}, and a linear functional exact on a 3-dimensional space is
  unique), so the established Cartesian second-order accuracy is retained by construction;
- **continuous**: every term is rational in the coordinates with denominators bounded by mesh
  validity, so dm → dm + δ moves the gradient by O(δ);
- **no geometric cutoff**: both of the old predicates are deleted.

Changed files: `src/discretization/Gradient.cpp` (the treatment, the sweep, and
`obliqueNeumannFace`'s exact-zero test), `src/mesh/MeshGeometry.cpp` +
`include/cfd/mesh/MeshGeometry.hpp` (`FaceAlignment`/`boundaryFaceAlignment` deleted,
`BoundaryLineIntersection`/`boundaryLineIntersection` added). Final hashes in §8.

## 4. C1 — FAILED as frozen

C1 required, for φ ≡ 1, `max |∇φ| ≤ max(1e-13, 100 η)` on every gate mesh.

| mesh | h | measured \|∇φ\| | frozen bound | dimensional floor ε·φ/h |
| --- | --- | --- | --- | --- |
| 2D 16², L = 1 translated | 6.25e-02 | 1.421e-14 | 7.1e-13 | 8.882e-15 |
| **2D 16², L = 1e-3 translated** | 6.25e-05 | **6.939e-12** | **3.6e-13** | **8.882e-12** |
| 2D 16², L = 1e3 translated | 6.25e+01 | 0.000e+00 | 7.1e-13 | 8.882e-18 |

[logs/04](logs/04_diagnostics_new.log). The measurement sits **at** its floor, not above it: a
Green–Gauss gradient of a constant field is `φ·Σ S_f/V`, the face closure `Σ S_f` is exactly zero
here (measured `closure/V = 0.000e+00` on every mesh), and what remains is the round-off of the
interpolated face values, ~ε·φ/h. That floor **grows as the mesh shrinks**, and C1 bounded it by an
absolute constant. At L = 1e-3 the bound is 25× below the floor, so no implementation can satisfy it.

**Proof that this is not the new code:** the pre-GRAD-002 library gives the identical interior value,
6.939e-12 ([logs/05](logs/05_diagnostics_base.log)) — and interior cells never touch the new
correction at all.

## 5. C4 — FAILED as frozen, at the large offsets only

C4 required agreement ≤ `max(1e-13, 200 ε (X/h))` between a mesh and its translated copy, and
explicitly included the large offset (1234.5678, 987.6543).

**Where it passes** ([logs/01](logs/01_probe_new.log)) — every physically sane mesh, at the derived
law, for all three fields:

| mesh (offset) | X/h | boundary difference | bound |
| --- | --- | --- | --- |
| 2D 16² small | 1.61e+01 | 3.03e-14 | 7.14e-13 |
| 2D 64² small | 6.43e+01 | 4.62e-13 | 2.86e-12 |
| 2D 256² small | 2.57e+02 | 7.28e-12 | 1.14e-11 |
| 2D 16²–256² dyadic | — | **0.000e+00** | — |
| 2D 16² L = 1e3 | 1.61e+01 | 0.000e+00 | 7.14e-13 |
| 2D Q16 distorted, small | 1.76e+01 | 1.50e-14 | 7.82e-13 |

That is GRAD-001's failing quantity improved by 370× at 64² (2.678e-09 → 7.28e-12 at 256²), sitting
on the predicted linear-in-(X/h) law, with dyadic offsets exactly zero.

**Where it fails** — the large offset, at every resolution:

| mesh | interior difference | boundary difference | bound |
| --- | --- | --- | --- |
| 2D 16² LARGE, linear | 5.960e-08 | 5.961e-08 | 8.79e-10 |
| 2D 64² LARGE, quadratic | 2.917e-04 | 2.912e-04 | 3.69e-09 |
| 2D 256² LARGE, linear | 3.639e-01 | 8.530e+00 | 1.41e-08 |

**Proof that this is not the new code, twice over:**

1. **Interior cells fail identically to boundary cells** (5.960e-08 vs 5.961e-08), and interior cells
   use an unchanged code path.
2. The pre-GRAD-002 library produces the **same interior numbers to every printed digit**
   (5.960e-08 at 16², 3.639e-01 at 256²; [logs/02](logs/02_probe_base.log)).

The cause is the mesh generator's own geometry at extreme coordinate-to-size ratios
([logs/04](logs/04_diagnostics_new.log)): the shoelace centroid and area lose their significance to
cancellation.

| mesh | X/h | cell-volume error | centroid error / h |
| --- | --- | --- | --- |
| 2D 16² small | 1.61e+01 | 2.8e-14 | 2.2e-13 |
| 2D 256² small | 2.57e+02 | 1.1e-11 | 1.0e-09 |
| 2D 16² LARGE | 1.98e+04 | 5.96e-08 | 5.0e-04 |
| **2D 256² LARGE** | 3.16e+05 | 1.53e-05 | **4.119** |

At 256² with that offset the computed cell centroids are wrong by **four cell widths**: the mesh is
numerically meaningless, and no gradient formulation defined on it can be translation-invariant.
C4's error was to apply a law derived for the *formulation's* sensitivity to meshes whose *inputs*
are destroyed — the same species of mistake as GRAD-001's GR1, in a different guise.

## 6. C5 — PASSED, and it is the phase's central result

A controlled family shears the strictly interior vertices of a Cartesian 16² mesh so that the
boundary misalignment sweeps 0 → 5e-3 while the domain boundary, the patch normals and therefore the
exact boundary values are untouched. Criterion: `|Δe| ≤ 10 Δm + 1e-12`.

**GRAD-002** ([logs/07](logs/07_sweep_corrected_new.log)) — every step passes, and the Lipschitz
quotient is *constant* across nine decades:

```text
m_f 0.000e+00  error 0.000e+00
m_f 5.116e-13  error 8.013e-15   quotient 1.566e-02  PASS
m_f 5.001e-11  error 7.834e-13   quotient 1.567e-02  PASS
m_f 5.000e-09  error 7.813e-11   quotient 1.562e-02  PASS
m_f 5.000e-07  error 7.813e-09   quotient 1.562e-02  PASS
m_f 5.000e-06  error 7.813e-08   quotient 1.563e-02  PASS
m_f 5.000e-05  error 7.813e-07   quotient 1.563e-02  PASS
m_f 5.000e-04  error 7.813e-06   quotient 1.563e-02  PASS
m_f 5.000e-03  error 7.822e-05   quotient 1.565e-02  PASS
```

The error is exactly m_f/64 throughout: proportional to the geometric perturbation, with no jump
anywhere, and exactly zero at zero misalignment.

**Negative control — the pre-GRAD-002 library** ([logs/08](logs/08_sweep_corrected_base.log)) fails
the bound at the very first step, as the criterion required it to:

```text
m_f 0.000e+00  error 6.280e-16
m_f 5.116e-13  error 7.813e-03   quotient 1.527e+10  FAIL
m_f 5.001e-11  error 7.813e-03   ... plateau at 7.813e-03 for every larger m_f
```

A misalignment of 5e-13 — pure round-off — costs a factor of 1.2e+13 in boundary-gradient error, and
the error then *stops depending on the misalignment at all*, because the second-order treatment has
been switched off wholesale. That is the MESH-007 G6.3 defect isolated in a single measurement, and
GRAD-002 removes it.

### 6.1 An instrument defect, found and corrected

The first version of this sweep sheared the boundary vertices too. Above amplitude ≈1.4e-6 that
tilted the left/right boundary faces, my patch classifier stopped recognising them as planes of
constant x, and silently substituted a Neumann condition for the exact Dirichlet one — producing a
spurious O(1) "jump" at m_f = 1e-5 **in all three libraries, the unmodified baseline included**
([logs/04](logs/04_diagnostics_new.log), [05](logs/05_diagnostics_base.log),
[06](logs/06_diagnostics_grad001.log), preserved). The fix was to the instrument, not to any
threshold: shear only strictly interior vertices, and make the probe report whether every prescribed
boundary value is exact, so such a substitution can never again be mistaken for a discontinuity of
the discretization. C5's bound is unchanged from the freeze.

## 7. C2, C3, C6 and the criteria not reached

- **C2 (linear fields) — PASS.** ≤ 3.13e-14 on every Cartesian variant, all cells; 3.77e-10 on Q16
  (bound 1e-9); 2.55e-15 on 3D 8³ ([logs/01](logs/01_probe_new.log)).
- **C3(a) (Cartesian second order retained) — PASS, exactly.** The quadratic field's gradient is
  **0.000e+00 in all cells**, boundary-adjacent included, on 2D exact Cartesian, 2D shoelace
  Cartesian and 3D 8³ — measured with exact per-patch boundary values, which the old
  branch-dependent treatment could not be tested against. On Q16 the boundary cells are now the same
  order as the interior (7.01e-04 all cells vs 5.83e-04 interior) instead of O(h) worse.
- **C3(b)** (refinement orders) — **NOT RUN**.
- **C6 (scale invariance)** — L = 1e3 exact (0.000e+00); L = 1e-3 carries C1's ε·φ/h floor, so the
  criterion is not satisfied as frozen for the same reason as C1.
- **C7 (static cavity reproducer), C8 (previous-phase verification), C9 (3D), C10 (interior
  bit-identity), C11 (aligned-Cartesian equivalence and case outputs), C13 (focused tests, full
  regression, sanitizers) — NOT RUN**: the gate stops at the first failure. Nothing about them is
  claimed.
  - The probe's interior columns do agree with the baseline to every printed digit on every mesh
    (e.g. 3.463e-14, 1.263e-13, 4.746e-13, 1.859e-12, 1.096e-11 at 16²–256²), which is *consistent
    with* C10 but is not the bitwise check C10 specifies.
- **C12 (no geometric threshold branch remains)** — the audit is factual and complete:
  `tryPairedBoundaryContribution`'s alignment and equal-area tests are gone,
  `FaceAlignment`/`boundaryFaceAlignment` are deleted, and `obliqueNeumannFace`'s
  `cross(d, sf) == Vector3{}` is replaced by a test on the tangential offset that the general formula
  itself needs, whose two sides are bit-identical (d·n̂ == |d| because `sqrt(x*x) == |x|` for every
  normal double, and the transfer term is exactly 0.0) — an iteration trigger, not a choice of
  formulation. The one remaining exact-zero geometric test in the boundary path,
  `decomposeAreaVector`'s exact-parallel short-circuit (`src/mesh/MeshGeometry.cpp:163`), is in the
  documented benign class: its two branches agree exactly at the crossing (`formulation.md` §6).
- **C14 (formatting and hashes) — done.** `clang-format` is clean over all three changed files
  (which also cleared the five pre-existing MESH-007 violations in `MeshGeometry.cpp`), and the
  rebuilt library is **byte-identical** to the one every log above was produced with, so no evidence
  needed reproducing.

## 8. State of the tree

Final hashes (post-format, byte-identical binary):

```text
d24882a96e5cbc0025121a71  src/discretization/Gradient.cpp
0173e7e792b7e81b6fa3b691  src/mesh/MeshGeometry.cpp
30034b62dbf79fe662d974db  include/cfd/mesh/MeshGeometry.hpp
51e82ee8e0ed16363169b929  build/release/src/libcfdcore.a
```

The implementation is left in place, uncommitted. No ALE code was touched, MESH-007 G6.3 is
unmodified, no later phase was started, and nothing was committed or pushed. The unrelated
pre-existing P12-MESH-004 sanitizer defect (`MeshQualityReport.DisconnectedMeshIsFatal`,
use-after-free in `tests/unit/mesh/test_mesh_quality_report.cpp:487-489`) is untouched and remains a
pre-push CI blocker.

## 9. Decision

**P12-GRAD-002 BLOCKED / FAILED GATE.** How to proceed is the user's decision:

- **(a) Re-derive C1, C4 and C6 as a disclosed amendment and rerun this gate.** The corrected
  statements follow from the measurements above, and each is dimensionally sound where the frozen one
  was not: C1 on `|∇φ| h / |φ| ≤ k ε` rather than on `|∇φ|`; C4's mesh list restricted to
  X/h ≲ 1e3, where the generator's own centroid error stays below ~1e-9 h, with the large-coordinate
  behaviour recorded as a **separate, pre-existing mesh-generator limitation** rather than a
  boundary-gradient criterion; C6 likewise normalized. The evidence above already meets all three in
  their corrected form, but they were frozen wrongly, so rerunning them needs explicit
  authorization. This is the smallest path to resuming MESH-007.
- **(b) Authorize a separate phase for the large-coordinate mesh-generator limitation.** Centroids
  wrong by four cell widths at X/h ≈ 3e5 is a real defect of `createStructuredQuad2D`'s shoelace
  formulas (fixable by accumulating about a local origin), independent of any gradient work and
  invisible to every current case.
- **(c) Revert the formulation and leave MESH-007 blocked.** The measurements in §4–§7 would be lost
  as working code, though they stay on record here.

Two gates in a row have now failed on my own threshold derivations while the implementation under
test was sound. The procedural lesson is recorded: the gate probe must be **dry-run against the
unchanged baseline before freezing**, because every criterion that failed here — C1's constant-field
floor, C4's large-offset rows, and the instrument defect of §6.1 — would have shown up immediately as
the *baseline* failing a criterion it should trivially satisfy.
