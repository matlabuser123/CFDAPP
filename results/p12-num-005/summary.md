# P12-NUM-005 — Grid Convergence

**Status: COMPLETE — `[x]` in `TODO.md`** (2026-09-13).
- Every acceptance gate in §13 passes with the measured evidence below.
- §12 lists the remaining limitations; none of them is a failed gate.
- Not committed.

Environment: WSL2 Ubuntu, GCC, `build/debug` (Debug), 2026-09-13. Every
number below was printed by a committed test (named alongside it), by
`ctest`, or by one of the scratch evidence drivers. The drivers link the
same library and are not committed: `pois.cpp` (the Poiseuille four-grid
and iterative-sensitivity runs), `triplets.cpp` and `cavtrip.cpp` (library
analysis of the measured scratch sequences) and `validate_reports.cpp`
(report validation). Nothing is estimated. Method documentation:
`docs/validation/grid_convergence.md`.

---

## 1. Starting baseline

- Tree: post-NUM-004 (uncommitted NUM-001..004 on top of `fd9bae3`). Full
  regression **1579/1579 passed**, 0 failed, `ctest -N` = 1592 (13
  disabled), 684.5 s.
- What existed for grid convergence: three `DISABLED_` tests
  (`PoiseuilleValidation.GridRefinementReducesVelocityError`,
  `CavityGhiaValidation.GridRefinementReducesGhiaError`,
  `NaturalConvectionValidation.GridRefinementReducesNusseltError`). They
  asserted only that the error against a reference did not grow with
  refinement. There was no observed order, no Richardson extrapolation, no
  GCI, no report, and none of them ran in CI. No GCI/Richardson code
  existed anywhere in the repository.

## 2. API (new, `cfd::validation`)

`include/cfd/validation/GridConvergence.hpp` — the analysis:

- `representativeGridSize(measure, cells, dim = 2)` gives h = (A/N)^(1/dim).
- `analyzeGridConvergence(fine, medium, coarse, options) -> GridConvergenceResult`
  takes `GridLevel{h, value}` triples.
- `solveObservedOrder(r21, r32, ratio, maxOrder)` is the order solver on its
  own.
- `GridConvergenceOptions` (all configurable per quantity):

  | option | default |
  |---|---|
  | `safetyFactor` | 1.25 |
  | `formalOrder` | unset |
  | `asymptoticTolerance` | 0.1 |
  | `absoluteNoise` | 0 |
  | `relativeNoise` | 1e-12 |
  | `minimumRefinementRatio` | 1.1 |
  | `maximumOrder` | 20 |
  | `gridIndependenceThreshold` | unset |

- The result carries:
  - status, convergence class, diagnostic, warnings;
  - r21, r32, ε21, ε32, R;
  - p, the order-solver iteration count;
  - φ_ext21, φ_ext32;
  - e_a and e_ext for both pairs;
  - U21, U32 (absolute), GCI21, GCI32;
  - asymptotic ratio, p/pf;
  - the oscillation bound;
  - grid independence and its reason.

  Every optional value is unset rather than NaN.

`include/cfd/validation/GridConvergenceStudy.hpp` — the automated study:

- `runGridConvergenceStudy(name, description, {coarse, medium, fine}, solve, quantities)`
  runs coarse → medium → fine through a caller-supplied solve function. It
  times each grid, stops at the first rejected grid, and analyses every
  `QuantitySpec`. A spec carries an optional analytical or benchmark
  reference, which is reported separately and never enters the analysis.
- `assessSimpleSolve(result, massTol)` is the solver-status gate: the status
  must be `Converged`, every velocity, pressure and face-flux value finite,
  and the global mass imbalance ≤ tol.
- `analyzeGridStudy(...)` analyses precomputed grids.
- Reports:
  - `gridConvergenceReportJson`, `gridConvergenceReportMarkdown` and
    `writeGridConvergenceReport` produce them (deterministic);
  - `validateGridConvergenceReport` and `validateGridConvergenceReportFile`
    check them (schema plus consistency).

Build: `src/validation/GridConvergence.cpp` and
`src/validation/GridConvergenceStudy.cpp` are added to `cfdcore`. The new
test target `CFDValidationUnitTests` (label `unit`) is in
`tests/unit/validation/`.

## 3. Method (as implemented; full derivation in the docs)

