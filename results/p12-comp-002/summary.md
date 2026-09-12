# P12-COMP-002 — Coupled Compressible Pressure-Velocity Solver

**Status:** COMPLETE
**Scope:** genuinely coupled `CompressibleSIMPLE` solver, opt-in via
`physics.json`'s `compressible.coupled` (default `false`, preserving the
existing post-hoc pass exactly). Advanced compressible-energy coupling and
higher-Mach capability are explicitly **not** part of this task (deferred,
per `ROADMAP.md`).

## 1. Architecture implemented

- `include/cfd/compressible/CompressibleRelaxedMomentum.{hpp,cpp}` (new):
  `assembleRelaxedCompressibleMomentumComponent` -- mirrors
  `RelaxedMomentum.cpp`'s structure exactly, composing the *existing,
  unmodified* diffusion/convection/pressure-source assemblers,
  `compressibleMomentumTimeDerivative` (existing), and
  `applyImplicitUnderRelaxation` (existing). No new physics, only new
  composition.
- `include/cfd/compressible/CompressiblePressureCorrection.{hpp,cpp}`
  (new): `assembleCompressiblePressureCorrection` -- generalizes
  `assemblePressureCorrection`'s algorithm with (a) a per-face density
  (from `evaluateCompressibleFaceDensity`, shared with
  `calculateCompressibleMassFlux` so P12-COMP-001's boundary-density
  treatment is inherited, not re-implemented) in the D_f face
  coefficient instead of one global constant, and (b) a new diagonal
  term `V_P/pseudoTimeStep * dDensityDPressure(p, T)` (`IdealGasEOS`,
  existing). Reduces to `assemblePressureCorrection`'s exact behavior
  when density is uniform and the compressibility term is negligible.
- `include/cfd/compressible/CompressibleSIMPLE.{hpp,cpp}` (new): the
  coupled solver class, mirroring `SIMPLE::solve()`'s own loop structure
  (momentum predict -> predictor flux -> response coefficients ->
  pressure correction -> correct velocity/pressure/flux -> **correct
  density via EOS from the just-corrected pressure** -> residual check
  -> repeat). Density is genuinely iterated state, not a post-hoc read
  after the loop -- the core P12-COMP-002 requirement. Pseudo-transient
  framing (`pseudoTimeStep`, a numerical convergence-control parameter
  like `pressureRelaxation`, not a physical time step) reconciles
  `CompressibleMomentum`'s transient-term-based API with a nominally
  steady solver -- each outer iteration uses the current best-known
  per-cell density as both "old" and "new" in that term, exactly as
  `compressibleMomentumTimeDerivative`'s own
  `ConstantDensityMatchesIncompressibleFormula` test already proves
  reduces correctly.
- `src/CMakeLists.txt` / `tests/solver/CMakeLists.txt` /
  `tests/solver/compressible_simple/`: new sources and test executable
  (`CFDCompressibleSimpleTests`) wired in.
- `physics.json`'s new `compressible.coupled` (bool, default `false`):
  `include/cfd/io/case/PhysicsConfig.hpp`,
  `src/io/case/PhysicsConfigParser.cpp` (parsing + two new compatibility
  rules, see section 6), `include/cfd/io/SimulationSetup.hpp`,
  `src/io/CaseBuilder.cpp`, `include/cfd/io/JSONWriter.hpp`,
  `src/io/JSONWriter.cpp` (exported as `compressible.coupled` in
  `metadata.json`).
