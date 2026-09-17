# P12-DIFF-002 — Amendment A3 acceptance gate (W7 validation reconciliation only)

Authorized 2026-09-16 after A2. Status on entry: `P12-DIFF-002 BLOCKED / FAILED A2 GATE`.

**This is a validation/test amendment only.** A1 and A2 production implementations are not authorized
for further numerical modification. The production numerical files were hashed before A3
(`a3/logs/01_production_before_A3.log`) and must be **byte-identical** afterwards; if not, A3 FAILS.

Preserved, not rewritten: the original gate `51079f6d…`, A1 `c4b08824…`, A2 `3a268060…`, the original
W3b failure (`logs/08`, `logs/09`), A1's PASS (`a1/logs/04`), A2's W7 failure (`a2/logs/10`).
Accepted A2 state: `W3b-A1 PASS`, `W4 PASS`, A2 activation gates PASS, k-ε PASS, `W5 PASS`,
`W6 PASS`, `W7 FAIL`.

**Stop rule.** Stop at the first failed criterion. Never adjust a threshold after seeing a result.

---

## 1. A3-1 — `StructuredQuadProductionCase.NonOrthogonalCorrectionIsActiveOnTheProductionPath`

**Original intent** (test comment, lines 422–425): "non_orthogonal_corrections reaches the distorted
mesh through the production path and is what makes it accurate: without it the 64x8 solution's
velocity error is ~2x the corrected one (measured 3.31e-2 vs 1.71e-2, i.e. 2.2x the Cartesian error)
and the refined grids diverge". It is an **activation** test: does the solver.json setting actually
reach the assembly and take effect.

**Original assertions**

```cpp
EXPECT_GE(uncorrectedError, 1.5 * correctedError);          // still passes
EXPECT_GT(uncorrectedError, 1.5 * cartesianVelocityL2(8));  // FAILS: 0.016547 vs 0.022774
```

**Why A2 invalidated it.** The second assertion is an *indirect proxy*: it infers "the correction is
active" from "the N = 0 solution is measurably bad". A2 deliberately made the Dirichlet wall flux
second order at N = 0 as well, improving the N = 0 velocity error from the recorded **3.31e-2** to
**1.65e-2**. The premise the assertion encodes — that N = 0 is substantially worse than the Cartesian
discretization — is exactly what A2 was authorized to remove. This is a **test-instrument defect,
not a production defect**: the property the test wants (the setting reaches the assembly and switches
the internal-face correction on) still holds, and is now measured directly.

**Replacement mathematical property.** On a non-orthogonal mesh, the internal-face non-orthogonal
correction must be *absent* at `non_orthogonal_corrections = 0` and *present* at ≥ 1, while the
Dirichlet wall-flux scheme is unaffected by that setting.

**Replacement instrument** (operator level, plus the preserved solution-level check):

- **D1 — direct activation.** Rows of the production momentum-diffusion assembly whose cell touches
  **no** boundary face (so the wall treatment contributes nothing to them at all) must be **bitwise
  identical** between assemblies that differ only in the correction being off, and must **differ**
  once it is on.
- **D2 — pass count does not change the operator.** Those same interior rows must be bitwise
  identical between N = 1 and N = 2.
- **D3 — case-level propagation.** `solver.json`'s value must flow through `CaseReader` →
  `CaseDefinition::solver.nonOrthogonalCorrections` → `SIMPLESettings` → the production
  `nonOrthogonalOptions().enabled`.
- **S1 — preserved solution-level check.** `uncorrectedError >= 1.5 * correctedError`, **unchanged**,
  on the committed 45°-distorted production case: enabling the correction still improves the
  distorted solution by at least 1.5×.

**Threshold derivation.** D1/D2 are **bitwise** — no tolerance to derive; the quantity is an identity
of the assembled operator, and interior-only rows exclude the boundary treatment by construction
rather than by tolerance. D3 is integer equality. S1 keeps its original 1.5 factor unchanged.

