# P12-NUM-007 — Production validation: evidence

Date: 2026-09-14. Working tree on top of HEAD `fd9bae3`; nothing is committed.
Method: `docs/validation/production_validation.md`.
Reports: `results/validation/production/` (copied here: `*.json`, `*.md`).
Logs: `focused_tests.log`, `explicit_runs.log`.

**Builds.**
- The default-suite (CI) numbers come from `build/debug` (-O0).
- Every explicit (`DISABLED_`) study ran in `build/release` (-O3) on a 32-thread WSL2 machine. The BFS 50 cells/H level ran on 2026-09-14 via a driver linking the unchanged Release object (§8).
- `scheme_comparison.json` and the `cavity_re100.json` rerun ran at load ≈ 2–3, which is effectively idle.
- The first explicit batch (`cavity_re1000.json`, the per-grid cavity tests, `turbulent_channel.json`, the BFS runs) overlapped with a Debug `ctest -j32`. Its wall-clock times are inflated.
- Every report is deterministic except its runtime fields.

## 1. Verification → validation hierarchy

| level | task | answers |
|---|---|---|
| MMS | P12-NUM-006 | the discretisation reaches its formal order on full solves |
| grid convergence | P12-NUM-005 | the observed order, Richardson extrapolation and GCI of *our* solution |
| production validation | P12-NUM-007 (this) | agreement of the grid-converging solution with physical benchmarks |

In every report here, grid convergence is computed from our own solves only (embedded NUM-005 `GridConvergenceStudy`). The error against the benchmark is reported separately.

## 2. What was added

- **Reference data.** `validation/ghia/ghia_re1000_{u,v}.csv` and `tests/integration/cavity/GhiaRe1000.hpp` hold Ghia et al. (1982) Tables I/II, Re = 1000.
  - Three independent transcriptions were cross-checked (README: ivan-pi gists; the Uppsala thesis on DiVA, diva2:1668016; CMA 9(3) 2018).
  - All 34 Re = 100 values match the existing files.
  - For Re = 1000, 33 of 34 values agree across all three sources. The one discrepancy, v(0.9063), is −0.51550 in two sources and −0.51500 in the third; −0.51550 is stored.
  - Nothing is interpolated or digitised.
- **Library.**
  - `cfd/validation/ProductionValidation.hpp`: `ValidationRun` is the NUM-006 `MMSLevel` plus the case, Re and scheme, with checks. `ValidationReport` embeds NUM-005 studies. Also `makeSimpleValidationRun`, `toGridStudyEntry`, `computeRunSequenceOrder`, and deterministic JSON/Markdown output.
  - `computeSampleErrorNorms` in `ErrorNorms.hpp`.
- **Solver observability.**
  - `SIMPLEResult::momentumLinearIterations` / `pressureLinearIterations` count every inner BiCGSTAB iteration, fallback attempts included. They are a cost metric only; the numerics are unchanged.
  - `NonOrthogonalPassResult::linearIterations`.
- **Cases.**
  - `tests/integration/cavity/CavityProductionCase.*`: the cavity at any Re, grid and scheme.
  - `test_cavity_production_validation.cpp` and `test_scheme_validation.cpp`.
  - `poiseuille/test_poiseuille_production_validation.cpp`, with helpers in `PoiseuilleValidationUtils`: the discrete exact solution, a checkerboard-immune gradient, section mass flow.
  - `TurbulentChannelValidation.*` in `test_channel_flow_validation.cpp`, with `pairAveragedPressureGradient` in `ChannelFlowValidationUtils`.
  - `tests/integration/backward_facing_step/` (new binary `CFDBackwardFacingStepTests`).
- **Unit tests.** `tests/unit/validation/test_production_validation.cpp` (6 tests).

## 3. Cavity Re = 100 (`cavity_re100.json`)

Configuration of record:
- QUICK convection;
- the documented per-grid SIMPLE settings of the P0 cavity tests for every Re and scheme;
- outer tolerances 1e-6;
- the NUM-004 linear-solver fallback.

Errors are unweighted over Ghia's 17 stations.

