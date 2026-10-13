# P18.7 — coverage map (§45)

Every requirement mapped to the **assertion** that covers it, with the metric and the negative
control that would break it. §45: *"If any requirement has no meaningful detector, P18.7 is not
complete."*

---

## 1. The ten scope checkboxes

| Requirement | Test(s) | Metric | Negative control | Result |
|---|---|---|---|---|
| Species mass conservation | `ClosedReactorBalanceClosesPerSpeciesWithEveryTermReported` · `SteadyPlugFlowConservesSpeciesAndEnergyAcrossTheOpenDomain` | `\|ΔM − ∫ωV dt\|/\|M\|` and `\|in−out+src\|/\|in\|` vs `8·eps·N` | N1, N2, N11, N13 | **PASS** |
| `ΣY_i = 1` | both of the above, plus every other case | `max\|ΣY−1\|` vs `4·eps·N_species` | N4 | **PASS** |
| Elemental conservation | — | — | — | **N/A — NOT MODELED** (§9) |
| Energy conservation | `ClosedReactorEnergyClosesPerStepAndOverall` · the plug-flow test | relative drift, max/RMS per-step, vs `8·eps·N` | N6, N7, N12 | **PASS** |
| Zero-reaction limit | `N14_ZeroReactionFlowIsBitwiseTheNonReactingSolution` | **bitwise** equality; `A=0` vs chemistry-off | N14 | **PASS** |
| Zero-heat-release limit | `ThermoneutralFlowSeparatesReactionFromHeatRelease` | `S_E` exactly 0, species change, `T` per the **derived** prediction | N7 | **PASS** |
| Simple batch-reactor case | `MatchesTheIndependentQuadratureReferenceAtEveryCheckpoint` | `Y_i(t)`, `T(t)` vs mpmath quadrature at 7 checkpoints | N8, N10 | **PASS** |
| Canonical reacting-flow case | `SteadyPlugFlowConserves…` · `ProfileErrorFallsUnderGridRefinement` | profile and outlet error vs the same quadrature | N3, N14 | **PASS** (plug flow; **not** a flame — see §3) |
| Grid/time-step sensitivity | `ErrorFallsUnderTimeStepRefinement` · `ProfileErrorFallsUnderGridRefinement` | error falls across 3 levels; observed order reported | N13 | **PASS** |
| Deterministic repeatability | `BatchReactorAndReactingFlowRepeatBitwise` | bitwise fields, histories **and** iteration counts | — | **PASS** |

## 2. Section by section

| § | Requirement | Asserted by |
|---|---|---|
| 1 | audit first; prefer instrumentation | `audit.md` §3 — every metric classified; **no production change made** |
| 2 | freeze criteria before measuring | `acceptance_gate.md`, hashed at `logs/01_freeze.log` |
| 3 | integral balance, every term separately | the closed- and open-domain tests report in/out/source individually |
| 4 | per-species balance | both species carried; the mixture sum checked separately |
| 5 | local `Σω_i`, no global cancellation | `max_cell\|Σω\|` = **exactly 0**, asserted per step |
| 6 | total mixture mass | `TotalMixtureMassIsExactlyConserved` |
| 7 | `ΣY−1` at every stage; disclose normalization | measured initial/first-update/intermediate/final; **normalization: NONE**, grep-verified |
| 8 | bounds, no silent clipping | `min/max Y` asserted; P18.1 **refuses** rather than clips |
| 9 | elemental audit | **NOT MODELED** — `audit.md` §2 |
| 10, 39 | per-reaction / boundary elemental balance | **N/A** |
| 11 | full modeled energy, not just sensible | P18.6's `totalEnergy`, both pools reported |
| 12 | per-step balance | max and RMS step imbalance |
| 13 | species and energy share one `q_r` | structural — the rates are one argument; P18.6's N8 measures a mismatch at 150 % |
| 14 | zero-reaction limit | `N14_…` — bitwise |
| 15 | zero-heat-release, expectation derived first | `ThermoneutralFlow…` |
| 16–18 | batch reactor + metrics at checkpoints | `MatchesTheIndependentQuadratureReference…` |
| 19 | reference independent of production | **quadrature, not time integration** — see `batch_reference.py` header |
| 20 | temporal order, not forced | observed ≈ 1 for explicit Euler; bracket 0.7–1.5 |
| 21 | stiffness awareness | `StaysInsideTheExplicitStabilityEnvelope` — `dt < 0.05·2/k_final` asserted |
| 22–24 | canonical case, justified | `reacting_flow` = steady plug flow, §24 choice 2; see §3 below |
| 26 | quantitative metrics, not images | profile, outlet, conversion errors — no image anywhere |
| 27, 28 | grid and time-step sensitivity | two separate refinement tests |
| 29 | studies separated | **by construction**: 1 cell ⇒ no spatial error; steady ⇒ no temporal error |
| 30 | combined check | the 400-cell plug flow with the finest settings |
| 31 | `Σ ω V` vs net change + flux | the open-domain balance |
| 32 | boundary terms reported separately | inflow, outflow, source each reported |
| 33 | reject invalid inlet composition | `N4_…` — `MassFractionState` refuses `ΣY ≠ 1` |
| 34 | non-reacting flow equivalence | `N14_…`, same geometry |
| 35 | thermoneutral flow equivalence | `ThermoneutralFlow…` |
| 36 | reaction-off vs `A = 0` | `N14_…` — **bitwise** agreement |
| 37, 38 | stoichiometric ratio / progress consistency | `StoichiometricRatioAndProgressAreConsistent` |
| 40 | reference-state invariance | P18.6's, re-run (42/42 preserved) |
| 41–43 | determinism | `BatchReactorAndReactingFlowRepeatBitwise` |
| 44 | the fourteen controls | §4 below |
| 46 | no self-reference | the reference is a quadrature in mpmath; see `batch_reference.py` |
| 47, 48 | gate evidence and table | `summary.md` §1 |
| 49 | focused suite counts | 19 selected / 19 passed / 0 failed / 0 skipped / 0 disabled |
| 50–56 | predecessors preserved | 302/302 species; thermal 163, compressible 215, transport 22, turbulence 246 |
| 57 | three-configuration regression | `summary.md` §5 |
| 64 | repository integrity | `logs/05_final_integrity.log` |

