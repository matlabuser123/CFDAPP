# P12-COMP-001 — Compressible Boundary-Density Model

**Status: COMPLETE — implemented and verified with real evidence.**

## Context

Audited and planned as part of the P12-COMP reconciliation (see
`ROADMAP.md`'s `P12-COMP` section). Explicitly staged separately from
`P12-COMP-002` (the coupled compressible pressure-velocity solver, not
started) by instruction.

## What changed

Prior to this task, `CompressibleMassFlux.cpp`'s boundary-face density
was the owner cell's own density value (a disclosed simplification —
"no separate boundary-density state... the owner cell's own density is
used directly", per `CompressibleMassFlux.hpp`'s prior header comment).
This replaces that with a real EOS-evaluated boundary density:

```
faceGaugePressure   = interpolateFace(pressureGauge, pressureBoundaries)   // at this face
faceAbsolutePressure = referencePressure + faceGaugePressure
faceTemperature      = thermal_coupled
                        ? interpolateFace(temperature, *temperatureBoundaries)
                        : temperature[owner]   // isothermal: already uniform
faceDensity          = thermodynamics.density(faceAbsolutePressure, faceTemperature)
```

`cfd::discretization::interpolateFace` is the project's existing,
already-generic scalar boundary-value evaluator (dispatches correctly to
every `ScalarBoundaryCondition` subtype: `FixedValue`, `FixedGradient`,
`FixedTemperature`, `HeatFlux`, `Adiabatic`) — reused directly, unmodified.
**No new boundary-condition classes and no new `physics.json`/
`boundaries.json` keys were needed**: a compressible case's existing
velocity/pressure (and, when `thermal_coupled`, temperature) boundary
conditions already fully determine the boundary thermodynamic state.

## Architecture audit (performed before implementing, per instruction)

- `ScalarBoundaryCondition::boundaryValue(ownerValue, normalDistance)` —
  the existing per-BC-type interface (`include/cfd/boundary/BoundaryCondition.hpp`).
- `cfd::discretization::interpolateFace`/`interpolateBoundaryFace`
  (`src/discretization/Interpolation.cpp`) — the existing generic
  per-face scalar evaluator already used throughout the codebase for
  exactly this purpose (dispatch to the right BC, internal vs. boundary).
- `SimulationSetup::pressureBoundaries` (always present) and
  `::temperatureBoundaries` (present iff `thermal` enabled) — already
  available to `ProjectRunner.cpp` for the base incompressible solve;
  reused as-is, no new plumbing.

Conclusion: the existing architecture already had everything needed. The
only genuinely new code is the boundary-face density formula itself
(inside `calculateCompressibleMassFlux`) and threading five additional
parameters (`pressureGauge`, `pressureBoundaries`, `referencePressure`,
`thermodynamics`, `temperatureBoundaries`) through its signature and its
one call site.

## Explicit behavior per boundary type

| Type | Typical pressure BC | Resulting behavior |
|---|---|---|
| Outlet | Dirichlet (`fixed_value`) | Boundary density is the EOS value at *exactly* `referencePressure + BC value`, regardless of the interior cell's own (generally different) gauge pressure — a genuine correction, confirmed to differ from the old owner-cell value in a real case (see below). |
| Inlet | Zero-gradient (`fixed_gradient(0)`) | Boundary gauge pressure extrapolates to exactly the owner cell's own value when the gradient is genuinely zero, so boundary density reduces to the old owner-cell result in this common case — confirmed by a dedicated test, not assumed. |
| Wall | Zero-gradient | Same reduction as Inlet for the density value itself, but moot: Wall's velocity BC is no-slip (zero velocity, not just zero-normal), so `mDot_f = rho_f * (u_f . Sf) = rho_f * 0 = 0` regardless of `rho_f` — confirmed by a dedicated test with a deliberately extreme boundary pressure (5 MPa) showing zero effect. |

## New/updated test coverage

`tests/unit/compressible/test_compressible_mass_flux.cpp` — the old
`BoundaryFaceUsesOwnerCellsOwnDensity` test (which asserted exactly the
behavior this task supersedes) was replaced; 12 tests total now:

- `UniformDensityMatchesIncompressibleMassFluxExactly`,
  `InternalFaceUsesArithmeticMeanDensity` — updated for the new
  signature, behavior unchanged (internal-face interpolation untouched).
- `OutletBoundaryUsesEosEvaluatedPressureNotOwnerDensity` — Dirichlet
  outlet, deliberately-wrong owner density/pressure, confirms the BC
  wins.
- `InletBoundaryReducesToOwnerDensityUnderZeroPressureGradient` — confirms
  the reduction case.
- `WallBoundaryMassFluxIsZeroRegardlessOfBoundaryDensity` — confirms the
  moot case, with an extreme (5 MPa) wall pressure.
- `BoundaryDensityMatchesDirectEosEvaluation` — EOS-consistency check
  against an independently-computed expected value, non-uniform outlet
  pressure.
- `ThrowsOnNonPositiveBoundaryTemperature` — invalid-state propagation.
- `UniformStateGivesUniformBoundaryDensityMatchingInterior` — low-Mach/
  uniform-state limiting behavior, checked at inlet/outlet faces (wall
  faces are already covered by the zero-flux test above and would only
  trivially check 0==0 here).
- `MismatchedVelocitySizeThrows`, `MismatchedDensitySizeThrows`,
  `MismatchedPressureGaugeSizeThrows`, `MismatchedTemperatureSizeThrows`
  — size-validation coverage extended to the two new required fields.

`tests/integration/case/test_compressible_production_case.cpp` — new test
`OutletBoundaryMassFluxUsesReferencePressureDensity`: runs the real
`cases/compressible_validation` case through `ProjectRunner::run()` (not
the equation-level function directly), reads
`run.compressibleResult->massFlux` at the outlet face, confirms it
matches `thermodynamics.density(referencePressure, temperature)` (the new
formula) and explicitly confirms this differs from the interior cell's
own density (the old, superseded value) in this real case — proving the
new treatment is genuinely exercised end-to-end through the production
dispatch path, not just reachable at the unit level.

`tests/integration/compressible/test_low_mach_regression.cpp` — updated
call site only (new required parameters); all assertions unchanged and
still passing, confirming the existing low-Mach regression still holds
under the new boundary treatment.

## API change

`cfd::compressible::calculateCompressibleMassFlux` gained five required
parameters (`pressureGauge`, `pressureBoundaries`, `referencePressure`,
`thermodynamics`, `temperatureBoundaries`) — a breaking signature change,
justified since correcting its boundary-density treatment is this task's
entire purpose. `cfd::app::CompressibleRunResult` gained a `massFlux`
field (`cfd::fields::SurfaceField`) — the value was already computed
internally in `ProjectRunner.cpp`; it is now also kept, so it can be
independently inspected (by the new integration test above, and
potentially future GUI/CLI use) rather than being consumed only by the
continuity diagnostic.

## Verification evidence (fresh run, this session)

- Incremental rebuild (WSL2/gcc debug): clean, zero errors.
- `tests/unit/compressible/CFDCompressibleTests`: **50/50 PASS** (was 43
  before this task — net +7: +9 new/rewritten in the mass-flux suite, −1
  removed superseded test, but the mass-flux suite itself grew from 5 to
  12 tests, i.e. +7 net there, +0 elsewhere).
- `tests/integration/compressible/CFDLowMachRegressionTests`: **7/7 PASS**
  (unchanged — all 7 pre-existing assertions still hold under the new
  boundary-density treatment).
- `tests/integration/case/CFDCaseIntegrationTests` filtered to
  `CompressibleProductionCaseTest.*`: **8/8 PASS** (was 7 — +1 new
  production-path integration test).
- `tests/unit/io/CFDIoTests`: **194/194 PASS** (unchanged — confirms zero
  case-format impact, as expected since no new JSON keys were added).
- Full regression suite (`ctest -j8`): **100% passed, 0 failed,
  1313/1313** active tests (13 pre-existing disabled unchanged) — up from
  1305/1305 before this task (net +8: mass-flux suite 5→12 tests = +7,
  production-case suite 7→8 tests = +1), zero regressions, parallel
  `-j8` clean (no fixture race).

## Case-format impact

None. Confirmed no new `physics.json`/`boundaries.json` keys were needed
(the condition under which this task's instructions required stopping
before implementing) — every input the new boundary-density formula needs
already exists via the case's own existing pressure/temperature boundary
conditions.