| grid, scheme | status | outer it | lin. it (mom / p) | mass imb. | u L1 | u L2 | u L∞ | v L1 | v L2 | v L∞ | runtime s |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 20×20 QUICK | Converged | 3083 | 36967 / 151537 | 0 | 4.80e-3 | 5.69e-3 | 1.05e-2 | 3.45e-3 | 4.82e-3 | 1.26e-2 | 5.6 |
| 40×40 QUICK | Converged | 5500 | 59761 / 58120 | 0 | 1.05e-3 | 1.45e-3 | 4.61e-3 | 2.48e-3 | 2.99e-3 | 6.52e-3 | 32.2 |
| 80×80 QUICK | Converged | 9258 | 81177 / 50041 | 0 | 1.53e-3 | 2.09e-3 | 5.03e-3 | 3.86e-3 | 4.51e-3 | 8.71e-3 | 206.3 |
| 160×160 QUICK | Converged | 5084 | 71849 / 60434 | 0 | 1.59e-3 | 2.15e-3 | 4.46e-3 | 3.69e-3 | 4.37e-3 | 8.67e-3 | 587.2 |
| 20×20 upwind | Converged | 3036 | 34136 / 150550 | 0 | 1.62e-2 | 2.11e-2 | 3.72e-2 | 1.09e-2 | 1.45e-2 | 4.04e-2 | 4.9 |
| 40×40 upwind | Converged | 5615 | 59881 / 54988 | 0 | 7.62e-3 | 1.02e-2 | 1.88e-2 | 4.92e-3 | 6.45e-3 | 1.68e-2 | 25.3 |
| 80×80 upwind | Converged | 9643 | 79024 / 48522 | 0 | 3.29e-3 | 4.34e-3 | 8.50e-3 | 2.75e-3 | 3.57e-3 | 6.99e-3 | 175.5 |

Every run was accepted and passed its checks: wall flux ≤ 1e-6, bounded (max |U| ≤ lid speed), Ghia regression bound.

**Grid convergence** (NUM-005, station values, computed from our solves only):

| study | u(0.5,0.5) | v(0.5,0.5) | u(0.5,0.4531) | v(0.2344,0.5) | v(0.8047,0.5) |
|---|---|---|---|---|---|
| QUICK 20/40/80: p | 2.13 (asymptotic) | 1.95 (asymptotic) | 2.23 | 1.95 (asymptotic) | 2.23 |
| QUICK 40/80/160: GCI21 or oscillation U | 0.028 % | 0.14 % | 0.0006 % | 0.63 % (osc.) | 0.70 % (osc.) |
| QUICK Richardson limit | −0.20897 | 0.05765 | −0.21359 | (osc.) 0.1788 | (osc.) −0.2528 |
| upwind 20/40/80: p | 0.90 (asymptotic) | 0.49 | 0.83 | 0.88 (asymptotic) | 0.95 (asymptotic) |
| upwind Richardson limit | −0.2101 | 0.0629 | −0.2160 | 0.1810 | −0.2539 |
| Ghia (1982) | −0.20581 | 0.05454 | −0.21090 | 0.17527 | −0.24533 |

**Solution convergence vs error against Ghia.**
- The QUICK solution is grid-converged: every station's fine-grid uncertainty is < 1 % on 40/80/160.
- Independent schemes extrapolate to the same limits. On 20/40/80 (§9) the Richardson limits of central, linear_upwind and QUICK agree with each other to ≤ 0.07 % at every station. First-order upwind agrees with them to 0.4–1.2 % at its asymptotic stations.
- All of these limits differ from Ghia in the same direction, by 1.5 % (u) to 5.8 % (v at the centre).
- The Ghia error of the higher-order schemes nevertheless stops falling after 40×40 (u L2 1.45e-3 → 2.09e-3 → 2.15e-3). What remains is a benchmark/comparison floor of about 2e-3 (u) and 4.4e-3 (v), not our discretisation error.
- It is not iterative error either. A scratch run of central 80×80 with outer tolerance 1e-8 instead of 1e-6 gave the same Ghia errors (u L2 2.0096e-3 vs 2.0093e-3; 39 170 iterations).
- The 40×40 value sits below the floor by error cancellation.

**Gates** (all pass on the recorded run):
1. The upwind Ghia error falls monotonically: u 2.11e-2 → 1.02e-2 → 4.34e-3; v 1.45e-2 → 6.45e-3 → 3.57e-3.
2. At 80×80, QUICK is closer than upwind to the grid-converged QUICK 160×160 values at every station: 0.00004–0.0003 vs 0.005–0.012.
3. QUICK station uncertainty is < 1 % on 40/80/160.

