# P17.3 — Compressible Continuity: summary

**P17.3 STATUS: COMPLETE**, with the GPU-equivalence row recorded as `NOT PRESENT` rather
than passed. 2026-09-26.

Evidence root: `results/p17/compressible_continuity/`. Gate frozen before any edit,
sha256 `a89642a491acc0c86fda87dc17345fc1c5d24de7ef4cf6f43b33cbda545d23ef`.

---

## 1. What P17.3 turned out to be

Seven of nine requirements were already satisfied. **The production change is ~28 lines of
executable code** across two additions to the transient diagnostic.

```text
requirement                                class                 outcome
------------------------------------------------------------------------------------
Audit existing continuity implementation   ALREADY SATISFIED     audit.md
Verify transient form                      DIAGNOSTIC ONLY       verified + globalNetFlux
Verify steady form  div(rho U) = 0          ALREADY SATISFIED     verified, exactly 0
Confirm m_dot_f = rho_f (U_f.n_f) A_f      ALREADY SATISFIED     verified
Verify boundary mass fluxes                ALREADY SATISFIED     verified, signs measured
Verify global mass conservation            METRIC EXISTS         MassBalance reused
Verify low-Mach reduction                  ALREADY SATISFIED     verified, unchanged
Add manufactured / canonical tests         NEEDS EXTENSION       12 added
CPU/GPU equivalence where a GPU path exists NO GPU PATH          NOT PRESENT  [~]
```

## 2. Continuity architecture

```text
Continuity implementation    STEADY, in production, as the PRESSURE-CORRECTION equation --
                             its own header: "this equation IS the discretized compressible
                             continuity constraint solved for pressure, which is why no
                             separate solve-continuity step exists".
                             physics::evaluateContinuity reports the residual at
                             CompressibleSIMPLE.cpp:478.
                             TRANSIENT, as a DIAGNOSTIC with 0 production callers:
                             evaluateCompressibleContinuity.
Authoritative rho source     the P17.1 cell field, rho = EOS(p_abs, T), re-derived every
                             outer iteration; values validated finite and > 0
Authoritative rho_f source   evaluateCompressibleFaceDensity -- ONE SurfaceField.
                             internal: interpolateInternalFace of the cell field
                             boundary: the EOS at that face's own interpolated p_abs and T
Authoritative mass-flux owner calculateCompressibleMassFlux.
                             m_dot_f = rho_f * dot(U_f, areaVector), and areaVector carries
                             the normal AND the area, so no area factor is applied twice.
                             Recomputed fresh per call; the pressure-correction D_f is
                             HANDED that same field rather than recomputing it.
Steady formulation           sum_f m_dot_f = 0 per converged cell
Transient formulation        (rho^{n+1} - rho^n) V_P/dt + sum_f(±m_dot_f)
Boundary-flux path           polymorphic ScalarBoundaryCondition; no per-BC special case.
                             Outward-positive: inlet negative, outlet positive, wall zero.
```

## 3. Quantitative conservation

```text
maximum cell continuity residual      0            EXACTLY   (row-varying rho 2:1)
global boundary mass imbalance        0            EXACTLY
normalized mass imbalance             0            EXACTLY   (MassBalance.relativeImbalance
                                                              and .normalizedContinuity)
internal-face cancellation error      0            EXACTLY   (17 internal faces)
transient mass conservation error     0            EXACTLY   (balanced case)
                                      <= 8 eps of the mass scale (case with mass changing)
inflow / outflow                      -10 / +10    net exactly 0
fluxScale                             10
quiescent closed box                  every boundary flux 0, net 0, per-cell 0, mass fixed
```

## 4. Low-Mach reduction

```text
Mach number                     machMax < 0.1 AND > 1.25 * inletMach    inherited, PASS
density variation               deltaRhoOverRho < 1e-2                  inherited, PASS
velocity/flux difference vs ref  maxRelativeMassFluxDifference < 0.15    inherited, PASS
EOS consistency                 maxEosError < 1e-9                      inherited, PASS
global imbalance                <= its computed bound                   inherited, PASS
MMS observed order              1.0 in band [0.55, 1.30]                inherited, PASS
the reduction itself            at uniform rho the compressible mass flux equals
                                physics::calculateMassFlux BITWISE -- 0 differing faces
low-Mach + MMS suite            10 / 10
```

