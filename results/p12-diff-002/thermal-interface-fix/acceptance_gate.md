# P12-DIFF-002 — ThermalInterface conjugate boundary-flux defect fix — acceptance gate

Authorized 2026-09-16. Frozen **before any production modification**. Status on entry:
`BLOCKED AT STEP 3 — VM-8 (F-A production defect)`.

**Stop rule.** Stop at the first failed criterion. Never adjust a threshold to make a test pass.

---

## 1. The preserved F-A finding this gate fixes

From `results/p12-diff-002/validation-migration/summary.md` §3 and
`investigation-f/logs/12_conjugate_divergence.log`, both of which remain unchanged:

`src/thermal/ThermalInterface.cpp:60-73` assembles a boundary face as

```cpp
const Real diffusionCoefficient = conductivity * face.area() / distance;   // pre-A2 two-point k|S|/d
const auto boundary = boundaryDiffusionContribution(
    mesh, face, temperature, temperatureBoundaries, diffusionCoefficient);  // no boundaryValue-
builder.add(ownerId, ownerId, boundary.diagonal);                           // Coefficient,
rhs[ownerId] += boundary.source;                                            // no far-cell entry,
continue;                                                                   // no explicitFlux
```

while its own comment claims "Same boundary treatment as the single-material assembly". P12-DIFF-002
A2 made that untrue by making `EnergyEquation.cpp`'s Dirichlet wall flux the DIFF-002 three-point
reconstruction unconditionally. Measured divergence for a **single region**, where the two production
paths must be identical (shipped default `non_orthogonal_corrections = 0`):

| mesh | rows differing | max \|Δ diagonal\| | max \|Δ rhs\| | solved max \|ΔT\| |
| --- | --- | --- | --- | --- |
| Cartesian 2×1 | 2 of 2 | 2.500000e+00 | 5.333333e+02 | **3.023810e+00** |
| Cartesian 10×10 | 36 of 100 | 5.000000e+00 | 1.133333e+03 | **6.606599e-01** |
| Cartesian 20×20 | 76 of 400 | 5.000000e+00 | 1.133333e+03 | **6.566056e-01** |
| Cartesian 3D 8×8×8 | 296 of 512 | 9.375000e-01 | 2.250000e+02 | **1.430871e+00** |

`ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo` detects it and is **not weakened
by this gate**.

## 2. Intended fix

Factor the boundary-face contribution that `EnergyEquation.cpp` already performs into **one** exported
production helper, and call it from both the single-material and the region-aware assembly, so the two
paths cannot drift apart again. No new numerical method is introduced: the helper is the existing
A2 block moved, not rewritten. It must handle `boundaryValueCoefficient`, the owner coefficient, the
far-cell coefficient, the explicit correction, and the fallback where no valid opposite stencil
exists; gradient-type (Neumann) faces keep the exact prescribed flux they have today. Stencil
availability stays **topological** (`boundaryInwardStencil`'s `valid` flag) — no floating-point
geometry predicate is introduced.

## 3. Criteria

| id | criterion | threshold |
| --- | --- | --- |
| **T0** | **Reproducer first.** Focused tests that FAIL on the unfixed library, proving the conjugate path differs from the single-material one for one region on Cartesian 2×1, 10×10, 20×20 and 3D 8×8×8, recording matrix, RHS and solved-temperature differences | the §1 numbers reproduced; failures recorded before any production edit |
| **T1** | **Single-material path unchanged by the refactor.** The single-material assembly's matrix and RHS are **bitwise identical** to the pre-fix library on 2D Cartesian, 3D Cartesian, graded and distorted meshes | bitwise |
| **T2** | **Single-region equivalence.** For one region the conjugate and single-material assemblies agree in matrix coefficients and RHS | **bitwise** (both call the same helper with the same inputs) |
| **T3** | **Single-region solution and boundary flux.** Solved temperature and the total Dirichlet boundary flux agree between the two paths | temperature ≤ **1e-12** absolute (the existing test's own bound, unchanged); boundary flux ≤ 1e-12 relative |
| **T4** | T2/T3 hold on **2D Cartesian (2×1, 10×10, 20×20), 3D Cartesian (8×8×8), a graded mesh and a distorted/non-orthogonal mesh** | all six geometries |
| **T5** | **Fallback topology.** Per boundary face, the conjugate path takes the higher-order reconstruction exactly where a valid opposite interior stencil exists and the historical fallback exactly where it does not, agreeing with the independent topology oracle of `acceptance_gate_A1.md` §5 | classification mismatches **0**; on fallback faces the contribution is **bitwise** the historical two-point one |
| **T6** | **One-cell-thick directions** explicitly covered: 2D 1×1, 8×1, 1×8; 3D 1×1×1, 8×8×1 | included in T5's count |
| **T7** | **DIFF-002 boundary accuracy now reached by conjugate conduction.** Manufactured fields through the conjugate assembly: constant and linear **exact**, quadratic **exact**, cubic second order | constant/linear/quadratic ≤ **1e-12**; cubic observed order ≥ **1.8** (the W1c bound) |
| **T8** | **True multi-region physics.** Two materials, conductivity ratio ≠ 1, against the **analytical** two-layer series-resistance slab (piecewise-linear T, continuous flux): temperature at every cell, interface flux continuity, and global energy conservation | temperature ≤ **1e-10** relative to ΔT; interface flux continuity ≤ **1e-10** relative; global energy imbalance ≤ **1e-10** relative to the throughput |
| **T9** | Multi-region with a **gradient/Neumann exterior boundary**, on a **graded** and on a **non-orthogonal** mesh: conservation and flux continuity hold | same bounds as T8 |
| **T10** | **Matrix assembly** for a small multi-region system matches an independently hand-derived one | exact (≤ 1e-14 relative) |
| **T11** | **Regression.** `CFDThermalTests`, `CFDDiscretizationTests`, `CFDCaseIntegrationTests`, `CFDTurbulenceTests` from a clean build with the A4 harness; plus the committed thermal/conjugate cases | no failure other than the 13 **U** tests frozen in `validation-migration/acceptance_gate.md` §3, and no new failure of any kind |
| **T12** | Production files changed are **only** `src/thermal/ThermalInterface.cpp`, `src/thermal/EnergyEquation.cpp` and `include/cfd/thermal/EnergyEquation.hpp`; every other production numerical file stays byte-identical to `a4/production_freeze.txt` | verified by hash |

## 4. Out of scope

The conjugate **interface** faces between *different* materials keep the existing
`interfaceConductance` series-resistance treatment: giving those a higher-order reconstruction needs
an interface-temperature reconstruction, which A2's audit already recorded as separate scope. Also
untouched: W8, the GRAD-002 gradient-order test, MESH-007 G6.3, the MESH-004 ASan defect, and the 13
**U** tests. No commit, no push.

## 5. Verdicts

```text
any criterion fails                  -> BLOCKED AT <criterion>
T0-T12 all pass                      -> the F-A defect is fixed; resume validation-migration Step 3
```
