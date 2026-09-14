# Grid-convergence analysis (observed order, Richardson, GCI)

P12-NUM-005. Implementation: `include/cfd/validation/GridConvergence.hpp`
(the analysis) and `include/cfd/validation/GridConvergenceStudy.hpp` (the
three-grid runner, the solver-status gate and the reports). Method: Celik,
Ghia, Roache, Freitas, Coleman & Raad, *Procedure for Estimation and
Reporting of Uncertainty Due to Discretization in CFD Applications*,
J. Fluids Eng. 130 (2008) 078001, built on Roache, *Verification and
Validation in Computational Science and Engineering* (1998). Evidence and
measured studies: `results/p12-num-005/summary.md`.

## What it answers

A CFD result depends on the mesh. A grid-convergence study solves the same
case on three systematically refined meshes and asks:

- Is the quantity converging as the mesh is refined, and how fast (the
  observed order p)?
- What would it be on an infinitely fine mesh (the Richardson extrapolated
  value)?
- How large is the remaining discretization error on the finest mesh (the
  Grid Convergence Index, GCI)?
- Are the meshes fine enough for these estimates to be trustworthy (the
  asymptotic-range check)?

## Conventions (used everywhere, reports included)

- **Indexing:** 1 = fine, 2 = medium, 3 = coarse, with h1 < h2 < h3.
- **Refinement ratios:** r21 = h2/h1 > 1 and r32 = h3/h2 > 1. They need
  not be equal.
- **Differences:** ε21 = φ2 − φ1 and ε32 = φ3 − φ2.
- **Grid size:** h = (domain measure / cells)^(1/dim), the average cell
  size. For a uniform nx × ny mesh on Lx × Ly this is √(Δx Δy). It equals
  1/nx only on a unit square with nx = ny, so 1/nx is never assumed.
  Explicit h values can be passed directly, e.g. for non-uniform meshes.
- **Units:** every *relative* quantity (e_a, e_ext, GCI) is a **fraction**
  (0.01 = 1 %), never a percentage. Absolute uncertainties are in the
  quantity's own units.

## Formulas (as implemented)

**Observed order p.** p is the p > 0 solving

    ε32 / ε21 = r21^p (r32^p − 1) / (r21^p − 1)

This is exact for φ(h) = φ_exact + C h^p. For r21 = r32 = r it reduces to
p = ln(ε32/ε21) / ln r. The right-hand side is strictly increasing in p, so
the equation is solved by bisection. Unequal ratios are therefore solved
exactly, not approximated by the equal-ratio formula (which is wrong by
> 0.2 in p for r21 = 1.3, r32 = 1.54 — a unit test pins this). Two cases
have no valid p:

- No positive root exists when ε32/ε21 is at or below the p → 0 limit
  ln r32 / ln r21. The differences do not shrink, and the sequence is
  classified **divergent**.
- A root above `maximumOrder` (default 20) is not trusted (ε21 is
  negligible against ε32) and is classified **insufficient separation**.

**Richardson extrapolation.**

    φ_ext21 = φ1 − ε21 / (r21^p − 1)
    φ_ext32 = φ2 − ε32 / (r32^p − 1)

**Relative errors.**

- e_a21 = |ε21/φ1| and e_a32 = |ε32/φ2|.
- e_ext21 = |(φ_ext21 − φ1)/φ_ext21| and e_ext32 likewise.
- They are left unset when the denominator is within the noise level
  (a quantity whose converged value is ~0 has no meaningful relative
  error). The absolute uncertainties below remain available.

**GCI.**

    U21   = Fs |ε21| / (r21^p − 1)      (absolute uncertainty of phi1)
    GCI21 = U21 / |φ1|                   (fraction)
    U32   = Fs |ε32| / (r32^p − 1),   GCI32 = U32 / |φ2|

The safety factor is **Fs = 1.25**, the Roache / Celik value for three
grids with an *observed* order (3.0 applies to two-grid studies with an
assumed order). It is configurable per quantity.

**Asymptotic-range check.**

    asymptotic ratio = U32 / (r21^pf U21),   with U computed using the FORMAL order pf

The ratio equals 1 when the three values follow φ_exact + C h^pf; it is
> 1 when the observed order exceeds pf and < 1 when it falls short. The
study is **asymptotic** when |ratio − 1| ≤ `asymptoticTolerance` (default
0.1, configurable).

- It **must** use the formal order. With the observed p the ratio is
  identically 1 for *any* monotonic triple, because p is fitted to exactly
  those three values, which makes it uninformative. If no formal order is
  supplied, the status is `monotonic_asymptotic_range_unknown`; a
  decreasing error alone never makes a study "asymptotic".
- The absolute-uncertainty form makes the ideal value exactly 1. The
  relative form GCI32 / (r21^pf GCI21) differs from it by |φ1/φ2|.

## Classification (`status`)

