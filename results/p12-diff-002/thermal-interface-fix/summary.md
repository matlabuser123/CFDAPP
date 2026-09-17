# P12-DIFF-002 — ThermalInterface conjugate boundary-flux defect fix — report

**Gate:** `results/p12-diff-002/thermal-interface-fix/acceptance_gate.md`, sha256
`9944d066659d4127cbfdba5246f67b023461738fb9e9f5cd96d0d289258e8870`, frozen before any production
modification (`logs/00_freeze.log`, which also froze the pre-fix hash of every production numerical
file and of `libcfdcore.a`).

```text
T0  reproducer fails first              PASS   all 7 tests failing pre-fix, gate section 1 reproduced
T1  single-material path unchanged      PASS   bitwise, 64 configurations, 68962 exact-hex lines
T2  single-region matrix/RHS            PASS   bitwise (0 rows differing) on all 11 meshes
T3  single-region solution + flux       PASS   0.0 exactly, both metrics
T4  all six geometries                  PASS
T5  fallback topology vs A1 oracle      PASS   812 faces, 0 mismatches, 0 T-vs-G disagreements
T6  one-cell-thick directions           PASS   174 fallback faces exercised
T7  DIFF-002 accuracy via conjugate     PASS   const/linear/quadratic exact; cubic order 2.000
T8  analytical two-layer slab           PASS   T <= 8.5e-15, flux uniformity <= 5.5e-13
T9  Neumann / graded / non-orthogonal   PASS   conservation and interface continuity
T10 hand-derived multi-region matrix    PASS   0.000e+00
T11 regression, four suites             FAILED — 11 frozen M-A tests still fail (section 8)
                                               "no new failure of any kind": PASS, 0 new, 9 fixed
T12 production scope                    PASS   exactly 3 permitted files changed
```

**Verdict: `BLOCKED AT T11`.** The defect itself is fixed and every criterion about the fix (T0–T10,
T12) passes. T11 is not met *as written* because the 11 hand-derived-coefficient tests that
`validation-migration/acceptance_gate.md` §2 froze as **M-A — authorized for migration** are still
failing: that migration (validation-migration Steps 4–5) was never performed, because that phase
stopped at the very defect this gate fixes. No threshold was adjusted and no test was weakened to
avoid this.

---

## 1. The defect

`src/thermal/ThermalInterface.cpp:60-73` computed its own boundary coefficient,
`conductivity * face.area() / distance`, and passed only that to `boundaryDiffusionContribution` —
the pre-DIFF-002 two-point wall flux, with no `boundaryValueCoefficient`, no far-cell entry and no
explicit correction — while its own comment claimed "Same boundary treatment as the single-material
assembly". P12-DIFF-002 A2 had made that claim false by making `EnergyEquation.cpp`'s Dirichlet wall
flux the DIFF-002 three-point reconstruction unconditionally.

Consequence: a **single-region** conjugate solve disagreed with the equivalent single-material solve
by up to 3.02 K, and conjugate conduction still carried the first-order half-cell wall-flux error
that DIFF-002 exists to remove.

## 2. The fix — one shared implementation, not a second copy

Rather than duplicate the corrected block (which is how the two paths drifted apart in the first
place), the boundary-face contribution `EnergyEquation.cpp` already performed was factored into one
exported production helper and is now called by **both** assemblies:

```text
include/cfd/thermal/EnergyEquation.hpp   + thermalBoundaryCorrectionGradient()
                                         + assembleThermalBoundaryFaceContribution()
src/thermal/EnergyEquation.cpp           both assembler overloads' boundary branches now call it
src/thermal/ThermalInterface.cpp         boundary branch calls it; local formula removed
```

