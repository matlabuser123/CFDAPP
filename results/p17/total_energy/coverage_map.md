# P17.5 — the CRITERION → TEST coverage map

This completes §13 of `acceptance_gate.md`, and it is the deliverable P17.4 lacked.

**Why it is a separate file.** §13 of the frozen gate contains a table marked `TO BE FILLED`.
Filling it in place would have changed `acceptance_gate.md`'s SHA-256 and broken the freeze, so the
completed map lives here instead. That placeholder was a **flaw in my own gate design**: a frozen
document should not contain a section intended to be edited later. Recorded rather than quietly
worked around; the fix for P17.6 is to freeze the criteria and the map *separately* from the start.

Every criterion below names the test that measures it. **A criterion with no test named here is not
verified, whatever the suite total says** — which is exactly how P17.4 closed with seven unmeasured
criteria.

---

## Group T — the thermodynamic state

| criterion | test | measured |
| --- | --- | --- |
| T1 `ke = 0.5\|U\|^2` | `TotalEnergyStateTest.KineticEnergyIsHalfVelocityMagnitudeSquared` | exact, incl. a 3-component case |
| T2 `eTotal = e + ke` | `TotalEnergyStateTest.StateComponentsComposeExactly` | exact |
| T3 `rhoE = rho eTotal` | `TotalEnergyStateTest.StateComponentsComposeExactly` | exact |
| T4 `hTotal = h + ke` | `TotalEnergyStateTest.StateComponentsComposeExactly` | exact |
| T5 `h - e = R T` | `TotalEnergyStateTest.EnthalpyMinusInternalEnergyIsExactlyRT` | **exactly 0**, both references, 4 temperatures |
| T6 `h = e + p/rho` | `TotalEnergyStateTest.StaticEnthalpyEqualsInternalEnergyPlusPOverRho` | within 4 eps, 6 states |
| T7 `rho hTotal = rhoE + p` | `TotalEnergyStateTest.DensityTimesTotalEnthalpyEqualsRhoEPlusPressure` | 5.91e-12 vs a 9.2e-11 bound |
| T8 `e = cv T`, `h = cp T` | `TotalEnergyStateTest.CaloricallyPerfectShortcutsHoldUnderTheZeroReference` | `h` exact; `e` 0.52 eps |
| T9 the shortcuts are wrong under a shifted reference | `TotalEnergyStateTest.CaloricallyPerfectShortcutsAreWrongUnderANonzeroReference` | off by 299640.75 = `cp T_ref` |

## Group C — conversion and recovery

| criterion | test | measured |
| --- | --- | --- |
| C1 primitive → conserved | `TotalEnergyConversionTest.PrimitiveToConservedMatchesHandComputedValue` | **exact** vs hand-computed |
| C2 conserved → primitive | `TotalEnergyConversionTest.ConservedToPrimitiveRecoversTheWholeState` | within 8 eps |
| C3 temperature recovery | `TotalEnergyConversionTest.ConservedToPrimitiveRecoversTheWholeState` + `TotalEnergyStateTest.InternalEnergyInverseAgreesWithTheForwardModel` | 4.55e-13 K vs P15's 1e-12 K |
| C4 pressure from the EOS | `TotalEnergyConversionTest.ConservedToPrimitiveRecoversTheWholeState` | **bitwise equal** to `IdealGasEOS::pressure` |
| C5 prim → cons → prim | `TotalEnergyConversionTest.PrimitiveConservedPrimitiveRoundTrip` | 4 states × 2 references |
| C6 cons → prim → cons | `TotalEnergyConversionTest.ConservedPrimitiveConservedRoundTrip` | 2.19 eps |
| C7 the gamma pressure form | `TotalEnergyConversionTest.GammaPressureFormAgreesUnderTheZeroReference` | 0.76 eps under the zero reference; **>10% wrong** under a shifted one, asserted |

## Group E — the conservative equation

| criterion | test | measured |
| --- | --- | --- |
| E1 `d(rhoE)/dt`, states separate | `TotalEnergyEquationTest.TransientTermUsesSeparateOldAndNewDensity` | the wrong form differs by > 1 |
| E2 uses the passed mass flux | structural: `massFlux` is a `SurfaceField` argument; `totalEnergyConvectiveDivergence` has no flux computation | 0 recomputation sites |
| E3 `(rhoE+p)U` ≡ `m_dot hTotal` | `TotalEnergyEquationTest.ConvectiveFluxCarriesPressureWorkAndMatchesRhoEPlusP` | ≤ 8 eps, every internal face |
| E4 pressure work is present | same test, second half | dropping `p` changes the answer by > 1e-6 |
| E5 conduction sign + magnitude | `TotalEnergyEquationTest.ConductiveTermMatchesAnalyticalQuadraticTemperature` | `-2kcV` exact at x-interior cells |
| E6 reuses P15's boundary handling | same test, second half | a uniform `T` conducts **exactly 0** through every face |
| E7 body-force power, `U=0` | `TotalEnergyEquationTest.BodyForcePowerIsExactlyZeroForZeroVelocity` | **exactly 0**; doubling rho doubles it exactly |
| E8 volumetric source | `TotalEnergyEquationTest.VolumetricSourceAdvancesEnergyExactly` | `dE = S_E dt` within 8 eps |
| E9 the viscous shortfall is reported | `TotalEnergyEquationTest.ViscousWorkLimitationIsReportedNotHidden` | `stressTensorAvailable == false` |

