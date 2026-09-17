# P12-DIFF-002 — acceptance gate (pre-registered)

Authorized 2026-09-16 after DIFF-002-INV-001 (Classification B). Frozen **before any change to
production numerical behaviour**; the only production edit that precedes this freeze is the
numerically **neutral** interface widening of DIFF-002-A (`FaceDiffusionTerms` gained three fields
whose neutral values reproduce the two-point form exactly, and the six call sites read them),
verified neutral by rerunning the distorted-Poiseuille case to identical digits
([logs/03](logs/03_widening_neutrality.log)): iterations 2789, velocity L2 3.7082e-03,
dp/dx −1.191854 — the pre-widening values.

**Stop rule.** Stop at the first failed criterion. Never adjust a threshold after seeing a result.

**Pre-freeze dry-run, recorded.** Every numerical criterion below was built and run against the
unchanged (two-point) library *before* this document was frozen
([logs/04](logs/04_gate_dryrun_baseline.log)): **14 PASS / 8 FAIL**, where the 8 failures are
exactly the intended negative controls (quadratic exactness and cubic order, on all four
axis-aligned families) and every sanity criterion (constant, linear, W2 invariance, the
no-regression bound) passes. This establishes that no criterion is unreachable and that the defect
tests have power. The standing rule from GRAD-002's A1 is therefore satisfied.

## 1. Formulation under test

`results/p12-diff-002/architecture.md`. Per value-prescribing boundary face, with h1 = (x_f−x_P)·n̂,
h2 = (x_f−x_F)·n̂ and the cell values transferred onto the wall-normal ray:

```text
cP = h2/(h1 (h2-h1)),   cF = h1/(h2 (h2-h1)),   cB = 1/h1 + 1/h2      (cP - cF = cB)
A(P,P) += Gamma |S| cP ;  A(P,F) -= Gamma |S| cF ;  rhs += Gamma |S| cB phi_b
rhs += Gamma |S| ( cP (grad_P . delta_P) - cF (grad_F . delta_F) )
```

Fallback (no opposite interior face, or h1 ≤ 0, or h2 ≤ h1): today's two-point values exactly,
i.e. cF = 0, cP = cB = 1/h1, which reproduces `Gamma |S_orth|/|d|` identically (DIFF-001 verified
that identity to 4.5e-16). Neumann/flux-prescribing faces are untouched.

## 2. Criteria

| id | criterion | threshold, and where it comes from |
| --- | --- | --- |
| **W1a** | Constant and linear manufactured fields: per-face wall-flux L1 error, exact transfer gradient, on 2D Cartesian, translated Cartesian, distorted 48°, 3D Cartesian and the committed curved multi-block mesh | **≤ 1e-12**. Dry-run: baseline already passes at ≤ 1.9e-14, so satisfiable |
| **W1b** | **Quadratic** (axial) field on 2D Cartesian, translated Cartesian, distorted 48°, 3D Cartesian | **≤ 1e-12** (exact). Dry-run: baseline **fails at 6.25e-02** — the negative control. INV-001 measured exactly 0 for this field/geometry pairing |
| **W1c** | **Cubic** field, per-face L1 observed order on those same four families | **≥ 1.8**. Dry-run: baseline **fails at 1.000–1.002**. INV-001 measured 1.998–2.000 |
| **W1d** | Radial (cubic-in-r) field on the committed curved multi-block mesh — one resolution only, so a **no-regression** bound, not an invented exactness bound | **≤ 1.8616e-02**, the measured two-point value |
| **W2** | Translation and scale invariance of the wall flux: same mesh at the origin vs translated by (0.005, 0.0025), and at L = 1e-3 vs 1e3, quadratic and cubic fields, Cartesian and distorted | difference **≤ max(1e-12, 200 ε (X/h))**, the envelope form GRAD-002's A1 established. Dry-run: baseline passes at 5.4e-16 against 6.4e-12 |
| **W3a** | Every buildable committed case obtains the higher-order stencil on **100 %** of its boundary faces | 100 %. INV-001 measured 100 % availability on all 19 |
| **W3b** | Degenerate meshes (2D 1×1, 8×1, 1×8; 3D 1×1×1, 8×8×1) fall back on **every** face, and their results are **bitwise identical** to the pre-change library | 100 % fallback; bitwise identity |
| **W4** | Hand-derived matrix tests: for small systems derived independently on paper, the assembled sparsity pattern, every coefficient, the RHS, the signs and the far-cell entry match | exact (≤ 1e-14 relative). Includes the cross-check that on an orthogonal mesh the new coefficients reproduce `Diffusion.cpp`'s existing 3-point formula (architecture.md §6a) |
| **W5** | **Convergence-performance guard (mandatory).** Outer-iteration counts must not exceed **1.25×** these recorded pre-change values: `poiseuille_flow` 1319, `poiseuille_distorted` 1983, `lid_driven_cavity` 3036, `curved_channel_multiblock` 1400, `duct_3d` 67, `channel_transpiration_graded` 1563 ([logs/05](logs/05_cases_before.log)); thermal and species solver iterations likewise | ≤ 1.25×. GRAD-002 measured a 4.1× slowdown from a lagged boundary correction; this is the explicit guard against repeating it |
| **W6** | Production Poiseuille (the INV-001 relevance case): record dp/dx error, velocity L1/L2/L∞, wall flux, observed orders and iterations at 64×8, 96×12, 144×18 | the 144×18 dp/dx error **≤ 1.10 ×** the pre-change 0.6788 %, i.e. ≤ 0.747 %. A second-order-consistent wall flux must not make the finest grid worse; the 1D model predicts a large improvement |
| **W7** | Focused verification from P12-NUM, MESH-001…006 and GRAD-002 passes, each against **its own** originally frozen thresholds | each phase's own threshold, not a ratio chosen now |
| **W8** | `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` and `MultiBlockProductionCase.CurvedChannelGridConvergence` rerun **unchanged** | **Reported, not presupposed.** INV-001 showed DIFF-002 cannot guarantee their order/GCI/monotonicity assertions. If either still fails: **STOP**, preserve the result, and request a separate decision on amending the historical gate. Do not retune it |
| **W9** | Committed-case backward compatibility: solution norms and conservation from [logs/05](logs/05_cases_before.log) rerun; every change bounded and explained, conservation no worse | mass imbalance not worse than the recorded value; solution-norm changes explained |
| **W10** | Only after W1–W9: Release and Debug + GUI regression with exact counts, sanitizers at CI settings (only permitted failure: the known MESH-004 test defect), clang-format clean | exact counts from the actual run; no estimates |

## 3. Out of scope (recorded, not fixed)

The MESH-004 sanitizer use-after-free, the warped-3D-face quadrature limitation, `Diffusion.cpp`'s
4-point Laplacian path and its known two-cells-across defect (MESH-005). If one of these blocks a
required gate, the blocker is reported rather than the scope expanded.

## 4. After DIFF-002 passes

Rerun the affected **GRAD-002** frozen compatibility gates unchanged. If GRAD-002 becomes fully
green, rerun the original frozen **MESH-007 G6.3** unchanged — Cartesian 16×16 translating lid
cavity, `max |u_B − b − u_A| ≤ 1e-8`, no mesh substitution. If any GRAD-002 or MESH-007 gate still
fails: stop and report.
