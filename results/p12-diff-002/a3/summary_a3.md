# P12-DIFF-002 Amendment A3 — decision report

**A3 gate:** `results/p12-diff-002/acceptance_gate_A3.md`, sha256
`f6dbdd06032c333a4347735abd81d6450ba9a0442879eb4eaa9edb1adaae0e95`, frozen before either test was
edited (`a3/logs/03_gate_a3_freeze.log`).

```text
A3-P production freeze        PASS
A3-1 activation test          PASS  (amended instrument passes)
A3-2 sector conservation      PASS  (amended instrument passes)
A3-5 focused rerun            FAILED -> STOP
A3-6 fresh complete W7        not run
A3-7 W8                       not run
A3-8 W9/W10                   not run
```

**Verdict: `P12-DIFF-002 BLOCKED / FAILED A3 GATE`.** Both reconciled instruments were confirmed
obsolete and were replaced with stronger, direct validation, and both now pass. A3-5's suite rerun
then revealed **14 further tests failing for exactly the same reason** — hand-derived assertions that
encode the pre-A2 two-point Dirichlet wall coefficient at `non_orthogonal_corrections = 0`. They were
invisible in A1's and A2's W7 runs because of an instrument defect in my own W7 script (§5). Amending
14 more tests is outside A3's authorized scope, so the phase stops here.

---

## 1. A3-P — production freeze verified

`a3/logs/01_production_before_A3.log` vs `a3/logs/04_production_after_A3.log`: all 14 production
numerical files **byte-identical**, library unchanged at `58be6b751328c9bb…`. A3 modified only test
code. No production numerics were changed, and none appeared necessary.

## 2. A3-1 — `NonOrthogonalCorrectionIsActiveOnTheProductionPath`

**Original intent.** An *activation* test: prove `solver.json`'s `non_orthogonal_corrections` reaches
the assembly and takes effect on the distorted production mesh.

**Root cause of the failure.** The assertion
`EXPECT_GT(uncorrectedError, 1.5 * cartesianVelocityL2(8))` inferred "the correction is active" from
"the N = 0 solution is measurably bad" (its comment records 3.31e-2 vs the corrected 1.71e-2). A2
deliberately made the Dirichlet wall flux second order at N = 0 too, improving that error to
**1.65e-2**, below the 2.2774e-02 the assertion demands.

**Test defect or production defect? → TEST DEFECT (obsolete instrument).** The property the test
exists for still holds and is now measured directly; nothing in production is wrong.

**Replacement validation method.** The indirect proxy is replaced by direct, operator-level checks;
the solution-level check is preserved with its original factor:

- **D3** `solver.json` → `CaseDefinition` → `SIMPLESettings` → `nonOrthogonalOptions().enabled`,
  asserted for both 1 and 0.
- **D1** rows of the production momentum-diffusion assembly whose cell touches **no** boundary face —
  so the Dirichlet wall treatment contributes nothing to them at all — must be bitwise identical with
  the correction off and **differ** with it on.
- **D2** those same interior rows must be bitwise identical for N = 1 and N = 2, so the pass count
  changes the iteration, not the operator.
- **S1** `uncorrectedError >= 1.5 * correctedError`, unchanged.

**Threshold derivation.** D1/D2 are **bitwise** — an identity of the assembled operator, with the
boundary treatment excluded by construction (interior-only rows) rather than by a tolerance. D3 is
integer equality. S1 keeps its original 1.5 factor. Nothing was lowered.

**Negative control** (pre-freeze, `a3/logs/02_dryrun.log`): assembling with the correction forced off
while the case asks for N = 1 — a production path that ignored the setting — leaves interior rows
differing on **0** rows, so D1 **fails**. Required behaviour achieved.

**Positive control:** 196/196 interior rows differ on a sheared 16×16 (worst |Δrhs| 3.607e-05) and
900/900 on 32×32; an orthogonal 16×16 correctly reports 0 (the correction is bit-identical there), so
the instrument responds both ways.

**Result on the committed case:** `interior rows 372, changed by the correction 372, changed by a
second pass 0`; velocity L2 uncorrected 1.6547e-02 vs corrected 1.0827e-02, ratio **1.528** ≥ 1.5.
**PASS.**