**Negative control.** Assemble with the correction forced off while the case asks for N = 1 — the
behaviour of a production path that ignored the setting. Measured: interior rows then differ on
**0** rows, so D1 **fails**, as required.

**Positive control.** With the real production path: interior rows differ on **196 of 196**
(sheared 16×16, s = 0.45) and **900 of 900** (32×32), worst |Δrhs| 3.607e-05 / 2.405e-05 → D1 passes.
D2: N1 vs N2 differ on 0 rows. D3: poiseuille_distorted 1 → 1 → enabled 1; poiseuille_flow
0 → 0 → enabled 0; sector 0 → 0 → enabled 0.

**Non-vacuity evidence** (`a3/logs/02_dryrun.log`, recorded before this freeze). A third row,
**orthogonal** Cartesian 16×16, reports 0 differing rows — i.e. the instrument correctly says "not
applied" where the non-orthogonal correction genuinely has nothing to do (it is documented and
verified bit-identical on an orthogonal mesh). So "differs" is a real signal with a demonstrated
both-ways response, not an artifact, and the test must therefore be scoped to a non-orthogonal mesh.

## 2. A3-2 — `MultiBlockProductionCase.SectorConductionInterfaceConservation`

**Original intent** (test comment, line 588–589): "G5: heat crosses both interfaces exactly as it
enters and leaves, and the temperature converges at second order."

**Original assertions:** `spread ≤ 1e-6 · exactFlow` across the four key cross-sections (hot end,
interface 1, interface 2, cold end); `|in − out| ≤ 1e-6 · exactFlow`; temperature L2 decreasing with
observed order ≥ 1.5; the committed mesh equals the medium grid to one ulp.

**Why A2 invalidated the instrument.** `runSector`'s own helper comment states its premise: "discrete
heat flow through every radial face line **from the solver's own two-point face coefficients
(non_orthogonal_corrections 0: the flux is exactly coefficient * dT)**". It evaluates boundary faces
as

```cpp
boundaryFaceDiffusionTerms(mesh, face, k, distance, nullptr, false).coefficient
    * (temperature[owner] - tb)
```

— explicitly requesting the historical two-point path. After A2 the solver's Dirichlet wall flux is
the DIFF-002 three-point form, so on **boundary** faces the flux is no longer `coefficient · ΔT`;
interior faces are unaffected (the interior correction is still gated and this case runs N = 0). The
instrument therefore compares a production higher-order boundary flux against a test-only two-point
estimate. **Test-instrument defect, not a production defect** — established in the dry-run: with the
matching operator all four cross-sections agree to **8.1e-14 … 1.9e-12**, and the per-cell discrete
balance holds to **1.5e-13 … 9.2e-12**.

**Replacement mathematical property.** The assembled steady conduction system makes every cell's net
flux zero (there is no volumetric source), satisfied to the linear solver's tolerance. Hence (a) any
correct flux evaluation reproduces a per-cell balance at that level, and (b) cross-section flows
agree by telescoping. Plus the original purposes: interface flux continuity, global energy
conservation, temperature accuracy and grid convergence.

**Replacement instrument.** Evaluate every face with the **same operator production uses** and
assemble the flux independently from the documented convention (architecture.md §4):

```text
flux_into_owner = -(coefficient * phi_P - farCellCoefficient * phi_F)
                  + boundaryValueCoefficient * phi_b + explicitFlux
```

then sum independently over each cross-section, and separately accumulate the per-cell net flux.
Production never computes a cross-section heat flow or a per-cell imbalance, so this is not circular:
the test forms its own conservation statement and still compares against the analytic
`Q = log(2)/sweep`. Interior faces keep the existing `internalFaceDiffusionTerms` evaluation, which
still matches production exactly. Sign/orientation independence is preserved: the line flow is
projected onto `+e_theta` with the face's own area-vector sign, as before.

**Threshold derivation.** The case's linear solver is configured `absolute_tolerance 1e-10`,
`relative_tolerance 1e-8`, so the per-cell residual target is `max(1e-10, 1e-8·|b|)`; with the flux
scale `Q = log(2)/sweep ≈ 0.147` the bound is the solver's own relative tolerance times that scale:

