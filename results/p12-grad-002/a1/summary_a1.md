# P12-GRAD-002 Amendment A1 — results

Status: **BLOCKED / FAILED GATE**. Every criterion A1 amended (**C1-A1, C4a, C6-A1**) passes freshly,
as do **C5**, **C7** and **C3(b)** — the phase's central claims. The gate fails on **C9/C2's
"3D deformed" clause**, which A1 carried over unchanged and which my audit wrongly recorded as
passing: a linear field's gradient on a sinusoidally deformed 3D mesh measures **2.029e-03** against
C2's frozen **1e-9**. The pre-GRAD-002 library measures **2.028e-03** on the same mesh, so the
failure is pre-existing, structural, and not caused by this phase — but it is a frozen criterion, so
the stop rule applies.

MESH-007's original G6.3 was **not** rerun. G6.3 is unmodified. Nothing was committed or pushed.

## 1. Chronology (preserved)

```text
original GRAD-002 gate
        ↓
C1 FAILED
        ↓
stop
        ↓
diagnostic investigation
        ↓
C4/C6 also shown mis-derived
        ↓
C5 central continuity test PASS
        ↓
user-authorized Amendment A1
        ↓
audit of all 14 criteria, A1 drafted (C1, C4, C6 only)
        ↓
pre-freeze baseline dry-run: 2 impossible tests caught and fixed
        ↓
A1 frozen (353b72ef…), binary frozen (51e82ee8…)
        ↓
fresh runs: C1-A1, C4a, C4b, C6-A1, C5, C7, C3(b), C9-translation all PASS
        ↓
C2's never-before-measured "3D deformed" clause FAILS — pre-existing, identical on the baseline
        ↓
stop; G6.3 not rerun
```

The original gate (`../acceptance_gate.md`, `a46973ed…`), its failure (`../summary.md`) and
GRAD-001's evidence are unchanged and hashed in [logs/01_a1_freeze.log](logs/01_a1_freeze.log).
Nothing is rewritten as successful.

## 2. Freeze

| artefact | sha256 |
| --- | --- |
| `acceptance_gate_A1.md` | `353b72ef11345922c849af0a456e2c8082e92188a8a10647b7c9fde2e249f186` |
| `a1/audit.md` | `8bb5515605701ba3f3fd10380750bfe16c407cd4721e3cd43932b3c1b4407c87` |
| `a1/dryrun.md` | `606b43e5b32345fcb5653e96fb581d73bf35a29a0beb5544ea7572f8a27b7d14` |
| `a1/logs/00_dryrun_baseline.log` | `b8ff74531048d059a65aa19dca1fff5cdeba1f6c38b7cb02ec903a559435a608` |
| `libcfdcore.a` (final formatted source) | `51e82ee8e0ed16363169b929934961c343ceff2eab58e656f1ada8ac0696f3cd` |

Frozen 2026-09-16T03:47:15Z, after the dry-run and before every fresh run below. The production
formulation was not changed by A1.

## 3. Pre-freeze baseline dry-run (required, recorded)

[dryrun.md](dryrun.md), [logs/00](logs/00_dryrun_baseline.log): **61 PASS / 7 FAIL on the unchanged
baseline**, and all 7 failures are the pre-registered negative controls (quadratic field on
translated Cartesian meshes, ratios up to 3.7e+08). It caught two impossible tests **before** the
freeze:

1. **C1-A1's envelope was impossible on the distorted mesh Q16** — the baseline failed a *sanity*
   test at ratio 7.6, because on a skewed mesh the linear-field error is NUM-003's four-sweep
   truncation residual, not round-off. Fixed by restricting C1-A1 to orthogonal, unskewed meshes and
   leaving Q16 to C2.
2. **The C4b coordinate sweep measured nothing** — powers of ten are exactly representable, so every
   row reported zero geometry error. Fixed with a non-representable mantissa.

## 4. Fresh A1 results — the amended criteria all pass

[logs/02_a1_fresh_new.log](logs/02_a1_fresh_new.log): **68 PASS, 0 FAIL.**

