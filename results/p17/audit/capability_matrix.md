# P17.0 — authoritative P17 capability matrix

Measured 2026-09-26 against the frozen P17 baseline. Classification is **strict** and
follows the authorization's own rules:

```text
class exists but unused by production           -> PARTIAL
equation exists but no validation               -> PARTIAL
CPU implementation only                         -> PARTIAL for a CPU/GPU capability
temperature equation only                       -> MISSING total-energy equation
unit test only, no production integration       -> PARTIAL
no repository evidence                          -> MISSING
```

Evidence for every row is in `compressible_inventory.md` (callers, forms, limits) and
`test_inventory.md` (counts, criteria). Nothing here is classified from a symbol
census alone.

---

## Summary

```text
capability                           status
---------------------------------------------------
Variable density                     IMPLEMENTED
Ideal-gas EOS                        IMPLEMENTED
Compressible continuity              PARTIAL
Compressible momentum                PARTIAL
Total-energy equation                MISSING
Low-Mach validation                  IMPLEMENTED
HLLC / Riemann flux                  MISSING
Density-based compressible solver    MISSING
Shock validation                     MISSING
CPU compressible support             IMPLEMENTED
GPU compressible support             MISSING
CPU/GPU compressible equivalence     MISSING
---------------------------------------------------
IMPLEMENTED 4    PARTIAL 2    MISSING 6
```

The headline: **the low-Mach steady 2D regime is genuinely built and validated; the
higher-Mach shock-capturing regime does not exist at all.** P17's real weight is in
P17.5, P17.7, P17.8 and P17.9.

---

## Variable density — IMPLEMENTED

```text
files        ThermodynamicProperties.{hpp,cpp} (evaluateDensityField),
             CompressibleMassFlux.{hpp,cpp} (evaluateCompressibleFaceDensity)
classes      fields::ScalarField density; one canonical SurfaceField face density
production   YES. ProjectRunner.cpp:799 (initial), CompressibleSIMPLE.cpp:455 (per
             outer iteration). Reachable from physics.json via
             "compressible": {"coupled": true}; cases/compressible_channel_coupled
             exercises it.
tests        7 evaluateDensityField + 12 mass flux + the 7 low-Mach cases
CPU / GPU    YES / no
evidence     CompressibleSIMPLE.cpp:450-489; low-Mach deltaRhoOverRho < 1e-2 gate;
             MMS manufactures rho in [0.95, 1.08] with div(rho U) = 0 exactly
gap          Density is DERIVED, never authoritative: rho = EOS(p_abs, T) always. It
             is not an independently evolved conserved variable, which is precisely
             what a density-based shock solver needs. Also: in the DEFAULT mode
             (coupled: false) density is a post-hoc reinterpretation and does not
             influence the flow solution at all.
```

Classified IMPLEMENTED rather than PARTIAL because spatially varying density is
genuinely production-reachable, iteratively coupled and validated in the regime it
claims. The "derived, not authoritative" property is a **design fact** that P17.8
must work around, not a defect in this capability.

## Ideal-gas EOS — IMPLEMENTED

```text
files        EquationOfState.hpp (abstraction), IdealGasEOS.{hpp,cpp},
             ThermodynamicProperties.{hpp,cpp}
API          density(p,T), dDensityDPressure, dDensityDTemperature, pressure(rho,T)
             R, cp, cv = cp - R, gamma = cp/cv, speedOfSound(T), machNumber(U,a)
production   YES for density/derivatives/R/cp/cv/gamma/a/Mach.
             pressure(rho,T) is P17.2's addition and HAS NO CALLER YET.
tests        21 IdealGasEOSTest + 7 ThermodynamicPropertiesTest
CPU / GPU    YES / no
relations    p = rho R T        verified, <= 1e-12 relative, both round trips (P17.2)
             cp - cv = R        holds BY CONSTRUCTION -- cv is derived, never stored
                                independently, so it cannot be violated
             gamma = cp/cv      same, by construction
             a = sqrt(gamma R T) implemented and tested (7 cases)
invalid state throws InvalidArgumentError on non-finite or non-positive p, T or rho,
             and on a non-finite or non-positive result. Messages name the field.
             The non-finite path is defended TWICE (input validation and result
             check); only the non-positive path depends on input validation alone --
             a property P17.2's N3 control surfaced.
gap          No configured validity BAND. Positivity and finiteness are enforced, but
             there is no [Tmin, Tmax] / [pmin, pmax] range the way P15's
             TemperatureDependentProperty carries one. For an ideal gas the physical
             domain IS positivity, so this is a difference in kind rather than a
             defect -- but a caloric or real-gas model added later would need the
             band, and the abstraction has no place to put it.
```