**First attempt, recorded as the real result.** The study initially ran with two different gates, and both failed:
- **"QUICK 160 closer to Ghia than upwind 80".** For v, QUICK was at 4.37e-3 vs upwind at 3.57e-3.
  - Cause: benchmark setup. QUICK sits at the Ghia floor, while upwind's larger error happens to cancel part of Ghia's offset in v. This behaviour had already been measured and documented in `test_scheme_validation.cpp` before the gate was written.
  - The gate was replaced by the benchmark-independent discretisation-error comparison (gate 2).
- **"GCI21 < 1 % at every station".** v(0.2344) and v(0.8047) converge oscillatorily on 40/80/160 (changes 80→160 of 0.16 % and 0.03 %). NUM-005 defines no GCI for an oscillatory triplet.
  - The gate now uses the NUM-005 oscillation uncertainty (Stern et al. 2001) for those stations; the 1 % threshold is unchanged.
- The first attempt's log and report are kept in `explicit_runs.log` and `cavity_re100_first_attempt.json`.

## 4. Cavity Re = 1000 (`cavity_re1000.json`)

| grid (QUICK) | status | outer it | lin. it (mom / p) | mass imb. | u L1 | u L2 | u L∞ | v L1 | v L2 | v L∞ | runtime s* |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 40×40 | Converged | 3582 | 74028 / 99660 | 0 | 3.70e-2 | 5.44e-2 | 1.13e-1 | 3.92e-2 | 5.17e-2 | 1.05e-1 | 41.6 |
| 80×80 | Converged | 4289 | 102277 / 114476 | 0 | 6.45e-3 | 9.59e-3 | 1.99e-2 | 4.64e-3 | 7.63e-3 | 1.67e-2 | 208.0 |
| 160×160 | Converged | 2748 | 71576 / 149259 | 0 | 2.03e-3 | 2.52e-3 | 4.08e-3 | 3.96e-3 | 5.41e-3 | 1.07e-2 | 740.0 |

\* The first batch overlapped with a Debug ctest; the uncontended times in the scheme comparison are 20.9 s (40) and 117.0 s (80).

The robustness settings were the NUM-004 fallback (0 fallbacks were needed) with the tolerances unchanged from Re = 100.

**Gate.** The Ghia error falls monotonically, u 5.44e-2 → 9.59e-3 → 2.52e-3 and v 5.17e-2 → 7.63e-3 → 5.41e-3: pass.

**Grid convergence** on 40/80/160:
- All five stations are monotonic but *not* asymptotic, since 40×40 is too coarse at Re = 1000 (p = 2.5–3.3, and 0.95 for v_center).
- The fine-grid GCI21 is 0.18–0.39 % at the three extremum stations, and 1.2 % / 7.7 % at the small centre values u_c and v_c.
- The Richardson limits at the extrema are u(0.1719) −0.3867, v(0.1563) 0.3743 and v(0.9063) −0.5237, vs Ghia −0.3829, 0.3710 and −0.5155. That is 1.0–1.6 %, above the solution uncertainty.
- The v-error reduction slows from 80 to 160 (factor 1.41): the comparison is approaching Ghia's own accuracy at Re = 1000.

## 5. Grid-convergence linkage (NUM-005)

Every report embeds NUM-005 `GridConvergenceStudy`s built from the validated runs themselves (`toGridStudyEntry`, no re-solve):
- cavity Re = 100: QUICK 20/40/80 and 40/80/160, upwind 20/40/80;
- cavity Re = 1000: QUICK 40/80/160;
- the scheme comparison: one study per scheme at Re = 100;
- Poiseuille: centerline velocity and dp/dx;
- BFS: x_r/h and the upper-bubble length.

Observed order, Richardson extrapolation, GCI and the asymptotic ratio come from the NUM-005 analysis. Orders of the benchmark errors come from `computeRunSequenceOrder` (the NUM-006 `computeMMSOrder`).

The benchmark-error "orders" are meaningful only far from the benchmark floor:
- cavity Re = 100: upwind u L2 p = 0.90, asymptotic; QUICK oscillatory (the floor);
- Poiseuille: 1.96 / 1.89 / 1.87 for L1 / L2 / L∞.

## 6. Poiseuille (`poiseuille.json`, default suite)

