# Production validation

P12-NUM-007. Record and reports: `include/cfd/validation/ProductionValidation.hpp`.
Cases: `tests/integration/{cavity,poiseuille,turbulence_channel,backward_facing_step}/`.
Reference data: `validation/ghia/`, `validation/data/turbulence/channel_flow/`.
Measured results: `results/p12-num-007/summary.md`.

## Verification → validation hierarchy

Each level answers a different question, and each depends on the one before it.

| level | question | reference | where |
|---|---|---|---|
| 1. MMS (P12-NUM-006) | Are the equations solved correctly? The discretisation reaches its formal order on the whole solve. | a manufactured exact solution | `docs/validation/manufactured_solutions.md` |
| 2. Grid convergence (P12-NUM-005) | How far is *this* solution from its own grid-converged limit? This gives the observed order, the Richardson extrapolation and the GCI. | the code's own three solutions only | `docs/validation/grid_convergence.md` |
| 3. Production validation (P12-NUM-007) | Is the right physics solved? Does the grid-converging solution agree with a physical benchmark? | published benchmark data or an exact physical solution | this document |

The two numbers are never mixed.
- **Grid convergence** of a production case is always computed from our own solutions. The benchmark never enters the observed order or the GCI.
- **Error against a benchmark** is reported separately, per grid.

A benchmark that is itself a numerical solution has its own error. Ghia et al. (1982) is a 129×129 solution. Once our discretisation error falls below that level, the distance to the benchmark stops decreasing; this is the "benchmark floor" of the Re = 100 cavity. It is not an error of this code, and the grid-convergence study is what shows that.

## The record

`ValidationRun` is one solve: a case, a Reynolds number, a mesh and a convection scheme.
- Its `level` is the P12-NUM-006 `MMSLevel`. It holds the mesh size, h, solver status, acceptance, iterations, mass imbalance, error norms, diagnostics and runtime.
- Its `checks` are pass/fail criteria (`MMSGate`).

`makeSimpleValidationRun` fills the level from a `SIMPLEResult`:
- acceptance via P12-NUM-005 `assessSimpleSolve` (Converged, finite, mass imbalance within tolerance);
- the cost and residual diagnostics: `momentum_linear_iterations` and `pressure_linear_iterations` (the SIMPLEResult counters added in P12-NUM-007), `linear_solver_fallbacks`, and the final residuals.

`ValidationReport` holds:
- the runs;
- the reference and its source;
- the physical coefficients and configuration;
- P12-NUM-005 `GridConvergenceStudy`s of the same solves, via `toGridStudyEntry` (no re-solve);
- observed orders of the benchmark errors (`computeRunSequenceOrder`, which is `computeMMSOrder`);
- report-level gates;
- limitations.

`passed()` means: at least one run, every run accepted with all checks passed, and every gate passed.

`validationReportJson` / `validationReportMarkdown` are deterministic except wall-clock times: the `runtime` block and each embedded grid study's `runtime_seconds`. Unset values are `null`, never NaN.

Error norms:
- **Against sample-point tables** (Ghia stations): `computeSampleErrorNorms`, which is unweighted: L1 = mean|e|, L2 = √(mean e²), L∞ = max|e|.
- **Against fields**: the volume-weighted norms of `ErrorNorms.hpp`. On uniform rows (the Poiseuille profile) the two are identical.

## Cases

### Lid-driven cavity, Re = 100 and Re = 1000

**Reference.** Ghia, Ghia & Shin (1982), Tables I/II, both Reynolds numbers, 17 stations per centerline. The Re = 1000 transcription was cross-checked against three independent sources (`validation/ghia/README.md`). The only discrepancy, v(0.9063), was resolved to −0.51550.

**Configuration of record:**
- QUICK convection;
- the documented per-grid SIMPLE settings of `test_cavity_ghia.cpp` for every Reynolds number and scheme (`cavitySettings`);
- outer tolerances 1e-6;
- the P12-NUM-004 linear-solver fallback.

Nothing is tuned per case.

**Metrics per run:**
- u(0.5, y) and v(x, 0.5) L1/L2/L∞ against Ghia;
- iterations and linear iterations;
- status, mass imbalance, maximum wall flux, runtime;
- max |U| (boundedness);
- the values at five Ghia stations. These feed the grid-convergence study.