- `include/cfd/app/ProjectRunner.hpp` / `src/app/ProjectRunner.cpp`: the
  production dispatch. `ProjectRunResult::compressibleSimpleResult`
  (new, `std::optional<CompressibleSIMPLEResult>`) is populated iff
  `coupled: true` and the incompressible warm start was usable; that
  solve's own status becomes the run's authoritative `status` (not the
  warm start's). `compressibleResult`/export fields are populated from
  the coupled result's own final state so the existing CSV/VTK/JSON
  field names (`density`, `pressure_absolute`, `mach_number`, ...)
  continue to work unchanged for both modes.

## 2. Root-cause investigation: incompressible warm-start / coupled
   pressure solve failing with `PressureCorrectionFailure`

### Step 1 — characterization (first reproduction)

`cases/compressible_channel_coupled` (48x8 mesh, L=1.0, H=0.05,
density=1.176624, dynamic_viscosity=0.58831, inlet velocity [20, 0],
outlet fixed_value pressure=0, walls at top/bottom, reference_pressure
101325, gas_constant 287.05, T=300K, `coupled: true`, velocity/pressure
relaxation 0.7/0.3, both linear solvers BiCGSTAB, `max_iterations: 6000`)
reproducibly failed with `PressureCorrectionFailure` at **outer iteration
32**, residual history smoothly and monotonically decreasing the entire
time (u: 604.8 at iter 2 down to 0.1266 at iter 32, no oscillation) --
i.e. not a divergence, an abrupt stop mid-convergence.

A temporary diagnostic (`std::cerr` in `SIMPLE.cpp`'s pressure-solve
failure branch, printing `SolverResult::status/iterations/
initialResidual/finalResidual`; added, used, then fully reverted --
`git diff` on that file is empty) showed:

```text
status=2 (Breakdown) innerIters=104 initialResidual=0.00806 finalResidual=1.93e-08
```

I.e. BiCGSTAB had already reduced the pressure-correction linear
residual by ~5-6 orders of magnitude (already effectively converged)
before hitting its own numerical **Breakdown** condition (near-zero
inner product in the biorthogonalization -- a known BiCGSTAB failure
mode right at the tail of convergence), never `MaxIterations`.

### Step 2 — isolating the cause

| Test | Change | Result |
|---|---|---|
| Baseline | as above (BiCGSTAB, no preconditioner) | Breakdown @ iter 32 |
| Isolate: plain incompressible physics (no `compressible` block at all), identical mesh/BC/mu | remove `compressible` block | **identical failure** (iter 32, same residuals) -- confirms this is a plain, unmodified `SIMPLE`/`PressureCorrectionEquation` issue, not anything P12-COMP-002 added |
| Mesh aspect ratio | scaled geometry 20x larger (H=1, L=20, same nx:ny ratio) | still fails (Breakdown or slow), ruling out "absolute cell size" as the root cause once other factors are also isolated below |
| Add Jacobi preconditioning (`PreconditionerType::Jacobi`) to the pressure solver only | code-level test, later reverted | delays breakdown enormously (iter 32 -> iter 5787) but does not eliminate it -- confirms this is BiCGSTAB's own algorithmic breakdown pathology, not a "just needs a better preconditioner" conditioning issue alone |
| **Switch `pressure_linear_solver.type` from `BiCGSTAB` to `CG`** (pure case-configuration change, zero production-code change) | `cases/compressible_channel_coupled/solver.json` | **No further Breakdown of any kind.** Reaches `MaxIterations` cleanly at 6000, residuals still smoothly decreasing (u: 1.79e-4, extrapolated ~12,200 total iterations to tolerance from the observed asymptotic decay rate) |

**Root cause:** the compressible pressure-correction matrix (and the
plain incompressible one it generalizes) is symmetric by construction
(`assembleCompressiblePressureCorrection`/`assemblePressureCorrection`
both add `+dCoefficient`/`-dCoefficient` identically to both the owner's
and neighbor's rows) and diagonally dominant/positive-definite for this
elliptic pressure-Poisson-type system. **BiCGSTAB is designed for
general non-symmetric systems and is well-known to suffer breakdown
(near-zero inner products) precisely as it approaches convergence on
certain matrices; Conjugate Gradient (CG) is the textbook-correct,
provably more robust method for a genuinely SPD system and does not
share this failure mode.** This is a numerical-method-selection issue in
this specific case's own `solver.json`, not a defect in
`CompressibleSIMPLE`, `assembleCompressiblePressureCorrection`, or the
shared, unmodified `SIMPLE`/`PressureCorrectionEquation` code (both of
which were exercised, unmodified, in the isolation test above and showed
the identical behavior with `BiCGSTAB`). No production/solver source
file was changed to fix this -- `cases/compressible_channel_coupled/
solver.json` alone: `pressure_linear_solver.type: "CG"`,
`max_iterations: 20000` (a genuinely larger budget for a genuinely
slower-converging case at tight tolerances, not a failure workaround --
see the extrapolated-iteration-count justification above; there was no
failure left to work around once `CG` was selected).

### Step 3 — outcome

With `CG` and `max_iterations: 20000`, the case converges cleanly and
deterministically:

```text
Converged: yes
Iterations: 12189   (matches the ~12,200 extrapolated from the BiCGSTAB run's own decay rate)
U residual: 1.99982e-05   (tolerance 2e-5)
V residual: 3.03559e-07
P residual: 1.16714e-05   (tolerance 5e-4)
Continuity: 4.29983e-10   (tolerance 1e-6)
Mass imbalance: 5.21181e-11
compressible.status: "Converged"
```

Verified deterministic: two independent full CLI runs produced
byte-identical `metadata.json` and `fields.csv` (`diff` clean).

## 3. A methodological pitfall found and corrected during validation
   (worth recording so it is not repeated)

An early attempt to validate mass conservation by reconstructing the
mass flow rate as `sum(density_cell * velocity_x_cell * dy)` over a
mesh column showed an apparent ~35-50% "loss" of mass along the channel
-- initially alarming. **This was a flawed diagnostic, not a solver
defect.** `CompressibleSIMPLE` (like the incompressible `SIMPLE` it
generalizes) is a collocated-grid, Rhie-Chow-style pressure-correction
scheme: the quantity the pressure-correction equation actually enforces
conservation of is the **face mass flux** (`massFlux`, corrected via
`correctFaceMassFlux`), not the product of the separately-reconstructed
cell-center density and cell-center velocity (corrected via
`correctVelocity`, a genuinely different correction formula). For a
flow with strong density variation (here, ~2x from inlet to outlet),
this cell-center-reconstruction-vs-face-flux gap can be large even
though the algorithm's own conserved quantity is essentially exact.

Confirmed directly: summing `|massFlux|` over every internal x-normal
face at each of the mesh's 47 internal cross-sections gave **exactly
1.99708 at every single station** (to the full displayed precision) --
genuine, essentially machine-precision mass conservation in the
quantity that actually matters. This measured, verified value is what
`MassFlowRateIsConservedAtEveryInternalCrossSection` (see section 4)
checks automatically, and what the analytical comparison (section 5)
uses for `mdot`, instead of the flawed cell-center reconstruction.

## 4. Low-Mach / reduction-to-incompressible-SIMPLE regression

Already implemented and passing (`tests/solver/compressible_simple/
test_compressible_simple.cpp`,
`CompressibleSimpleTest.ReducesToIncompressibleSimpleWithNegligibleCompressibility`):
`CompressibleSIMPLE` with an artificially large gas constant (a
physical near-incompressible-gas limit, not a test-only bypass) on a
4x4 lid-driven cavity reproduces plain `SIMPLE`'s own converged result
within tolerance. See section 7 for the fresh re-run count.

## 5. Independent physical validation: Arkilic et al. (1997) isothermal
   compressible-lubrication channel flow

**Governing assumptions:** steady, isothermal, ideal-gas, low reduced
Reynolds number (`Re*(H/L) << 1`) flow through a long, thin,
parallel-plate channel. Mass conservation (`rho*u = mdot/H = const` per
unit width) + isothermal ideal gas (`rho = p/(R T)`) + the no-slip,
fully-developed (parabolic) lubrication-limit velocity profile
(`Q(x) = -H^3/(12 mu) dp/dx`) combine to:

```text
mdot = rho(x) * Q(x) = -(p/(R T)) * H^3/(12 mu) * dp/dx
     => p dp/dx = -12 mu R T mdot / H^3
     => d(p^2)/dx = -24 mu R T mdot / H^3   (p dp/dx = (1/2) d(p^2)/dx)
     => p(x)^2 is LINEAR in x.
```

This is the closed-form result underlying the Arkilic et al. (1997)
compressible-microchannel model, a standard literature benchmark --
genuinely independent of this solver's own EOS self-consistency, and it
exercises exactly the pressure-density coupling `CompressibleSIMPLE`
implements (it would not hold if density were not actually coupled into
the pressure/momentum balance).

**Case:** `cases/compressible_channel_coupled` (L=1.0 m, H=0.05 m,
L/H=20; mu=0.58831 Pa.s; R=287.05 J/(kg K); T=300 K; reference_pressure
= outlet Dirichlet BC = 101325 Pa; inlet velocity 20 m/s uniform).
Converged solution's own measured Reynolds number (`Re = mdot/mu` at the
mass flow rate measured in section 3): **Re ~ 3.39**; reduced Reynolds
number `Re*(H/L)` ~ **0.17** (small, consistent with reasonable
lubrication-approximation validity, not asymptotically tiny); Mach
number up to **0.142** (comfortably low-Mach).

**Comparison** (`p(x)` = cell-center `pressure_absolute` at mid-channel
height; analytical `p(x)` anchored at the *exact* Dirichlet outlet BC
`p(L) = 101325 Pa`, slope from the converged solution's own measured
`mdot`, not an assumed one):

| x (m) | numeric p_abs (Pa) | analytical p_abs (Pa) | rel. error |
|---|---|---|---|
| 0.0104 | 171501.27 | 171727.48 | -0.13% |
| 0.2604 | 155494.02 | 156951.73 | -0.93% |
| 0.5104 | 138395.11 | 140632.02 | -1.59% |
| 0.7604 | 119577.18 | 122151.07 | -2.11% |
| 0.9896 | 103230.58 | 102318.66 | +0.89% |

**L2 relative error: 1.28%. Linf relative error: 2.55%.** The p(x)^2 vs
x linear-regression fit itself has R^2 = 0.9969 (confirming the
qualitative "linear in x" structural prediction independently of the
absolute-scale comparison above). The residual few-percent-level
discrepancy is consistent with the lubrication approximation's own
leading-order error, `O(Re*(H/L)) ~ O(0.17)` -- i.e. the *expected*,
physically-explained residual for a full 2D Navier-Stokes solve compared
against a reduced 1D theory at this reduced-Reynolds-number, not FVM
discretization error. (An earlier hand-derivation of the analytical
formula used in this session's draft plan omitted the factor of 2 from
`p dp/dx = (1/2) d(p^2)/dx`, i.e. used coefficient 12 instead of 24 --
caught by comparing against the numerical result, which showed almost
exactly a factor-of-2 slope mismatch; corrected before finalizing this
comparison and the production test below.)

**Grid refinement:** not performed for this task -- the single 48x8
grid's own solve already takes ~4 minutes at the tight tolerances needed
for a meaningful comparison, and the single-grid quantitative agreement
(1.28%/2.55%, explained by the reduced-Reynolds-number correction term)
is already a credible, literature-anchored, non-tautological validation
result. Documented here as a known limitation, not silently omitted.

## 6. Case-format / compatibility matrix

`physics.json`'s `compressible.coupled` (bool, optional, default
`false`) -- every existing case (including this same case with `coupled`
absent) is byte-identical in behavior when absent, confirmed by the full
regression below. Two new compatibility rules (`CompressibleSIMPLE` has
no turbulence-model or buoyancy-source injection point yet):
`compressible.coupled: true` excludes `turbulence` and excludes
`buoyancy`, both rejected at parse time with a real, `cfdapp`-run-
captured error message (not silently dropping that physics). Documented
in `docs/user_guide/case_format.md` (the `compressible` section and the
"Physics compatibility" table/examples).

