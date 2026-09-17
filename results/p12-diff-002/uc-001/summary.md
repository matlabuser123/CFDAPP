# P12-DIFF-002-UC-001 — Summary

**Question.** Three `PoiseuilleValidation` tests failed after P12-DIFF-002. Is the DIFF-002
discretisation wrong, or is the tests' "discrete exact" reference stale?

**Answer.** The reference was stale. Production implements the DIFF-002 system exactly as
independently derived — agreement to **2.2e-16** on the assembled operator and on every cell of
the solved system. The three failures were the tests comparing a correct solution against the
superseded two-point wall treatment's closed form.

## Evidence classes, kept separate

| class | content |
|---|---|
| **analytical reference** | Wall stencil `cP=3Γ\|S\|/h`, `cF=Γ\|S\|/(3h)`, `cB=8Γ\|S\|/(3h)` with `cP−cF=cB` exactly. Wall row `−4u₀+(4/3)u₁=Gh²`. Discrete solution = the continuum parabola **exactly at the cell centres**. `dp/dx = dp/dx_exact·2ny²/(2ny²+1)`; relative error `1/(2ny²+1)`; drop ratio `2ny²/(2ny²+1)`. Superseded two-point: `u_j` carried an extra `+Gh²/8`, `ny²/(ny²+2)`, `2/(ny²+2)`. |
| **independent numerical reference** | Exact-`Fraction` Gauss-Jordan solve of the `ny+1` system (momentum rows + flow-rate closure), no CFDApp code. `ny=8`: `dp/dx = −256/215`, profile `15/43, 39/43, 55/43, 63/43`, sampled centreline `63/43`. |
| **CFDApp production result** | Subject of every comparison, never a reference. `logs/02, 03, 05, 08`. |
| **literature benchmark** | None — planar Poiseuille has a closed form. (Part B `UF-001` is where a literature benchmark enters.) |
| **policy judgment** | §"Classification" below and `acceptance_gate.md` §5. |

## Step 5 — production vs the independent reference

| level | check | worst deviation |
|---|---|---|
| 1 | assembled momentum row vs hand-derived row, **every entry**, 8 rows | **1.11e-16** (RHS 0.00e+00) |
| 2 | solved 1D profile vs exact rationals, **every cell** | **2.22e-16**; `dp/dx` rel 3.73e-16 |
| 3 | matrix structure: far-cell column present at the derived index, 4 nonzeros per wall row | as derived |
| 4 | `cB` against a **nonzero** wall value | 0.00e+00 (two-point `cB` would differ by 2.5e-02) |
| 5 | wall flux; global momentum balance | 6.94e-17; balance 9.32e-16 |
| 2D | solved 2D profile L∞ vs the reference, ny 8/12/18/27 | 4.07e-06 / 3.59e-08 / 5.48e-09 / 8.23e-09 |

Level 4 exists because the original RHS comparison multiplied `cB` by a still wall's `u_b = 0`
and was therefore vacuous whatever `cB` held.

## Step 6 — accuracy and order (the A6 observation, checked)

Both operators are **second order**; DIFF-002 changes the error *constant*:

| | dp/dx error ratio (two-point ÷ DIFF-002) | centreline error ratio |
|---|---|---|
| `ny = 8` | **3.909** | **1.303** |
| `ny → ∞` | **4.000** | **1.333** (4/3) |

Observed orders → 2.000 (two-point 1.8745→1.9995, DIFF-002 1.9668→1.9999).

A6 recorded "≈1.30x centreline / ≈3.5x dp/dx". The centreline figure is **confirmed**. The dp/dx
figure is **corrected**: the true discrete ratio is 3.909 at `ny=8` (→4), and A6's 3.5 came from
production's *measured* dp/dx, inflated by that estimator's 8.3e-04 relative bias. Improvement is
recorded as a consequence, never used as an acceptance criterion.

## Step 7 — non-vacuity

Every criterion fails for all four wrong controls — superseded two-point, far-cell coefficient
doubled, far-cell sign flipped, far-cell dropped. Weakest margin 4.6x (`C2`, `ny=18`). The order
gates `C7`/`C8` are explicitly recorded as *not* operator-discriminating (the two-point scheme is
also second order); `C1`–`C6` carry the whole operator claim.

## Two results that were not part of the brief

**1. The dp/dx order overshoot is an instrument-resolution limit.** `pressure_gradient_asymptotic`
reported observed order 2.2652 (band 1±0.1 on the asymptotic ratio; measured 1.1135). The exact
DIFF-002 sequence on the same grids is asymptotic (1.9846, ratio 0.9938). Adding production's
measured pressure-extraction offsets (+9.93e-04, +1.06e-04, +2.13e-07) to the *exact* reference
reproduces **2.2652 / 1.1135 to all printed digits**. The same offsets leave the two-point
sequence asymptotic (1.0073) — which is why the gate passed before DIFF-002: the offset is 19.3 %
of the grid-to-grid difference Richardson consumes for DIFF-002 versus 5.0 % for two-point,
because DIFF-002 shrank the discretisation error 4x while the extraction error stayed put. The
declared `absoluteNoise = 1e-5` is also wrong for this quantity by up to 99x, but raising it
cannot rescue the gate (it only reclassifies the sequence as `InsufficientSeparation`, which the
gate also rejects). The order claim was therefore kept at full strength and moved to
`ny = 12/18/27`, where the offset is ≤ 4.6 % of that difference.