## 3. A3-2 — `SectorConductionInterfaceConservation`

**Original intent.** "G5: heat crosses both interfaces exactly as it enters and leaves, and the
temperature converges at second order."

**Root cause of the failure.** `runSector`'s own comment stated its premise — "from the solver's own
two-point face coefficients (non_orthogonal_corrections 0: the flux is exactly coefficient * dT)" —
and it evaluated boundary faces as
`boundaryFaceDiffusionTerms(..., nullptr, false).coefficient * (T_P − T_b)`, explicitly requesting the
historical path. After A2 the Dirichlet wall flux is the DIFF-002 three-point form, so on boundary
faces the flux is no longer `coefficient · ΔT`. The instrument compared a production higher-order
boundary flux against a test-only two-point estimate, producing a spurious end-vs-interface spread of
8.6e-05 while the solver conserved to ~1e-12.

**Test defect or production defect? → TEST DEFECT (obsolete instrument).** Proven in the dry-run: with
the matching operator all four cross-sections agree to **8.1e-14 … 1.9e-12** and the per-cell discrete
balance holds to **1.5e-13 … 9.2e-12**.

**Replacement validation method.** Every face is evaluated with the **same operator production uses**,
and the flux is assembled independently in the test from the documented convention
(architecture.md §4):

```text
flux_into_owner = -(coefficient phi_P - farCellCoefficient phi_F)
                  + boundaryValueCoefficient phi_b + explicitFlux
```

Interior faces keep the existing two-point evaluation, which still matches production exactly. The
conservation statement is still formed independently — production computes neither a cross-section
heat flow nor a per-cell imbalance — and is still compared against the analytical `log(2)/sweep`. The
sign/orientation projection onto `+e_theta` is unchanged. Not circular: the test does not ask
production for the answer it then checks.

**Threshold derivation.** The assembled steady system makes every cell's net flux zero (no volumetric
source), satisfied to the linear solver's tolerance. The case configures `absolute_tolerance 1e-10`,
`relative_tolerance 1e-8`, so the per-cell residual target is `max(1e-10, 1e-8·|b|)`; with the flux
scale `Q = log(2)/sweep ≈ 0.147` the bound is the solver's own relative tolerance times that scale:

```text
bound = 1e-8 * Q ~= 1.471e-09    applied to the per-cell imbalance, the cross-section spread and |in - out|
```

Derived from the case configuration, not from the measurement, and **100× tighter** than the
`1e-6 · Q` it replaces. The recorded A2 values (7.4e-14, order ≈3.0) are historical evidence only.

**Negative control** (pre-freeze, all three resolutions): the legacy estimator gives per-cell imbalance
**3.264e-05 / 1.003e-05 / 3.047e-06** and spread **8.567e-05 / 3.810e-05 / 1.694e-05** — **FAIL** at
every resolution, missing the bound by three to four orders.

**Positive control:** the corrected instrument gives imbalance **1.505e-12 / 1.516e-13 / 9.171e-12**
and spread **8.116e-14 / 1.897e-12 / 1.237e-12** — **PASS** with ≥160× margin.

**Result (amended test, all three resolutions):** all four cross-sections equal to every printed digit
(e.g. 12×30: hot/if1/if2/cold all 0.1470663344); energy balance |in − out| 7.17e-14 / 7.11e-14 /
4.11e-14; temperature L2 order **2.998 / 2.999**; and the **added** coverage — the cross-section heat
flow converges to the analytic value at observed order **1.945 / 1.968** (formal 2, bound 1.5).
Coverage is strictly stronger than before: nothing deleted, one assertion 100× tighter, one new
convergence assertion added. **PASS.**

## 4. A3-5 — focused rerun: FAILED

Both amended validations pass individually (§2, §3). The four suites, with every target **rebuilt
first** (`a3/logs/05_A3_5_focused.log`):

| suite | run | passed | failed |
| --- | --- | --- | --- |
| CFDCaseIntegrationTests | 63 | **61** | 2 |
| CFDDiscretizationTests | 169 | 167 | 2 |
| CFDThermalTests | 92 | 79 | **13** |
| CFDTurbulenceTests | 136 | 136 | 0 |
| **total** | **460** | **443** | **17** |