**C1-A1** — 22 rows, all pass. The constant field is now **exactly 0.000e+00** on every plain,
dyadic and L = 1e3 mesh (baseline 5.024e-15); the worst ratio to the envelope is 5.1e-02 (graded),
and the dimensionless artefact bound is met by ~10⁷ margin everywhere. The L = 1e-3 mesh that failed
the original C1 measures 6.939e-12 against E = 2.842e-10 — the same number, now against a
dimensionally correct envelope whose floor term is ε·8·Φ·(Σ|S_f|/V).

**C4a** — every valid-geometry mesh passes for all three fields:

| mesh | quadratic Δ, baseline | quadratic Δ, GRAD-002 | E | ratio |
| --- | --- | --- | --- | --- |
| 2D 16² small | 7.813e-03 | **2.932e-14** | 2.139e-11 | 1.4e-03 |
| 2D 32² small | 3.906e-03 | 1.155e-13 | 1.679e-10 | 6.9e-04 |
| 2D 64² small | 1.953e-03 | 4.584e-13 | 1.376e-09 | 3.3e-04 |
| 2D 128² small | 9.766e-04 | 1.826e-12 | 1.095e-08 | 1.7e-04 |
| 2D 256² small | 4.883e-04 | 7.262e-12 | 1.006e-07 | 7.2e-05 |
| dyadic, all resolutions | 0.000e+00 | **0.000e+00** | — | — |
| Q16 distorted | 2.022e-14 | 2.038e-14 | 1.392e-11 | 1.5e-03 |

**C4b** (diagnostic, no pass/fail) — `createStructuredQuad2D`'s geometry leaves the 1e-6 validity
domain between **X/h ≈ 1.0e3** (centroid error 1.7e-07, valid) and **X/h ≈ 9.9e3** (2.2e-04,
invalid), degrading to a centroid error of 2.4e+05·h at X/h ≈ 9.9e6. Recorded as a separately
scoped pre-existing defect; not fixed here.

**C6-A1** — every scale passes; ρ spreads across L = 1e-3, 1, 1e3 are 2.05 (constant), 25.6
(linear) and 1.10 (quadratic), against the frozen 100×.

**C5** (unchanged, rerun fresh, [logs/03](logs/03_c5_fresh_new.log)) — all nine steps pass with a
**constant** Lipschitz quotient of **1.563e-02** across nine decades of misalignment, error exactly
0.000e+00 at zero misalignment. The negative control ([logs/04](logs/04_c5_fresh_base.log)) fails at
the first step (7.813e-03 at m_f = 5.1e-13, quotient 1.527e+10) and then plateaus, as required.

**C7 — the static cavity reproducer, no ALE** ([logs/05](logs/05_c7_cavity_new.log),
[logs/06](logs/06_c7_cavity_base.log)). The baseline reproduces the originally recorded numbers
**exactly**, which confirms this is the same experiment:

| mesh | step 1, before | step 20, before | step 1, after | step 20, after | worst | bound |
| --- | --- | --- | --- | --- | --- | --- |
| 16² small | **5.418e-03** | **2.487e-02** | 6.136e-14 | 6.452e-14 | 9.835e-14 | 1e-9 **PASS** |
| 32² small | 1.674e-02 | 2.920e-02 | 2.638e-13 | 4.573e-13 | 4.889e-13 | 1e-9 **PASS** |
| 16² LARGE | 5.418e-03 | 2.486e-02 | 2.362e-05 | 7.076e-05 | 7.193e-05 | *reported* |

54 of 64 boundary faces lose the old exact predicate on the translated mesh (A 64, B 10), and the
solution is now invariant to 9.8e-14 — a 3.9e+11× improvement end-to-end through PISO. The
large-offset row is *reported*, not bounded, per C7's frozen text; that mesh fails C4a's geometry
gate (centroid error 5.030e-04·h), so the residual is the generator's, and the log's uniform "FAIL"
label is the tool applying the bound to all three rows rather than a gate verdict.

**C3(b) — Cartesian convergence** ([logs/07](logs/07_convergence_new.log),
[logs/08](logs/08_convergence_base.log)), grad(x³) with exact per-patch boundary values, all cells:

