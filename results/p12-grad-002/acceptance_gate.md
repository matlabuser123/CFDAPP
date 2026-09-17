# P12-GRAD-002 — Continuous Green–Gauss boundary treatment: acceptance gate (pre-registered)

Authorized by the user on 2026-09-16, after P12-GRAD-001 was recorded **BLOCKED / FAILED GATE**.
Frozen, with `formulation.md`, **before any change to production source** (sha256 in
[logs/00_gate_freeze.log](logs/00_gate_freeze.log)).

**Scope.** The Green–Gauss boundary-gradient treatment in `src/discretization/Gradient.cpp` and the
geometry helpers it needs. No ALE change. No MESH-007 G6.3 amendment. GRAD-001's gate and evidence
are historical and are **not** re-scored.

**Stop rule.** Stop at the first failed item. Never adjust a threshold after seeing a result. If any
item fails: record `P12-GRAD-002 BLOCKED / FAILED GATE`, do not proceed to MESH-007 G6.3.

**What this gate deliberately does not test.** No criterion refers to which branch a face takes, to
an alignment classification, or to a misalignment cutoff. GRAD-001 failed because two such criteria
(GR3 and GR6) were mutually unsatisfiable. Every criterion below is a statement about the
**numerical behaviour of the formulation**.

## 1. Notation and derivation inputs

ε = 2.220446049250313e-16. X = the largest absolute coordinate of a mesh; h = the local cell size;
η ≡ ε X/h. Fields and domains are O(1) with O(1) gradients unless stated, so absolute and relative
differences coincide; where they do not, the normalization is named.

Derived once, in `formulation.md` §4.3, and used by C1, C4, C6 and C7:

```text
relative sensitivity of the new formulation to a coordinate translation  ≈  C η,  C = O(10), C ≤ 50
```

**GRAD-001's floor, for contrast** (measured, `results/p12-grad-001/logs/05`): 0.6 ε (X/h)⁴ — 2.7e-9
at 2D 64². The claim under test is that the new formulation's floor is *linear* in X/h.

Gate meshes: 2D Cartesian 16², 32², 64², 128², 256² (built by `createStructuredQuad2D` from
Cartesian vertices), each plain and translated by the non-dyadic (0.005, 0.0025), the dyadic
(1/128, 1/256) and the **large** (1234.5678, 987.6543); the same at L = 1e-3 and L = 1e3 (offset
0.005 L); the distorted Q16; a graded mesh; 3D 8³ and 16³ plain, translated small and large, and
sinusoidally deformed; and the existing committed case meshes.

## 2. Criteria

