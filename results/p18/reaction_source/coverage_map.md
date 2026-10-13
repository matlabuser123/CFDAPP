# P18.4 — coverage map (§43)

Every requirement mapped to the **assertion** that covers it. §43 says a printed metric, a
constructed object or a test with no failing condition does not count as coverage.

---

## 1. The seven checklist items

| Requirement | Asserted by | Result |
|---|---|---|
| Generic reaction definition | `NetStoichiometryUsesTheFrozenSignConvention` · `ACatalystCancelsToZeroExactlyOnce` · `ASpeciesRepeatedOnOneSideIsSummed` | PASS |
| Stoichiometry | the above, plus `RecognizableReactionBalancesExactly` and `ZeroInputCoefficientsAreAcceptedAndContributeNothing` | PASS |
| Species production/destruction rates | `IrreversibleSourceHasCorrectSignsAndMagnitudes` · `SourceRatioRecoversTheProgressRateForEverySpecies` | PASS |
| Mass-conserving source terms | `MassResidualIsExactlyZeroForABalancedReaction` · `EveryCellConservesMassIndependently` · `ClosedReactorConservesTotalSpeciesMass` | PASS |
| Reversible / irreversible support | `ReversibleNetRateDrivesDirection` · `ReversibleDirectionSymmetryIsExact` · `N6_IrreversibleReactionRefusesANegativeRate` | PASS |
| Invalid chemistry guards | `RejectsInvalidDefinitions` · `N4_...` · `N7_...` · `N8_...` · `RejectsDuplicateReactionNames` | PASS |
| Source-term tests | all 38 | PASS |

## 2. Section by section

| § | Requirement | Asserted by |
|---|---|---|
| 2 | net `nu_i` sign convention | `NetStoichiometryUsesTheFrozenSignConvention` |
| 3 | species from the authoritative registry; unknown fails | `N7_AnUnknownSpeciesCannotEnterAMechanism` (3 ways) |
| 4 | coefficient validity, no silent `abs()` | `RejectsInvalidDefinitions` |
| 5 | canonical species-indexed vector | `NetStoichiometryUsesTheFrozenSignConvention` |
| 6 | progress-rate abstraction, no kinetics | structural — 0 occurrences of `exp`/`Ea`/`Arrhenius` in the source |
| 7 | molar → mass, units | `N2_...`, `N9_...`, `RecognizableReactionBalancesExactly` |
| 8 | `Σ M_i ν_i = 0` from the **definition** | `MassResidualIsExactlyZeroForABalancedReaction` |
| 9 | unbalanced rejected, diagnostics | `N4_...` · `MassUnbalanceDiagnosticNamesTheReactionDefectAndStoichiometry` |
| 10 | elemental | **DEFERRED** — no elemental data exists; stated in `mass_conservation.md` §7 |
| 11 | irreversible signs | `IrreversibleSourceHasCorrectSignsAndMagnitudes` |
| 12 | reversible directions | `ReversibleNetRateDrivesDirection` |
| 13 | `ω(−q) = −ω(q)` | `ReversibleDirectionSymmetryIsExact` — **bitwise** |
| 14 | multiple reactions superpose | `MultipleReactionsSuperposeLinearly` |
| 15 | shared species across reactions | `SharedSpeciesAccumulatesBothContributionsWithCorrectSigns` |
| 16 | inert species exactly zero | `InertSpeciesSourceIsExactlyZero` (32 cells, with a non-vacuity check) |
| 17 | zero rate → zero source | `ZeroProgressRateGivesExactlyZeroSource` · `ZeroProgressRateReproducesP18_2Exactly` |
| 18 | P18.2 source integration | `ClosedReactorMatchesTheAnalyticalSourceUpdate` |
| 19 | closed homogeneous reactor | same, against an analytical reference |
| 20 | species mass under reaction | `ClosedReactorConservesTotalSpeciesMass` |
| 21 | **per-reaction** mass balance | `EveryReactionIsCheckedIndividuallyNotInAggregate` |
| 22 | **per-cell** source conservation | `EveryCellConservesMassIndependently` (256 cells, signed rates) |
| 23 | negative-projection diagnostic | `NegativeProjectionIsDiagnosedAndNothingIsClamped` |
| 24 | invalid rates; direction semantics | `N8_...` · `N6_...` |
| 25 | duplicate reaction names | `RejectsDuplicateReactionNames` |
| 26 | empty reactions | `RejectsInvalidDefinitions` |
| 27 | species on both sides | `ACatalystCancelsToZeroExactlyOnce` |
| 28 | deterministic canonicalisation | `ConstructionAndEvaluationAreBitwiseDeterministic` |
| 29, 30 | determinism, accumulation order | same, plus the probe's 200 repeats |
| 31 | unit consistency, 1000× control | `N9_AKmolMolecularWeightWouldBeWrongByExactlyAThousand` |
| 35 | one molecular-weight source | `N3_...`; structurally, P18.4 stores none |
| 36 | hand-verifiable reaction | the dyadic `A + 2B -> C`, reference in `data/reference_values.txt` |
| 37 | recognizable reaction | `RecognizableReactionBalancesExactly` (2 H₂ + O₂ → 2 H₂O) |
| 38 | source ratio | `SourceRatioRecoversTheProgressRateForEverySpecies` |
| 39 | molar vs mass consumption ratios | `StoichiometricConsumptionRatiosAreMolarNotMass` |
| 40 | reversible zero-net | `ReversibleNetRateDrivesDirection` |
| 41 | cancellation, non-vacuous | `TwoReactionsCanCancelForOneSpeciesWithoutTheTestBeingVacuous` |
| 42 | N1–N9 | `negative_controls.md` |

