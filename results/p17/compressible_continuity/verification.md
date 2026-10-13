# P17.3 — verification

Consolidates the steady and transient verification, conservation, low-Mach reduction,
manufactured tests, determinism and the CPU/GPU position. Measured numbers, not verdicts.

Raw: `logs/00_dryrun.log` (before), `logs/01_gate.log`, `logs/07_controls.log`.
Probe: `tools/dryrun.cpp`.

---

## 1. Steady form — exact on a genuinely varying density

Row-varying construction, 4x3 mesh, `rho` 2:1, `U = (2, 0)`:

```text
maxCellImbalance                 0        EXACTLY
totalAbsoluteImbalance           0        EXACTLY
globalNetFlux                    0        EXACTLY
internal faces                  17        worst |(+F) + (-F)| = 0  EXACTLY
totalMassOld / totalMassNew     20 / 20
```

Exact because the construction makes a cell's left and right faces take the **same**
`rho_f` (they lie in the same row), so their fluxes cancel bit for bit, while y-normal
faces carry zero against an x-only velocity. Density genuinely varies 2:1 throughout.

**It is `div(rho U)`, not `rho div(U)`:** control **N4** collapses `rho_f` to a constant —
the `rho div(U)` form — and three tests catch it, including the refinement study.

## 2. Transient form

```text
cellImbalance[P] = (rho^{n+1} - rho^n) V_P/dt + sum_f(±m_dot_f)
```

```text
hand-derived value                   CombinedTimeAndFluxHandDerived        PASS (pre-existing)
time-only term, zero flux            TimeOnlyImbalanceWithZeroFlux         PASS (pre-existing)
reduces to the steady sum when
  rhoOld == rhoNew                   0 of 12 cells differ bitwise          EXACT
totalMass = sum(rho_i V_i)           TotalMassMatchesSumOfRhoTimesVolume   PASS, 4 eps
density genuinely changing in time    asserted in 3 of the new tests        PASS
```

### The integral conservation relation — what `globalNetFlux` was added for

```text
M(t2) - M(t1)  +  dt * globalNetFlux  ==  0
```

Two cases, `TransientIntegralMassConservationHolds`:

```text
case 1  balanced boundary flux      globalNetFlux = 0, dM = 0, residual EXACTLY 0
case 2  net outflow 0.8 over dt=0.25, density lowered to match
        globalNetFlux = 0.8 exactly
        residual bounded at 8 eps of the mass scale, and the mass GENUINELY changes
        (EXPECT_NE asserted, so the case cannot pass by staying still)
```

Before P17.3 this could not be written through the API: the boundary sum was absent and a
caller had to re-loop `mesh.boundaryPatches()` and re-derive the sign convention by hand —
which the dry-run probe demonstrably had to do.

## 3. Face mass flux

```text
m_dot_f = rho_f (U_f . n_f) A_f          F1  verified against an independent expectation
areaVector carries normal AND area       F2  no double area factor
internal-face antisymmetry               F3  worst |(+F) + (-F)| = 0  EXACTLY
velocity reversal negates every flux     F4  exact IEEE negation (P17.1 test)
one canonical rho_f reaches continuity,
  the pressure-correction D_f and the
  diagnostics                            F5  structural -- the field is PASSED, not
                                             recomputed; verified by reading call sites
```

## 4. Boundary mass fluxes — measured signs

```text
patch     faces   sum        expected
left        3     -10        NEGATIVE = mass entering     OK
right       3     +10        POSITIVE = mass leaving      OK
bottom      4       0        wall, exactly zero           OK
top         4       0        wall, exactly zero           OK
inlet + outlet                EXACTLY 0                   OK
```

Boundary density is EOS-derived at each face's own interpolated `(p_abs, T)`, so the face
state and face density are consistent by construction (P12-COMP-001, verified by the
existing `BoundaryDensityMatchesDirectEosEvaluation` and
`OutletBoundaryUsesEosEvaluatedPressureNotOwnerDensity`).