| family | baseline order (L∞) | GRAD-002 order (L∞) | error at 128², before → after |
| --- | --- | --- | --- |
| 2D Cartesian plain | 2.000 | **2.000** | 6.104e-05 → 6.104e-05 (identical) |
| 2D Cartesian **translated** | **0.985** | **2.000** | 5.798e-03 → 6.104e-05 (95× better) |
| 3D Cartesian plain | 2.000 | **2.000** | identical |

On origin-Cartesian meshes GRAD-002 is indistinguishable from the baseline; on a translated mesh the
baseline is **first order** and GRAD-002 restores **second order with the errors of the untranslated
mesh, digit for digit**. C3(b)'s ≥ 1.8 and "within 0.1 of pre-GRAD-002" are both met.

**Non-orthogonal accuracy** — the distorted family improves from uniformly first order to second
order in L2:

| level | baseline L∞ | GRAD-002 L∞ | baseline L2 order | GRAD-002 L2 order |
| --- | --- | --- | --- | --- |
| 16² | 4.741e-02 | 6.366e-03 | — | — |
| 32² | 2.472e-02 | 1.719e-03 | 1.483 | 1.956 |
| 64² | 1.260e-02 | 4.398e-04 | 1.492 | 1.975 |
| 128² | 6.355e-03 | 1.725e-04 | 1.496 | 1.980 |

Overall L∞ order 16²→128²: baseline **0.97**, GRAD-002 **1.74**. The finest-level L∞ order dips to
1.35 while L2 holds 1.98; the worst cell is boundary-adjacent, and a sweep-count test
([logs/09](logs/09_sweeps_diag_new.log)) rules out NUM-003's skew-correction truncation as the cause
(4, 8 and 16 sweeps give bit-identical results at every level). I have **not** isolated the cause;
it is reported as a limitation, is not bound by any frozen criterion, and is an improvement over the
baseline in both value and order at every level.

**C9 — 3D translation** ([logs/12](logs/12_3d_new.log)) — all pass, including at large coordinates,
because `createCartesian3D` plus a per-vertex translation involves no shoelace cancellation
(centroid inconsistency only 2.7e-12·h at X = 1234.5678):

| mesh | constant | linear | quadratic | verdict |
| --- | --- | --- | --- | --- |
| 3D 8³ small | 5.024e-15 | 7.350e-15 | 1.337e-15 | PASS |
| 3D 16³ small | 1.421e-14 | 2.033e-14 | 3.554e-15 | PASS |
| 3D 8³ dyadic | 3.553e-15 | 5.627e-15 | 9.155e-16 | PASS |
| 3D 8³ LARGE | 0.000e+00 | 3.346e-12 | 2.412e-12 | PASS |

## 5. The failure: C9/C2's "3D deformed" clause

C2 (carried over unchanged) requires a linear field's relative gradient error ≤ **1e-9** on "the
distorted meshes (Q16, **3D deformed**), all cells", and C9 requires C1–C4 to hold on sinusoidally
deformed 3D meshes. Measured ([logs/12](logs/12_3d_new.log), [logs/13](logs/13_3d_grad001.log)):

| mesh | constant | **linear** | pre-GRAD-002 linear | bound |
| --- | --- | --- | --- | --- |
| 3D 8³ deformed (max m_f 0.189) | 7.802e-15 PASS | **2.029e-03** | **2.028e-03** | 1e-9 **FAIL** |
| 3D 16³ deformed (max m_f 0.203) | 1.904e-14 PASS | **5.341e-04** | **5.341e-04** | 1e-9 **FAIL** |

**Pre-existing and unchanged by this phase:** the pre-GRAD-002 library gives the same values (2.028e-03
vs 2.029e-03 at 8³; identical at 16³). The boundary conditions are exact on these meshes (the
sinusoidal motion vanishes on the box boundary, so the patches stay planar), and the error converges
at order **1.93** (2.029e-03 → 5.341e-04), so it is a consistent O(h²) discretization error, not an
inconsistency.

**Most likely cause, not independently isolated:** on a deformed 3D mesh the interior faces are
*warped* (bilinear, non-planar). Green–Gauss needs ∫_f φ n dS, which for a linear φ equals φ(x̄)·S_f
only when the stored point x̄ is the S-weighted centroid; for a non-planar face the stored area
centroid is not that point, so a linear field is not reproduced exactly. That is a property of
MESH-005's 3D face representation, independent of any boundary treatment — consistent with the
baseline measuring the same value.