## 3. Why the canonical case is a plug flow and not a flame

```text
Section 24's hierarchy puts a manufactured case FIRST and a premixed flame LAST, and says "Use the
smallest benchmark that genuinely exercises the coupled equations."

THE STEADY ADIABATIC PLUG FLOW IS CHOSEN BECAUSE IT HAS AN EXACT INDEPENDENT REFERENCE. For this
configuration the governing system reduces to the batch reactor with t -> x/U, so the same mpmath
quadrature validates it to 45 digits -- something no flame benchmark offers. It exercises
advection, species transport, chemistry and thermal coupling simultaneously, which is section 22's
requirement, and it does so with a reference that cannot be contaminated by CFDApp.

IT IS NOT A SUBSTITUTE FOR G8 AND IS NOT PRESENTED AS ONE. G8 asks for a flame speed against a
published spread; this is a different measurement, reported as itself.
```

## 4. The fourteen negative controls (§44)

| # | defect | test | detected by |
|---|---|---|---|
| N1 | species source sign | `N1_N2_…` | composition refused / imbalance |
| N2 | omit a species source | `N1_N2_…` | balance breaks by `>1e6×` the bound |
| N3 | remove the `ΣJ_i` closure | `N3_RemovingTheDiffusionClosureIsDetected` | raw sum `>1e6×` the corrected |
| N4 | break `ΣY_i` | `N4_N5_N9_…` | `MassFractionState` refuses |
| N5 | break reaction mass conservation | `N4_N5_N9_…` | `ReactionMechanism` refuses |
| N6 | energy-source sign | P18.6's N1, re-run | 2× + sign inversion |
| N7 | omit the reaction heat source | `N11_N12_…` | species react, `T` does not move |
| N8 | stale temperature in kinetics | `N8_N10_…` | `>100×` the correct error |
| N9 | perturb a stoichiometric coefficient | `N4_N5_N9_…` | refused at construction |
| N10 | Arrhenius exponent sign | `N8_N10_…` | `Ea<0` refused; 10× `A` gives `>100×` error |
| N11 | omit boundary species flux | `N11_N12_…` | imbalance `> 0.5` |
| N12 | omit boundary energy flux | `N11_N12_…` | `>100 K` outlet difference |
| N13 | wrong `dt` in accumulation | `N13_…` | residual of order the conversion |
| N14 | chemistry disabled, benchmark claimed | `N14_…` | reacting vs non-reacting differ `>100 K` |

```text
EVERY ONE IS AN ASSERTION ON THE PRODUCTION PATH, not a printed value and not a comment.
```

## 5. Requirements with no detector — stated as such

| requirement | why there is none |
|---|---|
| **G8 flame speed** | no reacting-flow solver, no stiff integrator, no flame-speed extraction exist; `audit.md` §1 |
| **elemental conservation** | elemental composition is not modeled; §9 forbids inventing it |
| separate-process determinism (§43) | not part of the frozen gate; §43 says to report but not invent a new requirement. **NOT MEASURED**, and not claimed |

```text
THESE ARE THE HONEST GAPS. Two are scope limits the authorization itself anticipates, and the
third is explicitly optional. None is a criterion that was skipped.
```
