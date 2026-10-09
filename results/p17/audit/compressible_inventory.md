# P17.0 — compressible architecture inventory

Measured 2026-09-26 against the frozen P17 baseline. Every row was traced to a
**caller**, not inferred from a symbol existing. Where a symbol exists but nothing in
`src/app` reaches it, the row says so.

The earlier, shorter Phase-0 note at `results/p17/compressible/audit/AUDIT.md` is
preserved unchanged; this document extends it, and §7 records where they disagree.

---

## 1. Files

```text
include/cfd/compressible/            src/compressible/
  CompressibleContinuity.hpp           CompressibleContinuity.cpp
  CompressibleMassFlux.hpp             CompressibleMassFlux.cpp
  CompressibleMomentum.hpp             CompressibleMomentum.cpp
  CompressiblePressureCorrection.hpp   CompressiblePressureCorrection.cpp
  CompressibleRelaxedMomentum.hpp      CompressibleRelaxedMomentum.cpp
  CompressibleSIMPLE.hpp               CompressibleSIMPLE.cpp
  EquationOfState.hpp                  IdealGasEOS.cpp
  IdealGasEOS.hpp                      ThermodynamicProperties.cpp
  ThermodynamicProperties.hpp
                                     9 headers, 8 sources
```

## 2. The production path, traced end to end

```text
physics.json "compressible" { gas_constant, specific_heat_pressure,
                              reference_pressure,
                              temperature | thermal_coupled, coupled }
  -> PhysicsConfigParser   cross-checks: exactly one of temperature/thermalCoupled;
                           thermal must be enabled when thermalCoupled is requested
  -> CompressiblePhysicsConfig               io/case/PhysicsConfig.hpp
  -> SimulationSetup.compressible            std::optional
  -> ProjectRunner.cpp:756   if (setup.compressible.has_value())
       |
       |  ALWAYS FIRST: incompressible SIMPLE runs and converges  -> result
       |  gate: !anyNonFinite(result) && (!thermalCoupled || thermalResult.has_value())
       |
       +-- coupled == false    POST-HOC REINTERPRETATION
       |     pressureAbsolute = referencePressure + result.pressure
       |     density  = evaluateDensityField(...)      EOS, once, after the fact
       |     mach     = machNumber(|U|, speedOfSound(T))
       |     massFlux = calculateCompressibleMassFlux(...)
       |     statusName = "Evaluated"
       |     NOT a compressible flow solve: velocity and pressure remain the
       |     incompressible solution, unchanged.
       |
       +-- coupled == true     CompressibleSIMPLE, warm-started from that result
             initialDensity = EOS at the warm start's own pressure
             solve() -> velocity, pressure (gauge), density, massFlux, status
             out.status = statusFor(coupled.status)    becomes the run's status
       |
       -> physics::evaluateContinuity(mesh, compressibleMassFlux)   STEADY balance
       -> CompressibleRunMetadata { R, cp, referencePressure, thermalCoupled,
                                    coupled, statusName, machMax, |globalNetFlux| }
       -> export: "density" plus the other compressible fields
```

Temperature is **never solved with** the compressible equations. It is either a
constant (`*c.temperature`) or a read-only copy of a separately converged thermal
field. `SimulationSetup.hpp:59` states this. The coupling is one-way by construction.

## 3. Inventory table

```text
symbol                                  file                        production caller       CPU GPU tests
---------------------------------------------------------------------------------------------------------
CompressibleSIMPLE::solve               CompressibleSIMPLE.cpp      ProjectRunner.cpp:824   YES no   28
validateCompressibleSIMPLESettings      CompressibleSIMPLE.cpp      self                    YES no  in 28
assembleRelaxedCompressibleMomentum-    CompressibleRelaxedMomentum CompressibleSIMPLE:265  YES no    5
  Component
assembleCompressiblePressureCorrection  CompressiblePressureCorr    CompressibleSIMPLE:357  YES no    6
calculateCompressibleMassFlux           CompressibleMassFlux.cpp    CompressibleSIMPLE:205  YES no   12
                                                                    ProjectRunner.cpp       YES
evaluateCompressibleFaceDensity         CompressibleMassFlux.cpp    CompressibleSIMPLE:324  YES no  in 12
evaluateDensityField                    ThermodynamicProperties.cpp ProjectRunner.cpp x2    YES no    7
ThermodynamicProperties R,cp,cv,gamma,a ThermodynamicProperties.cpp ProjectRunner, SIMPLE   YES no   13
machNumber                              ThermodynamicProperties.cpp ProjectRunner.cpp       YES no  in 13
IdealGasEOS::density and derivatives    IdealGasEOS.cpp             via ThermodynamicProps  YES no   21
IdealGasEOS::pressure   (P17.2, new)    IdealGasEOS.cpp             NONE YET                YES no  in 21
assembleCompressibleMomentumComponent   CompressibleMomentum.cpp    NONE -- tests only      YES no    7
compressibleMomentumTimeDerivative      CompressibleMomentum.cpp    via RelaxedMomentum     YES no  in 7
evaluateCompressibleContinuity          CompressibleContinuity.cpp  NONE -- tests only      YES no    6
```

