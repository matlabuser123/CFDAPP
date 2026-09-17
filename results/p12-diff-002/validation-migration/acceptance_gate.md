# P12-DIFF-002 — validation-migration acceptance gate

Authorized 2026-09-16 (Step 2 of the 10-step sequence). Frozen **before any test is changed**.
Status on entry: `P12-DIFF-002 BLOCKED — ADDITIONAL LEGACY VALIDATION FOUND`.

Scope: migrate validation instruments that evaluate the **superseded two-point wall operator**, and
nothing else. No production numerical code is changed; the production hashes in
`a4/production_freeze.txt` must hold throughout. No historical failure, amendment or evidence file is
rewritten.

**Stop rule.** Stop at the first failed criterion. Never adjust a threshold to make a test pass.

---

## 1. Evidence this gate rests on

| finding | where | what it established |
| --- | --- | --- |
| the assembled DIFF-002 boundary stencil is **conservative** | INV-002 §1 | constant-field net flux ≤ 2.13e-14; telescoping mismatch ≤ 1.10e-15 relative, on 12 mesh families incl. all-fallback, mixed, sheared, graded, 3D |
| species "imbalance" is the **test's own first-order estimator** | INV-002 §2 | consistent operator gives 3.48e-08 vs the test's 1.587e-03 — 45 600×; independent Σ(AY−b) route agrees at −3.5e-09 |
| low-Mach metric **does not converge in the pre-DIFF-002 build either** | INV-002 §3 | baseline 4.80e-05 → 1.03e-04 → 1.07e-04 at 32×6 → 64×12 → 128×24; already over 1e-4 pre-DIFF-002 |
| natural convection is caused by the **momentum wall shear alone** | INV-002 Step 1 | 2×2 factorial: thermal-only leaves u_max/v_max unchanged (0.10365/0.06631 vs 0.10442/0.06714); momentum-only reproduces the whole effect (0.13764/0.13089) |
| the reconstruction is **exact for linear and quadratic**, 2nd order for cubic | W1, W4 | quadratic exactness ≤ 1.9e-16; cubic order 1.963–2.000; 13/13 hand-derived matrix tests |
| where an analytic reference exists the momentum path **improves** | W6 / A2-6, A4 §C | distorted Poiseuille dp/dx 0.6788 % → 0.207 %; Cartesian centerline 1.45455 → 1.46512 against exact 1.5 |
| the coefficient changes are **hand-derivable**, matching production exactly | a4/derivations.md | thermal two-cell diag 21 → 24, rhs 180 → 200; 3D 20 → 24, −4 → −16/3; W4 baseline 5 → 6, −1 → −4/3, 6 → 8 |

## 2. Tests authorized for migration (**M**) — 23

Migrated because each **evaluates the superseded two-point wall operator** and its protected property
is intact.

### M-A — hand-derived boundary coefficient / RHS constants (18)

`BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne`;
`MomentumDiffusionTest.InternalFaceCoefficientsAreSymmetric`;
`MomentumVariableViscosityTest.InternalFaceMatchesHandDerivedLinearMuValue`;
`MomentumVariableViscosityTest.TabulatedMuGivesTheExpectedFaceValue`;
`EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients`;
`EnergyEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric`;
`EnergyEquationAssemblyTest.CombinedAssemblyMatchesHandDerivedCoefficients`;
`EnergyEquationThermalBCIntegrationTest.FixedTemperatureGivesIdenticalResultToEquivalentFixedValue`;
`EnergyEquationVariablePropertiesTest.DiffusionInternalFaceMatchesHandDerivedValue`;
`EnergyEquationVariablePropertiesTest.DiffusionBoundaryFaceUsesOwnerConductivityDirectly`;
`RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly`;
`SparseAssembly3DTest.TwoCellsInX`; `…TwoCellsWithUpwindConvectionAndAZeroGradientOutlet`;
`…TwoByTwoByOne`; `…TwoByTwoByTwo`;
`SpeciesEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients`;
`SpeciesEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric`;
`SpeciesEquationAssemblyTest.CombinedAssemblyUsesDensityTimesDiffusivityAsCoefficient`.

**Protected property:** exact matrix assembly and Dirichlet enforcement — retained. **Obsolete part:**
the expected numbers encode `Γ|S|/d` with no far-cell entry.

### M-B — flux estimators that request the pre-A2 path (3)