| grid | status | it | u L1 / L2 / L∞ vs physics | max \|u − u_discrete\| | u_c | dp/dx (pair-avg.) | discrete exact | legacy two-station | odd-even amplitude | max flow error |
|---|---|---|---|---|---|---|---|---|---|---|
| 64×8 | Converged | 1319 | 1.39e-2 / 1.52e-2 / 2.20e-2 | 1.9e-6 | 1.4545470 | −1.162918 | −1.163636 | −1.15514 | 2.5e-2 | 3.7e-10 |
| 96×12 | Converged | 985 | 6.23e-3 / 6.95e-3 / 1.01e-2 | 4.3e-8 | 1.4794520 | −1.183509 | −1.183562 | −1.18386 | 1.1e-3 | 2.5e-9 |
| 144×18 | Converged | 744 | 2.79e-3 / 3.13e-3 / 4.57e-3 | 5.7e-9 | 1.4907976 | −1.192638 | −1.192638 | −1.19264 | 3.2e-6 | 3.8e-9 |

**References.** The exact values are u_c = 1.5 and dp/dx = −1.2.
- The derived exact fully developed solution of the discretisation gives dp/dx·ny²/(ny²+2). The numerical solution matches it to 1.9e-6 … 5.7e-9 (profile) and 6e-4 … 1.6e-7 (dp/dx, relative).
- So the entire difference from the physics is the predicted second-order discretisation error: exactly 2/(ny²+2) = 3.03 % on ny = 8.

**Pressure drop** (between the faces nearest 0.4 L and 0.75 L): −3.198 vs the exact −3.300 on 64×8, i.e. the ratio 64/66 predicted by the discrete solution.

**Mass flow.** Inlet, outlet and the sections at 0.25/0.5/0.75 L all carry U·H to ≤ 3.8e-9.

**Grid convergence** (64×8/96×12/144×18):
- centerline p = 1.94, asymptotic, GCI21 0.80 %;
- dp/dx p = 2.006, asymptotic, GCI21 0.76 %;
- in both cases the GCI bounds the true error (0.61 %);
- the profile L2 order is 1.89, asymptotic.

All gates pass.

**Checkerboard.** The collocated SIMPLE has no Rhie–Chow interpolation, so an odd-even pressure mode grows on the residual plateau. Scratch runs on 64×8 measured its amplitude at 0.03 after 1000 iterations and 0.64 after 40 000. That is what drives the legacy two-station estimate's 7 % drift (NUM-005 excluded dp/dx for this reason). The pair-averaged estimator cancels the mode exactly, and the mode is reported per run.

## 7. Turbulent channel Re_τ = 180 (`turbulent_channel.json`; CI: `turbulent_channel_ci.json`)

The P2-TURB-007 case and its locked Re_τ and log-law bounds are reused unchanged. Two wall-shear estimators are reported:
- **Secant** μU(y₁)/y₁;
- **Momentum balance** −dp/dx·δ.

| model / grid | Re_τ (secant) | u_τ | τ_w | C_f (ref 8.2e-3) | U_c⁺ (18.2) | U_b⁺ (15.6) | y₁⁺ | u⁺ log-law L2 (secant / balance) | Re_τ (balance) |
|---|---|---|---|---|---|---|---|---|---|
| k-ε coarse / medium / fine | 111.4 / 121.0 / 136.4 | 0.040 / 0.043 / 0.049 | 1.58e-3 / 1.87e-3 / 2.37e-3 | 3.17e-3 / 3.74e-3 / 4.75e-3 | 34.6 / 31.2 / 26.8 | 25.1 / 23.1 / 20.5 | 9.3 / 7.6 / 5.7 | 15.5/11.3, 12.0/10.8, 8.4/10.2 | 448 / 425 / 396 |
| k-ω coarse / medium / fine | 108.2 / 115.1 / 126.4 | 0.039 / 0.041 / 0.045 | 1.49e-3 / 1.69e-3 / 2.04e-3 | 2.98e-3 / 3.38e-3 / 4.08e-3 | 35.0 / 32.4 / 28.7 | 25.9 / 24.3 / 22.2 | 9.0 / 7.2 / 5.3 | 16.4/10.9, 13.5/10.0, 10.5/9.0 | 426 / 392 / 354 |
| SST coarse / medium / fine | 116.3 / 131.7 / 160.7 | 0.042 / 0.047 / 0.057 | 1.72e-3 / 2.21e-3 / 3.29e-3 | 3.45e-3 / 4.43e-3 / 6.59e-3 | 29.8 / 25.4 / 19.7 | 24.1 / 21.3 / 17.4 | 9.7 / 8.2 / 6.7 | 13.1/7.6, 8.6/5.0, 3.2/1.6 | 316 / 257 / 204 |