### The three rows that matter most

**`evaluateCompressibleContinuity` has no production caller.** Its own header calls it
a *diagnostic*: it evaluates the residual of
`(rho^{n+1} - rho^n) V/dt + sum(mDot_f)` from an already-computed mass flux. It is
reached only from its own `.cpp`, 6 unit tests, and one line of
`test_low_mach_regression.cpp`. `CompressibleSIMPLE.cpp` includes
`physics/ContinuityEquation.hpp` and calls the **steady** `evaluateContinuity(mesh,
fluxNew)` instead, at `CompressibleSIMPLE.cpp:478`.

**`assembleCompressibleMomentumComponent` has no production caller either**, and its
sibling header explains why: it calls `.build()` internally, so it has no relaxation
hook and cannot be used inside a relaxed outer loop.
`assembleRelaxedCompressibleMomentumComponent` exists for exactly that reason and is
what production uses.

**`IdealGasEOS::pressure` — P17.2's own addition — has no caller yet.** That is
expected and correct at this point: it was added for the energy equation and Riemann
flux still to come. It is recorded here so that no later P17 item mistakes it for
integrated capability.

None of the three is dead code by mistake. All are deliberate, documented library
entry points. But none is production capability, and P17 must not cite them as such.

## 4. Density — where it lives, and what is authoritative

```text
cell density       fields::ScalarField, cell-ordered
                   ProjectRunner.hpp:143  "EOS-evaluated, cell-ordered"
face density       evaluateCompressibleFaceDensity(mesh, density, pressureGauge,
                     pressureBoundaries, referencePressure, thermodynamics,
                     temperature, temperatureBoundaries)
                   ONE canonical face field, shared by the predictor mass flux AND
                   the pressure-correction D_f coefficient -- passed in, never
                   recomputed. Boundary faces get a real EOS density at their own
                   boundary p and T (P12-COMP-001), not owner-cell reuse.
authoritative?     NO. Density is DERIVED, always:  rho = EOS(p_abs, T).
                   It is never an independently evolved unknown.
when updated       coupled=true  : every outer iteration, from the just-corrected
                                   pressure (CompressibleSIMPLE.cpp:455)
                   coupled=false : once, after the incompressible solve converged
iteratively        coupled=true  : YES -- pressure and density are coupled through
  coupled?                         the EOS and the pressure-correction
                                   compressibility term
                   coupled=false : NO
consumers          CompressibleContinuity (diagnostic only), CompressibleMassFlux,
                   CompressibleRelaxedMomentum (densityOld / densityNew),
                   CompressiblePressureCorrection (faceDensity + dDensityDPressure)
```

## 5. What each equation actually is

### Continuity — the pressure-correction equation IS the constraint

`CompressiblePressureCorrection.hpp` states it outright. The `D_f` face coefficient
uses the canonical face density, and a new diagonal term
`V_P/pseudoTimeStep * dDensityDPressure(p_abs, T)` makes that equation *the
discretized compressible continuity constraint solved for pressure* — its own words,
followed by "see why no separate solve compressible continuity step exists or is
needed".

```text
form in production      STEADY   div(rho U) = 0, enforced through pressure
transient d(rho)/dt     NOT in production. pseudoTimeStep is a numerical
                        stabilization parameter, not a physical time step --
                        CompressibleSIMPLE.hpp:27 says so explicitly, and
                        densityOld == densityNew in the momentum call.
face mass flux          m_dot_f = rho_f (U_f . n_f) A_f, one authoritative path
global mass balance     physics::evaluateContinuity -> |globalNetFlux| in metadata
```

### Momentum — conservative in convection and pressure, INCOMPLETE in viscous stress