| status | meaning | reported |
|---|---|---|
| `asymptotic` | monotonic, p found, asymptotic ratio within the band | everything |
| `monotonic_not_asymptotic` | monotonic, p found, ratio outside the band | everything, but not grid-independent |
| `monotonic_asymptotic_range_unknown` | monotonic, no formal order given | everything except the ratio |
| `oscillatory` | ε21 and ε32 have opposite signs | **no p, no φ_ext, no GCI**; oscillation bound ½(max φ − min φ) (Stern et al. 2001) |
| `divergent` | the differences do not shrink (no p > 0) | nothing |
| `insufficient_separation` | a difference is within the noise level, or p > `maximumOrder` | nothing |
| `invalid` | non-finite input, h ≤ 0, wrong ordering, r < `minimumRefinementRatio` (default 1.1, "nearly identical grids"), a bad option, or a rejected CFD solve | nothing |

Further rules:

- **Noise:** a difference |ε| ≤ max(`absoluteNoise`, `relativeNoise` ×
  max|φ|) is treated as zero. A study should set `absoluteNoise` from its
  measured iterative-convergence error.
- Ratios in [1.1, 1.3) are accepted with a **warning** (Celik et al.
  recommend r > 1.3).
- Nothing that is set is ever NaN or infinite.

## Grid independence

`grid_independent` is true only when:

1. the status is `asymptotic`, and
2. GCI21 ≤ `gridIndependenceThreshold`.

There is no universally valid engineering threshold, so the study
configures and justifies its own. With no threshold configured,
`grid_independent` is false and the reason says so. The reason is always
reported.

## Solver-status gate

A grid takes part in the analysis only if its CFD solve is valid. For
SIMPLE (`assessSimpleSolve`) that means:

- the status is `Converged` — not `MaxIterations`, `Stagnated`,
  `Diverging`, a linear-solver failure, …;
- every velocity, pressure and face-flux value is finite;
- the global mass imbalance is ≤ the study's tolerance.

The runner stops at the first rejected grid (remaining grids are not
solved). Every quantity is then `invalid`, carrying the rejection reason.

## Reports

`gridConvergenceReportJson` / `writeGridConvergenceReport` produce the
machine-readable report; `gridConvergenceReportMarkdown` produces the same
content as tables. The output is deterministic: the same study always gives
byte-identical text, with keys in fixed order and unset values as `null`.
Only the measured per-grid runtime changes between runs.

Top-level JSON keys:

- `format_version` (1), `study`, `description`;
- `method` (reference, indexing, grid-size definition, units);
- `all_solves_accepted`, `rejection_reason`;
- `grids[]`: level, name, nx, ny, cells, h, runtime_seconds,
  solver_status, solver_iterations, accepted, rejection_reason and the
  quantities;
- `quantities[]`: name, description, values {coarse, medium, fine}, the
  optional `reference` {value, kind, per-grid errors, extrapolated error},
  and `analysis`.

`analysis` contains:

- status, convergence, diagnostic, warnings;
- r21, r32, epsilon21, epsilon32, convergence_ratio (R = ε21/ε32);
- observed_order, formal_order, order_ratio;
- richardson_extrapolated (φ_ext21), richardson_extrapolated_32;
- approximate/extrapolated relative errors;
- uncertainty_21/32 (absolute), gci_fine_medium (GCI21), gci_medium_coarse
  (GCI32), safety_factor;
- asymptotic_ratio, asymptotic_tolerance, oscillation_uncertainty;
- grid_independence_threshold, grid_independent, grid_independence_reason.

**Validating a report.** `validateGridConvergenceReport` (JSON text) and
`validateGridConvergenceReportFile` (a file) check a report against this
schema. They return the list of problems found (empty = valid) and never
throw on malformed input. Checks:

- required keys and their JSON types; `format_version` 1;
- three grids in the order coarse, medium, fine when `all_solves_accepted`,
  with h strictly decreasing and cells = nx × ny;
- known status names;
- p > 0, φ_ext and U present for monotonic statuses; order, φ_ext, GCI and
  asymptotic ratio null for oscillatory / divergent / insufficient
  separation / invalid; an oscillation bound for oscillatory;
- `asymptotic` only with the ratio inside its band;
- every quantity `invalid` when a solve was rejected;
- `grid_independent` only for an asymptotic quantity with GCI21 ≤ its
  threshold.

The enabled studies validate the file they have just written.

**Reading a report.**

- Check `all_solves_accepted` first.
- Then check each quantity's `status`:
  - only `asymptotic` supports quoting φ_ext and GCI21 as the fine-grid
    discretization uncertainty;
  - `monotonic_not_asymptotic` means the order estimate (and thus the GCI)
    is not yet reliable — refine further;
  - `oscillatory` gives only a bound.
- `reference` errors (against an analytical or benchmark value) are
  reported separately and never enter the grid-convergence analysis.

## Analytical references vs benchmark data

- An **analytical** reference (e.g. the Poiseuille centerline velocity 1.5
  U_mean) is exact. The per-grid error against it is the true
  discretization error, so φ_ext can be *checked* (|φ_ext − exact| should
  be ≪ |φ1 − exact|).
- **Benchmark** data (Ghia et al. 1982; de Vahl Davis 1983) are themselves
  numerical solutions, with their own discretization error and limited
  sampling. The distance to a benchmark therefore mixes our error with
  theirs, and its decrease with refinement is *not* a measure of observed
  order.
- The studies thus compute p, φ_ext and GCI **only from this code's own
  three solutions**, and report the benchmark comparison separately.
