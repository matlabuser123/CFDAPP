# P17.1 — the authoritative density path

The answer to *"who owns density, and when and how may it change?"*, traced to code
rather than described. Line numbers are against the P17.1 tree.

---

## 1. The chain, end to end

```text
STATE VARIABLES
  pressure (GAUGE)      CompressibleSIMPLE solves it; SIMPLE's own convention
  temperature           an INPUT, never an unknown of the compressible system:
                          isothermal        a configured constant in every cell
                          thermal_coupled   a read-only copy of a separately
                                            converged thermal field
                                            (SimulationSetup.hpp:59)
        |
        v
THERMODYNAMIC CLOSURE
  p_abs = referencePressure + p_gauge        the SINGLE documented gauge->absolute
                                            bridge (ProjectRunner.hpp:144,
                                            PhysicsConfig.hpp:179,
                                            CompressibleSIMPLE.hpp:150)
  rho   = ThermodynamicProperties::density(p_abs, T)
        -> IdealGasEOS::density = p_abs / (R T)
           validates p_abs, T AND the resulting rho: finite and > 0, else
           InvalidArgumentError naming the field
        |
        v
AUTHORITATIVE CELL DENSITY          cfd::fields::ScalarField, one value per cell
  coupled: true    re-derived EVERY outer iteration from the just-corrected pressure
                   CompressibleSIMPLE.cpp:455-456
  coupled: false   derived ONCE, after the incompressible solve converged
                   ProjectRunner.cpp:799 via evaluateDensityField
                   (which wraps EOS failures with the offending CELL INDEX)
        |
        v
FACE AND BOUNDARY DENSITY           ONE SurfaceField, evaluateCompressibleFaceDensity
  internal face    interpolateInternalFace(mesh, face, density)
                   -- the distance-weighted mean of the two owning cells
  boundary face    the EOS at THAT FACE's own interpolated p_abs and T
                   (P12-COMP-001; supersedes the earlier owner-cell reuse)
                   isothermal mode: the owner cell's own T is the boundary T, since
                   the field is uniform -- not a simplification
  P17.1 PRECONDITION: every density[i] must be finite and > 0, enforced here, in
                      the ONE validator both public entry points call
        |
        v
MASS FLUX                           m_dot_f = rho_f * dot(U_f, Sf)
  Sf = face.areaVector() carries the normal AND the area, so this is exactly
  rho_f (U_f . n_f) A_f
  computed FRESH inside calculateCompressibleMassFlux on every call
        |
        v
EQUATION CONSUMERS
  continuity   physics::evaluateContinuity(mesh, massFlux)     CompressibleSIMPLE.cpp:478
               -> cellImbalance, globalNetFlux, totalAbsoluteImbalance
  momentum     assembleRelaxedCompressibleMomentumComponent    CompressibleSIMPLE.cpp:265
               densityOld / densityNew in the transient-storage term;
               massFlux in the convective term
  pressure     assembleCompressiblePressureCorrection          CompressibleSIMPLE.cpp:357
               the SAME faceDensity in D_f, plus
               dDensityDPressure(p_abs,T) in the compressibility diagonal
  energy       NOT this flux -- see section 4
```

## 2. Why no second density can drift out of step

The invariant is **structural**, not maintained by discipline. Three properties do it:

```text
1. RE-DERIVED, NEVER INCREMENTED
   The cell density is recomputed from (p_abs, T) every outer iteration. There is no
   incremental update, so there is no accumulated state to reconcile and no way for a
   density to be "left behind" by a pressure change.

2. ONE FACE FIELD, PASSED NOT RECOMPUTED
   evaluateCompressibleFaceDensity produces a single SurfaceField, and the
   pressure-correction equation is HANDED that exact field rather than recomputing it.
   CompressiblePressureCorrection.hpp states the reason: the D_f coefficient and the
   predictor mass flux must agree on the same per-face density. Two independent
   evaluations could differ by rounding alone.

3. RECOMPUTED PER CALL, NEVER CACHED
   The face field is rebuilt from the current cell field on every
   calculateCompressibleMassFlux call. Nothing caches it, so a face value cannot go
   stale behind a changed cell value.
```

Each is now covered by a test rather than only by argument:

```text
property 2   InternalFaceUsesArithmeticMeanDensity,
             BoundaryDensityMatchesDirectEosEvaluation
property 3   FaceDensityIsRecomputedPerCallNotCached          (P17.1, new)
precondition RejectsNegativeCellDensity, RejectsZeroCellDensity,
             RejectsNonFiniteCellDensity,
             FaceDensityEvaluationRejectsInvalidCellDensity   (P17.1, new)
```

## 3. The four density semantics, kept distinct