`CFDCaseIntegrationTests` improves from A2's 59/63 to **61/63** — the two reconciled tests now pass,
leaving only W8's two historical tests. But 14 other tests fail:

| test(s) | classification |
| --- | --- |
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence`, `MultiBlockProductionCase.CurvedChannelGridConvergence` | **PRE-EXISTING** — W8's own tests, failing since before DIFF-002, not amended |
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | **PRE-EXISTING** — reproduces 1.93558 / 1.96913 / 1.98396 exactly; not amended |
| `BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne` | **NEWLY REVEALED** — obsolete assertion, same class as §2/§3 |
| 13 × `CFDThermalTests` (`EnergyEquationDiffusionTest` ×2, `EnergyEquationAssemblyTest`, `EnergyEquationThermalBCIntegrationTest`, `EnergyEquationVariablePropertiesTest` ×2, `RegionAwareThermalDiffusionTest`, `ThermalBoundaryConsistency` ×2, `SparseAssembly3DTest` ×4) | **NEWLY REVEALED** — obsolete assertions, same class |

**One uniform mechanism, verified on two samples.** All 14 are hand-derived assertions that encode the
**pre-A2 two-point Dirichlet wall coefficient at `non_orthogonal_corrections = 0`**:

- `EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients`: expected diagonal **21**,
  measured **24** — the boundary coefficient is now DIFF-002's.
- `BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne` (this phase's own W4 test),
  lines 345–347: its *baseline* block asserts that with the correction disabled the system is the
  two-point one — `A(1,1) = 5`, `A(1,4) = −1`, `rhs(1) = 6`. Measured **6**, **−4/3**, **8**: exactly
  the DIFF-002 values, i.e. precisely the invariance A2 was authorized to create. The assertion is
  obsolete for the same reason as the two tests A3 reconciled.

None of these indicates a production defect; each is an instrument encoding a premise A2 removed on
purpose. But **A3 authorizes reconciling two named tests**, not fourteen, so the phase stops rather
than expanding scope.

## 5. Disclosure: A1's and A2's W7 counts were measured with stale test binaries

`results/p12-diff-002/tools/run_w7.sh` (and the A2 copy) resolves each suite binary and rebuilds
**only if it is missing**:

```bash
BIN=$(find build/release -type f -name "$s" -perm -u+x | head -1)
if [ -z "$BIN" ]; then cmake --build build/release --target "$s" ...; fi
```

So any suite whose binary already existed ran **without being rebuilt against the current library**.
The library has been unchanged at `58be6b751328c9bb…` since A2's production edit, yet A2's W7 run
reported `CFDThermalTests 92/92` and `CFDDiscretizationTests 168/169`, while a forced rebuild of the
same sources against the same library gives **79/92** and **167/169**. The 14 failures therefore
existed throughout A2 and were not caused by A3 — they were simply not observed.

**Consequence: A2's reported W7 figure of 970 run / 966 passed / 4 failed is unreliable and
understated, and should not be used.** A3-5's figures above are from forced rebuilds. A fresh complete
W7 (A3-6) with forced rebuilds was **not** run, because A3-5 failed first and the gate order is not
bypassed — so no reliable complete W7 count exists yet. This is a defect in my own instrument, not in
the product, and it is the second time in this phase that an instrument rather than the code produced
a misleading gate result.

## 6. Files modified by A3

```text
tests/integration/case/test_structured_quad_production_case.cpp   A3-1 replacement instrument
tests/integration/case/test_multiblock_production_case.cpp        A3-2 replacement instrument
```

No production numerics (verified byte-identical, §1). No W8 test amended. No threshold weakened, no
coverage deleted, no assertion disabled or turned into logging, no DIFF-002 special-casing. The
GRAD-002 gradient-order test was not touched. `clang-format --dry-run --Werror` clean on both.

## 7. Decision requested

The 14 newly revealed obsolete instruments need an explicit scope decision: they are the same class of
defect as the two A3 reconciled, with the same uniform cause, and each would need its hand-derived
expectation re-derived against the post-A2 Dirichlet wall coefficient. Until that is authorized,
W7 cannot be green and W8/W9/W10 are not reachable.
