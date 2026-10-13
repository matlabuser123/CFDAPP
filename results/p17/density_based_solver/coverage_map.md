# P17.8 — criterion-to-test coverage map

The frozen gate's §15 requires this file, and requires it to live **outside** the frozen document so
completing it cannot move that hash.

The **asserted?** column is the point of the exercise. It is not "does a test with this id exist"
but *"does something FAIL if the criterion is violated"*. Two criteria in the first pass had a test
bearing their name that could not fail — both are marked and both are now fixed.

```text
asserted?  meaning
---------------------------------------------------------------------------------------------------
YES        a test asserts it and a planted violation makes that test fail (Group N, or argued below)
STRUCT     asserted by tools/structural.sh, with a live SANITY control that must be non-zero
REPORTED   measured and printed, deliberately not gated -- the gate says so in that criterion's text
N/A        a cross-reference, not a separate measurement
```

---

## Group A — SIMPLE preservation

| id | criterion | test / probe | asserted? | measured |
| --- | --- | --- | --- | --- |
| A1 | `ProjectRunner.cpp` untouched | `structural.sh` §A1/A2 | STRUCT | SHA-256 identical to `02_freeze.log` |
| A2 | both SIMPLE paths untouched | `structural.sh` §A1/A2 | STRUCT | SHA-256 identical, 4 files |
| A3 | P13-001 SIMPLE fingerprint | `fingerprint.sh` §1 | YES | **BITWISE IDENTICAL**, 10 cases / 130 lines, 0-byte diff |
| A4 | 0 references from a SIMPLE path | `structural.sh` §A4 | STRUCT | 0, with a sanity control returning 7 |
| A5 | `CFL.*`, `TransientSolver.*` untouched | `structural.sh` §A5 | STRUCT | CFL.hpp by frozen SHA; other 3 git-clean vs HEAD |
| A6 | every P17.1–P17.7 production source untouched | `structural.sh` §A6 | STRUCT | 27 UNTOUCHED, 0 TOUCHED, **one declared exception** |

```text
A6's EXCEPTION, declared and not hidden: include/cfd/compressible/HllcFlux.hpp CHANGED, by
AMENDMENT_star_energy_sign.md -- a REAL sign error in P17.7's star energy found by P17.8's inviscid-
wall test. A6 as literally written ("0 TOUCHED") is therefore NOT met, and saying otherwise would be
the kind of quiet reinterpretation this gate exists to prevent. What IS true: the change is derived
from Rankine-Hugoniot, proved against a property neither artefact can satisfy by accident, re-
verified against P17.7's full suite, and the file is reported on its own line rather than counted as
untouched. See AMENDMENT_A6_upstream_exception.md.
```

## Group Q — the conserved state and its conversions

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| Q1 | `fromPrimitive` → `toPrimitive` round trip | `Q1_PrimitiveConservedRoundTripIsExact` | YES | `rho`, `rhoE` **BITWISE**; worst T and p error `0.000e+00` |
| Q2 | no duplicate EOS / sound speed / gamma arithmetic | `structural.sh` §Q2 | STRUCT | 0, with 3 sanity controls (1, 8, 3 hits) |
| Q3 | P17.5's conversions are the only ones called | `structural.sh` §Q3 | STRUCT | 1 / 1 / 1 call sites, 0 alternatives |
| Q4 | primitives not stored beside `Q` | `structural.sh` §Q4 | STRUCT | 0 stored, sanity control 4 members |

## Group R — the residual

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| R1 | owner/neighbour cancel, all 5 components, BITWISE | `R1_ResidualIsExactlyPlusFOwnerMinusFNeighbourBitwise` | YES (N1) | 12 cells × 5 components, 17 internal faces, **EXACTLY equal** |
| R1 | (the closed-box sum, kept) | `R1_OwnerAndNeighbourContributionsCancelExactly` | YES (N1) | mass `-5.68e-14` / `9.95e-14`, bound `2.85e-11` |
| R2 | two-cell residual vs an independent reference | `R2_TwoCellResidualMatchesAnIndependentReference` | YES | vs mpmath 50-digit, all 10 components within `8 eps × sum_f|F_f|A` |
| R3 | exactly one `hllcFlux` call site | `structural.sh` §R3 | STRUCT | 1 |
| R4 | closed uniform domain → zero residual | `R4_UniformStateGivesZeroResidual` | YES (N1) | worst `|R_rho|` and `|R_E|` **`0.000e+00`** |
| R5 | `+F` owner / `−F` neighbour, the repo convention | `structural.sh` §R5 | STRUCT | 1 and 1, reference file has 6 `+=`/`-=` |

