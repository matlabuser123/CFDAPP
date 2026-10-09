# P18.6 — coverage map (§50, §52)

Every requirement mapped to the **assertion** that covers it. §50: *"Do not count printed energy
balances as verification."*

```text
WHY THIS DOCUMENT EXISTS. In P17.4, seven frozen gate criteria turned out to be measured by
nothing, or by the wrong probe. In P18.5, G6 named three dimensions and the test covered two. The
map is built AFTER freezing and BEFORE closing, criterion by criterion, and a row that would read
"the implementation does this" rather than naming an assertion is treated as UNCOVERED.

AND WHERE A CRITERION NAMES A COUNT OR A DIMENSION, THE COUNT IS RECORDED -- that is the P18.5
lesson applied.
```

---

## 1. G7 — the primary gate

| G7 clause | Asserted by | Result |
|---|---|---|
| a closed energy balance **including** the reaction source | `TotalEnergyIsConservedToG7sFloor` — 16 cells × 50 steps, conserved quantity defined explicitly | **PASS, 6.29 eps of 128** |
| `q = -sum_i h_f,i omega_i` | `TheMolarAndMassRoutesAgree` — the mass route IS that formula, compared against the molar route | **PASS, 0 eps** |
| the cross-check that heat released = enthalpy change implied by the species consumed | the same test; the two routes take `q` and `omega` respectively | **PASS, 0 eps** |
| `\|imbalance\| / \|E\| <= 8 eps N_cells` | `TotalEnergyIsConservedToG7sFloor` | **PASS, 19× margin** |
| adiabatic reactor T vs an **independent** enthalpy balance, reported not gated on an invented constant | `FinalTemperatureMatchesTheIndependentAdiabaticBalance` — 2 conversion fractions | **PASS, bitwise exact** |
| *control:* heat-release sign flipped must FAIL | `N1_FlippingTheHeatReleaseSignIsDetected` | **2× margin + sign inversion** |
| *control:* rate fed to species but NOT energy must FAIL | `N8_UsingADifferentProgressRateForEnergyIsDetected` | **150 % gap** |
| *control:* `h_f,i` replaced by the SENSIBLE reference must FAIL | `N2_IsDistinctFromTheSensibleReferenceEnthalpy` | **> 1.8e4×** |

## 2. Section by section

