# P17.6 — the CRITERION → TEST coverage map

Completes §12 of `acceptance_gate.md`. Kept in a separate file so the frozen gate's hash cannot move
— P17.5 embedded a `TO BE FILLED` table inside its frozen gate and then could not complete it
without breaking the freeze.

**This map found a real gap.** Criterion **A3** was listed with a test name and the test did not
actually assert it; the suite passed 14/14 with A3 unmeasured. See `AMENDMENT_thresholds.md` §2. The
"asserted?" column below exists because of that: a map is a *claim* about the tests, not evidence
about them, and the claim was wrong once out of fifteen.

---

## Group M — the Mach family

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| M1 all 4 levels converge, both formulations | `EveryLevelConvergesInBothFormulations` | yes | 4/4, iterations > 0 |
| M2 family spans ≥ 4× in ACHIEVED Mach | `FamilySpansAtLeastFourTimesInAchievedMach` | yes | 8× (0.0028802 → 0.00036003) |
| M3 `machMax < 0.1` | `InheritedG5ThresholdsHoldAtEveryLevel` | yes | 0.0041552 → 0.00051994 |
| M4 `machMax > 1.25 × inletMach` | same | yes | ratio 1.4427, bar 1.25 |
| M5 `deltaRhoOverRho < 1e-2` | same | yes | 1.1192e-4 → 1.3482e-5 |
| M6 `maxEosError < 1e-9` | same | yes | **exactly 0** at every level |
| M7 `fluxMaxRelDiff < 0.15` | same | yes | 2.4723e-5 → 3.1e-6 |

## Group A — the trend

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| A1 Δρ/ρ strictly decreases | `EveryErrorMetricDecreasesMonotonicallyAsMachFalls` | yes | monotone, 4 levels |
| A2 velocity difference strictly decreases | same | yes | 7.9159e-5 → 1.2574e-5 |
| A3 pressure difference strictly decreases | same (`presLinf`) **+** `RelativePressureDifferenceFallsToItsConvergenceFloorAndStaysThere` | **NO at first — the gap this map found.** Now yes, on the ABSOLUTE measure | relative metric is floor-limited at 1.4–1.6× its 5.2083e-5 floor; absolute measure is monotone |
| A4 mass-flux difference strictly decreases | `EveryErrorMetricDecreases...` | yes | 2.4723e-5 → 3.1e-6 |
| A5 Δρ/ρ exponent in [0.80, 1.25] | `DensityVariationExponentMatchesViscousTheory` | yes | **1.0178**, theory 1 |
| A6 every other exponent > 0.5 | `EveryOtherErrorExponentIsGenuinelyDecreasing` | yes | 0.88 – 1.04 |

## Group E — energy, kinetic contribution bounded separately

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| E1 `keRelDiff < 1e-3` | `KineticContributionMatchesTheIncompressibleReference` | yes | 1.6233e-4 → 1.9409e-5 |
| E2 `keRelDiff` decreases, exponent > 0.5 | `EveryErrorMetricDecreases...` + `EveryOtherErrorExponent...` | yes | n = 1.0216 |
| E3 `ke/e` reported and decreasing | `KineticContribution...` + `EveryErrorMetricDecreases...` | yes | 4.832e-6 → 7.565e-8 |
| E4 temperature recovery exact | `TemperatureRecoveryFromConservedEnergyIsExact` | yes | **exactly 0**, all levels |
| E5 balance reported, NOT gated below S/N 1 | `EnergyBalanceIsReportedWithItsSignalToNoiseAndNotGatedBelowOne` | yes, **conditionally**, plus a non-vacuity guard that at least one level is above the floor | S/N 46.97 → 0.82; ratio 0.979, 0.917, 0.686, −0.220 |

## Group C — conservation

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| C1 `globalMassImbalance ≤ bound` | `MassFlowsBalanceAtEveryLevel` | yes | see AMENDMENT §1 — the bound is the solver's convergence contract, margins 66× → 10× |
| C2 inlet/outlet flows balance | same | yes | 1.7767e-8 on 1.1767 throughput |
| C3 momentum vs analytical Poiseuille wall shear | `MomentumBudgetMatchesThePoiseuilleWallShear` | yes, **plus** the entrance effect must shrink with Re | ratio 1.0713 → 1.0559, monotone toward 1 |
| C4 no P17.4 momentum-balance evaluator exists | structural; the budget is computed here and cross-checked analytically | n/a | 0 such evaluators in the tree |

## Group D — determinism (G9)

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| D1 repeated solve bitwise identical | `RepeatedCompressibleSolveIsBitwiseIdentical` | yes | `==` on velocity, pressure, density, massFlux |
| D2 iterations and residuals identical | same | yes | `==` on iterations and all four final residuals |

## Group F — anti-false-success (§20)

| criterion | test | asserted? | measured |
| --- | --- | --- | --- |
| F1 density is not uniform | `CompressiblePathIsGenuinelyActive` | yes, **plus** the spread must scale with Mach | 1.3169e-4 → 1.5864e-5 |
| F2 density satisfies the P17.2 EOS | same | yes | maxEosError **exactly 0** |
| F3 the two solutions DIFFER | same | yes | velRelL2 > 0 at every level |
| F4 status Converged, iterations > 0 | `EveryLevelConverges...` | yes | 1959 – 2439 |

## Group P — preservation, and Group X

| criterion | instrument | measured |
| --- | --- | --- |
| P1 existing low-Mach test UNCHANGED and passing | SHA-256 in `logs/03_gate.log` + re-run | hash matches the freeze; **9/9 pass** |
| P2 MMS compressible unchanged and passing | same | hash matches; 1/1 |
| P3 Poiseuille + cavity | `logs/03_gate.log` | 13/13 and 3/3 |
| P4 SIMPLE fingerprint | `tools/fingerprint.sh` | **BITWISE IDENTICAL**, 0-byte diff |
| P5 P14 / P15 / compressible | `logs/03_gate.log` | **68/68**, **161/161**, **225/225** = 210 + 15 |
| P6 full regression | `tools/p17_6_regress.sh mandated` | see `regression.md` |
| P7 clang-format | `logs/03_gate.log` | exit 0 |
| P8 upstream files UNTOUCHED, by SHA-256 | `logs/03_gate.log` | **15 UNTOUCHED, 0 TOUCHED** |
| X1 GPU compressible surface | `logs/03_gate.log` | all 10 probes **0**; reported NOT PRESENT, never PASS |

## Group N — controls

| criterion | mechanism | result |
| --- | --- | --- |
| N1 reference is the compressible solution itself | planted in the harness | see `logs/05_controls.log` |
| N2 `rho_ref = 1.0` instead of the EOS value | planted | same |
| N3 machMax reports the requested Mach | planted | same |
| N4 drop the `1.25 × inletMach` clause | **NOT planted — by design.** Deleting an assertion always "passes", so it is a degenerate control. Verified by MEASURING the margin instead: 1.4427 against the 1.25 bar. | measurement |
| N5 energy criterion normalised by the rhoE scale | planted, and must **PASS** — the only way to show the trap G5 names is real | same |

## Criteria with no dedicated assertion, stated explicitly

```text
C4    structural: "no P17.4 momentum-balance evaluator exists" is verified by grepping the tree
      (0 hits), not by a test. Recorded in audit.md and the gate.
N4    a degenerate control, replaced by a measurement -- see the row above and the reason given
      there rather than left as an unexplained gap.
```

Everything else in the gate has a named test above **and** a confirmed assertion inside it.
