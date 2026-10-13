# P17.7 — the CRITERION → TEST coverage map

Completes §17 of `acceptance_gate.md`. Kept outside the frozen gate so completing it cannot move that
hash — P17.5 embedded a `TO BE FILLED` table inside its frozen gate and then could not finish it, and
P17.6 found a criterion (A3) listed with a test name that did not actually assert it. The
**asserted?** column exists because of that: a map is a *claim* about the tests, not evidence about
them, and in P17.6 the claim was wrong once in fifteen.

Measured values are from `logs/03_hllc_measured.log` (33 CPU tests) and `logs/04_gpu_attempt1.log`
(4 GPU tests). `eps = 2.2204460492503131e-16`; `kTol = 32 eps = 7.105e-15`.

---

## Group D — the authoritative state definitions

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| D1 units documented per component, no bare `E` | structural, `HllcFlux.hpp` | n/a | `ConservedState`/`PrimitiveState`/`FluxVector`, units on every field |
| D2 no EOS in the flux layer | structural | n/a | 0 pressure or sound-speed formulas in `HllcFlux.hpp`/`.cpp` |
| D3 `conservedTotalEnergy` == zero-reference form | `D3_ConservedEnergyMatchesTheZeroReferenceForm` | yes | 4 states, all within `8 eps·|rhoE|` |
| D4 the factory is the only thermodynamic path | `D4_FactoryAgreesWithDirectConstruction` + `D4_ConservedRoundTripIsExact` | yes, **bitwise** on all three model calls | `speedOfSound`, `pressure`, `rhoE` each bitwise equal to the model's own output |

## Group F — the directional physical flux

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| F1 x-normal == textbook 1D formula | `F1_XNormalIsTheTextbookOneDimensionalFlux` | yes, **BITWISE** | all 5 components |
| F2 axis normals permute bitwise | `F2_AxisNormalsPermuteTheFluxBitwise` | yes, **BITWISE** | x↔y↔z |
| F3 matches the independent reference | `F3_MatchesTheIndependentReference` | yes | worst **1.114e-17** vs bound 1.776e-15 |

## Group C — consistency, G6(a)

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| C1 supersonic uniform is bitwise | `C1_C2_UniformStateReturnsThePhysicalFlux` | yes, `EXPECT_EQ`, **plus a guard that at least one supersonic case was exercised** | 2 bitwise selections |
| C2 subsonic uniform within 32 eps | same | yes | worst **exactly 0.000e+00** — the derived bound was conservative |
| C3 four normals × ≥5 states | same | yes | 6 states × 4 normals = 24 evaluations |

## Group W — wave speeds and non-degeneracy

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| W1 speeds match the reference | `W1_W2_W3_MatchTheReferenceAndAreWellConditioned` | yes | worst **2.172e-16** vs 1.776e-15 |
| W2 `S_L < S* < S_R` strictly | same | yes | 15/15 cases |
| W3 `|denom| >= rho_L a_L + rho_R a_R` | same | yes | smallest ratio **1.000000000000000** — the bound is ATTAINED, confirming the derivation |
| W4 the estimator BOUNDS the true signal speeds | `W4_EstimatorBoundsTheTrueSignalSpeeds` | yes, **plus a guard that ≥10 cases carried an exact solution** | **14 of 14** |
| W4 non-vacuity | `W4_NonVacuity_DavisFailsTheSameCriterion` | yes, **and asserts Davis fails ≥5** | Davis fails **8 of 14** |
| W5 near-sonic | `W5_NearSonicStaysFiniteAndPicksTheRightBranch` | yes | `S_L = 1e-09`, branch `F_L`, all finite |

## Group B — all four branches, G6(b)

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| B1 `S_L >= 0` returns `F_L` | `B1_B2_SupersonicBranchesReturnTheOneSidedFluxBitwise` | yes, **BITWISE** | all 5 components |
| B2 `S_R <= 0` returns `F_R` | same | yes, **BITWISE** | all 5 components |
| B3/B4 star branches vs the reference | `B3_B4_StarBranchFluxMatchesTheReference` | yes | worst **1.833e-16** over 12 cases |
| B5 every branch entered, branch matches | `B5_EveryBranchIsEnteredAndMatchesTheReference` | yes, with a `EXPECT_GT(count,0)` per branch | `F_L=2  F*_L=12  F*_R=1  F_R=1` |

## Group S — star states and positivity, G6(e)

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| S1 `rho* > 0` | `S1_S2_S3_S4_StarStatesAreAdmissibleAndMatchTheReference` | yes, boolean | min **1.314130e-05** |
| S2 finite | same | yes | all |
| S3 star internal energy `> 0` | same | yes | min **1.977800e+00** |
| S4 `rho*` vs the reference | same | yes | worst **2.220e-16** |
| S5 fallback/invalid counters read 0 | structural — **there are no counters, because there is no fallback** | n/a | see `positivity.md`; the denominator is provably non-degenerate so no fallback exists to count |

## Group T — the contact wave

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| T1 stationary contact carries no mass or energy | `T1_StationaryContactCarriesNoMassOrEnergyBitwise` | yes, **BITWISE**, and asserts the density jump is real | `S* = -0`, mass flux **0**, energy flux **0**, momentum flux **exactly** `p n` |
| T2 moving contact | `T2_MovingContactMatchesTheReference` | yes | `S* = 50` exactly `= u_n`; worst **0.000e+00** |
| T3 tangential shear | `T3_TangentialShearCarriesNoMassOrTangentialMomentum` | yes, **BITWISE**, and asserts the shear is real | mass **0**, tangential momentum **0** |
| T4 non-vacuity | `T4_NonVacuity_DeletingTheContactWaveBreaksTheContact` | yes — asserts HLL still passes consistency **and** produces `> 100` spurious mass flux | **243.4595971727 kg/(m²s)** vs a true 0, matching the 60-digit reference |