| id | criterion |
| --- | --- |
| **C1** | **Constant fields.** For φ ≡ 1, max \|∇φ\| ≤ max(1e-13, 100 η) on every gate mesh, 2D and 3D. Derivation: the correction's divided differences are differences of equal values, so the correction is identically zero; what remains is Σ S_f/V, whose closure is exact on representable geometry and O(η) otherwise. Expected exactly 0 on dyadic meshes. |
| **C2** | **Linear fields.** φ = a + bx + cy (+dz), exact per-patch boundary values: relative error ≤ **1e-11** on every Cartesian variant (plain, shoelace, translated small/large/dyadic, scaled, graded, 3D plain/translated), all cells; ≤ **1e-9** on the distorted meshes (Q16, 3D deformed), all cells. Derivation: the correction vanishes identically for a linear field once the lagged gradient is converged, leaving plain Green–Gauss with exact face values; 1e-9 is NUM-003's own established four-sweep level on distorted meshes. |
| **C3** | **Cartesian second-order accuracy is retained.** (a) On aligned Cartesian meshes (2D 16²/64², 3D 8³), a quadratic field's gradient is exact to ≤ **1e-11** relative in **all** cells including boundary-adjacent ones — the property the deleted paired treatment provided. (b) Grid refinement on a smooth non-polynomial field (2D 16²→32²→64²→128², 3D 8³→16³→32³): observed order ≥ **1.8** in L∞ and L2, and not below the pre-GRAD-002 order by more than **0.1**. |
| **C4** | **Translation invariance, as a law rather than a number.** For every (mesh, offset) pair above, with identical field values and identical boundary conditions on a mesh and its translated copy, the gradient agrees cell by cell to ≤ **max(1e-13, 200 ε (X/h))** for constant, linear and quadratic fields — including the **large** offset (1234.5678, 987.6543), the case whose m_f = 7.9e-4 made GRAD-001's criteria unsatisfiable. Derivation: §1's C η with K = 200, a 4× margin on C ≤ 50. **Structural claim:** the least-squares exponent p of the measured difference against (X/h) over the 16²–256² non-dyadic family must satisfy **p ≤ 2.0** (GRAD-001 measured ≈ 4). Dyadic offsets must give exactly 0.000e+00. |
| **C5** | **Continuity sweep — the decisive test.** A controlled family shears a Cartesian 16² mesh so that the boundary misalignment m_f takes the values 0, 1e-12, 1e-10, 1e-8, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2 (nearest numerically reliable values, reported). For the same analytic field, the change in the gradient error between consecutive members must satisfy \|Δe\| ≤ **10 Δm_f + 1e-12**, i.e. a Lipschitz constant ≤ 10 plus a round-off floor; the quotient \|Δe\|/Δm_f is reported at every step. Derivation: §4.2's chain gives de/dm = O(1); 10 allows an order of margin. **Negative control (must fail this bound):** the same sweep on the pre-GRAD-002 library, which is expected to jump by O(1e-2) at its exact-zero crossing. A control that does not fail invalidates the test. |
| **C6** | **Scale invariance.** The same physical mesh at L = 1e-3, 1 and 1e3 must give dimensionless gradient errors agreeing to ≤ **1e-9**, for constant, linear and quadratic fields, 2D and 3D. Derivation: every term is a ratio of displacements, so only round-off differs across scales. |
| **C7** | **Static cavity translation reproducer (mandatory, no ALE).** Static PISO, identical physics, settings, time step and step count, on A = Cartesian 16×16 lid cavity and B = a rigidly translated identical copy: max \|u_B − u_A\| ≤ **1e-9** over the same 20 steps as the original reproducer (pre-fix: 5.418e-3 at step 1, 2.487e-2 at step 20). Repeated on a finer 32×32 mesh with the same bound, and once with the **large** offset, reported. Derivation: the per-step gradient difference is O(η) = 3.6e-15 at 16², and 20 implicit steps amplify by at most a few hundred, so 1e-9 keeps ≥ 300× margin and sits 10× below MESH-007 G6.3's own 1e-8. |
| **C8** | **Previous-phase verification, against each phase's own originally frozen thresholds.** Rerun and report: NUM-002 and NUM-003 gradient/skewness verification; MESH-001's distorted Poiseuille (dp/dx, velocity error, conservation, observed order); MESH-002's graded-mesh study; MESH-003's curved-channel and multiblock interface results; MESH-004's Q0–Q4 degradation families and production-path MMS; MESH-005's 3D operator orders; MESH-006's A3 duct and lid-cube results. **Each must still satisfy the threshold its own phase froze** — not a ratio chosen now. Every changed number is reported with its pre-GRAD-002 value and an explanation; unexplained drift fails. |
| **C9** | **3D.** C1–C4 and C6 hold on 3D Cartesian, translated (small and large) and sinusoidally deformed meshes. MESH-005's 3D operator suites and MESH-006's 3D SIMPLE verification pass under C8. |
| **C10** | **Interior cells are untouched.** On meshes with no skewed faces, every interior (non-boundary-adjacent) cell's gradient is **bitwise identical** to pre-GRAD-002. On skewed meshes, interior differences are ≤ 1e-13 relative and attributable to the existing sweep coupling through boundary cells. |
| **C11** | **Aligned-Cartesian equivalence, and its cost.** On exactly-aligned Cartesian meshes the new boundary-cell gradient agrees with pre-GRAD-002 to ≤ **1e-13** relative (the same linear functional, different arithmetic order — `formulation.md` §4.1). Bit-identity is therefore **not** claimed and not achievable together with removing the branch: every committed case output and CLI fixture is compared against pre-GRAD-002 and each difference must be either byte-identical or bounded at solver-tolerance level (≤ 1e-6 relative in every exported field) and explained, with counts reported. |
| **C12** | **No geometric threshold branch remains in the boundary-gradient path.** A source audit must show: `tryPairedBoundaryContribution`'s alignment and equal-area tests removed; `MeshGeometry::FaceAlignment`/`boundaryFaceAlignment` deleted; `obliqueNeumannFace`'s `cross(d, sf) == 0` test removed. Every remaining branch in that path is listed and classified as topological, or as having provably zero effect at its crossing, or as out-of-scope-benign with its justification (`formulation.md` §4.2, §6). |
| **C13** | **Tests and regression.** Focused suites first, then the full regression: Release and Debug + GUI 100 %, exact counts, no estimates. ASan + UBSan at CI's settings (`--timeout 7200`), whose only permitted failure is the known pre-existing P12-MESH-004 test defect `MeshQualityReport.DisconnectedMeshIsFatal`. New tests must cover: C5's continuity sweep, C4's translation family, the constant/linear exactness, and the corner/degenerate cases (a cell whose opposite face is a boundary face; two boundary faces claiming one opposite face). |
| **C14** | **Formatting and hash discipline.** After the implementation is final: run `clang-format` over all changed files, rebuild, record the final `libcfdcore`/`cfdapp` sha256, and only then run the final acceptance verification. No source change after that hash without rebuilding and rerunning the affected evidence. GRAD-001's hashes stay as its own historical evidence. |

## 3. Reporting requirements

Report, for every criterion, the measured value beside its frozen bound; the `oppositeInteriorFace`
anti-parallel margin and the count of faces claimed by two boundary faces on every gate mesh (as
diagnostics, not pass/fail); and the exact test counts. Preserve failed attempts.

## 4. If every criterion passes

Only then rerun the **original frozen MESH-007 G6.3** unchanged — the Cartesian 16×16 translating lid
cavity, the same time steps, physics and comparison, and the same `max |u_B − b − u_A| ≤ 1e-8`, with
no substitution of Q16 — followed by G7.3 and the MESH-007 verification that stopped (G2.3, G9, the
performance baseline, documentation, formatting, focused tests, G10 and the sanitizer run).