No new numerical method was introduced — the helper is the existing A2 block moved, not rewritten.
It handles `boundaryValueCoefficient`, the owner coefficient, the far-cell coefficient, the explicit
transfer term and the topological fallback; gradient-type (Neumann) faces keep the exact prescribed
flux they had. Stencil availability remains topological (`boundaryInwardStencil`'s `valid` flag) —
no floating-point geometry predicate was added. The header now warns against calling
`boundaryDiffusionContribution` with only `coefficient`, which is what silently selected the
historical form.

## 3. T0 — the reproducer, written and run before any production edit

`tests/unit/thermal/test_conjugate_boundary_equivalence.cpp` (new, 7 test cases covering 11 meshes)
compares the two production assemblies entry by entry plus the solved field and the total boundary
flux. Against the **unfixed** library **all 7 tests failed** (9 of the 11 mesh configurations
diverged; the 2 all-fallback controls agreed), reproducing the gate's §1 numbers exactly (`logs/01_T0_prefix_reproducer.log`):

| mesh | rows differing | max \|Δ matrix\| | solved max \|ΔT\| |
| --- | --- | --- | --- |
| Cartesian 2×1 | 2 | 2.500000e+00 | **3.023810e+00** |
| Cartesian 10×10 | 36 | 5.000000e+00 | **6.606599e-01** |
| Cartesian 20×20 | 76 | 5.000000e+00 | **6.566056e-01** |
| Cartesian 3D 8×8×8 | 296 | 9.375000e-01 | **1.430871e+00** |
| graded 12×12 r=1.2 | 44 | 5.655273e+00 | 5.946810e-01 |
| distorted 12×12 | 44 | 6.241472e+00 | 2.473408e+00 |
| Cartesian 3D 8×8×1 | 28 | — | 4.526388e+00 |
| Cartesian 1×1, 3D 1×1×1 | 0 | 0 | all-fallback controls, agree exactly |

The two all-fallback controls agreeing pre-fix is the negative control: the divergence is the
reconstruction, not an unrelated difference between the paths.

`ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo` was **not weakened**; its file
is byte-identical to the hash frozen before the work (`32dedf7a…`, re-verified in
`logs/09_T12_production_scope.log`).

## 4. T1 — the refactor did not change the single-material path

The pre-fix sources were no longer on disk, so they were **reconstructed** by reverse-applying the
four recorded refactor patches — and then *proved* authentic rather than trusted: the reconstructed
files hash to the values frozen before the work,

```text
src/thermal/EnergyEquation.cpp          4689ea01db674b577b01f2e5089bc1efe6710d1d3ac83ef47e099476c5b98bba
include/cfd/thermal/EnergyEquation.hpp  887947ee41510e83685b72f0a2e37c9ec4b28e21342d9fd28da4fa8fc6145360
src/thermal/ThermalInterface.cpp        a9865ef59338e71a052e38536507265fee8dba7869b4e989789248061ef68cc1
```

and the library built from that isolated tree hashes to `58be6b751328c9bb…`, which is exactly the
pre-fix `libcfdcore.a` frozen in `logs/00_freeze.log`. The **entire pre-fix library** was therefore
reproduced bit-for-bit, not merely the three files. The authoritative tree was never reverted.

`tools/ti_t1_dump.cpp` then dumped every stored matrix entry and every RHS value in `%a` hex — an
exact representation, so a byte diff is a bitwise comparison — for **64 configurations**: both
assembler overloads (constant k and the per-cell k field, both of whose boundary branches were
replaced), both non-orthogonal settings (the refactor also rewrote the `gradTPtr`/`internalGradT`
lines), all-Dirichlet and mixed Dirichlet/HeatFlux/Adiabatic patches, on 2D Cartesian (2×1, 10×10,
20×20), 3D Cartesian, graded, distorted and one-cell-thick meshes.

```text
pre-fix  68962 lines  sha256 9fc1f5b4d9fb3fc176cf65e83e2305f7…
post-fix 68962 lines  sha256 9fc1f5b4d9fb3fc176cf65e83e2305f7…
BITWISE IDENTICAL
```

## 5. T2/T3/T4 — single-region equivalence

`logs/02_T2_T3_T4_postfix_reproducer.log`: all 11 meshes now report

```text
rows differing 0 | max |d matrix| 0.000000e+00 | max |d rhs| 0.000000e+00
                | max |dT| 0.000000e+00 | boundary-flux rel diff 0.000e+00
```

Every O(1) difference the gate pre-registered — 3.024 K, 0.661 K, 0.657 K, 1.431 K — is gone, and
not merely below tolerance: the two assemblies are bitwise identical, because they now execute the
same code.

## 6. T5/T6/T7 — classification, fallback and accuracy

`logs/04_T5_T6_topology.log`. Classification is decided by the **frozen A1 oracles**, transcribed
verbatim: Oracle-T is an integer comparison on the mesh generator's arguments with no floating point
at all; Oracle-G walks connectivity by normal **depth** whereas production selects by **alignment**,
so agreement is evidence rather than tautology.

```text
boundary faces classified             812
oracle higher-order / fallback        638 / 174     (both classes well represented — not vacuous)
classification mismatches               0           (T5 requires 0)
Oracle-T vs Oracle-G disagreements      0 of 812
fallback faces not bitwise historical    0           (T5 requires 0)
far cell not a face-neighbour            0
far cell not strictly deeper             0
worst |(cP - cF) - cB| / cB           3.438e-16     (the reconstruction's own analytic identity)
worst |assembled - independent| rel   1.776e-16     over 8646 entries
```

The last line compares the assembled conjugate system against one accumulated independently, face by
face, from the oracles' verdicts — confirming the conjugate path applies the **complete** term set
(diagonal, far-cell entry, prescribed-value multiplier, explicit transfer), which is precisely what
it failed to do before.

`logs/05_T7_accuracy.log` measures the wall flux recovered **from the assembled matrix and RHS**
(not by re-calling the terms function, which would only re-measure W1), using W1's own fields, exact
flux and normalisation, in both a one-region and a two-region (4× conductivity jump) assembly:

```text
constant / linear / quadratic   exact, worst 7.1054e-13   (bound 1e-12)
cubic, Cartesian 2D and 3D      observed order 2.000      (bound 1.8)
```

## 7. T8/T9/T10 — true multi-region physics

`logs/06_T8_T9_T10_multiregion.log`. Single-region equivalence is deliberately not the only
validation. Two materials are solved and checked against the **analytical** two-layer
series-resistance slab, derived in the probe itself — no value taken from CFDApp:

| case | kB/kA | T vs analytic | flux uniformity | per-cell conservation | global |
| --- | --- | --- | --- | --- | --- |
| Cartesian 2D 8×16 | 10 | 7.96e-15 | 1.59e-13 | 1.80e-14 | 6.20e-14 |
| Cartesian 3D 4×8×4 | 10 | 3.41e-15 | 1.57e-13 | 6.00e-15 | 5.00e-15 |
| Cartesian 2D 8×16 | 0.1 | 4.55e-15 | 5.47e-13 | 4.26e-14 | 1.53e-13 |
| Cartesian 2D 8×16 | 1000 | 4.55e-15 | 1.95e-11 | 1.51e-12 | 2.25e-12 |
| Cartesian 2D, **Neumann** top | 10 | 7.96e-15 | 1.91e-13 | 1.88e-14 | 5.00e-15 |
| graded 2D r=1.15 | 10 | 8.53e-15 | 3.83e-13 | 2.86e-14 | 4.80e-14 |
| graded 2D, **Neumann** | 10 | 4.26e-14 | 3.10e-13 | 2.83e-14 | 8.20e-14 |
| sheared 2D s=0.35 | 10 | (see below) | (see below) | 4.82e-14 | 6.68e-14 |
| sheared 2D, **Neumann** | 10 | (see below) | (see below) | 1.54e-14 | 2.24e-14 |

"Flux uniformity" is the worst deviation of the per-area conduction flux over **every** y-normal
face — both walls, both layer interiors and the **material interface** — from the analytical q, so a
discontinuity at the interface would appear directly. Interface coupling antisymmetry,
`|A(P,N) − A(N,P)|` read from the assembled matrix, is **0.000e+00** on every mesh: each interface
face contributes the same conductance to both rows, which is what makes flux continuity structural.

T10: a 4-cell two-region system, matrix and RHS hand-derived in the probe's comments from
h₁ = h/2, h₂ = 3h/2 ⇒ cP = 3/h, cF = 1/(3h), cB = 8/(3h) and the series conductance, matches the
assembled system at worst relative **0.000e+00** for both kB/kA = 10 and 0.1.

### Disclosure — the sheared-mesh analytic deviation, and its attribution

On the strongly sheared mesh the solved field deviates from the 1D analytical profile by 1.23e-03
relative, and flux uniformity by 7.6e-02, where before the fix the conjugate path matched the
analytical profile to 8.5e-15. This is reported rather than set aside, and was attributed by running
the identical probe against both libraries (`logs/07_T9_sheared_attribution.log`). One region, so the
exact solution is simply linear:

| sheared 16×16, one region | pre-fix | post-fix |
| --- | --- | --- |
| conjugate vs linear exact | 9.095e-15 | 1.228e-03 |
| **single-material vs linear exact** | **1.228e-03** | **1.228e-03** |
| max \|single − conjugate\| | 1.228e-01 | 6.253e-13 |

The deviation is **exactly** the single-material path's own pre-existing value, unchanged to the last
digit (consistent with T1's bitwise result). The conjugate path's former apparent exactness was a
consequence of sitting on the superseded two-point wall flux — the defect itself. The mechanism is
A2's deliberate activation asymmetry: the wall reconstruction's tangential transfer term uses a
Green-Gauss gradient that is not exact on a sheared mesh, while the conjugate path's **internal**
faces remain uncorrected by design (`ThermalSolverSettings`' own comment; gate §4). T2–T4 *require*
the conjugate path to reproduce the single-material path bitwise, so this behaviour is inherited, not
introduced.

Note also what the fix **repaired** on that same mesh: the per-cell conservation residual of the
solved field, measured against the DIFF-002 boundary operator, went from 1.77e-03 to 4.82e-14, and
the Neumann global imbalance from 2.43e-02 to 2.24e-14. (Pre-fix those large residuals are the F-A
defect measured a second way: the pre-fix assembly's boundary flux is not the DIFF-002 one, so a
balance formed with the DIFF-002 operator cannot close.)

T9 asks for conservation and interface flux continuity on the graded and non-orthogonal meshes, and
both hold at ≤ 6.7e-14. The remaining question — whether the conjugate path's internal faces should
also be non-orthogonally corrected, which needs an interface-temperature reconstruction — is
pre-existing scope recorded by A2 and by gate §4, **not** resolved here.

## 8. T11 — regression, and why it is the failed criterion

`logs/08_T11_regression.log` (A4 harness: reconfigure → rebuild every target → verify the build →
only then execute, recording each binary's hash, so no stale binary can be reported):

```text
CFDThermalTests           99 run,  89 pass, 10 fail,  0 disabled
CFDDiscretizationTests   169 run, 167 pass,  2 fail,  0 disabled
CFDCaseIntegrationTests   63 run,  61 pass,  2 fail, 18 disabled
CFDTurbulenceTests       136 run, 136 pass,  0 fail,  0 disabled
TOTAL                    467 run, 453 pass, 14 fail, 18 disabled
```

Plus the committed thermal/conjugate cases (`logs/12_T11_thermal_conjugate_cases.log`):

```text
CFDConjugateHeatTransferValidationTests    6 run, 6 pass, 0 fail
CFDHeatedCavityValidationTests             5 run, 5 pass, 0 fail
```

Every one of the 14 failures is in the frozen inventory — **11 in M-A** (§2, authorized for
migration, not yet migrated) and **3 in U-H** (§3, must remain unchanged: `GridRefinementTest.`
`GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`,
`MultiBlockProductionCase.CurvedChannelGridConvergence`,
`StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence`). The other 10 **U** tests live in
suites not covered by T11's four targets.

### No new failure of any kind — measured, not asserted

The identical test sources were built against the pre-fix library (the hash-verified baseline of §4)
and run, so only the three production files differ (`logs/10_T11_prefix_attribution.log`,
`logs/11_T11_failure_set_comparison.log`):

```text
pre-fix  CFDThermalTests         99 run, 80 pass, 19 fail
post-fix CFDThermalTests         99 run, 89 pass, 10 fail
pre-fix  CFDDiscretizationTests 169 run, 167 pass, 2 fail
post-fix CFDDiscretizationTests 169 run, 167 pass, 2 fail

NEW failures (passed pre-fix, fail post-fix):  none
FIXED by this change:                          9
```

The post-fix failure set is a strict **subset** of the pre-fix one. The 9 fixed are the 7 new
`ConjugateBoundaryEquivalence` cases plus the two tests that were detecting the defect:
`RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly` and
`ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo` — the latter being the test the
gate forbade weakening, which now passes on its own unchanged assertions.

### Consequential finding for the pending migration

`RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly` is listed in the
frozen **M-A** set as a hand-derived-constant instrument to be migrated. It was **not** that: it was
asserting a genuine production property — that equal conductivity reproduces the single-material path
exactly — and it now passes untouched. Migrating it would have destroyed a correct defect detector.

That is direct evidence for the standing constraint "do not migrate tests that are testing genuine
production behaviour", and it means the M-A inventory must be re-examined before Steps 4–5 proceed:
at least one of its 18 entries needs no migration at all. The remaining 11 M-A failures do look like
genuine obsolete constants (they encode `Γ|S|/d` with no far-cell entry), but each must be
re-confirmed individually rather than migrated on the strength of the frozen list.

## 9. Files changed

```text
include/cfd/thermal/EnergyEquation.hpp                        production — two helpers declared
src/thermal/EnergyEquation.cpp                                production — helpers implemented, both
                                                              boundary branches now call them
src/thermal/ThermalInterface.cpp                              production — the fix
tests/unit/thermal/test_conjugate_boundary_equivalence.cpp     new — the T0 regression reproducer
tests/unit/thermal/CMakeLists.txt                             registers it
results/p12-diff-002/thermal-interface-fix/                   gate, tools, logs, this report
```

Exactly three production files differ from `a4/production_freeze.txt`; the other 20 production
numerical files are byte-identical, and every frozen gate, the preserved F-A evidence and the
detecting test are unchanged (`logs/09_T12_production_scope.log`).

No commit, no push. The MESH-004 ASan defect, W8, the GRAD-002 gradient test, MESH-007 G6.3 and the
13 **U** tests were not touched.

## 10. clang-format

`logs/13_clang_format.log`: the three production files and the new test are **clean**. The standalone
evidence probes under `results/.../tools/` are not (2-54 replacements), which matches existing
precedent -- earlier phases' probes carry 7-16 replacements and the repository's format tooling does
not cover `results/`.

## 11. What this phase did NOT do

Per the stop rule, work stopped at T11. Not started:

* the 20 outstanding authorized migrations (M-A 17 remaining after the finding in §8, M-B 1, M-E 2) —
  these are validation-migration Steps 4–5, and performing them from inside this gate would mean
  editing the very tests T11 measures;
* validation-migration Step 3's remaining instruments;
* the four **U-D** MMS policy tests (no investigation, no modification, no amendment);
* DIFF-002 W1–W10, GRAD-002 revalidation, MESH-007 G6.3.

The known W7 policy blocker recorded in `validation-migration/acceptance_gate.md` §3 is unchanged:
with the 13 **U** tests deliberately left failing, W7 still cannot pass.