**Grids.** Re = 100: 20/40/80/160. Re = 1000: 40/80/160.

**Gates:**
- the first-order sequence falls monotonically (far above the floor);
- at 80×80 QUICK is closer than upwind to the grid-converged (QUICK 160×160) station values. This comparison is benchmark-independent: a comparison against Ghia fails at the floor;
- the QUICK Re = 100 station values are grid-converged on 40/80/160, with fine-grid uncertainty < 1 %. The uncertainty is GCI21 for a monotonic station, and the NUM-005 oscillation uncertainty for an oscillatory one;
- the Re = 1000 error falls monotonically.

The `ghia_error` bounds of single runs are *regression* bounds: the measured value plus about 30 %. They are not literature targets.

### Planar Poiseuille flow (Re = 10, L = 8H)

There are two references:
1. **The continuous solution:** u = 6U(y/H)(1 − y/H), dp/dx = −12μU/H².
2. **The exact fully developed solution of the discretisation itself**, derived in `PoiseuilleValidationUtils.hpp`: u_j = (G/2)y_j(H − y_j) + G·dy²/8, with dp/dx scaled by ny²/(ny² + 2).

The relative discretisation error of dp/dx is therefore exactly 2/(ny² + 2): second order, as designed.

The numerical solution matches reference 2 to 1e-6 … 1e-9. This shows that the entire difference from the physics is the predicted discretisation error, not an entrance, outlet or iterative effect.

**Pressure.** The collocated SIMPLE has no Rhie–Chow interpolation, so an odd-even pressure mode a·(−1)ⁱ grows on this open channel's residual plateau. The pair-averaged column pressure P(i) = (p̄ᵢ + p̄ᵢ₊₁)/2 cancels that mode exactly. The gradient and the pressure drop use it; the mode amplitude is reported per run.

**Mass flow** is checked at the inlet, at the outlet and at three internal sections.

**Grids.** 64×8 / 96×12 / 144×18, the NUM-005 grids. The grid study is asymptotic, with p ≈ 2 for the centerline velocity, dp/dx and the profile norms.

### Turbulent channel, Re_τ = 180

This uses the P2-TURB-007 case and gates unchanged (k-ε, k-ω, SST; coarse/medium/fine). It adds:
- u_τ, τ_w (both walls), achieved Re_τ;
- C_f = τ_w / (½ρU_b²) against 8.2e-3;
- U_c⁺ against 18.2 and U_b⁺ against 15.6;
- first-cell y⁺;
- u⁺ against the log law (y⁺ > 30).

Each quantity is computed with **two** wall-shear estimators:
- **Secant** μU(y₁)/y₁. It under-estimates τ_w when y₁ is outside the viscous sublayer (no wall function).
- **Momentum balance** −dp/dx·δ (pair-averaged dp/dx between 0.8 L and 0.9 L). At L = 8H the core is still accelerating, so this over-estimates τ_w.

The two estimates bracket the wall shear and converge toward each other with refinement. No turbulence model is changed.

### Backward-facing step (Gartling 1990, ER = 2)

**Geometry.** The fluid domain downstream of the step is a plain rectangle, so no blocked-cell capability is needed. The existing Cartesian mesh keeps its cells and faces. Only its left boundary is re-partitioned:
- a no-slip step face (y < h);
- one inlet patch per face (y > h), carrying the exact face average of u = 24(y − h)(H − y). The mean is 1 and the inflow rate is exactly 0.5 on every grid.

**Flow conditions.** Re = U_mean·H/ν, with a zero-gradient outlet at p = 0. L = 15H was measured to be long enough: L = 30H gives the same separation topology to 4 digits.

**Reference.** Re = 800: the published 2D numerical x_r/h lie in [11.48, 12.20], from ten studies compiled in arXiv:2507.16509 Table 2. Gartling's own value is 12.20 (restated in arXiv:1906.05387). The upper-wall bubble length lies in [10.60, 11.52]. The Re = 100 value 3.00 is a single source and is reported only, never gated.

**Reattachment, computed algorithmically** from the wall shear μu_P/(dy/2) of the wall-adjacent cells, with zero crossings located by linear interpolation:
- x_r is the downstream-most negative → positive crossing on the bottom wall;
- the corner eddy ends at the positive → negative crossing just before x_r;
- the upper bubble runs from the first negative → positive top-wall crossing to the next one.