```text
NOT PRESENT in the codebase, stated rather than skipped:
  mass-flow inlet, periodic BC -- no such classes exist at all
```

## 5. Global conservation — the established metric

`physics::computeMassBalance`, reused rather than replaced:

```text
row-varying case    inflow 10   outflow 10   net 0
                    relativeImbalance 0   fluxScale 10   normalizedContinuity 0
                    maxCellImbalance 0    rmsCellImbalance 0
quiescent closed box  every boundary flux 0, net 0, per-cell 0, mass unchanged  EXACTLY
```

### A closed box with a moving interior is NOT a conservation test — recorded

The dry-run measured, for a closed box with a uniform interior velocity of `(2,0)`:

```text
inflow 0   outflow 0   net 0        <- the boundary criterion is satisfied
maxCellImbalance 4                  <- but the per-cell balance is NOT
```

That is **correct behaviour for an inconsistent state I constructed**: a uniform velocity
is not solenoidal inside impermeable walls, so mass piles up against the wall. The value
4 = `rho * u * A` = 2*2*1 exactly. The canonical closed-box case is therefore the
**quiescent** one, and `QuiescentClosedBoxConservesExactly` is written that way. Recorded
so a future reader does not mistake the 4 for a defect, or the quiescent choice for
weakening the test.

## 6. Manufactured and canonical tests

```text
M1  uniform rho, uniform U                 exactly 0
M2  row-varying rho, x-only velocity       exactly 0, with rho varying 2:1
M3  operator on a constructed
    divergence-free massFlux               exactly 0  (UniformDivergenceFreeFlux...)
M4  rho(x) U(x) = const                    REFINEMENT study, see below
M5  quiescent closed box                   exactly 0
M6  inlet/outlet balance                   exactly 0
```

### M4 — a convergence claim, deliberately not an exactness claim

With linear face interpolation `rho_f U_f = mean(rho) mean(U) != mean(rho U)`, so a cell's
left and right fluxes differ and the imbalance is `O(h^2)`, **not** zero. A test asserting
exact zero would fail a correct implementation.

```text
nx    maxCellImbalance          normalizedContinuity      ratio vs previous
8     0.0075960497835497187     0.010418324324846793      -
16    0.0019244223593570531     0.0037897426111169735     3.947
32    0.00048458280630511719    0.0013599913865735792     3.971
64    0.00012160083245853909    0.00048451148048800531    3.985
```

Converging to **4 per halving of h** — clean second order.

```text
gate threshold   ratio > 3.5 at the finest pair.
DERIVATION       second order gives 4, first order gives 2. 3.5 admits pre-asymptotic
                 behaviour while excluding first order by a wide margin. Derived from the
                 orders themselves, not fitted to the measured 3.985.
non-vacuity      the test also asserts the imbalance is NON-ZERO at every level -- if it
                 were exactly zero the test would not be measuring O(h^2) at all.
```

## 7. Summation-order consistency — measured, both ways

```text
O1  row-varying case          0 of 12 cells differ   BITWISE IDENTICAL
O2  irregular flux field      agreement within 4 eps * max|m_dot|
```

O2 exists because O1 cannot discriminate: on the row-varying case most face contributions
are equal-and-opposite or zero, so the two loop orders cannot be told apart. The irregular
field gives every face a distinct magnitude.

This was worth asking. The two evaluators sum the same terms in different orders
(`physics::evaluateContinuity` per cell over `cell.faceIds()`; the compressible one per
face into owner and neighbour), and **the project already treats that order as
load-bearing** — `cuda/kernels/DevicePressureCorrectionKernel.cu:28` and `:229` record
that the GPU port deliberately gathers "in `cell.faceIds()` order" because those are "the
two orders the CPU actually uses".

## 8. Low-Mach reduction

