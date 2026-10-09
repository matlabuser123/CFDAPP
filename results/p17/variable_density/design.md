# P17.1 — Variable Density: implementation decision

Written 2026-09-26, **before any code was changed**, from the completed P17.0 audit
(`results/p17/audit/`, `results/p17/baseline/`, `results/p17/gates/P17_GATES.md`).

---

## 1. What P17.0 established about density

Answers to the questions §1 of the authorization requires, each traced to a caller
rather than inferred:

```text
What density representation exists?   cfd::fields::ScalarField, cell-ordered, per cell.
                                      ProjectRunner.hpp:143 "EOS-evaluated, cell-ordered"
What class owns it?                   No single class OWNS it. ProjectRunner holds the
                                      field; CompressibleSIMPLE holds and iterates its
                                      own copy during a coupled solve and returns it in
                                      CompressibleSIMPLEResult::density.
What solver updates it?               CompressibleSIMPLE, once per outer iteration,
                                      CompressibleSIMPLE.cpp:455-456.
Iterated or derived?                  DERIVED, always: rho = EOS(p_abs, T). Iterated
                                      only in coupled mode. Never an independent
                                      unknown.
Where is EOS evaluation performed?    three places, all one EOS:
                                        evaluateDensityField        cells, ProjectRunner
                                        thermodynamics_.density     cells, per iteration
                                        evaluateCompressibleFaceDensity  boundary faces
How are face densities obtained?      evaluateCompressibleFaceDensity -- ONE canonical
                                      SurfaceField. Boundary faces: EOS at that face's
                                      own interpolated absolute p and T. Internal
                                      faces: interpolateInternalFace of the cell field.
                                      Computed fresh inside
                                      calculateCompressibleMassFlux, never cached.
How is density used in Compressible-  densityOld/densityNew into the relaxed momentum
SIMPLE?                               assembly; faceDensity into the pressure-
                                      correction D_f; plus dDensityDPressure in the
                                      compressibility diagonal.
Which paths support spatial variation? all compressible ones. The INCOMPRESSIBLE path
                                      does not and must not: FluidProperties::density()
                                      is a scalar Real.
What CPU tests exist?                 129 registered compressible tests, all passing at
                                      the P17.0 baseline.
What CUDA support exists?             NONE for compressible. One GPU-layer file mentions
                                      "compressible" and only to say it ports the
                                      INCOMPRESSIBLE assembly.
What limitations did P17.0 identify?  2D only; upwind only; no dilatational stress; no
                                      explicit density bounds; pseudoTimeStep has no
                                      case-format key; one-way thermal coupling.
```

## 2. Classification of every P17.1 requirement

```text
requirement                                    class                 action
---------------------------------------------------------------------------------------
Define authoritative density update path       ALREADY SATISFIED     DOCUMENT only
                                               (but undocumented)    -> authoritative_density_path.md
Support spatially varying rho                  ALREADY SATISFIED     VERIFY, nonuniform tests
Couple density to pressure / temperature       ALREADY SATISFIED     VERIFY both signs
Preserve constant-density limit                ALREADY SATISFIED     VERIFY by fingerprint
Define density bounds and invalid-state
  handling                                     NEEDS EXTENSION       the one code change
Verify density use -- continuity               ALREADY SATISFIED     VERIFY
Verify density use -- momentum                 ALREADY SATISFIED     VERIFY
Verify density use -- energy                   LIMITATION TO DOCUMENT  see §4
Verify density use -- mass flux                ALREADY SATISFIED     VERIFY
Verify density use -- boundary conditions      ALREADY SATISFIED     VERIFY
Unit tests                                     NEEDS EXTENSION       add negative +
                                                                     nonuniform coverage
Conservation tests                             NEEDS EXTENSION       add variable-density
                                                                     mass conservation
```

**Eleven of thirteen rows are verification or documentation.** Exactly one needs
production code. That is the P17.0 audit paying off: nothing below rewrites a working
capability.

## 3. The one gap that needs code — density values are never validated

Measured across every compressible module:

```text
module                             validates dt/pseudoTimeStep   validates density VALUES
------------------------------------------------------------------------------------------
CompressibleMassFlux.cpp                    n/a                            NO
CompressibleRelaxedMomentum.cpp             n/a                            NO
CompressiblePressureCorrection.cpp          YES (pseudoTimeStep)            NO
CompressibleMomentum.cpp                    YES (dt)                       NO
CompressibleContinuity.cpp                  YES (dt)                       NO
```