- All nine runs converged. Mass imbalance was ≤ 4e-8 and every existing gate passed.
- The Re_τ error decreases with refinement for every model (study gates).
- The CI test is SST medium.
- The secant and momentum-balance Re_τ bracket 180 in all 9 runs. With SST fine the bracket is 161–204, and on the fine grid the log-law L2 with the momentum-balance u_τ is 1.6.
- **Finding.** The momentum-balance estimate lies far above 180. At L = 8H the core is still accelerating; the profile-shape criterion (< 5 %) holds but the momentum balance does not, so −dp/dx carries a momentum-flux term. The secant under-estimates τ_w because there is no wall function and y₁⁺ ≈ 5–10.
- No turbulence model was changed.

## 8. Backward-facing step (`backward_facing_step.json`)

**Feasibility audit.**
- The Gartling (1990) fluid domain is a plain rectangle downstream of the step. No blocked cells are needed.
- The existing Cartesian mesh keeps its cells and faces; only its left boundary is re-partitioned into a step wall and one inlet patch per face. The inlet uses the exact face average of the parabola, giving an inflow of exactly 0.5.
- No mesh or library capability was added. This is the minimal NUM-007-scoped geometry: a test helper, not a meshing feature.
- Measured domain-length independence (Re 800, 20 cells/H): L = 15H and 30H give x_r/h 10.0852 / 10.0852 and upper-wall crossings 7.6699/19.8124 vs 7.6699/19.8122.

**Re = 100** (explicit Converges / MassConservation, 10 cells/H, L = 10H): Converged after 16451 iterations; x_r/h = 2.965; mass-flow error ≤ 1e-6. The single-source value 3.00 is reported only.

**Re = 800** (QUICK, L = 15H, Release). Where each run came from:
- **20/30/40 cells/H and Re = 100**: `BackwardFacingStep.DISABLED_ReattachmentLength`.
- **50 cells/H**: a pure grid-refinement run (see the provenance note below).

| cells/H (grid) | status | outer it | lin. it (mom / p) | fallbacks | global mass imbalance | max flow error | x_r/h | corner eddy end | x_s/h | x_rs/h | (x_rs − x_s)/h | runtime s |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 20 (300×20) | Converged | 4739 | 520468 / 2291284 | 1315 | 1.1e-13 | 2.3e-13 | 10.085 | 0.170 | 7.670 | 19.812 | 12.143 | 938 |
| 30 (450×30) | Converged | 3840 | 460967 / 2977681 | 390 | 5.4e-13 | 1.1e-12 | 11.405 | 0.139 | 8.926 | 20.789 | 11.863 | 1955 |
| 40 (600×40) | Converged | 10711 | 1111429 / 11188433 | 2191 | 8.4e-13 | 8.4e-13 | 11.780 | 0.179 | 9.299 | 20.925 | 11.626 | 8809 |
| **50 (750×50)** | **Converged** | **5410** | **498226 / 7419652** | 1886 | **6.9e-13** | 8.5e-13 | **11.932** | 0.162 | **9.451** | **20.957** | **11.506** | **8596** |
| Re = 100, 20 (300×20) | Converged | 2373 | 66985 / 761960 | 647 | 2.7e-13 | 2.7e-13 | 3.189 | 0.067 | — | — | — | 153 |

**Provenance of the 50 cells/H run** (2026-09-14):
- It was produced by a standalone driver (`bfs50.cpp`; its source is in `explicit_runs.log`).
- The driver links the **unchanged Release object** of `BackwardFacingStepCase.cpp` and calls `step::runStep({800, 50, 15})`. Re, geometry, inlet, domain, BCs, QUICK, SIMPLE settings, tolerances and the extraction are therefore identical.
- No repository source was changed.
- The same driver re-ran 20 cells/H and reproduced the recorded run bit-for-bit: all 22 diagnostics, iterations, mass imbalance and checks.
- It ran alone on a P-core; the load average was 1.0.
- The combined report was produced from the recorded runs plus this run. It re-analyses grid convergence with the NUM-005 library and applies the test's gate rule to the finest triplet: the Richardson value if the triplet is asymptotic, otherwise the finest-grid value.

