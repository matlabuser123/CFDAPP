# P18.2 — coverage map (§39)

Every P18.2 requirement mapped to the test that **asserts** it. §39 says not to trust test names or
printed metrics but to check assertions, so each row names what is asserted, not what is reported.

---

## 1. The six checklist items

| Requirement | Asserted by | Result |
|---|---|---|
| Conservative species transport | `TransientUsesBothDensityLevels` (a_P = ρ^{n+1}V/dt and b_P = ρ^nY^nV/dt against hand arithmetic) · `ClosedEnclosureConservesEverySpeciesMass` · `VariableDensityConservesRhoYNotY` | PASS |
| Species-dependent diffusivity | `EachSpeciesReceivesItsOwnDiffusivity` (J_B/J_A = −D_B/D_A exactly) · `GammaIsRhoTimesD` · `SumVanishesForThreeSpeciesWithThreeDistinctDiffusivities` | PASS |
| Boundary treatment | `ExactGradientBoundaryConvergesInAtMostTwoOuterIterations` (G4(c)) · `ZeroGradientWallCarriesExactlyZeroDiffusiveFlux` · `OpenDomainBalanceClosesWithEachTermSeparate` | PASS |
| Zero-reaction limit | `ZeroSourceContributesExactlyZero` (Su and Sp exactly 0, every species, every cell) · `RejectsDegenerateShapes` | PASS |
| Global species conservation | `SpeciesConservationTest` — all 7 | PASS |
| Canonical / manufactured tests | `SlabDiffusionMatchesTheAnalyticalProfileOnThreeGrids` (G4(a)) · `ConvectionDiffusionManufacturedSolutionConvergesAtFirstOrder` · `PureAdvectionConservesMassAndBoundsWithoutRequiringZeroDiffusion` | PASS |

## 2. Section-by-section

| § | Requirement | Asserted by |
|---|---|---|
| 2 | `ρY` conservative, `∂(ρY)/∂t` not `ρ∂Y/∂t` | `TransientUsesBothDensityLevels` · N1 |
| 3 | authoritative mass flux reused, never recomputed | structural — 0 flux-forming calls in the source (`negative_controls.md` §3) |
| 4 | conservative face application, owner + neighbour = 0 | `UniformCompositionSatisfiesTheConvectionEquation` · `ConstantFieldGivesExactlyZeroDiffusiveFluxEverywhere` |
| 5 | species-dependent `D_i`, units documented | `GammaIsRhoTimesD` · `formulation.md` §4 |
| 6 | `ΣJ_i = 0` closure | `SumOfCorrectedFluxesVanishesWithUnequalDiffusivities` + NV1 inside it |
| 7 | no post-step normalisation | `SumOfMassFractionsStaysOneWithoutNormalisation`; and structurally — 0 normalisation calls on any accepting path |
| 8 | bounds checked, never clipped | `SumOfMassFractionsStaysOneWithoutNormalisation` (min/max reported) |
| 9 | constant-density limit | `SlabDiffusionMatchesTheAnalyticalProfileOnThreeGrids` · `formulation.md` §7 |
| 10 | variable-density transport | `VariableDensityConservesRhoYNotY` (ρ ratio 2.75, asserted non-vacuous) |
| 11 | transient closed-domain conservation | `ClosedEnclosureConservesEverySpeciesMass` |
| 12 | open-domain conservation, terms separate | `OpenDomainBalanceClosesWithEachTermSeparate` — four terms reported individually |
| 13 | `Σ_i M_i` = total mass | `TotalMixtureMassEqualsTheSumOfSpeciesMasses` (mixture mass computed independently from ρ) |
| 14 | zero-reaction limit exact | `ZeroSourceContributesExactlyZero` |
| 16 | boundary semantics documented | `boundary_treatment.md` |
| 17 | boundary lag fixed | `boundary_lag.md` · G4(c) tests |
| 18 | lag control asserts the count, not the field | `LaggedGradientBoundaryNeedsFarMoreIterations` + `ExactAndLaggedAgreeOnceConverged` |
| 19 | Dirichlet composition | the slab and MMS cases prescribe a complement summing to exactly 1 |
| 20 | zero-gradient impermeable wall | `ZeroGradientWallCarriesExactlyZeroDiffusiveFlux` |
| 22 | outlet introduces no source | `OpenDomainBalanceClosesWithEachTermSeparate` |
| 23 | inlet composition valid | the open-domain case prescribes 0.3 / 0.7 |
| 24 | diffusion-only canonical | `SlabDiffusionMatchesTheAnalyticalProfileOnThreeGrids` + `TheSlabInstrumentCanDistinguishAWrongProfile` |
| 25 | pure advection | `PureAdvectionConservesMassAndBoundsWithoutRequiringZeroDiffusion` |
| 26 | convection-diffusion MMS | `ConvectionDiffusionManufacturedSolutionConvergesAtFirstOrder` |
| 27 | `D_1 ≠ D_2 ≠ D_3` | `SumVanishesForThreeSpeciesWithThreeDistinctDiffusivities` · N4 |
| 28 | uniform composition → `J = 0` | `UniformCompositionGivesExactlyZeroFluxEverywhere` (exact equality) |
| 29 | uniform-flow preservation | `UniformStateIsPreservedToRoundOff` |
| 30 | `max|ΣJ_i|` measured | `SumOfCorrectedFluxesVanishesWithUnequalDiffusivities` · `logs/09_` |
| 31 | `ΣY = 1` after transport | `SumOfMassFractionsStaysOneWithoutNormalisation` |
| 32 | per-species conservation | `ClosedEnclosureConservesEverySpeciesMass` loops species |
| 33 | internal-face cancellation | `SpeciesFaceConservationTest`, both tests |
| 34 | manufactured transient, wrong form detectable | `TransientUsesBothDensityLevels` · N1 (ratio exactly 1.10) |
| 35 | validity through transport | `SumOfMassFractionsStaysOneWithoutNormalisation` |
| 36 | determinism | `ClosureAndAssemblyAreBitwiseDeterministic` (20 repeats, bitwise) |
| 38 | N1–N8 | `negative_controls.md` |

