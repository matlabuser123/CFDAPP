# P12-DIFF-002 A4-2 — complete legacy-assumption inventory

Taken from **ground truth**, not from the subset A3 happened to look at: a full reconfigure, a full
build of every target, then the complete ctest suite (`a4/logs/01_full_inventory.log`,
`a4/logs/01_ctest_raw.log`), with per-failure assertion detail in `a4/logs/02_failure_reasons.log`.

```text
1913 tests run   1877 passed   36 failed   45 disabled   (98 % passed, 195.97 s)
library 58be6b751328c9bb   git HEAD b66310ca871811c4c7671056beec291a451af55c
```

**A3 saw 14 of these. The clean build shows 36.** A4 was scoped around the 14, so the inventory
below is the reason A4 stops before freezing rather than after.

---

## 1. Sub-class A — hand-derived boundary coefficient / RHS constants (18 tests)

These assert exact assembled coefficients computed by hand from the **pre-A2 two-point** Dirichlet
value `Γ|S|/d`. A2 replaced that with DIFF-002's `Γ|S|·cP` on faces with a valid inward stencil,
and added the far-cell entry `−Γ|S|·cF`, while keeping `Γ|S|/d` where no stencil exists.

| test | file | old → new (measured) |
| --- | --- | --- |
| `BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne` | test_boundary_reconstruction.cpp:345 | diag 5 → 6, off-diag −1 → −4/3, rhs 6 → 8 |
| `MomentumDiffusionTest.InternalFaceCoefficientsAreSymmetric` | test_momentum_diffusion.cpp:150 | boundary row changed |
| `MomentumVariableViscosityTest.InternalFaceMatchesHandDerivedLinearMuValue` | test_momentum_variable_viscosity.cpp:135 | " |
| `MomentumVariableViscosityTest.TabulatedMuGivesTheExpectedFaceValue` | test_momentum_variable_viscosity.cpp:181 | " |
| `EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients` | test_energy_equation.cpp:120 | diag 21 → **24**, off-diag −3 → −4, rhs 180 → 200 |
| `EnergyEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric` | test_energy_equation.cpp:152 | boundary row changed |
| `EnergyEquationAssemblyTest.CombinedAssemblyMatchesHandDerivedCoefficients` | test_energy_equation.cpp:417 | rhs + 20 |
| `EnergyEquationThermalBCIntegrationTest.FixedTemperatureGivesIdenticalResultToEquivalentFixedValue` | test_energy_equation.cpp:599 | rhs 180 → 200 |
| `EnergyEquationVariablePropertiesTest.DiffusionInternalFaceMatchesHandDerivedValue` | ..._variable_properties.cpp:132 | boundary row changed |
| `EnergyEquationVariablePropertiesTest.DiffusionBoundaryFaceUsesOwnerConductivityDirectly` | ..._variable_properties.cpp:160 | " |
| `RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly` | test_thermal_region.cpp:225 | 18 → **24**, 84 → **112** |
| `SparseAssembly3DTest.TwoCellsInX` | test_sparse_assembly3d.cpp:79 | 20 → **24**, −4 → **−16/3** |
| `SparseAssembly3DTest.TwoByTwoByOne` | " | 14 → **18**, −2 → **−8/3** |
| `SparseAssembly3DTest.TwoByTwoByTwo` | " | 9 → **12**, −1 → **−4/3** |
| `SparseAssembly3DTest.TwoCellsWithUpwindConvectionAndAZeroGradientOutlet` | " | 23 → **27**, −4 → **−16/3** |
| `SpeciesEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients` | test_species_equation.cpp:127 | rhs + 2 |
| `SpeciesEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric` | test_species_equation.cpp:154 | boundary row changed |
| `SpeciesEquationAssemblyTest.CombinedAssemblyUsesDensityTimesDiffusivityAsCoefficient` | test_species_equation.cpp:382 | rhs + 20 |

**Original intent:** exact matrix-assembly correctness and Dirichlet enforcement.
**Old assumption:** the boundary diagonal is `Γ|S|/d`, the RHS is `Γ|S|/d · φ_b`, and there is no
far-cell entry. **Affected by A2: yes.** **Disposition: `AMEND_IN_A4`** — the protected property
(exact assembly) is intact; only the reference formula is obsolete, and the new values are
independently derivable (see `derivations.md`).

## 2. Sub-class B — flux estimator that explicitly requests the pre-A2 path (2 tests)

| test | file | measured |
| --- | --- | --- |
| `ThermalBoundaryConsistency.ConvergedFieldIsConsistentWithItsBoundaryValues` | test_thermal_boundary_consistency.cpp:193 | worst cell 9.83e-02 vs 5.75e-10 |
| `ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo` | :247 | same class |

Its helper (`boundaryHeatFlowOut`, :79–88) calls
`boundaryFaceDiffusionTerms(..., nullptr, false).coefficient * (T_P − T_b)` — exactly the defect A3-2
already fixed in the sector test. **Disposition: `AMEND_IN_A4`**, identical remedy: evaluate with the
operator production actually uses and sum conservation independently.

## 3. Sub-class C — expected value is the closed form of the SUPERSEDED discretization (3 tests)

| test | measured |
| --- | --- |
| `PoiseuilleValidation.Profile` | `profile_matches_discrete_exact` max\|u − u_discrete\| **0.0148** (bound 1e-4); `pressure_gradient_matches_discrete_exact` **0.0224** (bound 1e-3) |
| `PoiseuilleValidation.PressureDrop` | same checks |
| `PoiseuilleValidation.ProductionGridConvergence` | 64×8 0.0148, 96×12 0.00782 |