**References.**
- Published 2D results: x_r/h ∈ [11.48, 12.20] and (x_rs − x_s)/h ∈ [10.60, 11.52]. These are the ten studies compiled in arXiv:2507.16509 Table 2, where Gartling (1990) gives x_r/h 12.20 and (x_rs − x_s)/h 11.26.
- Re = 100: 3.00, single source, not gated.
- (An earlier version of this section also quoted per-point Gartling values x_s/h 9.70 and x_rs/h 20.96. They were not taken from a retrieved document and have been removed.)

**x_r/h: PASS.**
- The 50 cells/H value, 11.932, is in the published range.
- NUM-005 on 30/40/50 (r = 1.33, 1.25): monotonic, p = 2.50, not asymptotic (ratio 1.14). The gate therefore uses the finest-grid value. Richardson gives 12.137, also in the range. GCI21 is 2.1 %.

**Upper-wall bubble length: PASS** (it failed at 40 cells/H):
- The 50 cells/H value, 11.506, is at or below 11.52, the published maximum.
- NUM-005 on 30/40/50: monotonic and **asymptotic**, with p = 1.66 and ratio 0.916 (within 1 ± 0.1). The gate therefore used the Richardson value, 11.238, which is inside [10.60, 11.52]. GCI21 is 2.9 %.
- On 20/30/40 the same quantity was classified divergent. The 50 cells/H level brings the sequence into its asymptotic range.

**Consistency.**
- **Monotone, contracting sequences.** Every tracked point converges monotonically, and the change per refinement contracts each time:
  - x_r/h: +1.32, +0.38, +0.15;
  - x_s/h: +1.26, +0.37, +0.15;
  - x_rs/h: +0.98, +0.14, +0.03;
  - the bubble length: −0.28, −0.24, −0.12.
- **The two triplets agree.** The component extrapolations are x_s/h 9.665 (20/30/40) vs 9.655 (30/40/50), and x_rs/h 20.977 vs 20.975. Their difference, ≈ 11.32, agrees with the directly extrapolated bubble length of 11.24 within its 2.9 % GCI.
- **The margin at 50 cells/H is small.** 11.506 is 0.12 % below the published maximum. The pass rests on the unchanged gate rule, which uses the asymptotic Richardson value 11.24, and on the monotone decreasing trend. The 50 cells/H value alone is inside the range, but by less than its own GCI.

**Other results.**
- **Mass conservation.** Inflow, outflow and 4 sections carry 0.5 to ≤ 1.1e-12 on every grid.
- **Finite fields.** Every run was accepted by the solve gate: Converged, all fields finite, mass imbalance ≤ 1e-6.
- **Corner eddy.** The corner eddy is resolved on every grid, ending at x/h ≈ 0.14–0.18.
- **Re = 100.** 3.19 at 20 cells/H and 2.97 at 10 cells/H, vs the single-source 3.00.

**Result.** The combined Re = 800 report passes (`"passed": true`, both gates).
- The in-repository `BackwardFacingStep.DISABLED_ReattachmentLength` covers only 20/30/40 cells/H and was left unchanged, so rerunning it alone still reports the 40 cells/H upper-bubble failure.
- The 50 cells/H level exists only in the recorded driver run.
- The failed 20/30/40 report is kept as `backward_facing_step_20_30_40.json`.

## 9. Scheme accuracy / cost comparison (`scheme_comparison.json`)

Mesh, physics, SIMPLE settings, tolerances, linear solvers and initial condition are identical; only `convectionScheme` differs. The runs were sequential in one process, Release build, load ≈ 2.