- **Convention:** 1 = fine, 2 = medium, 3 = coarse; r21 = h2/h1,
  r32 = h3/h2; ε21 = φ2 − φ1, ε32 = φ3 − φ2. Relative quantities are
  fractions.
- **Observed order:** the p > 0 solving ε32/ε21 = r21^p (r32^p − 1)/(r21^p − 1),
  i.e. the exact relation for φ = φ_exact + C h^p.
  - F(p) = ln[r21^p (r32^p − 1)/(r21^p − 1)] is strictly increasing
    (proof in the source comment). Bisection on [0, maximumOrder] runs to
    1e-14 or 200 iterations; `log(expm1)` keeps it stable for small and
    large p.
  - A positive root exists iff ε32/ε21 > ln r32 / ln r21 (the p → 0
    limit); otherwise the sequence is **divergent**. A root above
    `maximumOrder` gives **insufficient separation**.
- **Unequal ratios:** solved exactly by the same equation, with no
  equal-ratio approximation. `UnequalRefinementRatios` shows that the naive
  formula is wrong by > 0.2 in p at r21 = 1.3, r32 = 1.54.
- **Richardson:** φ_ext21 = φ1 − ε21/(r21^p − 1) and φ_ext32 = φ2 − ε32/(r32^p − 1).
- **GCI:**
  - U21 = Fs|ε21|/(r21^p − 1) and GCI21 = U21/|φ1|; the same for the
    32 pair.
  - Fs = 1.25: the Roache/Celik three-grid value with an observed order.
  - Relative metrics are unset when the denominator is within the noise
    level; U stays available.
- **Asymptotic range:**
  - ratio = U32/(r21^pf U21), with U evaluated at the **formal** order pf;
    in band when |ratio − 1| ≤ 0.1.
  - Using the observed p instead makes the ratio identically 1, a
    tautology; the library refuses that. Without pf the status is
    `monotonic_asymptotic_range_unknown`.
- **Classification:**
  - statuses: asymptotic / monotonic_not_asymptotic /
    monotonic_asymptotic_range_unknown / oscillatory (bound
    ½(max − min), no p or GCI) / divergent / insufficient_separation /
    invalid.
  - Noise: |ε| ≤ max(absoluteNoise, relativeNoise·max|φ|) counts as zero.
  - Ratio r < 1.1 → invalid ("nearly identical grids"); 1.1 ≤ r < 1.3 →
    warning.
- **Grid independence:** status `asymptotic` **and** GCI21 ≤ a threshold
  the study configures and justifies. Unconfigured → false, with the
  reason. The reason is always reported.

## 4. Synthetic verification (`CFDValidationUnitTests`, 22 tests)

Each exact sequence is φ = φ_exact + C h^p, so the expected values follow
from the closed form.