| § | Requirement | Asserted by |
|---|---|---|
| 1 | audit the existing energy path; add no second energy equation | `audit.md` §1, §3; integrity grep for new assemblers |
| 2 | freeze the thermochemical convention before coding | `energy_accounting.md`, frozen at `logs/01_freeze.log` |
| 3 | one authoritative species enthalpy; never `h_i = cp_i T` | `species_enthalpy.md` §1, §5; integrity grep |
| 4 | formation enthalpy handled, not faked | `MassAndMolarFormsAgreeWithTheReference` · `FromMolarRoundTripsExactlyForDyadicWeights` · `N2_...` |
| 5 | reaction-enthalpy sign convention frozen | `StandardReactionEnthalpyMatchesTheReferenceAndSigns` |
| 6 | energy source derived, sign not determined experimentally | `energy_accounting.md` §3; `ExothermicReactionAddsEnergy...` · `EndothermicReactionRemovesEnergy...` |
| 7 | independent cross-check via `omega_i` | `TheMolarAndMassRoutesAgree` (6 rate pairs × 6 cells) |
| 8 | **no energy double counting** — the design gate | `energy_accounting.md` §2; `N5_DoubleCountingChemicalEnergyIsDetected` |
| 9 | energy-accounting document before implementation | `energy_accounting.md`, sha256 at `logs/01_freeze.log` |
| 10 | exothermic analytical case | `ExothermicReactionAddsEnergyWithTheReferenceMagnitude` (5 rates) |
| 11 | endothermic analytical case | `EndothermicReactionRemovesEnergyWithTheReferenceMagnitude` (4 rates) |
| 12 | zero reaction-enthalpy case | `ThermoneutralReactionGivesExactlyZeroAtEveryRate` · `AThermoneutralReactionIsExactlyZeroByCancellation` |
| 13 | zero reaction-rate case | `N6_ZeroRateGivesExactlyZeroSourceAndZeroOmega` |
| 14 | the same `h_i(T)` everywhere | `species_enthalpy.md` §5; `N2_...` (bitwise independence of the sensible offset) |
| 15 | mixture enthalpy; reuse P18.3 | P18.3's own 34/34 at `logs/05_preservation.log`; integrity grep for a duplicate |
| 16 | internal-energy consistency; no second total-energy state | `audit.md` §1 (P17 `TotalEnergyState` untouched); integrity grep |
| 17 | temperature recovery uses the **updated** composition | `N4_TemperatureRecoveryUsesTheUpdatedComposition` |
| 18 | coupling named, not implied implicit | `coupling_order.md` §1, §5 |
| 19 | the production ordering frozen and followed | `coupling_order.md` §1; `N3_...` and `N4_...` test steps 7 and 8 |
| 20 | no stale-temperature chemistry | `N3_HeatReleaseRaisesTheRateAtTheNextEvaluation` |
| 21 | positive thermal feedback reaches the kinetics | the same, compared against `exp(-(Ea/R)(1/T1-1/T0))` |
| 22 | endothermic feedback | `EndothermicFeedbackLowersTheRate` |
| 23 | closed adiabatic reactor | `closed_reactor.md`; `TotalEnergyIsConservedToG7sFloor` |
| 24 | total-energy conservation, quantity defined explicitly | `energy_conservation.md` §1; the same test |
| 25 | independent per-step energy balance | `OneStepBalanceIsExactlyDtTimesTheSource` (3 q×dt pairs); `closed_reactor.md` §6 |
| 26 | one-step source wiring, hand arithmetic | the same test |
| 27 | source linearity | `SourceIsExactlyLinearInTheProgressRate` (5 factors, all bitwise) |
| 28 | multiple reactions superpose | `MultipleReactionsSuperposeExactly` (exo+endo **and** exo+exo) |
| 29 | reversible reaction heat | `ReversibleNetRateReversesBothOmegaAndTheSource` |
| 30 | one `q_r` shared by species and energy | `N8_...`; `coupling_order.md` §2 (structural) |
| 31 | temperature-dependent reaction enthalpy | `TemperatureDependentReactionEnthalpyMatchesTheReference` (4 T) |
| 32 | Kirchhoff consistency | `KirchhoffRelationHoldsAgainstAFiniteDifference` (3 reactions × 5 T) |
| 33 | reference-state invariance **and** its limits | `AMassSpecificReferenceShiftCancelsExactly` · `AMolarShiftAndASingleSpeciesShiftMustNotCancel` |
| 34 | non-reacting thermal limit, **bitwise** | `N10_WithNoReactionTheThermalStateIsBitwiseUnchanged` |
| 35 | zero heat-release limit, expectation documented first | `ZeroHeatReleaseChangesSpeciesButNotTemperature` (both `cp` cases) |
| 36 | composition→property feedback consumed | `N4_...` (asserts `cp_mix` 1000→1750); `ZeroHeatRelease...` second case |
| 37 | EOS feedback | `closed_reactor.md` §5 — **not applicable**, no pressure field; stated, not claimed |
| 38 | constant-volume reactor thermodynamics | `closed_reactor.md` §5; `rho` asserted bitwise unchanged |
| 39 | constant-pressure reactor | **optional, NOT implemented** — stated in `closed_reactor.md` §5 |
| 40 | invalid thermochemical states refused | `RejectsMismatchedSizesAndNonFiniteInput` · `RejectsBadInputAndAcceptsEveryPhysicalSign` · `N12_...` |
| 41 | bounded source update, refuse not clamp | `RefusesAStepThatWouldDriveSpeciesNegative` |
| 42 | thermal positivity, no undocumented floor | `RefusesAnEnergyUpdateThatWouldGiveANonPositiveTemperature` · `N12_...` (asserts the message) |
| 43 | units; mol/kmol control | `N9_AKmolFormationEnthalpyIsWrongByExactlyAThousand` |
| 44 | no pressure-work double counting | `energy_accounting.md` §6; integrity grep with a positive control |
| 45 | no sensible-enthalpy double counting | `N5_...`; `energy_accounting.md` §5 |
| 46 | no reacting-shock claim | `audit.md` §1 — P17 path untouched, refusal not lifted |
| 47 | CPU-first | no GPU code added; CPU-only configuration in the regression |
| 48 | determinism | `TheWholeCoupledStepIsBitwiseDeterministic` + probe, **0 differences** |
| 49 | the twelve negative controls | `negative_controls.md` — all twelve |
| 52 | focused suite counts | `logs/05_preservation.log` — **42** |
| 53–57 | P18.1–P18.5 preserved | `logs/05_preservation.log` — 60/34/34/38/38, summing with the rest to **283** |
| 58 | P15 / P17 non-reacting unchanged | thermal 163/163, compressible 215/215, transport 22/22 |
| 59 | three-configuration regression | `regression.md` |
| 60 | quantitative evidence | `logs/06_probe.log`, `logs/04_drift_probe.log` |
| 62 | repository integrity | `logs/07_final_integrity.log` |

## 3. The 42 assertions