**Why this is a gate failure anyway.** C2's 1e-9 was frozen in the original gate and carried into A1
unchanged. My audit ([audit.md](audit.md)) classified C2 as "valid unchanged — passed" on the
strength of the original run, which measured Q16 and 3D *Cartesian* but **never the 3D deformed
mesh**. One clause of C2 had therefore never been evaluated, and it is unachievable by any version
of the code. That is the same species of error as C1, C4 and C6 — and I made it inside the very
amendment meant to correct that species of error.

## 6. Focused test suites — reported for information, not as a gate item

The focused suites of C13 were already in flight when the C2 failure surfaced
([logs/10](logs/10_focused_new.log)). Exact counts:

| suite | result |
| --- | --- |
| CFDDiscretizationTests | **155 / 156** (1 failed) |
| CFDMeshTests | 156 / 156 |
| CFDPisoTests | 81 / 81 |
| CFDCoreTests | 30 / 30 |
| CFDFieldTests | 40 / 40 |
| CFDAlgebraTests | 97 / 97 |
| CFDThermalTests | 92 / 92 |
| CFDTurbulenceTests | 136 / 136 |
| CFDMMSValidationTests | 19 / 19 |
| CFDCaseIntegrationTests | **61 / 63** (2 failed) |

`CFDSolverTests` is not a target in this build (the SIMPLE tests live in other binaries); the full
regression, the GUI suite and the sanitizers were **NOT RUN**.

None of the three tests was modified and no golden output was updated. **One fails because the
scheme improved; the other two are genuine, unexplained degradations on non-orthogonal production
cases** — my first reading of them from their names was wrong, and the measured detail corrects it:

1. `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` — **fails
   because the scheme became second order.** It asserts the distorted-mesh global order is *below*
   1.9 ("observed order suspiciously high"), which held only while the boundary treatment was first
   order there. It now measures **1.936, 1.969, 1.984**. Expected and desirable.
2. `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` — **a genuine degradation on
   MESH-001's headline case.** Velocity L2 1.7466e-02 / 7.4975e-03 / 3.7082e-03 at 64×8 / 96×12 /
   144×18 (Cartesian reference 1.5183e-02 / 6.9492e-03 / 3.1294e-03), dp/dx −1.172639 / −1.186851 /
   −1.191854 against the exact −1.2. Two assertions fail:
   - `gradientOrder >= 1.5` → **1.181** (the dp/dx order over the medium→fine pair);
   - fine-grid relative error ≤ its own GCI band → **0.006788 vs 0.002851**.
   Against MESH-001's own recorded figures this is **dp/dx error 0.585 % → 0.679 %** and **velocity
   error 1.15× → 1.185× the Cartesian scheme's**. Conservation is unaffected (column flow error
   1.47e-11 … 3.80e-11) and convergence remains monotonic, but the fine-grid accuracy is worse and
   no longer sits inside its Richardson uncertainty.
3. `MultiBlockProductionCase.CurvedChannelGridConvergence` — **a genuine degradation on MESH-003's
   headline case.** Velocity L2 order 1.919 / 1.975 and dp/dθ order 1.827 / 1.898 are still second
   order, but the **radial pressure-rise error is no longer monotonic**: 1.0031e-03 → 1.4654e-03
   under refinement (order −0.935), failing both the monotonicity assertion and the
   error-within-uncertainty assertion (0.0017644 vs 6.1408e-05).

These two are exactly what the authorization's "reject unexplained regressions" clause is for. I have
**not** explained them, and they are not attributable to the pre-existing defects above: both cases
are genuinely non-orthogonal production meshes at moderate coordinates, inside C4a's valid-geometry
domain, where the formulation deliberately changes the boundary gradient from first to second order.
That the *operator* improves while these two *case* metrics degrade is an unresolved tension and is
the substantive finding of this run, independent of the C2 clause that formally stopped the gate.