## 3. Gates — what P18.4 claims, and what it does not

```text
CLOSES
  G6 criterion (b) ONLY -- the stoichiometric closure, sum_i W_i nu_i = 0 to 4 eps * max|W_i nu_i|,
    with G6's own negative control (a perturbed coefficient must FAIL) measured at 281 475x over
    tolerance.

EVIDENCE TOWARD, NOT CLOSED
  G6 OVERALL. Its criterion (a) is a RATE comparison against an independent Arrhenius
    implementation, and G6's own `phase` field says P18.5. P18.4 implements no kinetics, so
    claiming "G6 PASS" here would be an over-claim the gate's own phase field guards against.
  G3 species conservation -- sum_i omega_i = 0 per cell, and closed-reactor mass conservation.
    G3's phase is P18.7.
  G1 baseline preservation · G2 species-state validity · G10 determinism (the source layer).

MUST NOT CLAIM, and do not
  G7 reacting energy conservation · G8 canonical reacting validation · G9 CPU/GPU equivalence ·
  G11 final qualification. None is measurable from a source layer with no kinetics and no energy
  coupling, and section 44 forbids claiming them.
```

## 4. Deliberately not covered

```text
Arrhenius kinetics             P18.5. 0 occurrences of A, n, Ea or exp() in the production source.
heat release, reaction enthalpy P18.6. No energy source exists.
formation enthalpy              none. P15's referenceEnthalpy is a SENSIBLE offset and is NOT
                                repurposed -- the P18.0 audit warns specifically against that.
chemistry time integration      section 23 -- explicitly deferred, with a diagnostic provided.
elemental conservation          section 10 -- no elemental data exists.
case-file reachability          no case key defines a mechanism and nothing in production produces
                                a progress rate. Stated as a LIMIT, the same one P18.2 and P18.3
                                carry.
GPU                             no device reaction path; P18.8's.
```

## 5. Every enforced property has a control that breaks it

```text
stoichiometric sign / value   N1 -> residual 281 475x over tolerance
molecular-weight scaling      N2 -> 16x ; N9 -> exactly 1000x ; N3 -> mechanism refuses
mass balance                  N4 -> defect a third of the product mass
multi-reaction accumulation   N5 -> species C source drops to exactly 0
direction semantics           N6 -> irreversible refuses q<0, reversible accepts the same q
species identity              N7 -> three refusals, including the empty-string name
rate validity                 N8 -> NaN, +Inf, -Inf all refused
the cancellation test itself  asserts the individual contributions were LARGE, so a zero result
                              cannot pass vacuously
```