## Compressible continuity — PARTIAL

```text
files        CompressibleContinuity.{hpp,cpp}, CompressiblePressureCorrection.{hpp,cpp},
             CompressibleMassFlux.{hpp,cpp}
production   The STEADY constraint div(rho U) = 0 IS in production -- as the
             pressure-correction equation, whose own header states it "is the
             discretized compressible continuity constraint solved for pressure".
             Face flux: m_dot_f = rho_f (U_f . n_f) A_f, one authoritative path.
             Global balance: physics::evaluateContinuity -> |globalNetFlux|.
tests        6 CompressibleContinuityTest + 12 mass flux + low-Mach mass imbalance
CPU / GPU    YES / no
WHY PARTIAL  (a) The TRANSIENT form d(rho)/dt + div(rho U) = 0 has NO production
                 caller. evaluateCompressibleContinuity is reached only from its own
                 .cpp, 6 unit tests and one integration-test line. Its own header
                 calls it a DIAGNOSTIC. CompressibleSIMPLE calls the steady
                 physics::evaluateContinuity instead (CompressibleSIMPLE.cpp:478).
             (b) pseudoTimeStep is a stabilization parameter, NOT a physical dt
                 (CompressibleSIMPLE.hpp:27), and densityOld == densityNew in the
                 momentum call -- so no genuine time accuracy exists.
gap          A real transient compressible continuity solve, and a production route
             to the existing diagnostic.
```

## Compressible momentum — PARTIAL

```text
files        CompressibleMomentum.{hpp,cpp}, CompressibleRelaxedMomentum.{hpp,cpp}
production   YES, via assembleRelaxedCompressibleMomentumComponent
             (CompressibleSIMPLE.cpp:265).
             assembleCompressibleMomentumComponent has NO production caller -- it
             calls .build() internally so it has no relaxation hook.
tests        7 CompressibleMomentumTest + 5 RelaxedMomentum + 2 in test_operators3d
             + the MMS order gate on u and v
CPU / GPU    YES / no
conservative d(rho U)/dt    YES -- rho_new u_new / rho_old u_old kept separate,
                            reduces exactly to the incompressible formula for
                            constant density (proven by test)
             div(rho U x U) YES -- flux-based on the compressible mass flux,
                            telescoping, therefore conservative. UPWIND ONLY.
             -grad(p)       YES
             div(tau)       NO -- see below
WHY PARTIAL  The viscous term is physics::assembleDiffusionContribution, the
             INCOMPRESSIBLE Laplacian mu lap U. The dilatational stress
             -(2/3) mu grad(div U) is ABSENT:
               grad(div 0 hits, dilatation 0, bulkViscosity 0, twoThirds 0, Stokes 0
             and test_mms_compressible.cpp names it in its own not_covered field:
               "mu/3 grad(div U) stress term".
             In incompressible flow div U = 0 and the term vanishes, so this was
             harmless through P16. Compressible flow is exactly where div U != 0.
             Also: 2D ONLY -- requireTwoDimensional at CompressibleSIMPLE.cpp:147.
gap          The dilatational stress term; higher-order convection; 3D.
```

## Total-energy equation — MISSING

```text
evidence     totalEnergy / TotalEnergy / rhoE / rho_e / internalEnergy  0 hits
             pressureWork / viscousDissipation / viscousWork            0 hits
what exists  P15's thermal/EnergyEquation.hpp -- a STEADY TEMPERATURE-TRANSPORT
             equation with an EnergyAssembly. Per the authorization's own rule, a
             temperature equation is NOT a total-energy equation, and it is not
             classified as one here.
coupling     ONE-WAY. SimulationSetup.hpp:59 -- thermalCoupled means "use the case's
             own converged thermal field instead", i.e. temperature is a read-only
             EOS input. It is never an unknown of the compressible system.
needed       e_t = e + 0.5|U|^2,  E = rho e_t,  h_t = h + 0.5|U|^2
             d(rho e_t)/dt + div[(rho e_t + p) U] = div(tau.U) - div(q) + rho f.U + S_E
             q = -k grad(T);  temperature recovery T(e)
```

## Low-Mach validation — IMPLEMENTED

