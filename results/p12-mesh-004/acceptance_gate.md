# P12-MESH-004 — acceptance gate (fixed BEFORE the final campaign runs)

Written after the preliminary feasibility probe
([logs/02_preliminary_probe.log](logs/02_preliminary_probe.log)) and before any final campaign
run. The probe was used only to choose degradation ranges that stay constructible, the
manufactured-solution parameters, and SIMPLE settings with iteration error far below
discretisation error. No threshold here is a probe value scaled to pass; each comes from a
solver tolerance, a stated convergence-theory criterion, or an earlier P12 gate, as given in its
rationale. **The gate is not changed after the final runs.** Levels marked *reported* are run and
reported in full but are not pass/fail criteria; that status is decided here, in advance.

## Common configuration (every run)

Production path only: `CaseWriter` → `CaseReader` → `CaseBuilder` (production mesh, the
`MeshQuality` gate, boundary objects, `solver.json` settings) → `SIMPLE` with the case's own
settings → `ThermalSolver` with the production thermal settings (those of `ProjectRunner`).
The only non-production input is the manufactured source (momentum `f`, heat `q`), which an MMS
needs by definition. Machinery: `tests/integration/case/MeshQualityCampaign.{hpp,cpp}`.

**Manufactured solution (independent truth)**, unit square, all four sides no-slip walls,
zero-gradient pressure, `T = 0`:
- ψ = sin²(πx) sin²(πy) / π, so u = sin²(πx) sin(2πy), v = −sin(2πx) sin²(πy) (div u = 0,
  u = v = 0 on every wall);
- p = cos(πx) cos(2πy) (∂p/∂n = 0 on every wall);
- T = sin(πx) sin(πy) (T = 0 on every wall);
- ρ = 1, μ = 0.05 (Re = 20), k = 0.2, cp = 1 (Pe = 5);
- f = ρ(u·∇)u + ∇p − μ∇²u and q = ρcp u·∇T − k∇²T, derived by hand from the closed forms, never
  from a discrete operator of the code under test. They are checked against central differences
  of the exact fields by the regular test `ManufacturedForcingMatchesFiniteDifferences`.

Errors are measured cell by cell against the exact fields at the cell centroids (`computeErrorNorms`:
volume-weighted L1, L2 and L∞; pressure gauge-invariant).

**Discretisation, corrected recipe:** `linear_upwind` momentum convection, `least_squares`
gradients, `non_orthogonal_corrections` 2. The production energy equation always convects with
first-order upwind.

**Uncorrected recipe:** identical except `non_orthogonal_corrections` 0. That is the one case switch
that removes every non-orthogonal correction (momentum, pressure correction and thermal).

**SIMPLE:** relaxation 0.8 / 0.4; velocity, pressure and continuity tolerances 1e-7; at most 20000
iterations; BiCGSTAB momentum (1e-10 / 1e-8 / 500); CG pressure (1e-10 / 1e-8 / 5000).
*Rationale (logs/02 §3b):* between tolerance 1e-7 and 1e-8 the u L2 error changes by less than
0.01 %, the p L2 error by 0.40 %, and T not at all, so iteration error is negligible against
discretisation error. At tolerance 1e-6 the p L2 error moved 3.7 %, and one thermal solve hit the
BiCGSTAB breakdown limitation recorded in logs/02 §4.

## Degradation campaign (Step 9/11): 32 × 32-cell class meshes, one control parameter per family

| family | control parameter | Q0 | Q1 | Q2 | Q3 | Q4 | targeted metric |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D1 smooth non-orthogonality | amplitude A of x = X + A sin πX sin 2πY, y = Y + A sin 2πX sin πY | 0 | 0.025 | 0.05 | 0.075 | 0.10 | max non-orthogonality |
| D2 cell-scale irregularity | fraction f of the hashed interior-vertex displacement f·h·(ξ, η) | 0 | 0.075 | 0.15 | 0.225 | 0.30 | max skewness |
| D3 aspect ratio | nx × ny (uniform Cartesian, ≈1024 cells) | 32×32 | 45×23 | 64×16 | 91×11 | 128×8 | max aspect ratio |
| D4 expansion | geometric y-grading "both", ratio r (x uniform) | 1 | 1.1 | 1.2 | 1.3 | 1.4 | max expansion ratio |

