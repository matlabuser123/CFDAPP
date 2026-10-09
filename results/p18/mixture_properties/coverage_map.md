# P18.3 — coverage map

Every requirement mapped to the test that **asserts** it. Not to a test name, and not to a printed
metric — to an assertion.

---

## 1. The seven checklist items

| Requirement | Asserted by | Result |
|---|---|---|
| Mixture molecular weight | `MolecularWeightUsesTheMassFractionRelation` · `MolecularWeightIsCompositionSymmetric` · N1 | PASS |
| Mixture gas constant | `GasConstantAgreesWithTheReference` · `TheTwoGasConstantFormulationsAgree` · N2 | PASS |
| Mixture `cp` | `SpecificHeatIsMassWeighted` · `TemperatureDependentCpFlowsThroughTheMixture` · N3 | PASS |
| Mixture `cv` | `CvAndGammaComeFromTheExistingClosure` · `N4_CvFromTheClosureAgreesWithTheSpeciesSum` | PASS |
| Mixture `gamma` | `CvAndGammaComeFromTheExistingClosure` · `N5_DirectlyAveragedGammaIsDetected` | PASS |
| Composition-dependent density / EOS | `DensityAgreesWithTheReference` · `N6_DensityActuallyChangesWithCompositionAtFixedPAndT` · `EosRoundTripsAreConsistent` | PASS |
| Thermodynamic consistency tests | `ClosureIdentitiesHoldAcrossTheMatrix` (7 compositions × 4 temperatures) | PASS |

## 2. Section by section

| § | Requirement | Asserted by |
|---|---|---|
| 2 | species data; `R_i = R_u/M_i` derived | `GasConstantIsDerivedFromMolecularWeight` (incl. `== R_u/M` exactly) |
| 2, 21 | invalid `M_i`, `cp_i`, `gamma <= 1` rejected | `RejectsInvalidMolecularWeight` · `RejectsAGasWithGammaAtOrBelowOne` |
| 3 | harmonic `M_mix`; symmetry; endpoints | `MolecularWeightUsesTheMassFractionRelation` · `...IsCompositionSymmetric` · N1 |
| 4 | both `R_mix` forms, closure error reported | `TheTwoGasConstantFormulationsAgree` (max reported) |
| 5 | mass-weighted `cp`; reuse P15 | `SpecificHeatIsMassWeighted` · `TemperatureDependentCpFlowsThroughTheMixture` |
| 6 | `cv = cp − R`, cross-checked, discrepancy reported | `N4_CvFromTheClosureAgreesWithTheSpeciesSum` |
| 7 | `gamma` emerges; `cv>0`, `gamma>1` | `CvAndGammaComeFromTheExistingClosure` · N5 · asserted in the closure matrix |
| 8 | closure across the matrix | `ClosureIdentitiesHoldAcrossTheMatrix` |
| 9 | reuse `IdealGasEOS` | structural — 0 new density formulas; `eos.md` §1 |
| 10 | single-species limit **bitwise** | `SingleSpeciesLimitReproducesP17Bitwise` · `N8_TheSingleSpeciesP17ResultIsUnchanged` |
| 11 | every pure endpoint | `SingleSpeciesLimitIsBitwiseAtEveryPureEndpointOfAMixture` (on the **ternary** mixture) |
| 12 | binary analytical, independent reference | all reference values from `data/reference_values.txt` |
| 13 | arithmetic-average `M` detected | `N1_ArithmeticAverageMolecularWeightIsDetected` |
| 14 | composition sensitivity, monotonicity only where derived | `PropertiesVaryContinuouslyAcrossTheCompositionRange` |
| 15 | `cp_i(T)` from P15, integrated `h` | `TemperatureDependentCpFlowsThroughTheMixture` · `SingleSpeciesEnthalpyMatchesP15Exactly` |
| 16 | `h_mix`, `e_mix` | `MixtureEnthalpyIsMassWeightedAndInternalEnergyFollows` |
| 17 | total-energy extension point documented | `single_species_compatibility.md` §4 — documented, not built |
| 18 | density changes with composition | `N6_...AtFixedPAndT` (21.83× ratio, each `rho` = `p/(R_mix T)`) |
| 19 | EOS round trips, worst error reported | `EosRoundTripsAreConsistent` |
| 20 | valid P18.1 state required; no renormalisation | `N7_AnInvalidCompositionCannotReachTheMixtureThroughTheTypedPath` |
| 22 | determinism, bitwise | `MixturePropertiesAreBitwiseDeterministic` (50 repeats) + probe (1000 repeats) |
| 25 | P18.2 → P18.3, per-cell | `SpatiallyVaryingCompositionGivesSpatiallyVaryingProperties` · `RejectsAStateWhoseRegistryDiffers...` |
| 26 | canonical matrix | `thermodynamic_closure.md` §2 — 9 compositions, 5 T, 3 p, plus a cp(T) case |
| 27 | independent references | Python `decimal` at 50 digits, `data/reference_values.txt` |
| 28 | N1–N8 | `negative_controls.md` |

## 3. Gates

```text
MAY CLAIM
  G5  mixture thermodynamic closure -- IN FULL. Every identity within 8 eps (two exactly 0), the
      single-species limit BITWISE with 0 mismatches over 5T x 3p x 5 quantities, and all three of
      G5's own negative controls detecting.
  G1  baseline preservation -- P18.3 modifies no existing production source; measured across
      P14/P15/P16/P17/P18.1/P18.2 plus the three-configuration regression.
  G2  species-state validity -- reused, not re-implemented; N7 asserts the refusals.
  G10 determinism -- the property layer, bitwise. NOT the solver-level claim.

MUST NOT CLAIM, and do not
  G6 reaction-rate correctness · G7 reacting energy conservation · G8 reacting validation ·
  G9 final CPU/GPU equivalence · G11 final qualification.
  None is measurable from thermodynamic closure, and §29 forbids claiming them from it.
  G3 and G4 are P18.2's and are not re-claimed here.
```

## 4. Deliberately not covered

```text
§17 lifting the TotalEnergyState cp(T) refusal   DOCUMENTED, not built. The extension point is named
                                                 exactly; the check is byte-for-byte unchanged and
                                                 its 34 tests pass. Reasons in
                                                 single_species_compatibility.md §4.
case-file reachability                           the mixture EOS is LIBRARY-LEVEL. No case key
                                                 selects it and no solver consumes it. Stated as a
                                                 LIMIT, not claimed.
real-gas / non-ideal EOS                         out of scope.
mixture-averaged D_i, mu, k                      not thermodynamic closure. P18.2 takes D_i as given.
chemistry                                        §23 -- none. omega_i does not appear.
GPU                                              no device path; P18.8's.
```

## 5. Every enforced property has a control that breaks it

```text
harmonic M_mix          N1 -> 5.9690x margin on a 50/50 mixture (and the near-pure vacuity trap is
                              asserted as its own test)
R_mix responds to Y     N2 -> 21.83x spread; and the 50/50 value must be the exact mean, a derived
                              linearity property
mass-weighted cp        N3 -> 1.7995x margin
cv from the closure     N4 -> the two forms agree to 1.75e-16; a wrong weighting would break it
gamma derived not avg   N5 -> 3.67% error
density uses R_mix      N6 -> 21.83x, and each rho checked against p/(R_mix T)
invalid Y refused       N7 -> three refusals, including one with an exactly-1 sum so only the bounds
                              check can fire
P17 unchanged           N8 -> 0 bitwise mismatches over a 5x3x5 matrix
the slab of references  every expected value computed OUTSIDE CFDApp at 50 digits
```
