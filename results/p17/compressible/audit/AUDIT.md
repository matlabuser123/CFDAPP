# P17 — Compressible Flow: Phase 0 audit

Recorded 2026-09-26, before any P17 code was written.

**Five of the eight scope items substantially exist.** Three are genuinely absent.
The instruction to audit first was the right call: writing to the checklist without
looking would have duplicated a working EOS, continuity, momentum and low-Mach
validation suite.

---

## Baseline, frozen

```text
HEAD                    3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
branch                  main
working tree            DIRTY — 163 modified, 106 untracked (commit not authorized)
SOURCE_FINGERPRINT_V2   721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955
                        == P13's final qualified state, unchanged
```

P13–P16 are frozen and qualified. All ten P13 gates PASS as of 2026-09-26.

## Audit table

```text
Capability                  Existing  Partial  Missing   where
-------------------------------------------------------------------------------
Variable density              YES                        fields::ScalarField, EOS-evaluated
Ideal-gas EOS                 YES       (a)              compressible/IdealGasEOS +
                                                         ThermodynamicProperties
Compressible continuity       YES                        compressible/CompressibleContinuity
Compressible momentum         YES                        compressible/CompressibleMomentum
Total-energy equation                            YES     nothing: 0 hits for totalEnergy/rhoE
Low-Mach validation           YES                        7 tests, integration/compressible
HLLC / Riemann flux                              YES     nothing: 0 hits for HLLC/Riemann
Shock validation                                 YES     no Sod, no shock tube anywhere
```

`(a)` the EOS exposes `density(p,T)` and its derivatives but **no inverse**
`pressure(rho,T)`, which P17.2's round-trip verification requires.

Existing compressible test coverage: **90 tests** across 13 files.

## What exists, mapped

### Variable density — EXISTS

`ProjectRunner.hpp:143` — `cfd::fields::ScalarField density; // EOS-evaluated,
cell-ordered.` A field, not a constant, cell-centred, consumed by
`CompressibleContinuity`, `CompressibleMassFlux` and `CompressibleMomentum` as
`densityOld` / `densityNew`.

### Ideal-gas EOS — EXISTS, one gap

```cpp
class EquationOfState {                          // the abstraction already exists
  virtual Real density(Real p, Real T) const = 0;
  virtual Real dDensityDPressure(Real p, Real T) const = 0;
  virtual Real dDensityDTemperature(Real p, Real T) const = 0;
};
class IdealGasEOS final : public EquationOfState { ... };
```

`ThermodynamicProperties` already satisfies P17.2's thermodynamic-constant
requirements **by construction rather than by test**: it takes `(R, cp)` and
*derives* `cv = cp - R` and `gamma = cp/cv`, so `cp - cv = R` cannot be violated —
its header states this is deliberate, "so no mutually inconsistent independent
values can be supplied". It also already provides `speedOfSound(T) = sqrt(gamma R T)`
and there is a `machNumber(speed, soundSpeed)`.

**The gap:** no `pressure(rho, T)`. The `(rho,T) -> p -> rho` round trip P17.2 asks
for cannot be written against the current interface.

### Pressure convention — ALREADY EXPLICIT

P17.2 requires that absolute and gauge pressure not be silently mixed, and that the
convention be documented. Both already hold, in three places:

```text
CompressibleSIMPLE.hpp:90    "`pressure` is gauge pressure"
CompressibleSIMPLE.hpp:150   "referencePressure converts this solve's gauge pressure to absolute"
ProjectRunner.hpp:144        pressureAbsolute = referencePressure + SIMPLE's gauge pressure
PhysicsConfig.hpp:179        referencePressure converts SIMPLE's own gauge pressure to ...
```

SIMPLE solves gauge; the EOS consumes absolute; `referencePressure` is the single
documented bridge. Nothing to add — this is a preservation item, not new work.

### Compressible continuity — EXISTS, in exactly the form P17.3 specifies

`CompressibleContinuity.hpp:11` states the discrete form verbatim:

```text
(rho_P^{n+1} - rho_P^n) * V_P / dt + sum_faces(mDot_f) = 0
```