```text
WHAT R1's BITWISE TEST CAN AND CANNOT SEE, stated in the test itself: it fails if the neighbour's
contribution is dropped, scaled, or computed from a different value. It CANNOT detect a second
hllcFlux evaluation with swapped states, because P17.7 proved F(L,R,n) = -F(R,L,-n) BITWISE -- the
two would agree to the last bit. R3's structural "exactly 1 call site" covers that half. Neither
criterion alone covers the claim; the pair does.
```

## Group K — CFL and the time step

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| K1 | `dt` exactly linear in CFL | `K1_TimeStepIsExactlyLinearInCfl` | YES | ratio **exactly 2** |
| K2 | `dt` falls with `u`, `a`, cell size | `K2_TimeStepFallsWithSpeedSoundSpeedAndCellSize` | YES | `2.687e-4` → `1.675e-4` / `1.390e-4` / `6.717e-5` |
| K3 | `dt > 0` and finite for every valid state | `K6_EdgeCasesGiveFinitePositiveTimeSteps` | YES (N3) | 6 edge cases, all finite positive |
| K4 | consistency with the EXISTING `calculateCFL` | `K4_AgreesWithTheExistingCalculateCflInTheZeroSoundSpeedLimit` | YES (N3) | relative difference **`0.000e+00`**, bound `1e-12` |
| K5 | invalid CFL throws, naming the parameter | `K5_InvalidCflIsRefused` | YES | 4 invalid CFLs + 3 other parameters refused |
| K6 | edge cases give finite positive `dt` | `K6_EdgeCasesGiveFinitePositiveTimeSteps` | YES | `6.717e-8` … `6.717e-3` |

## Group U — the update and the two modes

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| U1 | `Q_new == Q_old − (dt/V) R`, BITWISE, 5 components | `U1_UpdateIsExactlyQMinusDtOverVTimesR` | YES (N2) | bitwise on a **graded** mesh; 1/V dependence within `64 eps` |
| U2 | GlobalPhysical: one `dt`, equal to `min_P dt_P` | `U2_GlobalModeUsesOneDtForEveryCell` | YES (N7) | local range `[2.146e-4, 2.642e-4]`, global = the minimum exactly |
| U3 | LocalPseudoTime refuses to report a physical time | `U3_LocalPseudoTimeRefusesToReportAPhysicalTime` | YES | accessor throws; GlobalPhysical reports `1.0e-5 s` |
| U4 | the mode is explicit; no automatic switching | `structural.sh` §U4 | STRUCT | 0 assignments outside construction, 5 reads |

```text
U1 WAS NOT ASSERTED BEFORE THE CONTROLS RAN. It built its mesh with createCartesian2D(3, 2, 3.0, 2.0)
-- dx = dy = 1, so every cell volume was EXACTLY 1.0 and dt/V was bit-identical to dt. Control N2
deletes the division by volume and U1 PASSED. The mesh is now graded and the test asserts that no
cell has unit volume before asserting anything else. See AMENDMENT_N2_N3_control_predictions.md §3.
```

## Group P — positivity of the updated state, and robustness

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| P1 | every candidate update checked by P17.5's model | `P1_P3_AnInadmissibleStateIsRefusedNamingTheCell` | YES | refused, naming the cell |
| P2 | deterministic retry, `dt ← dt × 0.5`, ≤ 5 times | `P2_P6_RetriesAreDeterministicAndCounted` | YES | CFL 50: 5 iterations, 5 retries, 0 rejections, repeat IDENTICAL |
| P3 | terminal failure names the offending cell | `P1_P3_…` and `P3_TerminalFailureReportsTheOffendingCellInTheFieldToo` | YES | field and message agree (cell 5); `rejectedSteps` 1, `totalRetries` 0 |
| P4 | nothing is clamped | `P4_NothingIsEverClamped` + `structural.sh` §P4 | YES (N4) | absurd-but-positive density refused; 0 clamp idioms |
| P5 | non-finite detection covers the enumerated list | `P5_EveryEnumeratedQuantityIsCheckedForNonFiniteness` | YES | 7 corruptions + p/T + dt + residual, each naming its cell |
| P6 | retries and rejections counted and reported | `P2_P6_RetriesAreDeterministicAndCounted` | YES | reported per step and in the result |