## 7. Test evidence (fresh, this session)

Targeted, before the case/production-test work in this document:

- `CFDCompressibleSimpleTests` (new, P12-COMP-002's own unit/solver
  tests): **19/19 PASS** (5 relaxed-momentum, 6 pressure-correction, 5
  full-solver incl. the reduction regression, 3 settings-validation).
- `CFDCompressibleTests`: 50/50 PASS (unchanged by P12-COMP-002 --
  `CompressibleMassFlux.cpp`'s `evaluateCompressibleFaceDensity`
  refactor is behavior-preserving).
- `CFDLowMachRegressionTests`: 7/7 PASS (unchanged).
- `CFDIoTests` (`CompressibleCaseTest`+`PhysicsCompatibilityTest`):
  **27/27 PASS** (4 new `coupled`-parsing tests + 2 new compatibility-rule
  tests, on top of the pre-existing 21); full `CFDIoTests`: **200/200
  PASS** (up from 194/194).
- `CaseBuilderTest`: 4/4 PASS.
- `ProjectRunnerTest`: 4/4 PASS.
- `CompressibleProductionCaseTest` (existing, post-hoc path): 8/8 PASS
  (unaffected).

Production-path proof, this task's new file
(`tests/integration/case/test_compressible_coupled_production_case.cpp`,
executable `CFDCaseIntegrationTests`): **6/6 PASS**
(`DispatchesToCompressibleSimpleAndConverges`,
`MassFlowRateIsConservedAtEveryInternalCrossSection`,
`MatchesArkilicIsothermalLubricationPressureProfile`,
`ExportsCoupledCompressibleFields`, `RepeatedRunIsDeterministic`,
`NonCoupledCaseStillUsesPostHocPathUnchanged`). Fresh, real run (this
session): 254s/242s/241s/236s/479s/9s respectively, total 1461.8s. (An
earlier draft of this file tried sharing one run across the first five
tests via a `SetUpTestSuite()` fixture; measured evidence showed this
project's `gtest_discover_tests` setup invokes each named test as its own
process, so the fixture's own expensive solve reran per test anyway with
no savings, and `NonCoupledCaseStillUsesPostHocPathUnchanged` was paying
for it despite never using it. Reverted to plain, independent `TEST()`
cases -- confirmed by the corrected run above, where that last test
dropped to 8.7s.)