```text
files        tests/integration/compressible/test_low_mach_regression.cpp (7 cases)
             tests/integration/mms/test_mms_compressible.cpp (1 system MMS)
criteria     machMax < 0.1 AND > 1.25*inletMach    (the second clause is a built-in
                                                    non-vacuity guard)
             deltaRhoOverRho < 1e-2
             maxEosError < 1e-9
             maxRelativeMassFluxDifference < 0.15
             globalMassImbalance <= a COMPUTED bound, not a constant
             all three metrics hold across nx refinement
             RepeatedRunIsDeterministic -- EXPECT_EQ, bitwise
CPU / GPU    YES / no
validation   Not only verification. MatchesArkilicIsothermalLubricationPressureProfile
             compares pressureAbsolute against the analytical isothermal compressible
             lubrication solution p(x) = sqrt(p_out^2 + 24 mu R T mdot (L-x)/H^3),
             through the real production path, at L2 and Linf < 5% -- a tolerance
             DERIVED from the lubrication approximation's own O(Re*(H/L)) ~ 0.17
             leading-order error, bounding a measured ~1.3% L2 / ~2.6% Linf.
             Plus a negative control, NonCoupledCaseStillUsesPostHocPathUnchanged.
gap          No ENERGY conservation check, because no energy equation exists. That is
             the one thing P17.6 adds beyond what is already measured.
```

## HLLC / Riemann flux — MISSING

```text
evidence     HLLC 0, HLL 0, Rusanov 0, LaxFriedrichs 0 hits
             Riemann 1 hit -- a forward-looking COMMENT at EquationOfState.hpp:51
                              that P17.2 itself wrote
             Roe 14 hits   -- ALL inside "zeroes"/"zeroed"/"Zero". Not one is a flux.
             Euler 107 hits -- ALL implicitEulerTimeDerivative and relatives.
                               TIME INTEGRATION, never the Euler equations.
             conservedVariable 0, primitiveVariable 0 hits
absent too   no conserved-state vector type, no inviscid physical flux, no wave-speed
             estimator, no left/right reconstruction interface, no flux-limiter
             infrastructure for a conserved state
note         The Roe and Euler counts are recorded because a naive census would have
             reported them as evidence that this capability partly exists.
```

## Density-based compressible solver — MISSING

```text
evidence     The only compressible solver is CompressibleSIMPLE: pressure-based,
             steady, segregated, density DERIVED from the EOS.
absent       conserved-variable update, CFL-based time-step control, local/global time
             stepping, primitive <-> conserved conversion, positivity protection on a
             conserved state
present and  non-finite-state detection (CompressibleSIMPLEStatus::NonFiniteState),
reusable     EOS-domain-exit detection (a caught InvalidArgumentError becomes
             NonFiniteState with a diagnostic detail string), residual histories
             (u, v, p, continuity), convergence criteria, the shared
             OuterIterationMonitor with stagnation and divergence detectors
constraint   The authorization requires the SIMPLE backend be PRESERVED, so this is a
             parallel solver, not a replacement.
```

## Shock validation — MISSING

```text
evidence     Sod 0, shock 0, shockTube 0, normalShock 0, supersonic 0, transonic 0,
             rarefaction 0, choked 0, nozzle 0 hits
             discontinuity 4 hits -- harmonic face interpolation, an alpha-advection
                                     comment, and a turbulence test name. None is a
                                     flow discontinuity.
absent       no Sod case file, no shock-tube case, no analytic Riemann reference, no
             shock-position or jump-ratio instrument
note         CompressibleSIMPLE is 2D-only and upwind-only, and its MMS explicitly
             excludes high Mach -- so there is no existing configuration that could
             even be pointed at a shock.
```

## CPU compressible support — IMPLEMENTED

```text
scope        steady, 2D, segregated, pressure-based, upwind convection, isothermal or
             one-way thermally coupled, low Mach
production   YES, both modes, exercised by cases/compressible_validation (post-hoc)
             and cases/compressible_channel_coupled (coupled)
tests        129 registered
evidence     MMS order gate 1.0 in band [0.55, 1.3]; 7 low-Mach cases; determinism
limits       2D only; upwind only; no dilatational stress; no energy; no transient;
             pseudoTimeStep has no case-format key
```

## GPU compressible support — MISSING

```text
evidence     Exactly ONE file in the GPU layer mentions "compressible":
             include/cfd/gpu/DevicePressureCorrection.hpp -- and only to record that
             it ports the INCOMPRESSIBLE assemblePressureCorrection.
             No compressible EOS, mass flux, momentum or pressure-correction kernel.
note         GPU compressible work is NOT in the authorized P17 scope list. Recorded
             as future work, not expanded into.
```

## CPU/GPU compressible equivalence — MISSING

```text
evidence     Vacuously missing: there is no GPU compressible implementation to
             compare a CPU result against. No equivalence test, no transfer
             evidence, no residency evidence.
gate note    This is the kind of row that must NOT be recorded as "PASS, nothing to
             compare". Under the authorization's rules it is MISSING, and any P17.10
             criterion over it would be VACUOUS until a GPU path exists.
```
