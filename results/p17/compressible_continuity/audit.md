# P17.3 — Compressible Continuity: audit and implementation decision

Written 2026-09-26, **before any code was changed**, from `results/p17/baseline/`,
`results/p17/audit/`, `results/p17/gates/P17_GATES.md`, `results/p17/variable_density/`
and `results/p17/ideal_gas_eos/`.

---

## 1. What exists, traced

### Two continuity evaluators, both diagnostics

```text
physics::evaluateContinuity(mesh, massFlux)            include/cfd/physics/ContinuityEquation.hpp
  STEADY.  cellImbalance[P] = sum over P's faces of (+F if owner else -F)
  returns  cellImbalance, globalNetFlux, totalAbsoluteImbalance, maxCellImbalance
  loop     per CELL, then over cell.faceIds(), sign by ownership
  CALLED BY CompressibleSIMPLE.cpp:478  -- this is the production path

compressible::evaluateCompressibleContinuity(mesh, rhoOld, rhoNew, massFlux, dt)
  TRANSIENT. cellImbalance[P] = (rho_new - rho_old) V_P/dt + sum_faces(±F)
  returns  cellImbalance, totalAbsoluteImbalance, maxCellImbalance,
           totalMassOld, totalMassNew
  loop     per CELL for the time term and the masses, then per FACE accumulating
           +F into owner and -F into neighbour
  CALLED BY nothing in production -- its own .cpp, 6 unit tests, and one line of
           test_low_mach_regression.cpp
```

Both use the **same sign convention**: owner-oriented, `+F` for the owner, `-F` for the
neighbour, with the face normal outward from the owner.

### The established mass-balance diagnostic — already normalized

```text
physics::computeMassBalance(mesh, massFlux) -> MassBalance {
  inflow, outflow, net, relativeImbalance,
  maxCellImbalance, rmsCellImbalance, fluxScale, normalizedContinuity }
```

**This is the project's established metric and P17.3 reuses it.** §8 of the authorization
asks for a normalized imbalance "preferably" and says "use the project's established
diagnostic if one exists" — it does, so no new metric is invented.

### The authoritative mass flux — settled by P17.1

```text
calculateCompressibleMassFlux(...)  ->  m_dot_f = rho_f * dot(U_f, face.areaVector())
  areaVector carries the normal AND the area, so this is exactly rho_f (U_f . n_f) A_f
  face density comes from ONE canonical field, evaluateCompressibleFaceDensity:
    internal face   interpolateInternalFace of the cell density field
    boundary face   the EOS at that face's own interpolated p_abs and T
  recomputed FRESH on every call; nothing caches it
  P17.1 added the density precondition: every density[i] finite and > 0
```

### Production execution path

```text
pressure (gauge) + temperature (input)
  -> p_abs = referencePressure + p_gauge
  -> rho = EOS(p_abs, T)                       per cell, every outer iteration
  -> rho_f                                     evaluateCompressibleFaceDensity
  -> m_dot_f = rho_f (U_f . n_f) A_f           calculateCompressibleMassFlux
  -> the PRESSURE-CORRECTION equation          assembleCompressiblePressureCorrection
       D_f uses that same rho_f; the diagonal carries
       V_P/pseudoTimeStep * dDensityDPressure(p_abs,T)
       -- its own header: this equation IS the discretized compressible continuity
          constraint solved for pressure, which is why no separate "solve continuity"
          step exists
  -> corrected pressure -> corrected rho -> corrected flux
  -> physics::evaluateContinuity(mesh, fluxNew)  CompressibleSIMPLE.cpp:478
       reported as the continuity residual and |globalNetFlux|
```

## 2. GPU status — measured

```text
compressible continuity on GPU        NOT PRESENT
```

Carried from P17.0 and re-checked: exactly one file in the GPU layer mentions
"compressible" (`DevicePressureCorrection.hpp`) and only to record that it ports the
**incompressible** assembly. There is no device continuity kernel, no device compressible
mass flux, and no device EOS. So gate **G8 is vacuous** for P17.3 and no CPU/GPU
equivalence test is possible — recorded, not built.

## 3. Classification of every P17.3 requirement

```text
requirement                                  class                 action
---------------------------------------------------------------------------------------
Audit existing continuity implementation     ALREADY SATISFIED     this document
Verify transient form                        DIAGNOSTIC ONLY       verify + one addition
  d(rho)/dt + div(rho U) = 0                 no production caller  (see section 4)
Verify steady form  div(rho U) = 0           ALREADY SATISFIED     verify with varying rho
Confirm m_dot_f = rho_f (U_f . n_f) A_f      ALREADY SATISFIED     verify orientation and
                                             (P17.1)               antisymmetry
Verify boundary mass fluxes                  ALREADY SATISFIED     extend per BC type
                                             (P17.1)
Verify global mass conservation              METRIC EXISTS         reuse MassBalance;
                                             (MassBalance)         transient needs §4
Verify low-Mach reduction                    ALREADY SATISFIED     verify
Add manufactured / canonical tests           NEEDS EXTENSION       the new tests
CPU/GPU equivalence where a GPU path exists  NO GPU PATH           record accurately
```

## 4. The gaps that need code — two, both small

### (a) `CompressibleContinuityResult` has no `globalNetFlux`