```text
transient storage  (rho_new u_new - rho_old u_old) V/dt, split diagonal/source.
                   Correctly keeps rho_old with u_old; reduces exactly to
                   implicitEulerTimeDerivative for constant density, proven by
                   ConstantDensityMatchesIncompressibleFormula.
convection         physics::assembleConvectionContribution fed the COMPRESSIBLE mass
                   flux -- flux-based and telescoping, therefore conservative.
                   UPWIND ONLY, first order. No higher-order scheme exists.
pressure           the pressure-source path of the shared momentum assembler
viscous            physics::assembleDiffusionContribution -- the INCOMPRESSIBLE
                   Laplacian mu lap U
```

**The dilatational stress term is absent.** Measured, not assumed:

```text
grad(div      0 hits      dilatation     0 hits      bulkViscosity  0 hits
twoThirds     0 hits      Stokes         0 hits      divU           0 hits
lambda        2 hits -- both are C++ lambdas in ThermalSolver.hpp
```

`tests/integration/mms/test_mms_compressible.cpp` names it in its own
machine-readable `not_covered` field:

```text
"energy equation, mu/3 grad(div U) stress term, higher-order convection, high Mach"
```

For incompressible flow `div U = 0` and the term vanishes, so this was harmless
through P16. **Compressible flow is exactly the regime where `div U != 0`**, so what
is implemented is not the compressible viscous stress. This is a physics gap, not a
cosmetic one, and it is the most consequential finding in this inventory.

### Energy — absent

```text
totalEnergy / TotalEnergy / rhoE / rho_e / internalEnergy    0 hits, whole tree
pressureWork / viscousDissipation / viscousWork              0 hits, whole tree
```

P15 provides a **steady temperature-transport** equation
(`thermal/EnergyEquation.hpp`, `EnergyAssembly`), not a conservative total-energy
equation. The two are not equivalent and are not treated as such here.

### Riemann and shock infrastructure — absent

```text
HLLC 0    HLL 0    Rusanov 0    LaxFriedrichs 0    Sod 0    shock 0
normalShock 0      supersonic 0    transonic 0     rarefaction 0    choked 0
conservedVariable 0    primitiveVariable 0    nozzle 0
Riemann   1 hit  -- a FORWARD-LOOKING COMMENT at EquationOfState.hpp:51 that P17.2
                    itself wrote: "the energy equation and the Riemann flux to come"
Roe      14 hits -- ALL substring matches inside "zeroes" / "zeroed" / "Zero".
                    Not one is the Roe flux.
Euler   107 hits -- ALL implicitEulerTimeDerivative / aleImplicitEuler /
                    ImplicitEuler. TIME INTEGRATION, never the Euler equations.
```

The `Roe` and `Euler` counts are recorded deliberately. A bare symbol census would
have reported "Roe: 14, Euler: 107" and concluded that a Riemann solver and an Euler
flux already exist. Neither does.

## 6. Limitations found that no checklist item asked about

```text
2D ONLY         CompressibleSIMPLE.cpp:147
                requireTwoDimensional(mesh, "CompressibleSIMPLE") -- a 3D mesh is
                refused explicitly (P12-MESH-006). P17 shock work inherits this
                restriction unless it is lifted.
UPWIND ONLY     first-order convection. The MMS order gate is 1.0, band [0.55, 1.3].
NO GPU          the entire compressible module is CPU-only. Exactly one file in the
                GPU layer mentions "compressible" -- DevicePressureCorrection.hpp,
                and only to say it ports the INCOMPRESSIBLE assembly.
pseudoTimeStep  has NO case-format key. ProjectRunner.cpp: "left at its own default
                (1.0) -- no case-format exposure for it in this phase". A production
                case cannot tune the compressible solver's only stabilization
                parameter.
NO BULK VISC.   see the viscous row in §5
ONE-WAY THERMAL temperature is an input, never an unknown
```

## 7. Where this differs from the earlier AUDIT.md

The earlier note recorded *"Existing compressible test coverage: 90 tests across 13
files."* That figure does not reproduce. Measured directly (see `test_inventory.md`):
**15 files, 124 test cases**, of which 9 are P17.2's — so 115 before P17.2. Per
`CLAUDE.md` §4, exact counts come from the actual run and an earlier aggregate is
never reused, so the measured table supersedes it. The earlier conclusions are
unaffected; the count was descriptive, not load-bearing.

The earlier note also classified compressible continuity and momentum as flatly
`YES / EXISTS`. This inventory refines both: continuity exists as a **steady**
constraint plus a **non-production diagnostic**, and momentum exists with an
**incomplete viscous stress**. Those two refinements change P17's real scope, which
is why re-auditing at depth was worth the time.