```text
1. CONSTANT-DENSITY / INCOMPRESSIBLE
   owner      physics::FluidProperties -- a scalar Real, from physics.json's top-level
              "density"
   changes    never
   isolation  STRUCTURAL: src/pressure_velocity/ and include/cfd/pressure_velocity/
              contain ZERO references to EquationOfState, IdealGasEOS or
              ThermodynamicProperties. Incompressible SIMPLE therefore CANNOT become
              EOS-dependent -- it is checkable by grep, and gate criterion C2 checks it.

2. EOS-DERIVED, POST-HOC        physics.json compressible.coupled absent or false
   owner      ProjectRunner
   changes    once, after the incompressible solve converged
   status     a REINTERPRETATION of an already-converged incompressible result. The
              velocity and pressure fields are the incompressible solution, unchanged.
              This is the DEFAULT mode, and in it density influences nothing.

3. ITERATED COMPRESSIBLE        compressible.coupled: true
   owner      CompressibleSIMPLE, which returns it in CompressibleSIMPLEResult::density
   changes    every outer iteration, from the just-corrected pressure
   coupling    two-way with pressure, through the EOS and the pressure-correction
              compressibility term. THE authoritative path for P17.1.

4. FUTURE CONSERVED-STATE       not implemented
   would own  a density-based solver, where rho is the FIRST CONSERVED UNKNOWN rather
              than an EOS evaluation
   note       This is the one semantics that inverts the chain above: rho would be
              evolved by the flux and the EOS would be used to recover p, not to
              produce rho. P17.1 deliberately does not pre-empt it -- which is why no
              positivity LIMITER was added here. See design.md section 3.
```

## 4. Where the chain does NOT reach: the energy path

Traced by **ordering** in `ProjectRunner.cpp`, which is sharper than "one-way
coupling":

```text
:531-537   massFlux = physics::calculateMassFlux(mesh, result.velocity, setup.fluid, ...)
           setup.fluid is FluidProperties -- a SCALAR CONSTANT density
:544       the THERMAL solve consumes *massFlux
:677       species consume the same *massFlux
:717       multiphase consumes the same *massFlux
:756       the COMPRESSIBLE block runs AFTERWARDS and builds its own
           compressibleMassFlux with variable density
```

So a compressible run carries **two mass-flux fields**, and energy, species and
multiphase all consume the constant-density one.

```text
Is this an inconsistency P17.1 should fix?   NO.
```

It is the deliberate one-way design. Making energy consume the compressible flux is
**two-way energy coupling — P17.5** — and doing it inside P17.1 would be unauthorized
scope and a silent physics change. The authorization is explicit that an intentionally
constant-density path should be **documented**, not quietly altered.

It is also not an inconsistency *within* the compressible solve, where one canonical
face density feeds both the predictor flux and the pressure-correction coefficient.

Worth recording for P17.5: P15's library layer is **already variable-density capable**
— `ThermalTimeState` carries `densityOld` and `densityNew` as `ScalarField*` for the
conservative `rho cp dT/dt` term — but that transient path has **no production caller**.
`ThermalSolver` runs steady, where `ThermalSolver.hpp:201-207` records that rho enters
only through the mass flux. So P17.5 has a foundation to build on, not a rewrite.

## 5. Invalid-state policy — the three kinds, kept separate

```text
INPUT VALIDATION        P17.1 scope, and what this item added.
                        IdealGasEOS: p, T and the resulting rho must be finite and > 0.
                        evaluateDensityField: wraps an EOS failure with the CELL INDEX.
                        validateFaceDensityInputs (NEW): every density[i] finite and
                        > 0, naming the offending cell.
                        Throws InvalidArgumentError. Never clamps.

SOLVER PROTECTION       Already present, unchanged by P17.1.
                        CompressibleSIMPLE catches the EOS InvalidArgumentError and
                        reports NonFiniteState with a diagnostic detail string, plus an
                        allFinite() sweep over velocity, pressure, density and flux.
                        NOTE its limit, measured: allFinite() CANNOT detect the finite
                        invalid cases (rho = -2 gives face rho_f = 0; rho = 0 gives a
                        plausible flux). That limit is exactly why input validation is
                        the right layer.

PHYSICAL CLIPPING       NOT PRESENT, and deliberately NOT ADDED.
                        A clamped density yields a mass flux no thermodynamic state
                        corresponds to, and lets a diverging solve look plausible --
                        the reasoning thermal/Enthalpy.hpp already records for an
                        out-of-range enthalpy ("an ERROR, not a clamp"). A positivity
                        limiter is a conserved-state concern and belongs with a
                        density-based solver (P17.8).
```

## 6. Bounds, stated explicitly

```text
quantity        requirement                     enforced where
-------------------------------------------------------------------------------
rho (cell)      finite and > 0                  IdealGasEOS::density result check;
                                                evaluateDensityField per cell;
                                                validateFaceDensityInputs (P17.1)
rho (face)      finite and > 0                  follows from the cell precondition:
                                                a distance-weighted mean of positive
                                                finite values is positive and finite,
                                                and boundary faces go through the EOS
p_abs           finite and > 0                  IdealGasEOS::density input check
p_gauge         finite only -- MAY BE NEGATIVE  legitimately below the reference
                                                pressure; positivity would be the
                                                WRONG rule and is not applied
T               finite and > 0                  IdealGasEOS; ThermodynamicProperties::
                                                speedOfSound
R               finite and > 0                  IdealGasEOS constructor
```

**No upper bound is imposed on rho.** For an ideal gas the physical domain is
positivity, and an arbitrary ceiling would reject valid high-pressure states. P17.0
recorded the absence of a configured validity BAND (unlike P15's
`TemperatureDependentProperty`, which carries `[Tmin, Tmax]`); that remains a genuine
difference and is the right thing to revisit if a caloric or real-gas model is ever
added, because the abstraction currently has nowhere to put such a band.