Measured: **0 hits** for `globalNetFlux` in `CompressibleContinuity.hpp` and `.cpp`.
`physics::ContinuityResult` has it; the compressible transient result does not.

§8 requires the integral conservation relation

```text
M(t2) - M(t1) + integral over [t1,t2] of sum_boundary(m_dot_f) dt  ~= 0
```

`totalMassOld` and `totalMassNew` give `M(t1)` and `M(t2)`. The boundary flux sum is
**not** available, so a caller must re-loop the boundary patches itself — duplicating
accumulation logic outside the API that owns the convention, with the sign convention
re-derived by hand. That is the same argument that justified P17.2's `temperature()`, and
it is the one thing standing between P17.3 and the metric §8 asks for.

### (b) Neither continuity evaluator validates field VALUES

```text
isfinite checks on massFlux or density VALUES
  src/compressible/CompressibleContinuity.cpp     0
  src/physics/ContinuityEquation.cpp              0
```

Both validate array **sizes** and `dt`; neither looks at the numbers. §15 asks that
continuity not accept invalid states silently, and contemplates validation "at an API
boundary" — which this is: `evaluateCompressibleContinuity` takes raw `ScalarField` and
`SurfaceField` arguments from an arbitrary caller.

P17.1 established the policy and the reason it matters: **the dangerous cases are the
finite ones.** A `NaN` imbalance fails a `< tolerance` comparison anyway, but a negative
density produces a *finite, wrong* `totalMass`, and the mass-conservation metric built on
it would look plausible.

**Scope boundary, decided now:** P17.3 adds the density-value precondition to
`evaluateCompressibleContinuity` only — the compressible API this item owns. It does
**not** touch `physics::evaluateContinuity`, which is shared with the incompressible path
and whose modification would put P17.3 in the way of the constant-density fingerprint for
no P17.3 benefit. That mismatch is recorded as debt.

## 5. A consistency question to MEASURE, not assume

The two evaluators sum the same terms in **different orders**:

```text
physics::evaluateContinuity          per cell: for faceId in cell.faceIds(): sum += ±F
evaluateCompressibleContinuity       per face: owner += F; neighbour -= F
```

Floating-point addition is not associative, so for a given cell the two can in principle
produce **bitwise different** `cellImbalance` from the same `massFlux`. The compressible
header asserts the conventions are the same; it does not claim the arithmetic is.

Whether they agree bitwise, or only to round-off, is a fact to measure in the dry-run and
to gate accordingly — not something to assert in either direction. §12 asks exactly this:
whether multiple mass-flux/continuity paths intentionally differ.

## 6. An exactness trap in the manufactured tests — recorded before designing them

§11B asks for "spatially varying density with analytically compatible velocity", e.g. 1D
`rho(x) U(x) = const` so that `d(rho U)/dx = 0` analytically.

**The discrete operator does not reproduce that exactly.** With linear face interpolation,

```text
rho_f = mean(rho_P, rho_N)     U_f = mean(U_P, U_N)
rho_f U_f = mean(rho) mean(U)  !=  mean(rho U)  in general
```

so a cell's left and right face fluxes differ and the imbalance is **not** zero — it is
`O(h^2)`, the interpolation error. A test asserting exact zero there would fail a correct
implementation.

Two constructions are used instead, and the distinction is stated in the gate:

```text
EXACT      rho varying by ROW (y) with velocity along x only. A cell's left and right
           faces then lie in the same row and take the SAME rho_f, so their fluxes cancel
           bit-for-bit while rho genuinely varies. (The P17.1 construction.)
EXACT      the OPERATOR tested directly against a constructed divergence-free massFlux
           field -- which is what §11C authorizes: "If full transient solver integration
           is not yet available, test the discrete operator directly."
CONVERGENT rho(x) U(x) = const, verified as a REFINEMENT study: the imbalance must
           DECREASE at the expected rate under mesh refinement, not vanish.
```

## 7. Plan, in gate order

```text
1. dry-run against the UNCHANGED tree: measure the two evaluators' agreement, the
   rho*U=const refinement behaviour, boundary fluxes per BC type, MassBalance on a
   varying-density case, the transient integral relation computed BY HAND (proving the
   globalNetFlux gap is real), and what invalid states are currently accepted
2. freeze acceptance_gate.md with thresholds derived or inherited, never fitted
3. implement: globalNetFlux on the transient result + the density precondition
4. non-vacuity controls; each must fail for its intended reason; restore by sha256
5. focused tests: steady, transient, antisymmetry, boundaries, manufactured, conservation
6. G1 preservation incl. the P13-001 SIMPLE fingerprint bitwise
7. regression: Release+CUDA and CPU-only against the P17.2 baseline (2519 / 2319)
8. evidence + TODO.md
```

## 8. Explicitly NOT in P17.3

```text
giving the transient diagnostic a production caller   -- that is a transient compressible
                                                         SOLVER, which no P17 item has
                                                         authorized
the dilatational viscous stress term                  -> P17.4
two-way energy coupling / total energy                -> P17.5
HLLC, wave speeds, conserved state                    -> P17.7
a density-based solver, CFL control                   -> P17.8
modifying physics::evaluateContinuity                 -- shared with the incompressible
                                                         path; recorded as debt instead
a GPU continuity path                                 -- none exists
exposing pseudoTimeStep in the case format            -- standing debt
the 22 pre-existing clang-format failures             -- standing debt
```