No threshold was re-derived or loosened.

## 5. CPU/GPU

```text
GPU path exists                       NO -- NOT PRESENT
rho_f difference                      N/A, nothing to compare
mass-flux difference                  N/A
continuity residual difference        N/A
global imbalance difference           N/A
```

Exactly one file in `cuda/` or `include/cfd/gpu/` mentions "compressible" —
`DevicePressureCorrection.hpp` — and only to record that it ports the **incompressible**
assembly. Gate **G8 is VACUOUS** and is recorded as `NOT PRESENT`, not as PASS. The GPU does
have an *incompressible* continuity gather, untouched by P17.3, and that is not evidence
about compressible continuity.

## 6. Tests and gates

```text
focused continuity tests       17 / 17    (6 + 11 new)
manufactured / canonical       6 of 6 covered: uniform, row-varying, operator-direct,
                               rho*U=const refinement, quiescent closed box, inlet/outlet
mass-flux suite                21 / 21    (20 + 1 new)
low-Mach + MMS                 10 / 10
compressible suite            161 / 161   (149 + 12)
CPU regression               2331 / 2331  0 failed   PREDICTED 2331
CUDA regression              2531 / 2531  0 failed   PREDICTED 2531
G1   baseline preservation     PASS -- SIMPLE fingerprint BITWISE IDENTICAL, empty diff;
                               P14 68/68, P15 161/161, P16 315/315
G3   variable-density conservation  PASS -- exactly 0 with rho varying 2:1
G5   low-Mach limiting behaviour    PASS -- inherited thresholds, unchanged
G8   CPU/GPU equivalence            VACUOUS -- NOT PRESENT
G9   determinism                    PASS -- bitwise over 8 repeats, every field
G10  full regression                PASS on both mandated configs; Debug+GUI NOT RUN
```

Every registry and execution count landed on a number frozen **before** the run:

```text
Release+CUDA   2593 registered - 13 excluded - 49 disabled = 2531 executed    exact
CPU-only       2391 registered - 13 excluded - 47 disabled = 2331 executed    exact
```

## 7. Controls — all six DETECTED, each matching its predicted set exactly

```text
N1   drop density-value validation                2 tests      as predicted
N2   drop mass-flux finiteness validation         2 tests      as predicted
N3   globalNetFlux sums ALL faces                 3 tests      as predicted
N4   face density collapsed to a constant         3 tests      as predicted
N5   flip the neighbour sign                      4 tests      as predicted
N6   drop V_P/dt from the transient term          2 tests      as predicted
```

All restores SHA-256 verified on both files, 0 markers left, 38/38 at baseline. Every
control's "must still pass" set also held, so each was isolated to its intended criteria.
No vacuous control this time — unlike P17.2's N6.

**N4 proves the `div(rho U)` vs `rho div(U)` distinction**: it collapses `rho_f` to a
constant and the row-varying and refinement tests both catch it. It also broke
`InternalFaceUsesArithmeticMeanDensity`, which P17.1's analogous control did not — because
P17.1's collapsed to the reference density, coinciding with that test's mean, whereas this
one collapses to `density[0] = 1.0` against a mean of 2.0. Exactly as
`variable_density/AMENDMENT_N_predictions.md` predicted.

## 8. Findings beyond the checklist