```text
bound = 1e-8 * Q ~ 1.471e-09      applied to BOTH the per-cell imbalance and the cross-section spread
```

Derived from the case configuration, not from the measurement. It is **100× tighter** than the
assertion it replaces (`1e-6 · Q`), so this is strictly stronger assurance. The recorded A2 values
(energy balance ≈ 7.4e-14, temperature L2 order ≈ 3.0) are historical evidence and are **not** used
as the acceptance threshold.

**Negative control.** The legacy estimator, on the same solutions: per-cell imbalance
**3.264e-05 / 1.003e-05 / 3.047e-06** and spread **8.567e-05 / 3.810e-05 / 1.694e-05** at
nt = 20 / 30 / 45 — **FAIL** at every resolution, missing the bound by three to four orders.

**Positive control.** The corrected instrument on the same solutions: per-cell imbalance
**1.505e-12 / 1.516e-13 / 9.171e-12**, spread **8.116e-14 / 1.897e-12 / 1.237e-12** — **PASS** at
every resolution, with ≥ 160× margin, and all four cross-sections equal to all printed digits.

**Non-vacuity evidence** (`a3/logs/02_dryrun.log`, before this freeze): both controls were run at
**all three** resolutions the amended test uses, and the legacy/corrected separation is three to four
orders of magnitude in the same instrument. Additionally the corrected flux accuracy against the
analytic value improves monotonically **3.599e-04 → 1.636e-04 → 7.366e-05** (observed order 1.94 /
1.97, formal 2), which the amended test asserts at ≥ 1.5.

## 3. Criteria

| id | criterion | threshold |
| --- | --- | --- |
| **A3-P** | Production numerical files byte-identical before and after A3 | every sha256 in `a3/logs/01_production_before_A3.log` unchanged |
| **A3-1** | The amended activation test passes, with D1/D2 bitwise, D3 exact, S1 at its original 1.5 factor | as §1 |
| **A3-2** | The amended sector test passes, per-cell imbalance and cross-section spread ≤ `1e-8 · Q`, energy balance and temperature order ≥ 1.5 as before | as §2 |
| **A3-5** | Focused rerun: the two amended validations individually, then `CFDCaseIntegrationTests`, `CFDDiscretizationTests`, `CFDThermalTests`, `CFDTurbulenceTests`, with exact counts | both amended tests pass; no new failure. `GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` stays PRE-EXISTING only while it reproduces 1.93558 / 1.96913 / 1.98396 |
| **A3-6** | Fresh, complete W7 rerun — the A2 counts are not reused | fresh exact counts; any new unexplained failure STOPS |
| **A3-7** | W8 rerun **unchanged**, both tests, raw errors at every resolution recorded before any order/GCI is computed | reported, not presupposed, not amended. If either fails: STOP |
| **A3-8** | W9 / W10 only if W8 passes | frozen gate order not bypassed |

## 4. Restrictions

No change to DIFF-002 production numerics; no amendment of W8, of the GRAD-002 gradient-order test,
of the k-ε threshold; no MESH-007 resumption; no sanitizer or warped-face fix; no new phase; no
commit; no push. Only the minimum necessary test code is modified — no coverage deleted, no
assertion disabled or turned into logging, no DIFF-002 special-casing.

## 5. Verdicts

```text
either original test exposes a production defect -> P12-DIFF-002 A3 BLOCKED — PRODUCTION DEFECT FOUND
an A3 criterion fails                            -> P12-DIFF-002 BLOCKED / FAILED A3 GATE
A3 and fresh W7 pass but W8 fails                -> P12-DIFF-002 BLOCKED — HISTORICAL W8 GATE DECISION REQUIRED
all required gates pass                          -> P12-DIFF-002 COMPLETE UNDER AMENDMENTS A1/A2/A3 —
                                                    READY FOR GRAD-002 REVALIDATION
```