```text
existing low-Mach + MMS suite            10 / 10, unchanged
machMax < 0.1 AND > 1.25 * inletMach     inherited verbatim
deltaRhoOverRho < 1e-2                   inherited verbatim
maxEosError < 1e-9                       inherited verbatim
maxRelativeMassFluxDifference < 0.15     inherited verbatim
globalMassImbalance <= computed bound    inherited verbatim
MMS order gate 1.0, band [0.55, 1.30]    inherited verbatim
```

**L3, the concrete reduction:** at uniform density the compressible mass flux equals
`physics::calculateMassFlux` **bitwise** — 0 differing faces of 7, measured in P17.1's
dry-run. Bitwise is the right requirement here because at uniform density the two formulas
are the same arithmetic, not merely close; this is not a comparison between physically
different formulations.

No threshold was re-derived or loosened.

## 9. Determinism

```text
repeated evaluateCompressibleContinuity, 8 repeats
  cellImbalance per cell, globalNetFlux, totalMassOld/New, maxCellImbalance
  ALL BITWISE IDENTICAL
```

Bitwise is the correct criterion and needs no caveat: the evaluator is straight-line double
arithmetic over a fixed traversal order, with no threading and no iteration.

## 10. Invalid states

```text
densityOld / densityNew in {-2, 0, NaN, +Inf, -Inf}   all REJECTED, cell index named
massFlux in {NaN, +Inf, -Inf}                          all REJECTED, face index named
massFlux negative (inflow)                             ACCEPTED -- valid, and asserted
sizes, dt                                              pre-existing checks still hold
clamping                                               NONE
```

The pre-P17.3 behaviour these replace:

```text
rhoNew[0] = NaN     -> maxCellImbalance 0    "perfectly conserved"
massFlux[0] = NaN   -> maxCellImbalance 0    "perfectly conserved"
rhoNew[0] = -2.0    -> totalMassNew 0        finite, plausible, catastrophic
```

## 11. Non-vacuity controls — all six DETECTED

```text
id   control                                     tests broken   predicted set matched?
--------------------------------------------------------------------------------------
N1   drop density-value validation                    2          YES, exactly
N2   drop mass-flux finiteness validation             2          YES, exactly
N3   globalNetFlux sums ALL faces                     3          YES, exactly
N4   face density collapsed to a constant             3          YES, exactly
N5   flip the neighbour sign                          4          YES, exactly
N6   drop V_P/dt from the transient term              2          YES, exactly
```

All restores VERIFIED by SHA-256 on both files, 0 markers left, 38/38 at baseline
afterwards. Each control's "must still pass" set also held, so every control was isolated
to its intended criteria.

**N4 is the control that proves the `div(rho U)` vs `rho div(U)` distinction** — it turns
the operator into the constant-density form and the row-varying and refinement tests both
catch it. Note it also broke `InternalFaceUsesArithmeticMeanDensity`, which P17.1's
analogous control did **not**: P17.1's collapsed to the reference density, which coincided
with that test's mean, whereas this one collapses to `density[0] = 1.0` against a mean of
2.0. The two controls differ in exactly the way `AMENDMENT_N_predictions.md` predicted.

## 12. CPU/GPU

```text
GPU compressible continuity path        NOT PRESENT
```

Measured: exactly one file in `cuda/` or `include/cfd/gpu/` mentions "compressible" —
`DevicePressureCorrection.hpp` — and only to record that it ports the **incompressible**
`assemblePressureCorrection`. There is no compressible continuity kernel, no compressible
mass flux and no EOS on the device.

```text
rho_f difference                  N/A -- nothing to compare
mass-flux difference              N/A
continuity residual difference    N/A
global imbalance difference       N/A
```

Gate **G8 is VACUOUS** and is recorded as `NOT PRESENT`, **not** as PASS. "Nothing to
compare, therefore equivalent" is exactly the vacuous pass this project forbids.

The GPU *does* have an incompressible continuity gather inside
`DevicePressureCorrectionKernel.cu`, and P17.3 did not touch it — but that is the
incompressible path and it is not evidence about compressible continuity.