## Groups R and N — rotation G6(d), conservation G6(c)

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| R1 dyadic rotation bitwise | `R1_DyadicRotationRotatesTheFluxBitwise` | yes, **BITWISE**, z90 + z180 + y90 | mass, energy and all three wave speeds bitwise equal |
| R2 oblique normal | `R2_ObliqueNormalIsRotationallyConsistent` | yes | mass diff **1.421e-14** vs bound 2.548e-12 |
| R3 area scales flux, never the wave speeds | `R3_AreaScalesTheFluxAndNeverTheWaveSpeeds` | yes, **BITWISE** for 0.25/1/4/1024 | exact; wave speeds unchanged |
| N1 `F(L,R,n) = -F(R,L,-n)` | `N1_FluxIsBitwiseAntisymmetricUnderReversal` | yes, **BITWISE**, all 5 components **and** `S_L' = -S_R` | 15 cases |
| N1 two-cell conservation | `N1_OneFluxEvaluationConservesAcrossTwoCells` | yes | mass sum **0**, energy sum **0**, exactly |

## Group X — the independent exact solver, G6(f)

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| X1 == exact Godunov flux where HLLC is exact | `X1_MatchesTheExactGodunovFluxWhereHllcIsExact` | yes, **plus a guard that ≥7 exactness cases ran** | worst **3.254e-16** over 8 cases |
| X2 == the 60-digit HLLC everywhere | `X2_MatchesTheSixtyDigitHllcEverywhere` | yes | worst **3.254e-16** at `near_sonic_left` |
| X3 strong discontinuities reported | `X3_X4_StrongDiscontinuitiesAreReportedNotGatedOnAccuracy` | **reported**; gated only on ordering, positivity, finiteness and branch | Sod 0.0061/0.0609/0.2961 — see `discontinuity_tests.md` |
| X4 Sod initial state | same | yes | `S_L < S* < S_R`, both `rho* > 0`, finite energy flux |

## Group P — invalid and extreme states

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| P1 inadmissible states refused | `P1_P2_P3_InadmissibleInputsAreRefusedWithANamedReason` | yes — typed code **and** the thrown message | **11** cases, each with the right `StateValidity` |
| P2 invalid normals refused | `P2_InvalidNormalsAreRefused` | yes | 5 normals + 2 invalid gammas; an **area vector is rejected** |
| P3 the message names the quantity | `P1_P2_P3_...` | yes, substring match on `describeValidity` | 11/11 |
| P4 extreme but valid states | `P4_ExtremeButValidStatesStayFiniteAndAdmissible` | yes | 5 cases, min `rho*` 1.3e-05, min `e*` 1.98 |
| P5 no clamping, no floor, no silent fallback | structural | n/a | `positivity.md`; nothing is repaired, only refused |

## Group G — CPU/GPU and determinism

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| G1 one shared source | structural, `DeviceHllcFluxKernel.cu` | n/a | 0 formulas in the `.cu`; it marshals and calls the header |
| G2 CPU vs GPU bitwise | `G2_CpuAndGpuAgreeBitwiseOverTheStateMatrix` | yes, **BITWISE** on flux, 3 speeds, denominator, both `rho*`, both `e*`, branch, validity | **17/17 rows**, max flux diff **0**, max speed diff **0**, 0 branch differences |
| G2 refusals match on device | `G2_InadmissibleInputsAreRefusedIdenticallyOnDevice` | yes, same reason code | **8** cases |
| G3 FMA contraction cost | `tools/fma_experiment.sh` | reported, not gated | fmad=true → **8/17** bitwise, max diff 1.526e-04 absolute (1.9e-15 relative); wave speeds unaffected; restore SHA-256 verified |
| G4 CPU determinism | `G4_RepeatedEvaluationIsBitwiseIdentical` | yes, **BITWISE** | 15 cases |
| G4 GPU determinism | `G4_DeviceEvaluationIsDeterministic` | yes, **BITWISE** | 17 rows |

## The reconstruction interface

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| first-order mode is owner/neighbour | `FirstOrderModeIsOwnerAndNeighbour` | yes, **BITWISE**, and that HLLC consumes the face state | exact |
| boundary exterior state comes from the caller | `BoundaryFaceTakesAnExteriorStateFromTheCaller` | yes | symmetric opposed pair gives mass flux ≈ 0 |

## Group V — controls

| id | mechanism | result |
| --- | --- | --- |
| V1 Davis wave speeds | **PERMANENT test**, not a plant — `WaveSpeedEstimator::Davis` is real code | fails W4 in **8 of 14**, asserted `>= 5` |
| V2 contact wave deleted | **PERMANENT test** — `hllFluxForControlOnly()` | 243.46 spurious mass flux, CPU and GPU; consistency still passes |
| V3–V7 | planted in `HllcFlux.hpp`, `verify_planted()` guarded, SHA-256 restored | `logs/06_controls.log` |

## Criteria with no dedicated assertion, stated explicitly

```text
D1, D2, G1, P5, S5   STRUCTURAL. Verified by reading the source and by grep, not by a test:
                     "there is no EOS in this file", "there is no fallback", "the .cu contains no
                     formulas". A test cannot assert the ABSENCE of code; the audit and the
                     evidence documents record it instead.
S5                   additionally VACUOUS BY DESIGN: it asks for fallback-activation counters to
                     read zero, and there are no counters because there is no fallback. Reported as
                     not-applicable with the reason, rather than as a satisfied criterion.
G3                   reported, never gated -- it measures a compile flag's cost, not correctness.
```

Everything else above has a named test **and** a confirmed assertion inside it.