Also run:
- **D1-Q5**, A = 0.12 (≈ 74°, beyond the 70° warning): *reported*. It exists to show what the
  warning threshold marks.
- **Uncorrected recipe** on D1 Q0–Q4 and D2 Q0–Q4: *reported*. These are the controlled comparison
  for the non-orthogonal correction.
- **Beyond-range levels** D1 A = 0.2 and D2 f = 0.45 (32 × 32): must be rejected (R6).

A family's parameter moves more than one metric when the geometry forces it: D2 raises
non-orthogonality and expansion ratio along with skewness, and D4 raises the aspect ratio of the
wall cells along with the expansion ratio. Every metric of every level is reported, and effects are
attributed only to the family's control parameter, never to a single metric.

## Gate criteria — ALL must hold

**R1 — the degradation parameter increases.** For each of D1–D4 the control parameter is strictly
increasing over Q0…Q4.

**R2 — the targeted metric responds in the expected direction.** For each family, the targeted
metric (table above) of the production `MeshQuality` report is strictly increasing over Q0…Q4.
Q0 is the orthogonal uniform mesh: max non-orthogonality 0, max skewness 0, aspect ratio 1,
expansion ratio 1, each within 1e-9.

**R3 — solver finite and convergent on the valid levels.** Every corrected-recipe run of D1–D4
Q0–Q4 is accepted by `CaseBuilder` (status `valid` or `valid_with_warnings`). SIMPLE ends
`Converged` and the thermal solve ends `Converged`, with every velocity, pressure and temperature
value finite.

**R4 — error against independent truth.** Every error is computed against the closed-form
manufactured solution at the cell centroids, never against another numerical solution. The
manufactured forcing matches second-order central differences of the exact fields (step 1e-5,
50 interior points) to a relative difference of at most 1e-6 (`ManufacturedForcingMatchesFiniteDifferences`;
rationale: the O(ε²) truncation plus round-off of the central difference, far below any
discretisation error measured).

**R5 — quantitative degradation-vs-error report.** For every family and level the report gives:
- every quality metric (max, mean);
- the L1, L2 and L∞ errors of u, v, p and T;
- the ratio of each field's L2 error to Q0's;
- SIMPLE iterations, final residuals and the continuity contraction rate per iteration;
- thermal outer iterations;
- mass and energy imbalance;
- runtime.

Plus, for D1 and D2, the corrected / uncorrected error ratio per level. This criterion requires the
report, not a particular trend. No causal claim is made beyond "changing this family's parameter,
all else fixed, changed the error by this measured ratio".

**R6 — invalid meshes rejected before solving.** Through `ProjectRunner`, each invalid mesh below
must give status `InvalidCase`, no SIMPLE result, zero progress callbacks (the solver loop never
ran), no exported results, and an error message that names both the defect and where it is:

| invalid mesh | required substring of the error |
| --- | --- |
| zero-area cell (a row of interior vertices collapsed onto the boundary) | `zero area` |
| inverted cell (a vertex moved through its neighbour) | `inverted` or `not convex` |
| non-finite coordinate (in-memory definition → `CaseBuilder`; `1e999` in mesh.json → `ProjectRunner`) | `non-finite` (builder); the file name `mesh.json` (reader) |
| degenerate face (two adjacent vertices coincide) | `degenerate edge` |
| broken connectivity (a multiblock interface whose vertices do not coincide) | `does not coincide` |
| disconnected domain (two multiblock blocks, no interface) | `disconnected cell regions` |
| D1 A = 0.2 and D2 f = 0.45 (beyond range) | `is not a strictly convex counter-clockwise quadrilateral` |