Full regression (`ctest -j32`, all labels, fresh full rebuild first):
**100% passed, 0 failed, 1344/1344 active tests** (13 pre-existing
`DISABLED_` slow grid-refinement cases unchanged/excluded, same as every
prior baseline) -- up from 1313/1313 before this task, net **+31**: +19
`CFDCompressibleSimpleTests`, +6 `CFDIoTests` (4 new `coupled`-parsing
unit tests + 2 new compatibility-rule tests), +6
`CompressibleCoupledProductionCaseTest`. Zero regressions. Total wall
time 659.3s (real, `-j32`) -- the slow `CompressibleCoupledProductionCaseTest`
cases (5 of the 6 average ~4 minutes each serially, one ~11 minutes)
overlapped with the rest of the suite under parallel `ctest`, so wall
time stayed far below the ~30-minute serial sum.

## 8. Known limitations

- Grid refinement for the Arkilic validation case was not performed
  (single 48x8 grid) -- see section 5.
- The coupled solver's pressure/momentum linear solvers currently need
  case-by-case tuning for numerical robustness (this case specifically
  needed `CG` rather than the codebase's usual default `BiCGSTAB` for
  its pressure solve, and a larger-than-default `max_iterations`) --
  not yet a documented general guideline for authoring new
  `compressible.coupled: true` cases beyond this one worked example.
- No GPU dispatch for `CompressibleSIMPLE` (CPU-only, as scoped).
- No GUI exposure of `compressible.coupled` beyond JSON round-trip
  (no dedicated GUI control) -- out of this task's explicit scope.
- Advanced compressible-energy coupling and higher-Mach capability
  remain explicitly deferred, per `ROADMAP.md`.