`validateFaceDensityInputs` in `CompressibleMassFlux.cpp` checks **sizes only**:

```cpp
if (density.size() != mesh.numberOfCells()) throw ...;   // size
// ... no check that any density VALUE is finite or positive
```

and the internal-face branch is a plain weighted mean with no validation:

```cpp
faceDensity[faceId] = cfd::discretization::interpolateInternalFace(mesh, face, density);
```

So a non-positive or non-finite **cell** density propagates silently into
`faceDensity` and then into `massFlux`. The boundary branch is incidentally protected
because it routes through `thermodynamics.density(...)`, which validates — the
internal branch is not.

### Why this matters, stated honestly

Production is **currently safe**: every density field reaching these functions is
EOS-evaluated, hence positive and finite by construction. So this is not a live bug.

It matters because:

```text
1. The CONTRACT does not say it. Nothing in the header requires a positive density,
   and nothing enforces it, so the safety is an accident of the present callers.
2. P17.1 is explicitly asked to "Define density bounds and invalid-state handling"
   and to "Add negative tests". There are none: test_compressible_mass_flux.cpp has
   12 tests, four are SIZE mismatches, one is a non-positive boundary TEMPERATURE,
   and ZERO feed a bad density VALUE.
3. P17.8's density-based solver will evolve rho as a conserved variable, which CAN
   go non-positive. That is exactly the path that would silently corrupt the mass
   flux, and P17.8 is where positivity protection is required.
```

### Policy — explicit failure, no clamping

The project already has a documented policy for this class of problem, and P17.1
follows it rather than inventing one. `thermal/Enthalpy.hpp`:

> An enthalpy outside `[h(Tmin), h(Tmax)]` is an **ERROR, not a clamp**. Clamping
> would return a temperature the energy does not correspond to and let a diverging
> solve look converged.

The same reasoning applies verbatim to density. Clamping a non-positive density would
produce a mass flux that no state corresponds to, and let a diverging compressible
solve look plausible.

Distinguishing the three kinds the authorization asks to keep separate:

```text
input validation    P17.1 SCOPE. Reject a non-finite or non-positive density VALUE at
                    the compressible entry points, naming the offending cell. Throws
                    InvalidArgumentError, matching the EOS's existing style and
                    evaluateDensityField's existing cell-index context.
solver protection   NOT P17.1. CompressibleSIMPLE already has it: it catches the EOS's
                    InvalidArgumentError and reports NonFiniteState with a diagnostic
                    detail, plus an allFinite() sweep. Unchanged.
physical clipping   NOT DONE, and deliberately not added. That would be a positivity
                    LIMITER, which belongs to P17.8 where a conserved-state update can
                    legitimately need one. P17.1 must not pre-empt that design.
```

### Scope of the change — minimal

```text
extend  validateFaceDensityInputs in src/compressible/CompressibleMassFlux.cpp
        to validate density VALUES as well as sizes
why     it is the single chokepoint: calculateCompressibleMassFlux calls
        evaluateCompressibleFaceDensity, and BOTH call this one validator. Validating
        here covers the canonical mass-flux and face-density path -- the exact path
        every equation consumer ultimately reads -- with one change instead of five.
not     the momentum / pressure-correction / continuity modules. They consume the
        SAME density field and the SAME faceDensity that this path validates, and
        adding four more validators would duplicate the check without adding coverage.
        Recorded as a deliberate boundary, not an oversight.
```

## 4. The energy row — a limitation to document, not to change

P17.0 found the compressible thermal coupling is one-way. P17.1 traced the **ordering**
that causes it, which is sharper:

```text
ProjectRunner.cpp:531-537   massFlux = physics::calculateMassFlux(mesh, result.velocity,
                                                                 setup.fluid, ...)
                            setup.fluid is FluidProperties -- a SCALAR constant density
ProjectRunner.cpp:544       the THERMAL solve consumes *massFlux
ProjectRunner.cpp:756       the COMPRESSIBLE block runs AFTERWARDS and builds its own
                            compressibleMassFlux with variable density
```