```text
P3's FIELD was not asserted before this phase's second pass: the message named the right cell while
result.offendingCell was hard-coded to 0 on the recover-failure path. The test asserted the message
only. Both are now asserted and the production code sets the field from the same source.
P5 FOUND A REAL GAP: solve() caught errors from toPrimitive and from the dt evaluation but NOT from
assembleResidual, so a non-finite flux escaped as an uncaught exception instead of a NonFiniteState
with a location. Fixed; the diagnostic now names the iteration, the face and the owner cell.
```

## Group C — the canonical cases

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| C1 | uniform grid-aligned flow preserved, 20 steps | `C1_UniformGridAlignedFlowIsPreserved` | YES (N1) | 23 steps, drift **`0.000e+00`** on rho/rhoU/rhoE, bound `1.421e-14` |
| C2 | uniform OBLIQUE flow preserved | `C2_UniformObliqueFlowIsPreserved` | YES (N1) | 23 steps, worst drift **`0.000e+00`** |
| C3 | advected contact keeps `p` and `u` uniform | `C3_AdvectedContactKeepsPressureAndVelocityUniform` | YES | `p` `7.181e-16`, `u` `6.496e-16`, detection bound `1e-9` |
| C4 | smooth wave vs its analytic propagation | `C4_SmoothAdvectedPulseConvergesToTheAnalyticSolution` | REPORTED + YES | see below |

```text
C4, as the gate requires: the order is REPORTED, never gated on an invented value. What IS asserted
is only what is derivable -- the error must fall under refinement, and p and u must stay uniform.

  nx      dx          L1(rho)        max|dp|/p      max|du|/u     steps
  64      1.5625e-02  1.038274e-02   2.393917e-15   8.120488e-16  134
  128     7.8125e-03  5.993063e-03   2.675554e-15   9.744586e-16  268
  256     3.9062e-03  3.255498e-03   7.322569e-15   1.948917e-15  536
  observed order  64 -> 128 : 0.7928      128 -> 256 : 0.8804

The case is a Gaussian density pulse on a uniform supersonic uniform-PRESSURE background -- an exact
Euler solution (an entropy wave) that advects rigidly. Both open boundaries are exact for it: the
flow is supersonic so every characteristic runs downstream, the prescribed inflow IS the background,
and the pulse never reaches either end. p and u are preserved to ROUND-OFF, which is the strong half
of the result; the first-order scheme's numerical diffusion is what the L1 error and the ~0.9 order
measure.
```

## Group G — conservation (gate criteria G3 and G4)

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| G1 | closed domain conserves mass and energy | `G1_ClosedDomainConservesMassMomentumAndEnergy` | YES (N1, N4) | mass `8.310e-16`, energy `1.696e-16` relative, 37 steps, bound `1.421e-13` |
| G1 | closed domain **momentum** against the integrated wall force | `G1_ClosedDomainMomentumMatchesTheIntegratedWallForce` | YES (N1, N6) | `Δ` momentum-x `+2.864e+01`, wall impulse `−2.864e+01`, residual **`1.066e-14`** (bound `2.610e-08`) |
| G1 | the same, on a **graded** mesh | `G1_ClosedDomainConservesOnAGradedMeshToo` | YES (N1, N2, N4) | volume ratio > 2; mass and energy at round-off |
| G2 | open domain balances against the boundary flux | `G2_OpenDomainBalancesAgainstTheIntegratedBoundaryFlux` | YES (N1, N4) | see below |
| G3 | R1 is what makes G1 exact | — | N/A | cross-reference |

```text
G2, 60 cells, 46 boundary faces, 40 steps:
  (i)  worst |sum_P R_P - boundary flux| / |boundary flux|   6.376e-16
  (ii) mass      d(total) +3.3803615524e-03  + integral -3.3803615524e-03  -> 3.816e-17  (bound 5.509e-13)
       momentum  d(total) +2.7193294163e+00  + integral -2.7193294163e+00  -> 1.332e-14  (bound 3.598e-10)
       energy    d(total) +1.8024890687e+03  + integral -1.8024890687e+03  -> 3.365e-11  (bound 2.401e-07)
The case TRANSPORTS: the domain totals move by a measurable amount, so the balance is not 0 == 0.
That is asserted, not assumed.
```

