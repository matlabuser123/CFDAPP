# P12-DIFF-002 — Second-Order Dirichlet Boundary Diffusion

## Final status (2026-09-17): **COMPLETE**

Every requirement of the frozen gate `acceptance_gate.md` (`51079f6d…`) is resolved, W1–W10. Where
a criterion was re-derived, the amendment was frozen before its run and its failures are preserved.

- **Nothing is committed or pushed.**
- **The first-run decision report further down is kept unchanged.** Its sha256 was
  `bf71a0f4…` (Git Bash hash, with `*` marker) before this section was prepended.
- **Chronology:** gates and amendments, in the order listed in the record table below.
- **Final evidence:** [w8b/summary.md](w8b/summary.md), [w9/summary_w9a.md](w9/summary_w9a.md),
  [w10/summary.md](w10/summary.md), [lowmach-001/summary.md](lowmach-001/summary.md) and
  [format-001/summary.md](format-001/summary.md).

| criterion | frozen requirement | final outcome | where |
|---|---|---|---|
| W1a–W1d | wall-flux exactness and order; curved radial check | PASS (≤ 1.9e-14; 1.5902e-16; order 1.963–2.000; 3.3578e-4) | logs/07, re-checked by A2 (a2/) |
| W2 | translation / scale invariance | PASS (3.614e-18) | logs/07 |
| W3a | higher-order stencil everywhere it applies | PASS (19/19) | logs/07 |
| W3b | degenerate-mesh fallback | **original FAILED** (this report, below). W3b-A1 (`acceptance_gate_A1.md`) PASS, with an independent topology oracle | a1/, a2/ |
| W4 | hand-derived matrix | PASS (13/13); its baseline block was migrated in A5 | logs/10, validation-migration/ |
| W5 | iterations ≤ 1.25× recorded | PASS (1.1554×, A2-5) | a2/ |
| W6 | 144×18 dp/dx error ≤ 0.747 % | PASS (0.207 %, A2-6) | a2/ |
| W7 | focused verification against each phase's own thresholds | Round 1 FAILED; A2-7, A3 and the first A6 attempt also failed. All preserved. The failures were resolved by production fixes and migrations: ThermalInterface fix, ROB-001, A5/A6, UC-001, UF-001, GRAD-002 VAL-001, LOWMACH-001, and W8 itself via W8B. Round 2 ran 1923 / 1920 passed; its 3 failures were those later resolved. W10 now passes **every** suite | final-w7/, w10/ |
| W8 | the two historical grid-convergence tests, unchanged | **original FAILED** (w8/); W8-INV-001 found no production defect; **W8A stopped before execution** (w8a/). **W8B PASS**: 15/15 from a clean build, and the controls reproduce (15 / 13 / 10 / 7 / 6) | w8/, w8-inv-001/, w8a/, w8b/ |
| W9 | committed-case compatibility: mass not worse, changes explained | **original FAILED** (the literal "not worse" is below the solve's resolution). **W9A PASS**, 19/19. W9A-6 rejects the mutant on 2 of 3 open-channel cases; the third is below its floor (disclosed) | w9/ |
| W10 | Release + Debug/GUI exact counts, sanitizers at CI settings, format clean | **PASS**: Release 1923/1923; Debug + GUI 1975/1975; ASan + UBSan 1923/1923 with 0 ASan, 0 UBSan and 0 LSan diagnostics and 0 timeouts; clang-format 0 of 567 | w10/ |

### Production changes of the phase

All of them are in the working tree and uncommitted.

**The reconstruction:**

- `include/cfd/discretization/NonOrthogonalDiffusion.hpp` and
  `src/discretization/NonOrthogonalDiffusion.cpp`: the one-sided second-order Dirichlet wall flux,
  independent of `non_orthogonal_corrections` (A2).
- `include/cfd/mesh/MeshGeometry.hpp` and `src/mesh/MeshGeometry.cpp`: `boundaryInwardStencil`.

**The call sites:**

- `src/physics/MomentumEquation.cpp`
- `include/cfd/thermal/EnergyEquation.hpp`, `src/thermal/EnergyEquation.cpp` (a shared helper)
- `src/species/SpeciesEquation.cpp`
- `src/turbulence/KEpsilonEquation.cpp`

**Defects found and fixed inside the phase:**

- `src/thermal/ThermalInterface.cpp`: the conjugate wall flux still used the two-point formula
  (thermal-interface-fix/).
- `src/solver/SolverRobustness.cpp`: the divergence-detector floor (rob-001/).

**Enabling the W8 case validation.** P12-GRAD-002-DRIFT-001 added `"face_flux": "rhie_chow"` to the
`solver.json` of `poiseuille_distorted` and `curved_channel_multiblock`, and one sentence to each
`case.json`. See `results/p12-grad-002/drift-001/`.

**Layout only (FORMAT-001, proven).** The library stays bit-identical (`143a1dda…`) across the
formatting of:

- `NonOrthogonalDiffusion.cpp`, `MeshGeometry.cpp`;
- the MESH-007 files `Mesh.cpp`, `MeshMotion.cpp`, `AlePISO.cpp`, `PISO.cpp`.

### Tests added or migrated

Each item has its own frozen gate or evidence.

- **New:**
  - `tests/unit/discretization/test_boundary_reconstruction.cpp`
  - `tests/unit/thermal/test_conjugate_boundary_equivalence.cpp`
  - `tests/support/BoundaryFluxProbe.hpp`
- **Migrated obsolete instruments** (A5/A6, validation-migration/):
  - `test_energy_equation.cpp`, `test_energy_equation_variable_properties.cpp`,
    `test_sparse_assembly3d.cpp`, `test_thermal_boundary_consistency.cpp`
  - `test_momentum_diffusion.cpp`, `test_momentum_variable_viscosity.cpp`
  - `test_species_equation.cpp`, `test_species_conservation.cpp`
  - `test_simple_robustness.cpp`
  - `MMSCases.{hpp,cpp}`, `test_mms_momentum.cpp`, `test_mms_simple.cpp`
- **ROB-001:** `test_solver_robustness.cpp`
- **UC-001:** `PoiseuilleValidationUtils.{hpp,cpp}`, `test_poiseuille_production_validation.cpp`
- **UF-001:** `test_natural_convection_validation.cpp`
- **A3 and W8B:** `test_structured_quad_production_case.cpp`, `test_multiblock_production_case.cpp`
- **LOWMACH-001:** `test_low_mach_regression.cpp`
- **CMakeLists:** `tests/integration/poiseuille`, `tests/integration/species`, `tests/unit/thermal`

GRAD-002 VAL-001 migrated `test_grid_refinement.cpp` under its own gate. P12-ASAN-001 fixed
`test_mesh_quality_report.cpp` under its own authorization.

### Preserved failures

These are never rewritten as passes:

- W3b (original);
- A1's W7, A2's W7, and A3 at A3-5;
- the A4 inventory;
- the first A6 attempt;
- W7 round 1;
- the original W8;
- W8A (stopped);
- the original W9.

### Findings carried forward (debt, not fixed here)

- **The 2D default face flux (Linear) has an undamped odd-even pressure mode on open domains**
  (DRIFT-001). Only the two W8 validation cases were switched to Rhie–Chow. Making Rhie–Chow the 2D
  default is a scope decision for the user.
- **The fine-grid instability of the one-correction recipe on smooth non-orthogonal meshes**
  (MESH-004 debt, DRIFT-001 §4).
- **W9A-6's sensitivity limit on `poiseuille_flow`.**

---

# Historical first-run decision report (2026-09-16), unchanged

# P12-DIFF-002 — Second-Order Dirichlet Boundary Diffusion — decision report

**Verdict: `P12-DIFF-002 BLOCKED / FAILED GATE`.**

Stopped at **W3b**, the first failed criterion in the frozen gate's own order. W1a, W1b, W1c, W1d,
W2, W3a and W4 were measured and all pass. W5–W10 were **not** run: the frozen gate's stop rule is
"Stop at the first failed criterion", and the authorization repeats "Stop at the first failed frozen
gate."

The W3b failure is **not** an implementation defect. It is a criterion I mis-derived before freezing,
and the evidence below proves which of the two it is. Per the authorization's standing prohibition —
"Do NOT modify their thresholds", "Do not silently retune it", "Never adjust a threshold after seeing
a result" — I have **not** amended W3b, and I am requesting a separate decision on it rather than
reinterpreting it myself.

---

## 1. What was implemented

Gate documents and hashes:

| artefact | sha256 |
| --- | --- |
| `acceptance_gate.md` (frozen before any numerical change) | `51079f6da5dd0a417b39fe332f92eccf32a9877a96035168bc566ed1d3a9e46b` |
| `architecture.md` | `af8a23107c2745ae…` |
| `build/release/src/libcfdcore.a` under test | `0a2a1398cd48c629d412985ba87a84470368cca939f56916f970f85211b230fb` |

**DIFF-002-A (architecture).** Settled without a new matrix mechanism. `SparseMatrixBuilder` is
triplet-based (`stable_sort` by (row, column), duplicates summed, exact zeros dropped), so an
arbitrary non-face coupling is already expressible and deterministic. More importantly, the coupling
this reconstruction needs is **not** a non-face coupling at all: the far cell is reached through
`MeshGeometry::oppositeInteriorFace(mesh, owner, boundaryFace)`, which returns a face **of the owner
cell**, so the far cell is always one of the owner's own face neighbours and its entry lands on a
column the owner's row already had. The sparsity graph is therefore unchanged — verified directly in
W4, below. The resulting row asymmetry (`A(P,F)` gains the term, `A(F,P)` does not) is safe because
momentum, energy, species and k-ε all use BiCGSTAB; the only CG-solved system is the pressure
correction, which never calls `boundaryFaceDiffusionTerms`.

**DIFF-002-B (production reconstruction).** Implemented in
`src/discretization/NonOrthogonalDiffusion.cpp`, from the general geometry, **not** the uniform
formula:

```text
cP = h2/(h1 (h2-h1)),   cF = h1/(h2 (h2-h1)),   cB = 1/h1 + 1/h2      (cP - cF = cB identically)
A(P,P) += Gamma |S| cP ;  A(P,F) -= Gamma |S| cF ;  rhs += Gamma |S| cB phi_b
rhs += Gamma |S| ( cP (grad_P . delta_P) - cF (grad_F . delta_F) )
```

with `h1 = (x_f − x_P)·n̂`, `h2 = (x_f − x_F)·n̂` the wall-**normal** distances and `delta` the
tangential transfer onto the wall-normal ray (exactly `{0,0,0}` on an orthogonal face). The
uniform-grid form `[9 φ_P − φ_F − 8 φ_b]/(3h)` appears **only** as a verification limit, in the tests.

The reconstruction **replaces** the former value-boundary non-orthogonal treatment rather than
augmenting it: the `S_nonorth · grad(φ)_P` term is gone on any face that takes the new path, so the
tangential contribution is not double-counted. Neumann/flux-prescribing faces are untouched
(`prescribesBoundaryValue` gates the whole branch), and `gradPhi == nullptr` still yields the
pre-existing two-point terms bit for bit.

Shared, single implementation: `NonOrthogonalDiffusion` is the one formula; `MomentumEquation` (2
sites), `EnergyEquation` (2 sites via one helper), `SpeciesEquation` and `KEpsilonEquation` only read
the widened struct. **`Diffusion.cpp` was deliberately left untouched** — see §4.

---

## 2. Measured gate results

| id | result | measurement |
| --- | --- | --- |
| **W1a** | **PASS** | constant/linear wall-flux L1 ≤ 1.9e-14 on all five families (bound 1e-12) |
| **W1b** | **PASS** | quadratic exactness, 2D distorted 48°: **6.25e-02 → 1.5902e-16** (bound 1e-12). The negative control that the baseline failed |
| **W1c** | **PASS** | cubic observed order **1.000 → 2.000 / 2.000 / 1.963 / 2.000** (bound ≥ 1.8) |
| **W1d** | **PASS** | curved multi-block radial: **1.8616e-02 → 3.3578e-04**, a 55× improvement against a no-regression bound |
| **W2** | **PASS** | translation difference 3.614e-18 against envelope 7.326e-12; origin 1.4311e-16 vs translated 1.3950e-16 |
| **W3a** | **PASS** | 100 % higher-order stencil availability on all 19 buildable committed cases |
| **W3b** | **FAIL** | **see §3** |
| **W4** | **PASS** | 13/13 hand-derived tests, `logs/10_gate_W4.log` — detail below |
| W5–W10 | **not run** | stop rule |

Evidence: `logs/07_gate_W1_W2_W3.log` (W1, W2, W3a), `logs/08_gate_W3b.log` and
`logs/09_gate_W3b_patches.log` (W3b), `logs/10_gate_W4.log` (W4).

### W4 in detail — `tests/unit/discretization/test_boundary_reconstruction.cpp`

13 tests, every expected number derived on paper from the mesh geometry, none read back from the
implementation's own expression:

- **Coefficients.** 4×4 unit square, Γ = 2, |S| = 0.25, h1 = 0.125, h2 = 0.375 ⇒ cP = 12, cF = 4/3,
  cB = 32/3, so `coefficient = 6`, `farCellCoefficient = 2/3`, `boundaryValueCoefficient = 16/3`,
  `farCell = 4`. All exact. The identity `coefficient − farCellCoefficient == boundaryValueCoefficient`
  holds exactly, so a constant field gives exactly zero flux.
- **Assembled system**, through the production assembler
  `cfd::thermal::assembleThermalDiffusionContribution` on a 3×3 mesh with h = 1, k = 1, two Dirichlet
  patches and two adiabatic ones — sparsity pattern, every coefficient, the RHS, the signs and the
  far-cell entry, all against a hand derivation:
  - row 1 columns exactly `{0, 1, 2, 4}`; `A(1,1) = 6`, `A(1,0) = A(1,2) = −1`, `A(1,4) = −4/3`,
    `rhs(1) = 8`
  - row 0 (a corner cell with **two** Dirichlet faces, far cells 3 and 1): columns `{0, 1, 3}`,
    `A(0,0) = 8`, `A(0,1) = A(0,3) = −4/3`, `rhs(0) = 12`
  - asymmetry confirmed: `A(1,4) = −4/3` while `A(4,1) = −1`
  - **the sparsity pattern is identical to the pre-DIFF-002 assembly** — same `nonZeros()`, same
    column list in every row — confirming DIFF-002-A's central claim
  - the same assembly with the correction disabled gives the weaker two-point system
    (`A(1,1) = 5`, `A(1,4) = −1`, `rhs(1) = 6`), so the comparison has power
- **`Diffusion.cpp` cross-check** (architecture.md §6a). `Diffusion.cpp` lines 167–169 fit the same
  quadratic and have since P0: `a = (2h₁+h₂)/(h₁(h₁+h₂))`, `b = −(h₁+h₂)/(h₁h₂)`,
  `c = h₁/(h₂(h₁+h₂))` in **its own** parametrisation (its `h₂` is the P→F distance, one `h₁`
  shorter than this reconstruction's `h2`). Substituting gives `a = cB`, `b = −cP`, `c = cF`. Checked
  numerically to ≤ 1e-14 relative on a uniform mesh **and on a geometrically graded one** (h1 = 1/30,
  h2 = 2/15 — the two spacings differ by a factor of 3), using `oppositeInteriorFace` and
  `ownerNeighborDistance` exactly as `Diffusion.cpp` calls them. The agreement is therefore with the
  **general** formula, not a uniform-grid coincidence.
- **Geometry, exactness and fallback.** Uniform h1/h2 and `farCell`; graded cell heights
  (1/15, 2/15, 4/15, 8/15 ⇒ h1 = 1/30, h2 = 2/15); assembled flux exact to 1e-13 against
  `Γ ∇φ·S_out` for linear *and* quadratic fields; a stencil-less face returns the pre-existing
  two-point terms exactly; `prescribedValue = false` and `gradPhi == nullptr` both unchanged;
  tangential transfer exactly 0 on an orthogonal face and non-zero on a sheared one; an internal face
  throws `InvalidArgumentError`.

---

## 3. W3b — the failed criterion, and which kind of failure it is

**As frozen:** "Degenerate meshes (2D 1×1, 8×1, 1×8; 3D 1×1×1, 8×8×1) fall back on **every** face,
and their results are **bitwise identical** to the pre-change library | 100 % fallback; bitwise
identity".

**Measured** (`logs/08_gate_W3b.log`, `logs/09_gate_W3b_patches.log`; the comparison is on raw bit
patterns, against the pre-change two-point expression transcribed literally from the production
fallback branch):

| mesh | boundary faces | higher-order | fallback | bitwise identical | W3b |
| --- | --- | --- | --- | --- | --- |
| 2D 1×1 | 4 | 0 | 4 | 4/4 | **PASS** |
| 2D 8×1 | 18 | 2 | 16 | 16/18 | **FAIL** |
| 2D 1×8 | 18 | 2 | 16 | 16/18 | **FAIL** |
| 3D 1×1×1 | 6 | 0 | 6 | 6/6 | **PASS** |
| 3D 8×8×1 | 160 | 32 | 128 | 128/160 | **FAIL** |

The per-patch breakdown settles the diagnosis:

- **2D 8×1** — higher-order on `left` and `right` only (1 face each, the x-normal patches, and the
  mesh is 8 cells across x); fallback on all 8 `bottom` and all 8 `top` faces (1 cell across y).
- **2D 1×8** — the exact mirror image: higher-order on `bottom`/`top`, fallback on `left`/`right`.
- **3D 8×8×1** — higher-order on all four side patches (`xmin`/`xmax`/`ymin`/`ymax`, 8 faces each
  = 32); fallback on all 64 `zmin` and all 64 `zmax` faces (1 cell across z).

So the code falls back **exactly** where no valid opposite interior cell exists, decided per face,
and it takes the higher-order path exactly where one does exist. That is precisely what the
authorization requires: the fallback applies "where no valid opposite interior cell exists", it must
"not depend on floating-point threshold branch instability" (the branch here is topological —
`oppositeInteriorFace` returning nothing — plus the exact sign tests `h1 > 0` and `h2 > h1`), and
"Do not reject otherwise valid one-cell-thick meshes merely because the higher-order stencil is
unavailable."

**W3b's premise is geometrically false.** 8×1, 1×8 and 8×8×1 are one-cell-thick in *one* direction
only; across their other direction(s) they have 8 cells and therefore a genuine interior stencil. The
only meshes in the list that are thin in *every* direction are 1×1 and 1×1×1 — and both pass W3b
completely, including bitwise identity on every face. Requiring 100 % fallback on the other three
would mean *discarding* a legitimately available second-order stencil, which is the opposite of what
the phase is for.

**Why the pre-freeze dry-run did not catch this.** The frozen gate records a 14 PASS / 8 FAIL dry-run
against the unchanged two-point library, whose stated purpose was to prove every criterion reachable
and every defect test powerful. W3b passed that dry-run — **vacuously**: the baseline library has no
higher-order path at all, so "100 % fallback" is trivially true on it. A baseline dry-run cannot test
a criterion that is tautological on the baseline. This is a real gap in the freeze methodology I have
been using, and it is the second distinct way I have now mis-derived a frozen criterion (after
GRAD-002's C1 and C4, and A1's C2).

**Disclosure of my own earlier wording error.** In interim notes I described the code as "falling back
on every face" for degenerate meshes. That was wrong, and it over-specified what the authorization
actually asks for. The measurements above are the correct picture; I am reporting the discrepancy
rather than quietly re-reading W3b to fit.

**Decision requested.** W3b as written cannot be satisfied by any correct implementation. It needs a
separate decision, exactly as the authorization prescribes for W8's historical gate — "Preserve the
result and request a separate decision about whether the historical gate itself should be amended. Do
not silently retune it." I have preserved the failing result and changed nothing. A defensible
replacement would assert 100 % fallback and bitwise identity on the meshes that are thin in every
direction (2D 1×1, 3D 1×1×1), and on the remaining meshes assert per-face fallback exactly on the
patches normal to a thin direction with bitwise identity **on those faces** — but that is a decision
to authorize, not one for me to make after seeing the result.

---

## 4. `Diffusion.cpp` — a discovery worth recording

`src/discretization/Diffusion.cpp` was **not** modified, and that is deliberate, not an omission. Its
*explicit* operator has implemented this same second-order boundary treatment since P0. Its 3-point
fallback is algebraically identical to the new coefficients (proved in W4 above). Its *preferred*
path is a different mathematical object — a 4-point cubic Laplacian correction that back-solves the
boundary flux so the cell's second derivative is second-order — and it is also where the MESH-005
two-cells-across defect lives, which the authorization places out of scope. Changing it would either
duplicate a formula it already has or pull an out-of-scope defect into this phase. The "avoid two
mathematically different implementations of the same Dirichlet wall diffusion formula" requirement is
met: the implicit assembly path now has exactly one implementation, and it agrees with the explicit
operator's long-standing one to ≤ 1e-14.

---

## 5. Out of scope, untouched, reported not fixed

The MESH-004 sanitizer use-after-free; the warped-3D-face quadrature limitation; `Diffusion.cpp`'s
4-point Laplacian path and its MESH-005 two-cells-across defect. None of these blocked W1–W4.

**One pre-existing failure encountered and attributed, not caused by DIFF-002.** Running the whole
`CFDDiscretizationTests` target after adding the W4 file gives 168/169 passing, the one failure being
`GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`. This is a
**gradient** test, and it asserts the distorted-mesh global order is *below* 1.9 ("observed order
suspiciously high") — an upper bound that held only while the Green–Gauss boundary gradient was first
order. It is already recorded as a GRAD-002 consequence in
`results/p12-grad-002/a1/summary_a1.md` §"None of the three tests was modified", item 1, and in
`results/p12-grad-002/a1/logs/10_focused_new.log`. The numbers I measure now — **1.93558, 1.96913,
1.98396** — are digit-for-digit the ones recorded there (1.936, 1.969, 1.984) **before DIFF-002
existed**, and DIFF-002 modifies no gradient code. So DIFF-002 changed nothing about this test. It was
not modified, and no threshold or golden output was touched.

---

## 6. Not claimed

- GRAD-002's frozen compatibility gates were **not** rerun (they follow a DIFF-002 pass).
- MESH-007 G6.3 was **not** rerun, and the original MESH-007 G6.3 failure stands as recorded.
- W5's convergence-performance guard is **unmeasured**, so no claim is made about whether this
  reconstruction repeats GRAD-002's 3–4× outer-iteration slowdown. The transfer term is the only
  lagged part and the coupling is implicit, which is why it was designed this way, but that is a
  design intention, not a measurement.
- W6's production Poiseuille accuracy is **unmeasured**.
- `TODO.md` records P12-DIFF-002 as blocked; no `[x]` was added for DIFF-002, GRAD-002 or MESH-007.

---

## 7. Files changed (working tree only — nothing committed, nothing pushed)

Production:

- `include/cfd/discretization/NonOrthogonalDiffusion.hpp` — `FaceDiffusionTerms` widened by
  `boundaryValueCoefficient`, `farCellCoefficient`, `farCell`, `higherOrder`
- `src/discretization/NonOrthogonalDiffusion.cpp` — the reconstruction; `twoPointTerms()` helper so
  every fallback path is identical by construction
- `include/cfd/mesh/MeshGeometry.hpp`, `src/mesh/MeshGeometry.cpp` — `BoundaryInwardStencil` and
  `boundaryInwardStencil()`
- `src/physics/MomentumEquation.cpp`, `src/thermal/EnergyEquation.cpp`,
  `include/cfd/thermal/EnergyEquation.hpp`, `src/species/SpeciesEquation.cpp`,
  `src/turbulence/KEpsilonEquation.cpp` — the six call sites read the widened struct

Tests and evidence:

- `tests/unit/discretization/test_boundary_reconstruction.cpp` (new, 13 tests) and its
  `CMakeLists.txt` entry
- `results/p12-diff-002/tools/diff2_degenerate.cpp`, `tools/w4.sh`,
  `logs/08_gate_W3b.log`, `logs/09_gate_W3b_patches.log`, `logs/10_gate_W4.log`, this file

`clang-format --dry-run --Werror` is clean on both new sources. `git diff --check` is clean (exit 0).
`git rev-parse HEAD` = `b66310ca871811c4c7671056beec291a451af55c`.

## 8. Reproduction

```bash
results/p12-diff-002/tools/w4.sh                                  # W4  -> logs/10_gate_W4.log
results/p12-diff-002/tools/run.sh diff2_degenerate 08_gate_W3b.log   # W3b
results/p12-diff-002/tools/run.sh diff2_gate 07_gate_W1_W2_W3.log    # W1, W2, W3a
```