**2. The centreline order needs a parity-consistent triplet.** The tests sample the centreline by
interpolating the cell-centre profile at `y = H/2`. For odd `ny` that is a cell centre; for even
`ny` it is the chord midpoint between the two centre cells. The deficit from the continuum apex is

```
1.5U − u_c  =  1.5/(2ny²+1)  (ny odd)      4.5/(2ny²+1)  (ny even)     — exactly 3x
```

verified as exact rationals for `ny ∈ {8,12,16,18,24,27,32,36}`. A mixed-parity triplet is not an
h-refinement sequence for this quantity: `12/18/27` yields p = 0.9376, asymptotic ratio 0.65 —
**from the exact reference values alone**, with no solver involved. Production reproduces 0.9376
exactly. So `ny = 27` serves the pressure gradient only, and the centreline keeps `8/12/18`.

## Classification (Step 8)

| test | classification |
|---|---|
| `PoiseuilleValidation.Profile` | MIGRATE_DERIVED_REFERENCE |
| `PoiseuilleValidation.PressureDrop` | MIGRATE_DERIVED_REFERENCE |
| `PoiseuilleValidation.ProductionGridConvergence` | MIGRATE_DERIVED_REFERENCE + REDESIGN_VALIDATION (the `pressure_gradient_asymptotic` gate's triplet) |
| `PoiseuilleValidation.MassFlow` | KEEP (passed throughout) |

No `PRODUCTION_DEFECT`. No `UNCERTAIN`.

## What changed

Only validation tests; **no production source**. `acceptance_gate.md` (`bed6b894…`) was frozen
first.

- `PoiseuilleValidationUtils.{hpp,cpp}` — `discretePressureGradient` → `2ny²/(2ny²+1)`;
  `discretePoiseuilleVelocity` → dropped the `+G dy²/8` offset; header carries the derivation and
  the parity caution.
- `test_poiseuille_production_validation.cpp` — the hard-coded `2.0/66.0` and `64.0/66.0` replaced
  by `discreteGradientRelativeError(ny)` and `discreteDropRatio(ny)`; the centreline identity by
  `centerlineSamplingDeficit(ny)` (all computed from `ny`); added the `216x27` grid; split the grid
  study into one triplet per quantity with the reason recorded; two new `limitations` entries.
- `tests/integration/poiseuille/CMakeLists.txt` — grid list comment.

**Every bound is unchanged** (1e-4, 1e-3, asymptotic band 1±0.1). Only reference *formulas* and
the grids each order claim is measured on changed. No production output was copied into a test.

## Step 10 — verification

Fresh `cmake --build build/release --clean-first` (431/431 targets, exit 0,
`logs/09_rebuild.log`). The rebuilt library is **bit-identical**:

```
build/release/src/libcfdcore.a  143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a
```

— the same hash as before UC-001, confirming no production code was touched.

| suite | result |
|---|---|
| **CFDPoiseuilleValidationTests** | **10 / 10 PASS** — all three U-C failures resolved |
| **CFDMMSValidationTests** | **19 / 19 PASS** (A6's amended order gates still hold) |
| CFDPhysicsTests | 98 / 98 |
| CFDPisoTests | 81 / 81 |
| CFDSimpleTests | 123 / 123 |
| CFDMeshTests | 156 / 156 |
| CFDSolverTests | 90 / 90 |
| CFDCoreTests | 30 / 30 |
| CFDFieldTests | 40 / 40 |
| CFDAlgebraTests | 97 / 97 |
| CFDThermalTests | 99 / 99 |
| CFDTurbulenceTests | 136 / 136 |
| CFDDiscretizationTests | 168 / 169 — 1 failure |
| CFDCaseIntegrationTests | 61 / 63 — 2 failures |

The three failures are all on the recorded pre-existing list
(`validation-migration/a6/resumed/logs/13_W7_ctest_raw.log`, 9 failures) and all are explicitly
out of UC-001's scope:

| failure | prior disposition (`a4/inventory.md`) |
|---|---|
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | "PRE-EXISTING / GRAD-002-ERA, `KEEP_UNCHANGED`" |
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | "W8's own tests, failing since before DIFF-002. `KEEP_UNCHANGED`" |
| `MultiBlockProductionCase.CurvedChannelGridConvergence` | same |

**No new failures.** Of the 9 pre-existing failures, the 3 U-C ones are resolved; 2 are the U-F
natural-convection tests reserved for Part B, and 4 remain out of scope
(GRAD-002 ×1, W8 ×2, `LowMachRegressionTest.GlobalMassImbalanceIsSmall` ×1 — the latter lives in
a suite not run here and is unchanged).

**Harness note, disclosed:** the first Step 10 pass reported `CFDMMSValidationTests` with an empty
result line. The freshly linked binary came out of the build without the executable bit on the
Windows mount, so the runner's `find -perm -u+x` filter skipped it; it was not a test failure and
not silently accepted. Re-run explicitly via `tools/run_mms.sh` (which sets the bit): 19/19 pass.

## Recorded limitations

- `C2`/`C5`/`C6` at `ny = 8` pass with only **1.2x** margin on their unchanged 1e-3 bound,
  bounded by the deterministic pressure-extraction bias (8.3e-04 relative; the solve is
  bit-reproducible — `Grid64x8IsDeterministic`).
- The grid study's runtime rose from ~19 s to ~45 s (release) with the added `216x27` grid.
- `C7`/`C8` do not discriminate the operator.
- `ny = 27` must never enter the centreline order triplet.