## Group M — residual norms and convergence

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| M1 | five norms, one per equation, volume-weighted L2 | `structural.sh` §M1 | STRUCT | 5 |
| M2 | the norm does not scale with mesh size | `M2_NormDoesNotScaleWithMeshSize` | YES | `0.000e+00` at 8×8 and 32×32 |
| M3 | the reference scale is captured once, at iteration 0 | `M3_TheResidualReferenceScaleIsCapturedOnceAtIterationZero` | YES (N5) | implied-reference drift ≤ `8 eps`; relative residual moves |
| M4 | convergence needs ALL of the three signals | `M4_EverySignalIsActuallyConsulted` | YES | all-loose 354 iters; blocking each in turn gives 4000 / 1548 / 2231 |
| M5 | reaching the end time is `TimeComplete`, not `Converged` | `M5_ReachingTheEndTimeIsTimeCompleteNotConverged` | YES | exact status |
| M6 | steady criteria evaluated only in LocalPseudoTime | `M6_SteadyCriteriaAreNotEvaluatedInGlobalPhysicalMode` | YES | same state and tolerances: pseudo-time Converged, transient never |

## Group D — determinism (gate criterion G9)

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| D1 | repeated solve is bitwise identical | `D1_RepeatedSolveIsBitwiseIdentical` | YES | 20 steps: dt, time, residual, state and retry histories all **BITWISE** |
| D2 | control-flow decisions are deterministic | `P2_P6_RetriesAreDeterministicAndCounted` | YES | the repeat made IDENTICAL retry decisions |

## Group S — scope boundaries that must REFUSE

| id | criterion | test | asserted? | measured |
| --- | --- | --- | --- | --- |
| S1 | `restartSupported()` is false | `S1_S2_RestartIsUnsupportedAndRefuses` | YES | false |
| S2 | a restart request throws, naming the fields | `S1_S2_RestartIsUnsupportedAndRefuses` | YES | message contains `rhoU`, `rhoE`, `NOT SUPPORTED` |
| S3 | no viscous or conduction term, and no zero stub | `structural.sh` §S3 | STRUCT | 0 in code (1 mention, in a comment) |
| S4 | no GPU density-based solver claimed | `summary.md`, this file | REPORTED | **NOT IMPLEMENTED** |

## Group N — the non-vacuity controls

| id | planted defect | must break | result |
| --- | --- | --- | --- |
| N1 | neighbour's `−F` dropped | R1, G1, G2, C1 | **all 4 detected**; Q1, K1 still pass |
| N2 | update uses `dt` not `dt/V` | U1, G1(graded) | **both detected**; C1 and uniform-G1 **cannot** detect it — amended |
| N3 | acoustic term dropped from the CFL | K4 | **detected**; K1 passes; K3 also breaks — amended |
| N4 | density clamped to a floor | G1, G2, P4 | **all 3 detected**; C1 still passes |
| N5 | residual reference re-captured each iteration | M3 | **detected**; K1 still passes |
| N6 | wall exterior state does not mirror `u_n` | wall flux | **detected**; C1 still passes |
| N7 | `min_P dt_P` replaced by cell 0's `dt` | U2 | **detected**; K1, K3 still pass |

Every plant was verified to have changed the file's SHA-256 before it was trusted, and the source was
verified restored to the pristine hash after each. `CONTROLS WITH AN UNEXPECTED RESULT: 0`.

---

## What this map says is NOT covered

```text
* G7 SHOCK VALIDATION -- not claimed, not attempted. No Sod tube, no normal shock, no shock-position
  or jump-ratio measurement anywhere in P17.8. That is P17.9's, and §49 forbids starting it.
* G8 CPU/GPU EQUIVALENCE for the SOLVER -- there is no GPU density-based solver. P17.7's flux is
  already bitwise identical on both, but the residual assembly, the CFL reduction and the update are
  host-only. Reported NOT IMPLEMENTED, never PASS.
* VISCOUS AND CONDUCTIVE TERMS -- Euler only. Compressible Navier-Stokes is not claimed.
* PRODUCTION REACHABILITY -- library level. No case-format key, no dispatch change. A case file
  cannot select this solver.
* SUBSONIC OPEN BOUNDARIES -- not implemented. Three treatments only: inviscid wall, supersonic
  inflow, supersonic outflow.
* FORMAL ORDER OF ACCURACY -- C4's ~0.9 is a measured order on one smooth case, reported as such,
  not a verified order-of-accuracy claim with Richardson extrapolation and GCI.
```