Its header also records the architectural decision P17 must not undo: the steady
compressible mass balance is `physics::evaluateContinuity` fed this module's
`calculateCompressibleMassFlux` output — "one authoritative mass-flux path", not a
second implementation.

### Compressible momentum — EXISTS

`compressibleMomentumTimeDerivative` (variable-density transient) and
`assembleCompressibleMomentumComponent` (conservative convective flux, viscous term,
pressure force), taking `densityOld`/`densityNew` and reusing
`physics::MomentumAssembly`. Its header explicitly reuses "existing viscous
discretization/property infrastructure" rather than duplicating it.

### Low-Mach validation — EXISTS, and is thorough

`tests/integration/compressible/test_low_mach_regression.cpp`, 7 tests:

```text
MachNumberStaysWellBelowPointOne                 machMax < 0.1, and > 1.25x inletMach
DensityVariationIsSmallRelativeToReferenceDensity  deltaRhoOverRho < 1e-2
EosConsistencyErrorIsAtFloatingPointTolerance      maxEosError < 1e-9
CompressibleMassFluxApproachesTheIncompressibleLimit  < 0.15 relative
GlobalMassImbalanceIsSmall                        within a computed bound
GridRefinementKeepsAllMetricsSmall                across nx
```

P17.6's requirements — density variation small, mass conservation, Mach recorded
with `a = sqrt(gamma R T)` — are already measured. What P17.6 adds is the *energy*
conservation check, which cannot exist until the energy equation does.

## What is genuinely missing

```text
totalEnergy / TotalEnergy / rhoE     0 hits in include, src and tests
HLLC / Hllc / Riemann / riemann      0 hits in include, src and tests
Sod / shock tube                     no case, no test
```

### Total-energy equation — MISSING

P15 provides a **steady temperature-transport** energy equation
(`thermal/EnergyEquation.hpp`, `EnergyAssembly`), not a conservative total-energy
equation. And the existing compressible energy coupling is **one-way**: `SimulationSetup.hpp:59`
— "`thermalCoupled` true means use the case's own converged thermal temperature
field" — i.e. temperature is a read-only input to the EOS. The ROADMAP records this
as "No two-way energy coupling".

So `E = e + 0.5|U|^2` and

```text
d(rho E)/dt + div[U(rho E + p)] = div(tau . U) - div(q) + sources
```

are new. Per the instruction, this must build on P15's thermal/material-property
infrastructure rather than creating a second thermal-property implementation —
`ThermodynamicProperties` (cp, cv, gamma) and `physics::TemperatureProperty` (mu(T),
k(T)) are the existing homes and must stay the only ones.

### HLLC / Riemann flux — MISSING

No flux-interface abstraction for left/right conservative states exists. This is the
largest genuinely new piece, and P17 requires it be added without contaminating the
incompressible path.

### Shock validation — MISSING

No Sod shock tube, no shock-tube case file, no discriminating second case.

## Consequences for how P17 should proceed

1. **P17.1, P17.3, P17.4 and P17.6 are primarily verification-and-gap work, not
   implementation.** They need frozen acceptance criteria, non-vacuity controls and
   evidence — the P13 method — against code that already exists.
2. **P17.2 needs one addition**: `pressure(rho, T)` on the EOS interface, for the
   round trip. Adding a pure virtual to `EquationOfState` touches every
   implementation; `IdealGasEOS` is currently the only one.
3. **P17.5, P17.7, P17.8 are the real implementation work**, in that order, and
   P17.7 is gated on P17.1–P17.6 being green per the instruction.
4. **GPU is not in the P17 scope list.** Recorded as future work rather than
   expanded into automatically, as the instruction directs.

## Preservation baseline to hold

```text
P13   all ten gates PASS; final V2 721ce161...
P14   generic transport, 104 tests
P15   thermal / energy, 248 tests
P16   turbulence, 363 tests + 126 GPU
SIMPLE fingerprint   10 cases, 130 lines, bitwise — the sharpest instrument;
                     P17 must not move a single bit of it
CPU backend          the numerical reference
```