So in a compressible run there are **two mass-flux fields**, and the energy, species
and multiphase solves all consume the constant-density one.

```text
Is this an inconsistency P17.1 must fix?   NO.
```

It is the deliberate one-way coupling design, and the authorization is explicit: *"If
P15 is intentionally constant-density in a particular path, document that rather than
silently changing its physics."* Making energy consume the compressible mass flux would
be **two-way energy coupling — that is P17.5**, and doing it here would be
unauthorized scope.

It is also not an inconsistency *within* the compressible solve: there, one canonical
face density feeds both the predictor flux and the pressure-correction coefficient.

P15's own library layer is already variable-density capable —
`ThermalTimeState` carries `densityOld` and `densityNew` as `ScalarField*` — but that
transient path has **no production caller**; `ThermalSolver` runs steady, where
`ThermalSolver.hpp:201-207` records that rho enters only through the mass flux.

Recorded in `boundary_density.md` and `integration_matrix.md` as
`CONSTANT-DENSITY BY DESIGN`, not as verified variable-density use. It will be reported
as such — **not** as a passed row.

## 5. Ownership and update sequence, as it actually is

The authorization's suggested pattern, adapted to the real architecture rather than
imposed on it:

```text
state variables            pressure (gauge) + temperature
        |                  temperature is an INPUT: a configured constant, or a
        |                  separately converged thermal field. Never an unknown here.
        v
thermodynamic closure      ThermodynamicProperties -> IdealGasEOS
        |                  p_abs = referencePressure + p_gauge      (the single bridge)
        |                  rho   = p_abs / (R T)
        v
authoritative cell rho     ScalarField, per cell. Re-derived EVERY outer iteration in
        |                  a coupled solve; there is no incremental update and no
        |                  stale copy to reconcile.
        v
face / boundary rho        evaluateCompressibleFaceDensity -- ONE SurfaceField
        |                    boundary face: EOS at that face's own interpolated p_abs, T
        |                    internal face: interpolateInternalFace of the cell field
        v
mass flux                  m_dot_f = rho_f * dot(U_f, areaVector_f)
        |                  areaVector carries normal AND area, so this is
        |                  rho_f (U_f . n_f) A_f exactly
        v
consumers                  continuity  physics::evaluateContinuity(mesh, massFlux)
                           momentum    densityOld/densityNew + massFlux convection
                           pressure    faceDensity in D_f + dDensityDPressure diagonal
                           energy      NOT this flux -- see §4
```

**Why this already avoids the competing-mechanism failure the authorization warns
about:** density is re-derived from `(p_abs, T)` every iteration rather than updated
incrementally, and the face field is recomputed from the current cell field on every
call. There is no second density store that can drift, and no cached face value that
can go stale. The invariant is structural, not maintained by discipline.

The one thing that was missing is a **statement** of that invariant plus enforcement
of its precondition — which is §3's change and this document.

## 6. Plan, in gate order

```text
1. dry-run the gate against the UNCHANGED tree, and PROVE the §3 gap empirically:
   show that a negative, zero and NaN cell density currently produce a silent,
   non-physical mass flux rather than an error                       [before any edit]
2. freeze acceptance_gate.md with thresholds derived, not fitted
3. implement the §3 change -- smallest correct edit
4. non-vacuity controls: each must fail for its intended reason, restore verified by
   sha256
5. focused tests: spatial variation, p/T coupling both signs, invalid states,
   mass flux, consumers
6. conservation: measured integrated mass imbalance, variable-density AND
   constant-density, with a control that would fail if density were internally constant
7. G1 preservation incl. the P13-001 SIMPLE fingerprint bitwise
8. regression: Release+CUDA and CPU-only, against the P17.0 baseline counts
9. evidence + TODO.md
```

## 7. Explicitly NOT in P17.1

```text
the dilatational stress term -(2/3) mu grad(div U)        -> P17.4
two-way energy coupling / total energy                    -> P17.5
a positivity LIMITER on a conserved state                 -> P17.8
EOS expansion beyond the existing interface               -> P17.2 (already closed)
lifting the 2D restriction, or higher-order convection    -> later
a compressible CUDA path                                  -> outside P17 scope
exposing pseudoTimeStep in the case format                -> recorded debt, not P17.1
reconciling the dual-cp hazard                            -> decide before P17.5
```