```text
FormationEnthalpyTest (4)
   1 MassAndMolarFormsAgreeWithTheReference            h_f and hbar_f vs mpmath
   2 FromMolarRoundTripsExactlyForDyadicWeights        bitwise round trip
   3 RejectsBadInputAndAcceptsEveryPhysicalSign        validation, and NON-over-rejection
   4 N2_IsDistinctFromTheSensibleReferenceEnthalpy     control N2, G7's own

ReactionEnthalpyTest (7)
   5 StandardReactionEnthalpyMatchesTheReferenceAndSigns   Delta H^0, 3 reactions, signs
   6 AThermoneutralReactionIsExactlyZeroByCancellation     exact 0 by cancellation
   7 ACatalystContributesExactlyNothing                    net nu = 0
   8 TemperatureDependentReactionEnthalpyMatchesTheReference  Delta H(T), 4 temperatures
   9 KirchhoffRelationHoldsAgainstAFiniteDifference        d(Delta H)/dT = Delta Cp
  20 AMassSpecificReferenceShiftCancelsExactly             section 33, invariance
  21 AMolarShiftAndASingleSpeciesShiftMustNotCancel        section 33, NON-invariance

HeatSourceTest (11)
  10 ExothermicReactionAddsEnergyWithTheReferenceMagnitude   section 10
  11 EndothermicReactionRemovesEnergyWithTheReferenceMagnitude  section 11
  12 ThermoneutralReactionGivesExactlyZeroAtEveryRate        section 12
  13 N6_ZeroRateGivesExactlyZeroSourceAndZeroOmega           control N6
  14 TheMolarAndMassRoutesAgree                              sections 6, 7 -- G7's cross-check
  15 SourceIsExactlyLinearInTheProgressRate                  section 27
  16 MultipleReactionsSuperposeExactly                       section 28
  17 N11_OmittingOneReactionIsDetected                       control N11
  18 N9_AKmolFormationEnthalpyIsWrongByExactlyAThousand      control N9
  19 N1_FlippingTheHeatReleaseSignIsDetected                 control N1, G7's own
  22 ReversibleNetRateReversesBothOmegaAndTheSource          section 29

EnergyAccountingTest (4)
  23 EnergyDensitySplitsIntoSensibleAndChemicalPools         section 24's quantity
  24 TemperatureRecoveryInvertsTheSensibleEnergyExactly      section 17
  25 TemperatureRecoveryHandlesATemperatureDependentSpecificHeat   the cp(T) root find
  26 N12_TemperatureRecoveryRefusesAnImpossibleEnergy        control N12, section 42

ClosedReactorTest (9)
  27 OneStepBalanceIsExactlyDtTimesTheSource                 sections 25, 26
  28 TotalEnergyIsConservedToG7sFloor                        G7, THE PRIMARY MEASUREMENT
  29 TheExplicitMarchHasAMeasuredSumToOneStepBudget          PINS the measured limitation
  30 FinalTemperatureMatchesTheIndependentAdiabaticBalance   G7's adiabatic clause
  31 N4_TemperatureRecoveryUsesTheUpdatedComposition         control N4, sections 17, 36
  32 N5_DoubleCountingChemicalEnergyIsDetected               control N5, sections 8, 45
  33 N8_UsingADifferentProgressRateForEnergyIsDetected       control N8, G7's own, section 30
  34 RefusesAStepThatWouldDriveSpeciesNegative               section 41
  35 RefusesAnEnergyUpdateThatWouldGiveANonPositiveTemperature  section 42

TemperatureFeedbackTest (3)
  36 N3_HeatReleaseRaisesTheRateAtTheNextEvaluation          control N3, sections 20, 21
  37 EndothermicFeedbackLowersTheRate                        section 22
  38 N7_UsingTheWrongSpeciesEnthalpyIsDetected               control N7

NonReactingLimitTest (2)
  39 N10_WithNoReactionTheThermalStateIsBitwiseUnchanged     control N10, section 34
  40 ZeroHeatReleaseChangesSpeciesButNotTemperature          section 35, both cp cases

HeatReleaseDeterminismTest (2)
  41 TheWholeCoupledStepIsBitwiseDeterministic               section 48
  42 RejectsMismatchedSizesAndNonFiniteInput                 section 40
```

## 4. Criteria NOT covered, stated as such

| requirement | why | where recorded |
|---|---|---|
| §37 EOS closure after reaction | the closed reactor carries **no pressure field**, so there is nothing to close. No constant-pressure behaviour is claimed | `closed_reactor.md` §5 |
| §39 constant-pressure reactor | explicitly **optional**; the frozen gate does not require it, and faking it by overwriting pressure is forbidden | `closed_reactor.md` §5 |
| compressible / total-energy coupling | P17's `TotalEnergyState` refuses a variable `cp`; lifting it needs a variable-gamma EOS, outside scope, and §46 forbids the reacting-shock claims it would enable | `audit.md` §1 |
| equilibrium consistency of a reversible pair | P18.5 has no `K_eq` and §20 of that phase forbids inventing one; carried forward | `TODO.md` |
| implicit / stiff chemistry coupling | explicit operator splitting only; the Patankar `Sp` stays zero (`d(omega)/d(Y)` absent) | `coupling_order.md` §5 |
| long explicit marches | the sum-to-one budget is ~72 steps, **measured**; §41 forbids the renormalisation that would extend it | `closed_reactor.md` §4 |
| case-file reachability | library/API scope — no case key, parser field, solver dispatch, CLI or GUI defines a mechanism, mixture or formation enthalpy | `TODO.md` |
| G8, G9, G11 | §51 forbids claiming them | — |

```text
THESE ARE THE HONEST GAPS. Each is a scope limit or a property not checkable in code, not a test
that was skipped, and none of them is a G7 criterion.
```