```text
1. A NaN MADE THE DIAGNOSTIC REPORT PERFECT CONSERVATION.
   std::max(a,b) is (a<b)?b:a, so with a running maximum of 0 and b = NaN the comparison
   0 < NaN is false and the NaN is SILENTLY SKIPPED. Measured pre-P17.3:
     rhoNew[0] = NaN     -> maxCellImbalance 0    "perfectly conserved"
     massFlux[0] = NaN   -> maxCellImbalance 0    "perfectly conserved"
   while totalAbsoluteImbalance became NaN -- the two metrics disagreed. A solver watching
   the max would declare convergence on a NaN state. The mirror of P17.1's finding: there
   a finite-but-wrong value evaded allFinite(); here a NaN evades the max-reduction.

2. A NEGATIVE DENSITY MADE totalMassNew EXACTLY 0 -- finite, plausible, catastrophic.

3. rho(x)U(x) = const IS NOT DISCRETELY EXACT, and asserting it would fail a CORRECT
   implementation. With linear interpolation rho_f U_f = mean(rho) mean(U) != mean(rho U),
   so the imbalance is O(h^2). Measured ratios 3.947, 3.971, 3.985 -> converging to 4,
   clean second order. Gated as a refinement study with a threshold of 3.5, derived from
   the orders (4 vs 2), and the test asserts the imbalance is NON-ZERO at every level --
   otherwise it would not be measuring O(h^2) at all.

4. A CLOSED BOX WITH A MOVING INTERIOR IS NOT A CONSERVATION TEST. The dry-run gave
   net = 0 but maxCellImbalance = 4, because a uniform velocity is not solenoidal inside
   impermeable walls; 4 = rho*u*A exactly. The canonical case is the QUIESCENT box, and the
   test was written that way rather than the criterion relaxed.

5. THE TWO CONTINUITY EVALUATORS SUM IN DIFFERENT ORDERS, and the project already treats
   that as load-bearing: cuda/kernels/DevicePressureCorrectionKernel.cu:28 and :229 record
   that the GPU port gathers "in cell.faceIds() order" because those are "the two orders
   the CPU actually uses". Measured: bitwise identical on the row-varying case, and within
   4 eps * max|m_dot| on an irregular flux field where nothing cancels by symmetry. The
   row-varying case alone could not have discriminated.

6. VALUE-VALIDATION ASYMMETRY, recorded as debt. evaluateCompressibleContinuity now
   validates its field values; physics::evaluateContinuity does not. Deliberate:
   the latter is shared with the incompressible path and changing it would put P17.3 in
   the way of the P13-001 fingerprint for no P17.3 benefit.
```

## 9. Repository impact

```text
source files added      0
source files modified   2   (CompressibleContinuity.hpp, CompressibleContinuity.cpp)
tests modified          2   (test_compressible_continuity.cpp, test_compressible_mass_flux.cpp)
tests added            12   (11 continuity + 1 mass-flux refinement)
evidence files         ~30  (7 documents, tools, raw logs)
threshold changes       NONE
```

```text
this session       4 files, 494 insertions, 0 deletions
cumulative vs HEAD 4 files, 770 insertions, 0 deletions  (includes P17.1's 276 in the
                                                          mass-flux test file)
executable production code  ~28 lines
tracked modified   171 -> 174   untracked 107 -> 107   tracked eng 120 -> 123
binaries added     0
physics::ContinuityEquation   UNTOUCHED, empty delta
```

`src/compressible/CompressibleMassFlux.cpp` has a post-freeze mtime because control N4
wrote and restored it. Content **byte-identical** to its P17.1 state: sha256
`2864c6eb…` matches both the controls' baseline and the P17.1 close, and its delta is still
exactly P17.1's `+34 -0`. Four files changed, not five.

## 10. Recommended next step

**P17.4 — Compressible Momentum.** Freeze and dry-run its gate first.

Why P17.4: it is next in the authorized working order, and unlike P17.1–P17.3 it contains
**genuine implementation work** that P17.0 identified and every phase since has deferred:

```text
the dilatational viscous stress term -(2/3) mu grad(div U) is ABSENT.
  grad(div 0 hits, dilatation 0, bulkViscosity 0, twoThirds 0, Stokes 0 -- measured.
  Momentum uses physics::assembleDiffusionContribution, the INCOMPRESSIBLE Laplacian
  mu lap U, and tests/integration/mms/test_mms_compressible.cpp names the omission in its
  own machine-readable not_covered field.
  Harmless through P16 because div U = 0 there. Compressible flow is exactly the regime
  where div U != 0.
```

What P17.4 inherits, now all verified: one authoritative density with a validated
precondition (P17.1), a complete three-way EOS closure (P17.2), and one conservative mass
flux whose steady balance is exact and whose boundary signs are measured (P17.3).

Two hazards it must design against, both carried forward: `assembleCompressibleMomentumComponent`
has **no production caller** (it calls `.build()` internally so it cannot sit in a relaxed
loop — production uses `assembleRelaxedCompressibleMomentumComponent`), and momentum is
**upwind-only and 2D-only**, so any convergence claim must say so.

**Not started.** P17.4 has no implementation authorization.