The `MeshQuality` fatal classification itself (non-finite / non-positive area, invalid ids,
owner == neighbour, reversed face orientation, open cell, boundary face in no patch, >1 region)
is verified on hand-built meshes by the mesh unit tests.

**R7 — conservation within tolerance.** On every converged run, corrected and uncorrected, of the
campaign and the MMS study:
- |global mass imbalance| ≤ 1e-10. *Rationale:* every boundary is a no-slip wall with exactly zero
  face flux, so the global imbalance is summation round-off (~1e-15). 1e-10 is 3 orders below the
  SIMPLE continuity tolerance, and a lost or duplicated face flux would be O(1e-3).
- |Σ energy-equation residual| / Σ |q| V ≤ 1e-8. *Rationale:* the thermal outer tolerance is 1e-8
  and its linear tolerance is 1e-12. Internal-face fluxes, including the explicit correction,
  cancel pairwise, so only boundary terms lagged by at most one outer change remain.

## MMS study (Step 12)

Smooth non-orthogonal family D1 with A = 0.05 (≈ 35°), n × n for n = 8, 16, 32, 64 (4 levels), with
the corrected and the uncorrected recipe; plus the Cartesian baseline (A = 0) with the corrected
recipe.

**M1.** Every corrected run (Cartesian and A = 0.05) is Converged (SIMPLE and thermal), finite,
and satisfies R7.

**M2.** For the corrected runs, the L2 errors of u, v and T decrease strictly over the 4 levels.

**M3.** Observed order p = ln(e_n / e_2n) / ln 2 of the corrected runs on the finest pair
(32 → 64):
- u and v L2 ≥ 1.5. *Rationale:* formal order 2 (linear upwind, least squares, corrected
  diffusion); ≥ 1.5 is the P12-MESH-001 and P12-MESH-003 criterion.
- T L2 ≥ 0.75. *Rationale:* formal order 1 (first-order upwind energy convection); 0.75 × formal
  is the same factor as 1.5 / 2.

Pressure orders and every pair's order are *reported*. Second-order behaviour is claimed only
where the measured order supports it.

**M4 (reported).** Corrected versus uncorrected on the same A = 0.05 meshes: per-level error ratio
and observed orders. The only difference between the two runs is the correction switch.

## Grid convergence with P12-NUM-005 (Step 13)

Quantities, both with an exact value (reference kind "analytical"):
- kinetic energy KE = ½ Σ |u_c|² V_c (exact 3/16, formal order 2);
- mean temperature ⟨T⟩ = Σ T_c V_c (exact 4/π², formal order 1).

Settings: `GridConvergenceOptions` with Fs = 1.25; absoluteNoise 1e-7 (the iteration noise in
logs/02 §3b); formal orders as above; grids 16 / 32 / 64 (r = 2). Families:
- Cartesian, corrected (baseline);
- smooth A = 0.05, corrected (non-orthogonal);
- D2 f = 0.15, corrected (degraded; its random displacement is regenerated on every grid, so it
  is not a geometrically similar family): *reported*.

**GC1.** For the Cartesian and smooth families, every grid's solve is accepted by
`assessSimpleSolve` with mass-imbalance tolerance 1e-10.

**GC2.** For every quantity the tool classifies `asymptotic`, the fine-grid uncertainty brackets the
exact value: |φ₁ − φ_exact| ≤ U21. Quantities classified otherwise (oscillatory, divergent,
insufficient separation, monotonic but not asymptotic) are reported with the tool's
classification. No order, extrapolation or GCI is forced on them (the tool never computes one), and
that is not a gate failure.

The JSON reports must pass `validateGridConvergenceReport`.

## Not relaxed by this gate

The other conditions are verified by their own tests and logs and reported in summary.md:
- metric definitions and classification (unit tests);
- CLI, GUI and JSON reporting;
- backward compatibility (field outputs of every committed case unchanged);
- focused tests and the full regression.
