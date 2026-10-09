# P17.3 — the discrete continuity formulation

The exact equations implemented, the authoritative owner of each quantity, and the sign
convention. Consolidates what the authorization suggested as `formulation.md`,
`mass_flux_definition.md` and `boundary_fluxes.md`.

---

## 1. Sign convention — stated once, used everywhere

```text
A face's areaVector points OUTWARD FROM ITS OWNER and carries the normal AND the area:
    areaVector_f = n_f * A_f       (so no separate area factor is ever applied)

m_dot_f = rho_f * dot(U_f, areaVector_f)  =  rho_f (U_f . n_f) A_f

OUTWARD-POSITIVE, owner-oriented:
    +m_dot_f  contributes to the OWNER's outflow
    -m_dot_f  contributes to the NEIGHBOUR's outflow
    a boundary face contributes +m_dot_f to its owner only

Therefore, measured on the row-varying case:
    inlet  (left)   sum = -10    NEGATIVE  = mass entering
    outlet (right)  sum = +10    POSITIVE  = mass leaving
    walls  (top/bottom) sum = 0
```

Both continuity evaluators use this convention. `physics::evaluateContinuity` states it
in its own header; `evaluateCompressibleContinuity` states that it matches.

## 2. The steady form — what production actually solves

```text
sum over the faces of P of  m_dot_f  =  0        for every converged cell
```

This is enforced **not** by a separate continuity solve but by the **pressure-correction
equation**, whose own header says so:

```text
assembleCompressiblePressureCorrection
  D_f          = rho_f A_f d_f / dPN      using the SAME canonical rho_f
  diagonal    += V_P / pseudoTimeStep * dDensityDPressure(p_abs, T)
  "this equation IS the discretized compressible continuity constraint solved for
   pressure -- which is why no separate 'solve compressible continuity' step exists"
```

`pseudoTimeStep` is a numerical stabilisation parameter, **not** a physical time step
(`CompressibleSIMPLE.hpp:27`), and `densityOld == densityNew` at the momentum call site.
So the form in production is **steady**, and the compressibility term is a dual-time
relaxation, not time accuracy.

`physics::evaluateContinuity(mesh, fluxNew)` at `CompressibleSIMPLE.cpp:478` then reports
the residual and `|globalNetFlux|`.

### It is `div(rho U)`, not `rho div(U)`

The distinction matters and is verified, not asserted. The flux carries a **per-face**
density, so

```text
sum_f rho_f (U_f . n_f) A_f       NOT      rho_P * sum_f (U_f . n_f) A_f
```

Control **N4** collapses `rho_f` to a single constant — turning the operator into the
second form — and three tests catch it, including the `rho(x)U(x) = const` refinement
study whose convergence rate would change.

## 3. The transient form — a diagnostic, and only that

```text
cellImbalance[P] = (rho_P^{n+1} - rho_P^n) * V_P / dt  +  sum over P's faces of ±m_dot_f
```

`evaluateCompressibleContinuity(mesh, densityOld, densityNew, massFlux, dt)`.

```text
production callers   0   -- measured, not inferred
```

It evaluates the residual of an already-computed state; it does **not** solve for density.
P17.3 does not give it a production caller, because that would be a transient compressible
**solver** and no P17 item has authorized one. What P17.3 adds is the one field the
integral conservation statement needed:

```text
globalNetFlux = sum over BOUNDARY faces of m_dot_f
```

so that

```text
M(t2) - M(t1)  +  dt * globalNetFlux  ==  0
```

can be checked through the API that owns the sign convention, rather than by a caller
re-deriving it. `totalMassOld` and `totalMassNew` already supplied `M(t1)` and `M(t2)`.

**Boundary faces only.** Internal faces cancel pairwise and contribute nothing to a domain
balance, so a sum over all faces would be a number with no physical meaning — which is
exactly what control **N3** plants, and three tests reject it.

## 4. The authoritative mass flux — one owner

```text
OWNER   cfd::compressible::calculateCompressibleMassFlux
        m_dot_f = rho_f * dot(U_f, face.areaVector())

rho_f   evaluateCompressibleFaceDensity  -- ONE SurfaceField
          internal face   interpolateInternalFace of the authoritative cell density
          boundary face   the EOS at THAT FACE's own interpolated p_abs and T
        computed FRESH on every call; nothing caches it
U_f     discretization::interpolateFace(mesh, face, velocity, velocityBoundaries)
        -- polymorphic through the BC, so no per-BC special case here
rho     the P17.1 authoritative cell field, rho = EOS(p_abs, T), re-derived every outer
        iteration; P17.1 added the precondition that every value be finite and > 0
```

### The same `rho_f` reaches every consumer — structurally, not coincidentally

```text
calculateCompressibleMassFlux          computes the field
assembleCompressiblePressureCorrection is HANDED that exact field (not given the cell
                                       density to re-interpolate)
physics::evaluateContinuity            consumes the resulting massFlux
evaluateCompressibleContinuity         consumes the resulting massFlux
```

`CompressiblePressureCorrection.hpp` records why it is passed rather than recomputed: the
`D_f` coefficient and the predictor flux "must agree on the same per-face density", and two
independent evaluations could differ by rounding alone.