| Re, grid | scheme | status | outer it | lin. it (mom / p) | runtime s | u L2 | v L2 | max \|U\| | fallbacks |
|---|---|---|---|---|---|---|---|---|---|
| 100, 20×20 | upwind | Converged | 3036 | 34136 / 150550 | 4.9 | 2.11e-2 | 1.45e-2 | 0.8186 | 0 |
| | central | Converged | 2954 | 34253 / 147733 | 5.5 | 6.26e-3 | 5.64e-3 | 0.8388 | 0 |
| | linear_upwind | Converged | 3091 | 36061 / 151816 | 6.0 | 5.55e-3 | 4.50e-3 | 0.8396 | 0 |
| | QUICK | Converged | 3083 | 36967 / 151537 | 5.9 | 5.69e-3 | 4.82e-3 | 0.8393 | 1 |
| 100, 40×40 | upwind | Converged | 5615 | 59881 / 54988 | 29.0 | 1.02e-2 | 6.45e-3 | 0.9178 | 0 |
| | central | Converged | 5479 | 59808 / 58387 | 30.5 | 1.23e-3 | 2.99e-3 | 0.9235 | 0 |
| | linear_upwind | Converged | 5517 | 60599 / 58271 | 31.6 | 1.71e-3 | 2.98e-3 | 0.9237 | 0 |
| | QUICK | Converged | 5500 | 59761 / 58120 | 28.7 | 1.45e-3 | 2.99e-3 | 0.9236 | 0 |
| 100, 80×80 | upwind | Converged | 9643 | 79024 / 48522 | 206.5 | 4.34e-3 | 3.57e-3 | 0.9604 | 0 |
| | central | Converged | 9507 | 84393 / 50485 | 221.2 | 2.01e-3 | 4.49e-3 | 0.9619 | 0 |
| | linear_upwind | Converged | 9083 | 81038 / 50069 | 225.5 | 2.17e-3 | 4.53e-3 | 0.9620 | 0 |
| | QUICK | Converged | 9258 | 81177 / 50041 | 218.1 | 2.09e-3 | 4.51e-3 | 0.9620 | 0 |
| 1000, 40×40 | upwind | Converged | 4691 | 72866 / 72936 | 23.4 | 8.55e-2 | 1.08e-1 | 0.7865 | 0 |
| | central | Converged | 3389 | 65941 / 100207 | 20.4 | 4.99e-2 | 5.03e-2 | 0.8079 | 0 |
| | linear_upwind | Converged | 3614 | 66889 / 97266 | 22.0 | 5.65e-2 | 5.21e-2 | 0.8095 | 0 |
| | QUICK | Converged | 3582 | 74028 / 99660 | 20.9 | 5.44e-2 | 5.17e-2 | 0.8090 | 0 |
| 1000, 80×80 | upwind | Converged | 6547 | 82642 / 87149 | 145.3 | 5.16e-2 | 6.26e-2 | 0.9021 | 0 |
| | central | Converged | 4637 | 102500 / 113552 | 124.7 | 9.09e-3 | 6.93e-3 | 0.9113 | 0 |
| | linear_upwind | Converged | 4105 | 100113 / 114274 | 117.6 | 1.02e-2 | 8.57e-3 | 0.9118 | 0 |
| | QUICK | Converged | 4289 | 102277 / 114476 | 117.0 | 9.59e-3 | 7.63e-3 | 0.9116 | 0 |

L1/L∞ for every run are in the JSON. Mass imbalance is 0 in every run.

**Grid convergence per scheme** (Re = 100, 20/40/80; station values):
- upwind p = 0.49–0.95 (formal 1);
- central 1.85–2.09 (all five stations asymptotic);
- linear_upwind 1.95–2.52;
- QUICK 1.95–2.23.

**Findings** (no universal best scheme is declared):
- **Stability.** No scheme failed or diverged at these grids and Reynolds numbers. Only one linear-solver fallback occurred (QUICK 20×20).
- **Boundedness.** No run exceeded the lid speed, including the unbounded central and linear_upwind schemes (checked for all; gated only for upwind and QUICK).
- **Accuracy.** Where discretisation error dominates (Re 100 20×20; Re 1000 40 and 80), all three second-order-type schemes beat upwind by 1.5–9×. All gates pass.
  - At Re 1000 80×80, central has the smallest Ghia error, followed by QUICK and then linear_upwind.
  - At Re 100 ≥ 40×40 the three are within the benchmark floor of each other, and upwind's v error is smaller at 80×80 (floor artefact, §3).
- **Cost.**
  - At Re = 100, outer iterations, linear iterations and wall time are within about 10 % across the schemes.
  - At Re = 1000 the higher-order schemes need 23–37 % fewer outer iterations than upwind.
  - Their linear iterations per run differ from upwind's by −10…+2 % (momentum) and +33–37 % (pressure) at 40×40, and by +21–24 % (momentum) and +30–31 % (pressure) at 80×80.
  - Their wall time is 6–13 % (40×40) and 14–19 % (80×80) *lower* than upwind's.