These compare the solution against **the old scheme's own discrete-exact closed form**
`G_discrete/G_exact = ny²/(ny²+2)` — precisely the half-cell wall error DIFF-002 removes. Measured
centerline **1.46511967** against that formula's 1.45454545, with the continuous exact value 1.5:

```text
old scheme   |1.5 - 1.45454545| = 0.04545   (3.03 %)
new scheme   |1.5 - 1.46511967| = 0.03488   (2.33 %)   -> 1.30x MORE accurate
```

So accuracy improved; the reference formula is obsolete. **Disposition: `AMEND_IN_A4`** in principle,
but the replacement reference must be re-derived for the DIFF-002 wall flux, which is a new
derivation rather than a constant swap.

## 4. Sub-class D — observed-order UPPER band exceeded because accuracy improved (4 tests + 1 pre-existing)

| test | measured |
| --- | --- |
| `MomentumMMS.UConverges` | v L∞ order **2.599** vs band [1.60, 2.40]; `u_boundary_ring` reduction factors 8.151/7.695/7.823 → order ≈ **3.0** |
| `MomentumMMS.VConverges` | same family |
| `SIMPLEMMS.VelocityConvergesAtExpectedOrder` | u L2 **2.675**, u L1 2.604, v L2 **3.104**, v L1 3.138 vs [1.60, 2.40] |
| `SIMPLEMMS.DistortedMesh` | u L2 **2.921**, u L1 2.887 vs [1.60, 2.40] |
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | 1.93558 / 1.96913 / 1.98396 — **PRE-EXISTING / GRAD-002-ERA**, `KEEP_UNCHANGED`, not amendable under A4 |

Every u/v L1/L2/L∞ error is *monotone decreasing*; the failures are all **upper**-bound violations of
an order band, driven by the boundary ring now converging at roughly third order. **Disposition for
the four MMS tests: `AMEND_IN_A4` only if the upper band is re-derived on principle.** An upper band
also legitimately guards against error-cancellation artifacts, so raising it is a numerical-policy
judgement, not a mechanical re-derivation — flagged rather than assumed.

## 5. Sub-class E — recorded iteration/status baselines (2 tests)

| test | measured |
| --- | --- |
| `SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline` | enum/count 0 vs 8 at :126; :196 |
| `SIMPLERobustnessTest.DivergenceStatus` | 21 vs 20 iterations |

**Disposition: `AMEND_IN_A4`** (recorded baselines, same character as W5's iteration counts) — but see
§7: they were not investigated in depth because A4 stops first.

## 6. Sub-class F — conservation / benchmark tolerances genuinely exceeded — **NOT** classifiable as obsolete instruments (4 tests)

| test | measured | bound | overshoot |
| --- | --- | --- | --- |
| `SpeciesConservationTest.OpenChannelWithVolumetricSourceBalancesNetOutflowAgainstSource` | **1.5873e-03** | 1e-3 | 1.59× |
| `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | **1.03433e-04** | 1e-4 | 1.03× |
| `NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3` | v_max error **0.13026** | 0.12 | 1.09× |
| `NaturalConvectionValidation.Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3` | **0.13026** | 0.12 | 1.09× |

These are **exactly the properties A4-3 lists as valid protected properties** — species conservation,
flux/mass conservation, and an independent literature benchmark (De Vahl Davis). Their references are
*not* the superseded discretization, so the "obsolete instrument" explanation that covers §1–§5 does
**not** apply. Each is a modest tolerance overshoot (1.03×–1.59×), consistent either with a genuine
mild accuracy/conservation degradation from the newly active boundary reconstruction, or with
tolerances calibrated to the old scheme. A3-2 did prove the *thermal* operator conserves per-cell to
~1e-12, so the operator is conservative in that path; these cases add convection, buoyancy and a
volumetric source, which this phase has not isolated.

**Disposition: `PRODUCTION_DEFECT_SUSPECTED`.** A4-3 is explicit: "If any failure instead reveals that
production violates its intended mathematical property: STOP. Do not amend it." I have **not** amended
them and have **not** modified production. I also do not claim a defect is proven — establishing or
excluding one needs an authorized investigation with production-level probing, which A4 forbids.

## 7. Sub-class G — pre-existing, out of A4 scope (2 tests)

`StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` and
`MultiBlockProductionCase.CurvedChannelGridConvergence` — W8's own tests, failing since before
DIFF-002. **Disposition: `KEEP_UNCHANGED`.**

---

## 8. Summary of dispositions

| disposition | count | tests |
| --- | --- | --- |
| `AMEND_IN_A4` (mechanical re-derivation) | 20 | §1 (18) + §2 (2) |
| `AMEND_IN_A4` (needs a new reference derivation / policy call) | 9 | §3 (3) + §4 (4) + §5 (2) |
| `PRODUCTION_DEFECT_SUSPECTED` | 4 | §6 |
| `KEEP_UNCHANGED` (pre-existing) | 3 | §4 gradient test + §7 (2) |
| **total** | **36** | |

## 9. Why A4 stops here

A4 authorized amending a scoped set built around A3's 14 findings, and required that once the gate is
frozen "NO additional test may be added to A4". The clean-build inventory shows **36** failures, of
which **33** are A2-related across six distinct sub-classes — including nine that need genuinely new
reference derivations or a numerical-policy decision, and four that cannot be classified as obsolete
instruments at all and instead suspect a production-level conservation/accuracy regression.

Freezing an A4 gate over that set would either understate the scope or quietly expand A4 into a much
larger phase, and amending §6 is explicitly forbidden by A4-3. So **no A4 gate was frozen and no test
was amended**; the inventory is delivered for a scope decision. Production numerics are unchanged and
verified byte-identical (`production_freeze.txt`).