| test | input | recovered |
|---|---|---|
| FirstOrderSynthetic | p = 1, r = 2 | p = 1 ± 1e-10, φ_ext = 2 ± 1e-12 |
| SecondOrderSynthetic | p = 2, r = 2 | p = 2 ± 1e-10, φ_ext21 = φ_ext32 = 1 ± 1e-12, R = 0.25 |
| ThirdOrderSynthetic | p = 3, r = 1.5 | p = 3 ± 1e-9, φ_ext = 5 ± 1e-12 |
| UnequalRefinementRatios | r21 = 1.3, r32 = 1.538, p ∈ {1, 1.37, 2, 2.8} | p ± 1e-9, φ_ext ± 1e-11; the naive formula is off by > 0.2 |
| RichardsonExactRecovery | p ∈ {1, 1.5, 2, 3} × φ_exact ∈ {0, 1, −123.456, 1e6} | φ_ext = φ_exact to 1e-12 (relative); always closer than φ1 |
| GCIKnownCase (hand) | φ = 1 + h² on h = 1, 2, 4 | U21 1.25, GCI21 0.625, U32 5, GCI32 1.0, e_a21 1.5, e_ext21 1.0; Fs = 3 scales exactly |
| GCIKnownCase (published) | Celik et al. 2008 Table 1: 6.063 / 5.972 / 5.863, r21 1.5, r32 1.333 | p 1.53, φ_ext 6.1685, e_a21 1.5 %, e_ext21 1.7 %, GCI21 2.2 % (the paper's rounding) |
| AsymptoticRatio | p = pf = 2, equal and unequal ratios | ratio 1 ± 1e-12 (1e-9 unequal) → asymptotic |
| AsymptoticRatio | p = 1 vs pf = 2, r = 2 | ratio 0.5 → not asymptotic |
| AsymptoticRatio | p = 2.1 | ratio 2^0.1 = 1.072: inside the ±0.1 band, outside ±0.05 (band configurable) |
| AsymptoticRatio | no pf | range unknown, no ratio |
| OscillatorySequenceDetected | 1.0 / 1.1 / 0.95 | oscillatory, R < 0, no p / φ_ext / GCI, bound 0.075 |
| DivergingSequenceDetected | growing ε; unequal r with ε32/ε21 = 1.2 < ln r32/ln r21 = 3.1; flat coarse pair | divergent, no p |
| ZeroDifferenceHandled | identical values; ε21 = 0 only; differences inside the configured noise; ε21 = 1e-9 | insufficient separation; the same tiny sequence without noise → p = 2; p > 20 not trusted |
| ZeroSolutionQuantityHasNoRelativeMetrics | φ1 = 0 | no GCI21 / e_a21, U21 available, nothing NaN |
| InvalidGridSpacingRejected | h ≤ 0, wrong order, equal h, r = 1.05 | invalid ("nearly identical"); r = 1.2 → warning |
| NonFiniteInputRejected | NaN/inf in φ or h; pf < 0 | invalid, never grid-independent, nothing NaN |
| GridIndependenceCriterion | unconfigured / GCI ≤ threshold / too strict / not asymptotic | false (reason) / true / false / false |
| ObservedOrderSolverMatchesClosedFormForEqualRatios | ratio ∈ {1.1, …, 100} | ln(ratio)/ln 2 ± 1e-12; ≤ 200 iterations |
| RepresentativeGridSize | | 64×8 on 8×1 → 0.125; not 1/nx on 2×1; bad input throws |
| GridConvergenceStudyTest.* | runner order, early stop, missing quantity, SIMPLE gate (8 non-converged statuses, NaN velocity, inf flux, mass leak), DeterministicReport, ReportSchemaValidation | all pass |

## 5. Poiseuille flow — `PoiseuilleValidation.GridConvergence` (ENABLED in CI)

**Setup:**
- Planar channel L = 8, H = 1, Re = 10, SIMPLE.
- Grids 64×8 / 96×12 / 144×18 (r21 = r32 = 1.5, square cells, h = H/ny).
- Per-grid settings: outer ≤ 8000, pressure inner 5000 at 1e-10/1e-8,
  u-residual gate 2e-5, mass gate 5e-4.
- Jacobi-preconditioned BiCGSTAB with the NUM-004 fallback.

**Quantity choice (measured, `pois.cpp`):**
- Centerline velocity u(0.75 L, H/2), exact **1.5** (analytical). Formal
  order 2: central diffusion, and the upwind convection vanishes in fully
  developed flow.
- Iterative sensitivity of u_c, tol-2e-5 stop vs a 40000-iteration run:
  - 32×4: 1.333417018 vs 1.333333419 (Δ 8.4e-5);
  - 64×8: 1.454547016 vs 1.454548172 (Δ **1.2e-6**).

  Both are far below the grid differences (~1e-2), hence
  `absoluteNoise = 1e-5`.
- **dp/dx is rejected as a study quantity:** the same comparison moves it
  by 7 % on 64×8 (−1.155 → −1.072). The pressure level drifts on this
  case's residual plateau, so dp/dx is not iteratively converged at the
  validation gates.
- The preconditioner does not change the answer (96×12 and 144×18,
  Jacobi vs none: u_c differs by 5e-9 and 3e-9). Unpreconditioned 144×18
  needed 70 BiCGSTAB fallbacks; with Jacobi it needed 1.

**Grids:**

| grid | nx × ny | cells | h | runtime (s) run 1 / focused | iterations | u_c | error vs 1.5 | relative error |
|---|---|---|---|---|---|---|---|---|
| coarse | 64 × 8 | 512 | 0.125 | 25.1 / 23.4 | 1319 | 1.4545470 | −0.04545 | 0.0303 |
| medium | 96 × 12 | 1152 | 0.08333 | 49.9 / 44.2 | 985 | 1.4794520 | −0.02055 | 0.0137 |
| fine | 144 × 18 | 2592 | 0.05556 | 94.5 / 88.4 | 744 | 1.4907976 | −0.009202 | 0.006135 |
| Richardson | | | | | | **1.5002906** | **+0.00029** | 0.00019 |

The report files are from the focused run. The three runs (run 1, the full
ctest and focused) are bit-identical apart from runtimes.

**Analysis:**

| quantity | r21 | r32 | p (pf) | φ_ext | U21 | GCI21 | GCI32 | asymptotic ratio | status | grid independent (1 %) |
|---|---|---|---|---|---|---|---|---|---|---|
| centerline_velocity | 1.5 | 1.5 | **1.939** (2) | 1.5002906 | 0.011866 | **0.00796** | 0.01761 | **0.976** | asymptotic | **yes** |
| velocity_profile_l2_error (limit 0) | 1.5 | 1.5 | 1.785 (2) | −0.00029 | 0.004077 | (1.373: relative, meaningless for a zero limit) | 1.307 | 0.917 | asymptotic | n/a (no threshold) |

**Checks that pass:**
- Richardson removes 97 % of the fine-grid error: 2.9e-4 vs 9.2e-3.
- GCI21 bounds the **true** relative error: 0.00614 ≤ 0.00796.
- GCI21 < GCI32.
- The error decreases monotonically.
- The extrapolated L2 norm is 0.00029 against a fine-grid norm of 0.00297.

**Why these grids, not coarser ones** (library on the measured 4-grid
sequence 32×4 / 64×8 / 128×16 / 256×32, `triplets.cpp`):

| triplet | p | ratio | status | φ_ext (error) | GCI21 |
|---|---|---|---|---|---|
| 32/64/128 | 1.840 | 0.895 | monotonic_not_asymptotic | 1.5014771 (1.5e-3) | 0.01101 |
| 64/128/256 | 1.958 | 0.972 | asymptotic | 1.5000917 (9.2e-5) | 0.002518 |

The coarsest triplet is honestly flagged as pre-asymptotic. The analysis
discriminates between the two triplets; it does not rubber-stamp them. The
CI grids (64 → 144, r = 1.5) are the cheapest set inside the range. The
measured u_c also follows the discrete closed form 1.5·ny²/(ny² + 2)
(4/3, 16/11, 64/43, …) to within iteration noise. It is exactly second
order, so p → 2 as h → 0.

**Test tolerances** (fixed from the method, not fitted):
- status must be `asymptotic` (the library-default ±0.1 band, i.e. p in
  [1.74, 2.24] at r = 1.5);
- GCI21 < GCI32;
- |φ_ext − exact| < 0.2·|φ1 − exact| (the extrapolated error is
  higher-order);
- the true relative error ≤ GCI21 (the GCI's defining conservative
  property);
- errors decrease monotonically;
- grid independence at 1 %, the customary numerical-uncertainty target
  for a validation quantity;
- the L2 extrapolation < 0.2 × the fine norm;
- the written report validates.

Measured test runtime 169 s (Debug).

## 6. Lid-driven cavity Re = 100 — `CavityGhiaValidation.DISABLED_GridConvergence` (run explicitly)

**Setup:**
- SIMPLE with first-order upwind convection, so the formal order is **1**.
- Grids 20/40/80 (r = 2), each with its per-grid settings and the
  fallback.
- Five quantities at Ghia stations, with the Ghia et al. (1982) values as
  **benchmark** references (reported separately).
- Disabled because the 80×80 grid alone takes 18.7 min in Debug. The
  enabled per-grid 20×20 test is unchanged.

| grid | nx × ny | cells | h | runtime run 1 (s) | runtime run 2 (s) | iterations |
|---|---|---|---|---|---|---|
| coarse | 20 × 20 | 400 | 0.05 | 35.0 | 29.9 | 3036 |
| medium | 40 × 40 | 1600 | 0.025 | 185.5 | 165.6 | 5615 |
| fine | 80 × 80 | 6400 | 0.0125 | 1120.1 | 1364.8 | 9643 |

The study was run twice.
- Run 2 came after the Markdown report gained its U21 column. Its 80×80
  grid overlapped the full `ctest -j32`, which accounts for the longer
  runtime.
- The two JSON reports are **identical except for `runtime_seconds`**: all
  values, iteration counts and analysis fields are bit-identical, so the
  study is deterministic.
- The report files here are from run 2.

| quantity | 20 | 40 | 80 | Ghia | relative benchmark error 20 → 40 → 80 |
|---|---|---|---|---|---|
| u(0.5, 0.5) | −0.172814 | −0.190055 | −0.199315 | −0.20581 | 16.0 % → 7.7 % → 3.2 % |
| v(0.5, 0.5) | 0.041916 | 0.047946 | 0.052239 | 0.05454 | 23.2 % → 12.1 % → 4.2 % |
| u(0.5, 0.4531) | −0.173677 | −0.192108 | −0.202505 | −0.2109 | 17.7 % → 8.9 % → 4.0 % |
| v(0.2344, 0.5) | 0.154644 | 0.166736 | 0.173288 | 0.17527 | 11.8 % → 4.9 % → 1.1 % |
| v(0.8047, 0.5) | −0.204916 | −0.228544 | −0.240780 | −0.24533 | 16.5 % → 6.8 % → 1.9 % |

| quantity | p (pf 1) | φ_ext | GCI21 | GCI32 | asymptotic ratio | status | grid independent (1 %) | fine benchmark error ≤ GCI21? |
|---|---|---|---|---|---|---|---|---|
| u_center | 0.897 | −0.210057 | 0.0674 | 0.1315 | 0.931 | asymptotic | no (GCI 6.7 %) | yes (3.2 % ≤ 6.7 %) |
| v_center | 0.490 | 0.062851 | 0.2539 | 0.3886 | 0.702 | monotonic_not_asymptotic | no | yes (4.2 % ≤ 25 %) |
| u_x0.5_y0.4531 | 0.826 | −0.215960 | 0.0831 | 0.1552 | 0.886 | monotonic_not_asymptotic | no | yes (4.0 % ≤ 8.3 %) |
| v_x0.2344_y0.5 | 0.884 | 0.181037 | 0.0559 | 0.1072 | 0.923 | asymptotic | no (GCI 5.6 %) | yes (1.1 % ≤ 5.6 %) |
| v_x0.8047_y0.5 | 0.949 | −0.253923 | 0.0682 | 0.1388 | 0.966 | asymptotic | no (GCI 6.8 %) | yes (1.9 % ≤ 6.8 %) |

**Reading:**
- Three of the five quantities are in the asymptotic range of the
  first-order scheme (p 0.90 / 0.88 / 0.95). v_center (p 0.49) and u near
  u_min (p 0.83) are not yet. **None is grid-independent at 1 %** on
  80×80: GCI21 is 5.6–25 %, as expected for first-order upwind at
  h = 1/80. This is reported, not hidden.
- **Solution convergence vs benchmark error, kept separate:** the Ghia
  values are a 129×129 numerical solution, not an exact answer. φ_ext is
  an estimate of *this scheme's* grid-converged value. For u_center and u
  near u_min, φ_ext lies beyond Ghia by 2.1 % and 2.4 %. For the three
  v quantities φ_ext overshoots Ghia by 3.3–15 %; v_center, the worst, is
  the least asymptotic (p 0.49). Richardson with p < pf over-extrapolates,
  which is exactly why the status flags it. The benchmark distance is never
  used as an order estimate.
- In every quantity the fine-grid benchmark discrepancy lies inside its
  own GCI21 band.

**Test tolerances:**
- all solves accepted;
- no quantity invalid;
- for every monotonic quantity: GCI21 < GCI32, and the fine value closer to
  φ_ext than the coarse value;
- the benchmark error decreases fine vs coarse (reported separately);
- the report validates.

**Scratch pre-study** (16/24/36, r = 1.5, `cav.cpp` plus `cavtrip.cpp`),
which motivated 20/40/80:

| quantity | status | p |
|---|---|---|
| u_center | asymptotic | 0.833 |
| v_center | **divergent** (ε32/ε21 = 0.79 ≤ 1) | – |
| u near u_min | not asymptotic | 0.533 |
| v near v_max | not asymptotic | 0.676 |
| v near v_min | not asymptotic | 1.506 |

Runtimes were 21.6 / 77 / 212 s. These grids are too coarse, and the
library says so.

## 7. Natural convection Ra = 1e3 — `NaturalConvectionValidation.GridConvergence` (ENABLED in CI)

**Setup:**
- Differentially heated square cavity, Pr = 0.71, SIMPLE plus
  ThermalSolver Picard coupling, first-order upwind (pf = 1).
- Grids 10/15/20: **unequal** r21 = 1.333, r32 = 1.5, solved exactly.
- A grid is accepted only if the final SIMPLE solve passes
  `assessSimpleSolve`, the thermal solve converged, and the outer Picard
  change is < 1e-8 (hence `absoluteNoise` 1e-6).
- References: de Vahl Davis (1983) **benchmark**.

| grid | nx × ny | cells | h | runtime (s) run 1 / focused | Nu_avg (ref 1.12) | Nu relative error | u_max (ref 3.634) | v_max (ref 3.679) |
|---|---|---|---|---|---|---|---|---|
| coarse | 10 × 10 | 100 | 0.1 | 24.3 / 22.0 | 1.1743839 | 4.86 % | 3.25454 (10.4 %) | 3.43200 (6.7 %) |
| medium | 15 × 15 | 225 | 0.06667 | 132.8 / 119.2 | 1.1504709 | 2.72 % | 3.53926 (2.6 %) | 3.54277 (3.7 %) |
| fine | 20 × 20 | 400 | 0.05 | 246.6 / 211.1 | 1.1406738 | 1.85 % | 3.49368 (3.9 %) | 3.56330 (3.1 %) |

Each grid's outer Picard loop stopped at iteration 50 of the 60 allowed.
The final changes were 9.8e-9, 9.8e-9 and 9.2e-9, all below the 1e-8
tolerance, so this is genuine convergence and not a cap. With the fixed
outer relaxation 0.3, the contraction rate is about the same on every grid.

The references are the de Vahl Davis values stored in
`de_vahl_davis_1983::kRa1e3`.

| quantity | r21 / r32 | p (pf 1) | φ_ext | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|
| nu_avg | 1.333 / 1.5 | 1.561 | 1.1233868 | 0.01894 | 0.02943 | 1.220 | monotonic_not_asymptotic | no |
| u_max | 1.333 / 1.5 | — | — | — | — | — | **oscillatory** (R = −0.160), bound 0.1424 | no |
| v_max | 1.333 / 1.5 | 3.740 | 3.5739209 | 0.003726 | 0.01099 | 2.698 | monotonic_not_asymptotic | no |

**Reading:**
- Nu_avg converges monotonically. φ_ext = 1.1234 lands within 0.3 % of
  the benchmark, versus 1.85 % for the fine grid. The observed order 1.56
  differs from the formal 1 (ratio 1.22), so the triplet is honestly **not
  asymptotic**, and the GCI is not claimed as a reliable bound.
- u_max and v_max are extrema **sampled at cell centres**. Their location
  error jumps between grids, so u_max oscillates, and v_max shows a
  spurious p = 3.7 whose GCI21 (0.37 %) is far below its actual 3.1 %
  benchmark distance. This is why a non-asymptotic GCI must not be quoted
  as an uncertainty; the status says so.
- Finer grids were not made the CI default because the 20×20 solve already
  takes 247 s. The limitation is recorded (§12).

**Test tolerances:**
- all accepted;
- Nu is not invalid;
- r21 = 4/3 and r32 = 1.5 exactly;
- for each quantity with an order: GCI finite, fine value closer to φ_ext
  than the coarse one;
- the Nu benchmark error decreases;
- the report validates.

Measured runtime 404 s (Debug). That is the same order as the
already-enabled per-grid 15×15 test (275 s), and the test runs in
parallel under `ctest -j32`.

## 8. Reports (`results/p12-num-005/`, copied from `results/validation/grid_convergence/`)

| file | study |
|---|---|
| `poiseuille_flow.json` / `.md` | §5 (regenerated by the enabled test) |
| `natural_convection_ra1e3.json` / `.md` | §7 (regenerated by the enabled test) |
| `cavity_re100.json` / `.md` | §6 (from the explicit `DISABLED_` run) |

- The format (format_version 1) is documented in
  `docs/validation/grid_convergence.md`.
- Deterministic: byte-identical for the same study (`DeterministicReport`).
  Only `runtime_seconds` changes between runs.
- All six files are also written in place by the tests, under
  `results/validation/grid_convergence/`.
- **Validation:** every enabled study validates the file it has just
  written with `validateGridConvergenceReportFile`. The three JSON files
  here were validated again by `validate_reports.cpp` (§10).

## 9. Tests added / changed

- `tests/unit/validation/` is new: `test_grid_convergence.cpp` has 16
  tests and `test_grid_convergence_study.cpp` has 6.
  `tests/unit/CMakeLists.txt` gained `add_subdirectory(validation)`.
- `PoiseuilleValidation.DISABLED_GridRefinementReducesVelocityError` was
  replaced by the **enabled** `PoiseuilleValidation.GridConvergence`.
- `NaturalConvectionValidation.DISABLED_GridRefinementReducesNusseltError`
  was replaced by the **enabled** `NaturalConvectionValidation.GridConvergence`.
- `CavityGhiaValidation.DISABLED_GridRefinementReducesGhiaError` was
  replaced by `CavityGhiaValidation.DISABLED_GridConvergence` (runtime).
- Fixes during development, in the new code only:
  - the rejection reason was overwritten by the generic "needs 3 grids"
    message; the specific cause now takes precedence;
  - two dangling-else warnings in tests.

## 10. Focused runs (`focused_tests.log`)

**25/25 passed.** Final binaries:

| suite | result | runtime |
|---|---|---|
| `CFDValidationUnitTests` | 22/22 | 23 ms |
| `PoiseuilleValidation.GridConvergence` | passed | 158.9 s |
| `NaturalConvectionValidation.GridConvergence` | passed | 358.9 s |
| `CavityGhiaValidation.DISABLED_GridConvergence` (explicit) | passed | 1587.8 s, run 2 |

- The Poiseuille and NC study runs include the in-test report validation.
- The cavity binary predates the in-test validation line, so its report
  was validated separately.
- `validate_reports.cpp` reports all three JSON reports in this directory
  **VALID**.
- Every value in the focused runs is identical to the first runs; only
  the runtimes differ.

## 11. Full regression

`ctest -j32 --output-on-failure` on `build/debug`, after the final build:

| | before (NUM-004 close) | after (NUM-005) |
|---|---|---|
| listed (`ctest -N`) | 1592 | **1614** |
| passed | 1579 | **1603** |
| failed | 0 | **0** |
| disabled | 13 | **11** |
| wall time | 684.5 s | 754.1 s |

**Name diff against the NUM-004 final log** (exact):

| change | count | tests |
|---|---|---|
| removed | 3 | the three old `GridRefinementReduces*` names (all disabled) |
| added | 25 | 16 `GridConvergenceTest.*`, 6 `GridConvergenceStudyTest.*`, `PoiseuilleValidation.GridConvergence` (passed, 240.6 s), `NaturalConvectionValidation.GridConvergence` (passed, 487.6 s), `CavityGhiaValidation.GridConvergence` (disabled) |

That gives 1592 − 3 + 25 = 1614 listed and 13 − 3 + 1 = 11 disabled. No
other test changed.

- The two enabled studies ran longer under `-j32` than standalone (169 s
  and 404 s). The suite's wall time rose 70 s.
- The run overlapped the cavity rerun (§6) for its first ~10 minutes.

**Generated-file diffs:**
- 13 tracked `results/validation/**/validation.json` files
  (natural_convection Ra1e3 10x10 ×6 and 15x15; turbulence channel
  k-ε/k-ω/SST coarse/medium) changed. Each was checked, and each diff is
  **only** the `runtime_seconds` line, so all 13 were reverted.
- The three `metadata.json` files keep their intentional NUM-004
  `robustness` block (no NUM-005 change).
- New, untracked, kept:
  - `results/validation/grid_convergence/` (the reports);
  - `results/validation/natural_convection/Ra1e3/20x20/`: the per-grid
    record the now-enabled NC study writes for its fine grid (previously
    only written by the disabled 20×20 test). It records outer iterations
    50 of the 60 allowed, a final change of 9.2e-9 < 1e-8, and Nu 1.14067.

## 12. Remaining limitations

- **Cavity study not in CI.** It takes 22 min in Debug, dominated by the
  80×80 grid. It is run explicitly (`--gtest_also_run_disabled_tests`).
  Poiseuille and natural convection are the enabled three-grid studies.
- **Partially pre-asymptotic real cases.**
  - Cavity: v_center and u near u_min are not asymptotic on 20/40/80, and
    no cavity quantity is grid-independent at 1 % with first-order upwind.
  - Natural convection: Nu_avg and v_max are not asymptotic on 10/15/20.
  - Neither was hidden by changing p or the tolerance band. Resolving
    them needs finer grids, or a higher-order scheme (NUM-001 provides
    one, but switching the validated schemes is out of NUM-005 scope).
- **Sampled extrema** (u_max, v_max at cell centres) are poor
  grid-convergence quantities: oscillatory or spurious p. An interpolated
  extremum would be needed.
- **dp/dx** in the Poiseuille case is not iteratively converged at the
  validation gates (7 % drift), so it is excluded. The iterative error
  must be ≪ the grid differences for any quantity studied.
- **Zero-limit quantities:** a relative GCI is meaningless when φ → 0; use
  U (absolute) instead.
- **The asymptotic check requires a formal order.** Without one the status
  is "range unknown" by design.
- **Grids:** structured 2D Cartesian in the runner, where h = √(A/N) (an
  explicit h is allowed in the analysis). Unstructured and 3D meshes need
  the caller to pass h. There are no GUI controls.
- **Report runtimes** make the tracked report files differ between runs,
  like the existing `validation.json` files.

## Git state

Not committed; HEAD is still `fd9bae3`, with NUM-001..004 uncommitted
beneath this work.

NUM-005 files, new:
- `include/cfd/validation/`, `src/validation/`;
- `tests/unit/validation/`;
- `docs/validation/grid_convergence.md`;
- `results/p12-num-005/`, `results/validation/grid_convergence/`,
  `results/validation/natural_convection/Ra1e3/20x20/`.

NUM-005 files, modified:
- `src/CMakeLists.txt` (+2 sources);
- `tests/unit/CMakeLists.txt`;
- the Poiseuille, cavity and thermal integration tests and their
  `CMakeLists.txt` comments;
- `TODO.md`.

No NUM-001..004 file was altered by NUM-005.

## 13. Acceptance gates

| gate | evidence | result |
|---|---|---|
| observed order implemented | `analyzeGridConvergence`, `solveObservedOrder`; §4 | PASS |
| unequal refinement ratios handled | exact bisection; `UnequalRefinementRatios`; NC study r21 1.333 / r32 1.5 | PASS |
| Richardson extrapolation implemented | `RichardsonExactRecovery` (1e-12); Poiseuille φ_ext error 2.9e-4 | PASS |
| GCI implemented | `GCIKnownCase` (hand + Celik 2008 Table 1) | PASS |
| asymptotic check implemented | formal-order ratio; `AsymptoticRatio`; real cases discriminated (§5 triplets, §6, §7) | PASS |
| numerical edge cases handled | oscillatory, divergent, zero difference, zero solution, invalid h, non-finite, bad options; nothing NaN | PASS |
| synthetic 1st order → ~1 | p = 1 ± 1e-10 | PASS |
| synthetic 2nd order → ~2 | p = 2 ± 1e-10 | PASS |
| Richardson recovers the exact value | ± 1e-12 relative | PASS |
| known GCI case matches | Celik 2008: p 1.53, φ_ext 6.1685, GCI21 2.2 % | PASS |
| real CFD: Poiseuille | asymptotic, p 1.94, GCI21 0.8 %, grid-independent at 1 % | PASS |
| real CFD: cavity | three-grid evidence (20/40/80), 3/5 asymptotic, honest non-asymptotic flags | PASS (evidence; the test is disabled) |
| real CFD: natural convection | three-grid evidence (10/15/20, unequal r) | PASS (evidence; Nu not yet asymptotic) |
| CI: ≥ 1 real three-grid study enabled | Poiseuille **and** NC `GridConvergence` enabled, passed in the full run | PASS |
| machine-readable reports generated and validated | 3 JSON + 3 MD; `DeterministicReport`, `ReportSchemaValidation`; in-test file validation; `validate_reports.cpp` | PASS |
| all existing tests green | 1603/1603, 0 failed | PASS |

No threshold was chosen after seeing a result:
- the ±0.1 band and Fs = 1.25 are library defaults fixed before any study;
- the 1 % grid-independence target was fixed with the method;
- the 0.2 × fine-error bound on Richardson follows from the
  higher-order-remainder argument.

The pre-asymptotic cavity and NC quantities are recorded as such, with
neither the tolerance nor p adjusted to make them pass.