**Regenerated validation artefacts.** Running the suites rewrote the 20 tracked generated reports
under `results/validation/mms/`. Substantive changes are confined to the *distorted-mesh* reports:
the momentum MMS error moves 6.7908e-03 → 7.0131e-03 at 16² (+3.3 %), 3.2354e-03 → 3.2672e-03 at
32² (+1.0 %), 1.5787e-03 → 1.5830e-03 at 64² (+0.3 %) and 7.7780e-04 → 7.7834e-04 at 128²
(+0.07 %) — a perturbation that shrinks under refinement, with convergence status and mass imbalance
(2.78e-17 … 3.82e-17) unchanged; the Cartesian reports changed in runtime and one iteration count
only. That is the expected signature of an intentionally changed boundary discretization. I then
**reverted `results/validation/mms/` to HEAD** rather than leave silently-updated reference numbers
in the tree; the revert also cleared timing-only noise that earlier phases had left there, and every
file is regenerable by rerunning the suite. No other generated directory was touched.

## 7. Criteria not reached

**C8** (previous-phase verification), **C10** (interior bit-identity), **C11** (aligned-Cartesian
equivalence and case/CLI output comparison) and the **full regression, GUI suite and sanitizers** of
C13 were **NOT RUN**: the stop rule applies at the first failed criterion. The focused suites of C13
were already in flight when the failure surfaced and are reported in
[logs/10_focused_new.log](logs/10_focused_new.log) for information only. The original MESH-007 G6.3
and G7.3 were **not rerun**, and the remaining MESH-007 verification (G2.3, G9, performance
baseline, documentation, G10, sanitizers) is untouched.

## 8. Decision

**P12-GRAD-002 BLOCKED / FAILED GATE**, and **P12-MESH-007 BLOCKED / FAILED GATE**.

The formulation passed every criterion that measures the formulation: C1-A1, C3(a), C3(b), C4a, C5,
C6-A1, C7, C9's translation rows, C12 and C14, plus a demonstrated first-to-second-order improvement
on both translated and genuinely non-orthogonal meshes. It failed a clause about a **pre-existing
3D face-representation limitation** that no version of this code satisfies.

There are now **two** independent obstacles, and the second one matters more than the first:

1. the frozen C2 clause that formally stopped the gate — a pre-existing 3D warped-face limitation
   that no version of the code satisfies (§5);
2. **two unexplained degradations on non-orthogonal production validation cases** (§6) — MESH-001's
   distorted Poiseuille (dp/dx error 0.585 % → 0.679 %, velocity 1.15× → 1.185× Cartesian, fine-grid
   error outside its GCI band) and MESH-003's curved channel (radial rise error non-monotonic,
   1.003e-03 → 1.465e-03). These are inside the valid-geometry domain and are not explained by any
   pre-existing defect.

Options, for your decision:

- **(a) Investigate (2) first, as a scoped continuation of GRAD-002.** The operator is demonstrably
  second order on translated and distorted meshes, so a production case getting *worse* points at an
  interaction between the new boundary face value and the momentum/pressure coupling on skewed
  meshes — plausibly the lagged tangential transfer inside the SIMPLE/PISO iteration, or the
  interaction with the Rhie–Chow and non-orthogonal-correction paths, none of which this phase
  measured. Until that is understood the formulation should not be accepted, regardless of C2.
- **(b) A second amendment (A2) confined to C2's "3D deformed" clause**, re-derived from the
  warped-face limitation (a bound consistent with the measured O(h²) coefficient, or the clause moved
  to a diagnostic as C4b was). Necessary eventually, but on its own it would not address (2).
- **(c) A separately scoped phase for the 3D warped-face integration limitation**, which would also
  need MESH-005's operator gates revisited.
- **(d) Leave GRAD-002 and MESH-007 blocked**, keeping the formulation in the tree as evidence.

I recommend (a) before (b): fixing the gate wording while a production case is quietly less accurate
would be the worse outcome of the two.

**Procedural lesson, now recorded in memory.** The dry-run requirement caught two defects in the
criteria I *amended* and none in the criteria I *carried over* — because I only dry-ran the amended
ones. The rule has to be: **dry-run every criterion in the gate against the baseline, including the
carried-over ones**, and treat "passed" as meaning "every clause of it was measured", not "the rows I
happened to run passed".