## Group G — global conservation

| criterion | test | measured |
| --- | --- | --- |
| G1 every term reported separately | `TotalEnergyConservationTest.BalanceReportsEveryTermAndAUniformStateIsAtRoundOff` | absent terms are **exactly 0**, not folded in |
| G2 adiabatic closed domain | `TotalEnergyConservationTest.AdiabaticClosedDomainConservesTotalEnergy` | drift ≤ **1 ulp** of the domain energy (1.86e-9 on 1.007e7 = 0.83 eps) over 20 steps |
| G3 uniform heating | `TotalEnergyConservationTest.UniformHeatingRaisesDomainEnergyByExactlySourceTimesVolumeDt` | `dE = S_E V dt n` within the derived bound |
| G4 uniform state | `TotalEnergyConservationTest.BalanceReportsEveryTermAndAUniformStateIsAtRoundOff` | imbalance within bound |
| G5 interior faces telescope | `TotalEnergyConservationTest.InteriorConvectiveFacesTelescope` | per-cell > 1e3, global sum within bound; opening the box **breaks** it |

## Group V — invalid states

| criterion | test |
| --- | --- |
| V1 `rho <= 0` / non-finite | `TotalEnergyInvalidStateTest.RejectsNonPositiveOrNonFiniteDensity` |
| V2 non-finite `rhoE` / momentum | `TotalEnergyInvalidStateTest.RejectsNonFiniteConservedEnergyOrMomentum` |
| V3 `ke > eTotal` | `TotalEnergyInvalidStateTest.RejectsKineticEnergyExceedingTotalEnergy` |
| V4 `T` out of range | `TotalEnergyInvalidStateTest.RejectsOutOfRangeRecoveredTemperature` |
| V5 non-finite `k`, bad sizes, `dt <= 0` | `TotalEnergyInvalidStateTest.RejectsNonFiniteConductivityAndBadSizes` |
| V6 nothing clamped | `TotalEnergyInvalidStateTest.FieldRecoveryNamesTheOffendingCellAndClampsNothing` |

## Group Z — the dual-`cp` decision

| criterion | test |
| --- | --- |
| Z1 the energy path never takes `cp` from the EOS block for enthalpy | `TotalEnergyContractTest.EnthalpyComesFromTheModelNotTheEosSpecificHeat` |
| Z2 disagreeing `cp` throws | `TotalEnergyContractTest.RejectsDisagreeingSpecificHeatSources` + `RejectsAVariableSpecificHeatModel` |
| Z3 a consistent pair constructs | `TotalEnergyContractTest.AcceptsConsistentSpecificHeatSources`, and `RejectsDisagreeing...` also asserts a within-tolerance pair is ACCEPTED |

## Group D — determinism

| criterion | test | measured |
| --- | --- | --- |
| D1 repeated evaluation bitwise identical | `TotalEnergyStateTest.RepeatedEvaluationIsBitwiseIdentical` | `==` on rhoE, T, p, e across 8 repeats |

## Group P — preservation, and Group X

| criterion | instrument | measured |
| --- | --- | --- |
| P1 P15 thermal 161 | `logs/03_gate.log`, with `-E TotalEnergy` | **161/161** |
| P2 compressible 176 + new | `logs/03_compressible.log` | **210/210** = 176 + 34 |
| P3 P14 68 | `logs/03_gate.log` | **68/68** |
| P4 SIMPLE fingerprint | `tools/fingerprint.sh` | **BITWISE IDENTICAL**, 0-byte diff |
| P5/P6 full regression | `tools/p17_5_regress.sh mandated` | see `regression.md` |
| P7 clang-format | `logs/03_gate.log` | exit 0 |
| P8/P9 upstream untouched | SHA-256 in `logs/03_gate.log` | **15 UNTOUCHED, 0 TOUCHED** |
| P10 static thermal limit | `tools/adiabatic_compression.cpp` §23 | convection and body power **exactly 0**; conduction sums to 1.37 eps |
| X1 GPU surface | `logs/03_gate.log` | all probes **0**; reported NOT PRESENT, never PASS |

## Criteria with no dedicated unit test, and why

Stated explicitly rather than left as a gap for someone else to find:

```text
E2   "uses the passed mass flux"  -- STRUCTURAL. The mass flux is a function ARGUMENT and the
     function contains no flux computation, so there is nothing a test could distinguish. Verified
     by reading the signature, the same way P17.4's F4 was.
§22  adiabatic compression        -- measured by tools/adiabatic_compression.cpp, a PROBE rather
     than a ctest row, because it verifies state relations along an isentrope rather than a
     property of one call. Logged in logs/07_adiabatic.log.
§23  static thermal limit (P10)   -- same probe, section 23.
```

Everything else in the gate has a named ctest row above.