`ThermalBoundaryConsistency.ConvergedFieldIsConsistentWithItsBoundaryValues`;
`ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo`;
`SpeciesConservationTest.OpenChannelWithVolumetricSourceBalancesNetOutflowAgainstSource`.

**Protected property:** discrete conservation — retained and, per §1, verified to hold far tighter
than the old assertions could see. **Obsolete part:** the estimator (`… nullptr, false` or an inline
`Γ|S|/d·(φ_P − φ_b)`).

### M-E — recorded iteration/status baselines (2)

`SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline`; `SIMPLERobustnessTest.DivergenceStatus`.

**Protected property:** the robustness machinery's behaviour and status reporting. **Obsolete part:**
recorded iteration counts/status from the superseded wall operator.

## 3. Tests that must remain UNCHANGED (**U**) — 13

| group | tests | why unchanged |
| --- | --- | --- |
| U-C superseded discrete-exact Poiseuille references | `PoiseuilleValidation.Profile`, `.PressureDrop`, `.ProductionGridConvergence` | the replacement reference — the discrete-exact channel solution of a **three-point** wall flux — **does not exist yet**. Deriving it is new numerical work, not an instrument migration. Accuracy demonstrably improved (centerline 1.45455 → 1.46512 vs exact 1.5), but the new reference must be derived and reviewed first |
| U-D MMS observed-order upper bands | `MomentumMMS.UConverges`, `.VConverges`, `SIMPLEMMS.VelocityConvergesAtExpectedOrder`, `.DistortedMesh` | the upper band legitimately guards against error-cancellation artifacts. Raising it is a numerical-**policy** decision, not a derivation. All errors are monotone; the band is exceeded because the boundary ring now converges at ≈3rd order |
| U-F natural-convection benchmark bounds | `NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3`, `…ConstantPropertyModels…` | the reference is independent literature and must not be redefined. Step 1 shows a genuine trade-off (wall flux better, coarse-grid interior peaks worse) that does not vanish under refinement; the bounds need a reviewed decision, not a migration |
| U-F low-Mach | `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | the metric does not converge in the pre-DIFF-002 build either and already exceeds 1e-4 there at 64×12. Its **formulation** needs review, separately |
| U-H historical / pre-existing | `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence`, `MultiBlockProductionCase.CurvedChannelGridConvergence`, `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | W8 and GRAD-002 history; explicitly excluded by every authorization in this chain |

**Consequence, stated before the work starts:** with 13 tests deliberately left failing, Step 5's
"zero unexplained new numerical failures" can be met (each is explained and frozen here), but Step 6's
W7 — "focused verification passes, each against its own originally frozen thresholds" — **cannot**.
That is recorded here rather than discovered later.

## 4. Criteria

| id | criterion | threshold |
| --- | --- | --- |
| **VM-P** | production numerical files byte-identical to `a4/production_freeze.txt` | every sha256 unchanged |
| **VM-1** | every migrated expected value is **independently derived** from the DIFF-002 geometry (`cP = h2/(h1(h2−h1))`, `cF = h1/(h2(h2−h1))`, `cB = 1/h1 + 1/h2`) and recorded, never copied from production output | a derivation per changed number, in `derivations.md` |
| **VM-2** | each migrated test states, in the test source, the physical/numerical reason for the migration | present in every changed test |
| **VM-3** | conservation tests verify the production balance **independently** (own summation, own comparison to the analytic value), not by asking production for its answer | per test |
| **VM-4** | migrated conservation tolerances are **derived**, and no looser than the assertion they replace | species keeps 1e-3; thermal-consistency keeps its `1e-9 × throughput` form |
| **VM-5** | negative control per migrated group: the old operator/estimator must FAIL the new assertion | demonstrated before the gate is satisfied |
| **VM-6** | no test deleted, disabled, converted to logging, or special-cased by name; coverage equal or stronger | inspected |
| **VM-7** | the 13 **U** tests are untouched | sha256 of their files, or line-level inspection where they share a file with a migrated test |
| **VM-8** | after migration, the 23 **M** tests pass and the four affected suites show no failure other than the 13 **U** tests | exact counts from a clean build |

## 5. Preserved

Every old expected value appears in §2/§3 of `derivations.md` or in the A3/A4/INV-002 evidence
already on record, and no evidence file from any earlier phase is modified. The A2 W7 aggregate stays
labelled `INVALID AS AUTHORITATIVE W7 EVIDENCE — STALE TEST BINARIES`.