**No duplicated mass-flux definition exists.** Verified by grep as gate criterion I3 during
P17.2 and unchanged: 0 hits for `p/(R*T)`-style arithmetic outside the EOS classes, and one
mass-flux constructor.

## 5. Boundary mass fluxes, per type

The assembler has **no per-BC special case** — it calls `boundaryValue(...)`
polymorphically. What differs per type is the resulting face velocity.

```text
BC            face velocity            boundary flux              density role
---------------------------------------------------------------------------------------
Inlet         the prescribed value     rho_f (U_in . n) A < 0     FULL: sets the inflow
                                       (negative = entering)       mass rate
Outlet        zero-gradient, so the    rho_f (U_own . n) A > 0    FULL
              owner's velocity
Wall          no-slip, U_f = 0         EXACTLY 0                  NONE -- zero for any rho
MovingWall    tangential only          0 on an axis-aligned face  NONE
Symmetry      zero normal component    EXACTLY 0                  NONE
FixedValue    the prescribed value     rho_f (U . n) A            FULL

pressure BC   used for p_gauge at the face -> p_abs -> the EOS
              FixedValue, FixedGradient, Symmetry -- all supported polymorphically
temperature   FixedTemperature, HeatFlux, Adiabatic, ConvectiveHeatTransfer, Symmetry
BC            -- all supported, when thermal_coupled
```

Boundary density is **EOS-derived at that face's own interpolated state** (P12-COMP-001),
never a stale cell value. So the face state and the face density are thermodynamically
consistent by construction, and an unphysical state is rejected by the EOS rather than
returned.

```text
NOT PRESENT, and stated rather than silently skipped:
  mass-flow inlet   no such BC class exists in this codebase
  periodic          no Periodic BC class exists at all -- not a compressible gap
```

## 6. A summation-order asymmetry, measured

The two evaluators sum the same terms in **different loop orders**:

```text
physics::evaluateContinuity        per CELL, over cell.faceIds(), sign by ownership
evaluateCompressibleContinuity     per FACE, into owner (+) and neighbour (-)
```

Floating-point addition is not associative, so agreement is not guaranteed in general.
This is not a theoretical worry: `cuda/kernels/DevicePressureCorrectionKernel.cu:28` and
`:229` record that the GPU port deliberately gathers "in `cell.faceIds()` order" because
those are "the two orders the CPU actually uses". **The project already treats continuity
summation order as load-bearing.**

Measured (see `verification.md`): bitwise identical on the row-varying case, and agreeing
within a bounded `4 eps * max|m_dot|` on a deliberately irregular flux field where nothing
cancels by symmetry. `ReducesToTheSteadyEvaluatorWhenDensityIsUnchanged` gates it.

## 7. Validity — the three layers, unchanged in kind

```text
INPUT VALIDATION          sizes and dt (pre-existing); and NEW in P17.3, the VALUES:
                          densityOld, densityNew finite and > 0, naming the cell;
                          massFlux finite, naming the face.
                          massFlux is checked for FINITENESS ONLY -- a face flux is
                          legitimately negative (inflow) and legitimately zero (a wall),
                          so positivity would be the wrong rule, the same distinction
                          P17.1 drew for gauge pressure.
RUNTIME STATE VALIDATION  the EOS rejects an unphysical boundary state where it is
                          produced (P17.2), and P17.1 validates the cell density at the
                          mass-flux entry points.
NUMERICAL PROTECTION      CompressibleSIMPLE catches the EOS's InvalidArgumentError and
                          reports NonFiniteState, plus an allFinite() sweep.
PHYSICAL CLIPPING         DOES NOT EXIST, deliberately, at any layer.
```

### Why the new input validation is load-bearing rather than tidy

Measured on the pre-P17.3 tree:

```text
rhoNew[0] = NaN     -> maxCellImbalance 0      "perfectly conserved"
massFlux[0] = NaN   -> maxCellImbalance 0      "perfectly conserved"
rhoNew[0] = -2.0    -> totalMassNew 0          finite, plausible, catastrophic
```

`std::max(a, b)` is `(a < b) ? b : a`, so with a running maximum of `0` and `b = NaN` the
comparison `0 < NaN` is false and **the NaN is silently skipped**. The max reduction
therefore reports zero imbalance for a non-finite field, while `totalAbsoluteImbalance`
becomes `NaN` — the two metrics disagree, and a solver watching the max would declare
convergence on a NaN state.

This is the mirror of the P17.1 finding: there a finite-but-wrong value evaded
`allFinite()`; here a NaN evades the max-reduction. Both are cases where the *diagnostic*
reassures rather than warns, which is why validation belongs at the input.

## 8. Global conservation metric — the established one, reused

```text
physics::computeMassBalance(mesh, massFlux) -> MassBalance {
  inflow, outflow, net, relativeImbalance,
  maxCellImbalance, rmsCellImbalance, fluxScale, normalizedContinuity }
```

§8 of the authorization asks for a normalized imbalance and says to use the project's
established diagnostic if one exists. **It does, and P17.3 reuses it — no new metric was
invented.**