## 3. Gates

```text
MAY CLAIM
  G4  transport / diffusion correctness -- IN FULL.
      (a) slab profile, 3 grids, l2 and linf < 1e-6, inherited tolerances      PASS
      (b) the MMS case exercises convection and diffusion together; the observed order
          criterion replaces an absolute tolerance, derived because P14's convection is
          first-order upwind                                                    PASS
      (c) <= 2 outer iterations on pure Neumann, with the lagged control failing it  PASS
      (d) constant field -> exactly zero diffusive flux, bitwise                PASS
  G3  species conservation -- EVIDENCE TOWARD, per-species and mixture balances closed. G3's
      phase is P18.7; P18.2 does not close it.
  G1  baseline preservation -- the regression half.
  G2  species-state validity -- preserved THROUGH transport, which is new evidence for it.
  G10 determinism -- the assembly/closure layer only, bitwise. NOT the solver-level claim.

MUST NOT CLAIM, and do not
  G5 mixture thermodynamics · G6 reaction rates · G7 reacting energy · G8 reacting validation ·
  G9 CPU/GPU equivalence · G11 final qualification.
  None is measurable from non-reacting transport, and §40 forbids claiming them from it.
```

## 4. Deliberately not covered

```text
§21 prescribed diffusive flux    no BC type prescribes a species flux; there is nothing to verify.
                                 Recorded as absent capability, not as a passing criterion.
§37 GPU                          no CUDA species path exists; P18.8's. G9 NOT claimed.
case-file reachability           the new conservative transport is LIBRARY-LEVEL. It is not reachable
                                 from a case file, because that needs the carrier-species decision
                                 §3 of compatibility deferred to P18.2's successor. The pre-P18
                                 passive path remains the production path and is unchanged.
                                 STATED AS A LIMIT, not claimed.
```

## 5. Every enforced property has a control that breaks it

```text
the closure          N5 -> species sum of the assembled system moves by 1e14
per-species D_i      N4 -> Sigma_f goes from 1e-8-scale to < 1e-18, corrected fluxes differ
the transient form   N1 -> RHS scales by exactly the density ratio 1.10
the boundary         N6 -> G4(c)'s <= 2 iterations fails
the sum bound        N8 -> drift scales with the solver tolerance across 7 orders
the slab norm        TheSlabInstrumentCanDistinguishAWrongProfile
the diffusion sign   N3, which arose as a real defect and was caught at 1.29e-3
```