**Grids and gates.** Re = 800 on 20/30/40 cells per H (the in-repo test), plus a 50 cells/H grid-refinement run through the same unchanged implementation (recorded in `results/p12-num-007/`). There are two gates. The grid-converged x_r/h must lie in [11.48, 12.20], and the upper-bubble length in [10.60, 11.52]. Each value is Richardson-extrapolated when its sequence is asymptotic, otherwise it is the finest-grid value, and the report says which.

Measured (`results/p12-num-007/summary.md` §8):
- At 40 cells/H, x_r/h = 11.78 passed but the upper-bubble length (11.63) failed.
- At 50 cells/H both pass: x_r/h = 11.93, and the bubble length is 11.51. The 30/40/50 triplet is asymptotic, with Richardson 11.24.

### Convection-scheme accuracy and cost

The cavity is run with upwind, central, linear_upwind and QUICK. Mesh, physics, settings, tolerances, linear solvers and initial condition are identical; only `convectionScheme` differs. The runs are sequential in one process.

**Recorded per run:**
- Ghia errors;
- outer and linear iterations;
- runtime;
- status and fallbacks;
- mass imbalance;
- boundedness (max |U| against the lid speed). This is a check only for the bounded schemes, upwind and QUICK.

**Grids.** Re = 100 on 20/40/80 and Re = 1000 on 40/80. Each scheme also gets a grid-convergence study at Re = 100.

No universal best scheme is declared. The only gate is what the numerics guarantee: where discretisation error dominates (Re = 100 20×20, every Re = 1000 grid), the second-order-type schemes are closer to Ghia than upwind. At the Re = 100 benchmark floor, v can be further from Ghia than upwind's result. This is reported.

## CI split

| default suite (ctest) | explicit (`--gtest_also_run_disabled_tests`, Release) |
|---|---|
| `CavityGhiaValidation.Re100Grid20` (QUICK, Ghia) | `CavityGhiaValidation.DISABLED_Re100Grid40/80/160`, `DISABLED_Re1000Grid40/80/160` |
| `SchemeValidation.Upwind/Central/LinearUpwind/Quick`, `.AccuracyCostComparison` (20×20) | `CavityGhiaValidation.DISABLED_Re100Study`, `DISABLED_Re1000Study` |
| `PoiseuilleValidation.Profile/MassFlow/PressureDrop/ProductionGridConvergence` | `SchemeValidation.DISABLED_AccuracyCostComparisonFull` |
| `TurbulentChannelValidation.ReTau180/LogLaw` (SST medium) | `TurbulentChannelValidation.DISABLED_Study` |
| `BackwardFacingStep.Geometry/ReattachmentDetection` | `BackwardFacingStep.DISABLED_Converges/MassConservation/ReattachmentLength` |
| `SampleErrorNormsTest.*`, `ProductionValidationTest.*` (unit) | |

The explicit runs, their logs and runtimes are recorded in `results/p12-num-007/`. Reports are written to `results/validation/production/`.

To regenerate the evidence (from the repository root, Release build):

```
build/release/tests/integration/cavity/CFDCavityValidationTests --gtest_also_run_disabled_tests --gtest_filter='CavityGhiaValidation.DISABLED_Re*Study:SchemeValidation.DISABLED_*'
build/release/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests --gtest_also_run_disabled_tests --gtest_filter='TurbulentChannelValidation.DISABLED_Study'
build/release/tests/integration/backward_facing_step/CFDBackwardFacingStepTests --gtest_also_run_disabled_tests
```

## Limitations

- **No Rhie–Chow interpolation.** The collocated SIMPLE has none, so pressure fields carry an undamped odd-even mode. The estimators used here are immune to it, but the pressure field itself is not validated.
- **Uniform Cartesian grids only.** There is no wall or corner clustering. The finest grids are 160×160 (cavity) and 40 cells per H (step).
- **Turbulent channel reference.** The reference is DNS summary statistics plus the log law, not a point-by-point DNS profile. The inlet/outlet channel is not momentum-developed at L = 8H.
- **Ghia is itself a numerical benchmark.** At Re = 100 the higher-order schemes reach its accuracy floor.