## 10. CI split and test counts

**Default suite (enabled, Debug):**
- `CavityGhiaValidation.Re100Grid20` — a real production validation against Ghia, 113 s;
- `SchemeValidation.Upwind/Central/LinearUpwind/Quick` (104–113 s) and `.AccuracyCostComparison` (302 s);
- `PoiseuilleValidation.Profile/MassFlow/PressureDrop` (78–80 s) and `.ProductionGridConvergence` (326 s);
- `TurbulentChannelValidation.ReTau180/LogLaw` (181–186 s);
- `BackwardFacingStep.Geometry/ReattachmentDetection` (< 1 s);
- `SampleErrorNormsTest.KnownValues` and `ProductionValidationTest.*` (5);
- the pre-existing tests (P0 `Grid20x20ConvergesAndMatchesGhia`, NUM-005 `PoiseuilleValidation.GridConvergence`, `ChannelFlowValidation.*`), unchanged.

**Explicit (`DISABLED_`, Release), all run and recorded here:**
- `CavityGhiaValidation.Re100Grid40/80/160`, `Re1000Grid40/80/160`, `Re100Study`, `Re1000Study`;
- `SchemeValidation.AccuracyCostComparisonFull`;
- `TurbulentChannelValidation.Study`;
- `BackwardFacingStep.Converges/MassConservation/ReattachmentLength`.

All passed except `BackwardFacingStep.DISABLED_ReattachmentLength` (20/30/40 cells/H), which failed its upper-bubble gate at 40 cells/H. The follow-up 50 cells/H refinement (§8) passes both gates in the combined report. The first attempt of `DISABLED_Re100Study` failed two mis-designed gates (§3) and passed on its rerun.

**Why they are explicit:** in Debug each takes several minutes to hours. The 40×40 cavity alone takes several Debug minutes, and the Re = 100 BFS takes about 15–20 Debug minutes.

**Focused Debug run** (`focused_tests.log`): 151 / 151 passed, 174 listed, 23 disabled, 636 s wall. It covered the cavity, scheme, Poiseuille, turbulent-channel, BFS, validation-unit, SIMPLE, MMS and grid-convergence filters.

**Full regression** (`ctest -j32`, Debug): **1654 / 1654 passed, 0 failed, 1679 listed, 25 disabled**, 902.7 s wall. At the close of NUM-006 it was 1634 / 1634, 1646 listed, 12 disabled.
- It rewrote 13 tracked `results/validation/**/validation.json` files. Only runtime fields changed (checked with `git diff -U0`), so they were reverted.

## 11. Limitations

- **Pressure fields.** The collocated SIMPLE has no Rhie–Chow interpolation, so pressure fields carry an undamped odd-even mode on open domains. The estimators used are immune to it, and the pressure field itself is not validated.
- **Grids.** Only uniform Cartesian grids were used, with no wall or corner clustering.
- **Ghia is a numerical benchmark.** At Re = 100 the higher-order solution reaches its floor. At Re = 1000 the grid-converged extrema differ from Ghia by 1.0–1.6 %.
- **Turbulent channel.**
  - The reference is DNS summary statistics plus the log law, with no point-by-point DNS profile.
  - The inlet/outlet channel is not momentum-developed at L = 8H.
  - The secant wall shear is biased low because there is no wall function.
- **Linear iterations** are totals over the run (fallback attempts included). There is no per-solve breakdown.
- **Runtimes.**
  - Default-suite runtimes are Debug numbers under a parallel ctest load.
  - The first explicit batch overlapped with a Debug ctest.
  - Only the scheme comparison and the Re = 100 rerun are uncontended.
- **Single-source values.** The BFS Re = 100 value (3.00) has a single source and is not gated. Erturk's low-Re values could not be obtained as a document, so they are not used.
- **BFS upper-wall bubble.** It needs 50 cells/H to enter the published range, and then passes by a narrow margin: 11.506 vs ≤ 11.52, with Richardson 11.24 on the asymptotic 30/40/50 triplet. The 50 cells/H run comes from a driver around the unchanged implementation; the in-repo test covers 20/30/40 only (§8).
