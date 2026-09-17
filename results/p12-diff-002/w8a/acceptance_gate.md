# P12-DIFF-002 W8 AMENDMENT — acceptance gate (frozen before any test edit)

**Authorized by the user** after `W8-INV-001` closed `COMPLETE / PRODUCTION DEFECT FOUND: NO /
W8 AMENDMENT JUSTIFIED: YES`. This gate implements **W8-R1 … W8-R4 exactly as derived in §13 of
`results/p12-diff-002/w8-inv-001/summary.md`** (that file frozen at
`a795cc4bb2ad07d2a0c337eb082503f127e83aed0cae95b08b94b291c322f4db`). Nothing in §13 is
reinterpreted, loosened, or re-derived here; §13 is quoted verbatim below.

## 0. Chronology — recorded, not rewritten

```
Original W8:    FAILED unchanged historical gate
W8-INV-001:     COMPLETE
W8 Amendment:   authorized
Amended W8:     <pending execution>
```

`results/p12-diff-002/w8/summary.md` (`96932c05f666…`) and `w8/logs/01_W8.log`
(`ff3fe9ced3da…`) are **preserved byte-identical**. The original W8 is **not** marked as having
passed. The three EXPECTs it failed are, verbatim from that log:

| # | site | assertion | actual |
|---|---|---|---|
| 1 | `test_structured_quad_production_case.cpp:406` | `EXPECT_GE(velocityOrder, 1.5)` | **1.4390906187401413** (pair 1) |
| 2 | `test_structured_quad_production_case.cpp:407` | `EXPECT_GE(gradientOrder, 1.5)` | **−5.0119102605839547** (pair 1) |
| 3 | `test_multiblock_production_case.cpp:502` | `EXPECT_GE(gradientOrder, 1.5)` | **1.0503234189823614** (pair 0) |

These three — and only these three — are the assertions this amendment replaces.

## 1. Pre-modification freeze

| file | sha256 |
|---|---|
| `tests/integration/case/test_structured_quad_production_case.cpp` | `d20c8443130ce2b7b90a4058de6c5930dcbdbe417875ddccfcf06d9931d2b6bc` |
| `tests/integration/case/test_multiblock_production_case.cpp` | `bcbbadb7e14904a96c491d115f59013ac5c08a5bafa38be5debc9af365f91f72` |
| `results/p12-mesh-001/summary.md` | `13e08a6b809480798b05f25b0548c20228fe4ed8f5346798f97d64643451c34b` |
| `results/p12-mesh-003/acceptance_gate.md` | `4b772e55be94defdc50b0624f671582f0c4cfbb2daa65911e9d3788989c0601a` |
| `results/p12-mesh-003/summary.md` | `6e943b596b4c61900aa2d9cb384d1fc7216a3b0a01ac9c48e4de8360a3a00d62` |
| `build/release/src/libcfdcore.a` (pre-amendment) | `143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a` |

The eight production numerical files **must be unchanged** at the end of this amendment. Their
frozen values and where each was recorded:

| file | sha256 | frozen in |
|---|---|---|
| `src/discretization/NonOrthogonalDiffusion.cpp` | `20a02b16499fa7b0…` | `w8-inv-001/logs/00_freeze.log` |
| `src/discretization/Gradient.cpp` | `d24882a96e5cbc00…` | same |
| `src/physics/MomentumEquation.cpp` | `29af1927587558e5…` | same |
| `src/pressure_velocity/SIMPLE.cpp` | `d38c2bb3d86b62b6…` | same |
| `src/solver/SolverRobustness.cpp` | `2b97f94a7dcf2bf9…` | same |
| `src/mesh/MeshGeometry.cpp` | `04c964c24d6098db…` | same |
| `src/thermal/EnergyEquation.cpp` | `fbd47a072fa221f0…` | `validation-migration/a5/production_freeze_A5.txt`, `a6/resumed/production_freeze_A6.txt` |
| `src/thermal/ThermalInterface.cpp` | `7d58766dfde9ba7f…` | same |

## 2. W8-R1 — keep every existing non-order assertion unchanged

§13 verbatim:

> Solve acceptance (`assessSimpleSolve`), conservation (column/line flow, block net flow, interface
> flow), finiteness, wall flux, mesh validity, and **monotone error decrease**. These pass today and
> carry real content. *Non-vacuity:* the **far-cell sign-flip** control fails solve acceptance
> outright (3000 iterations, no convergence, both grids).

**Implementation:** no edit. Every such assertion — including `expectPoiseuilleGates`' MESH-001
bounds (velocity L2 ≤ 1.5 × Cartesian, dp/dx error ≤ 1.5 × `cartesianPressureGradientError`), the
MultiBlock `G4(a)` monotone-decrease triple and the `G4(c)` NUM-005 uncertainty bracket — stays
byte-identical. **MESH-001's and MESH-003's thresholds are not touched**; the amendment's stricter
envelopes are added at the site of the deleted order assertions, not by editing the frozen helper.

## 3. W8-R2 — StructuredQuad: replace the two order assertions with a derived accuracy envelope

§13 verbatim:

> | criterion | derivation | current | two-point | far-cell ×2 |
> |---|---|---|---|---|
> | velocity L2 at each grid **≤ 1.0 × the Cartesian scheme's own L2 at the same `ny`** | MESH-001 already computes `cartesianVelocityL2(ny)` and already gates at ≤ 1.5×; requiring the distorted mesh to be **at least as accurate as the orthogonal one** is stricter and needs no order | 1.0827e-02 vs Cartesian 1.5183e-02 ✓ | 1.7466e-02 ✗ | 5.3310e-02 ✗ |
> | dp/dx relative error at 144×18 **≤ 2 × the orthogonal discrete-exact error at the same `ny`** = 2 × 1/(2ny²+1) = 2/649 = **0.308 %** | UC-001's exact discrete solution for the orthogonal channel (`acceptance_gate.md` §1), an independent analytical result | 0.207 % ✓ | 0.679 % ✗ | ≫ ✗ |
>
> Both thresholds are **tighter** than the ones they replace (W6's 0.747 %; MESH-001's 1.5×).

**W8A-1 (velocity).** At **each** of the three grids: `m.velocityL2 <= 1.0 * cartesianVelocityL2(ny)`.
`cartesianVelocityL2` is MESH-001's existing helper, used **as-is and unmodified** — §13 designates
it by name. Independent of any observed order, and of the iteration stopping point except through
the accuracy it certifies.

**W8A-2 (dp/dx).** At the finest grid **144×18** only, as §13 specifies:

```
|dp/dx − dp/dx_exact| / |dp/dx_exact|  <=  2 / (2·ny² + 1)  with ny = 18  =  2/649  =  0.30817 %
```

Derivation (UC-001, `results/p12-diff-002/uc-001/acceptance_gate.md` §1, frozen
`bed6b8941a7e0112…`): the exact discrete solution of the DIFF-002 orthogonal channel gives
`dp/dx = dp/dx_exact · 2ny²/(2ny²+1)`, i.e. relative error exactly `1/(2ny²+1)`. The gate allows the
distorted mesh **twice** the orthogonal scheme's own discrete-exact error. Recorded reference:
**`2 / 649 = 0.308 %`**. The threshold is a closed-form function of `ny` alone — it contains **no
production output**, and no convergence order is computed across the dp/dx sign change.

The **existing** `cartesianPressureGradientError(ny) = 2/(ny²+2)` helper (the superseded two-point
formula, MESH-001's frozen reference) is **not modified and not reused** for this criterion.

## 4. W8-R3 — MultiBlock: keep the velocity order, replace only the G order

§13 verbatim:

> - **Keep** `velocityOrder ≥ 1.5`: measured 1.9190 / 1.9752 (baseline) and 2.0538 / 2.0565
>   (current), stable to 0.5 % between gate and plateau — a genuinely meaningful order.
> - **Replace** `gradientOrder ≥ 1.5` with `|G − G_exact| / |G_exact| ≤ 0.30 %` at the finest grid.
>   *Derivation:* the velocity field is second order and converged; the exact `G = 2 μ A` is
>   analytical; 0.30 % is the accuracy a second-order scheme delivers at `nr = 18` given the
>   measured velocity error, and it sits **above** the iterative uncertainty (≈ 0.17 % of `G_exact`)
>   and **below** the baseline's error.
>   *Non-vacuity:* current 0.154 % ✓ | two-point baseline 0.690 % ✗ | far-cell ×2 ✗.
> - **Record, do not gate**, G's iterative uncertainty (≈ 1e-2 absolute) so no future order claim is
>   made on it without first tightening the pressure tolerance.

**W8A-3 (velocity order, KEPT).** `EXPECT_GE(velocityOrder, 1.5)` on both pairs — unchanged, and now
guarded by W8A-5.

**W8A-4 (G envelope, REPLACES the G order).** At the finest grid `18×45×3`:

```
|G − G_exact| / |G_exact|  <=  0.30 %      G_exact = 2 μ A = −1.8282207204 (analytical)
```

The analytical reference `CurvedExact::pressureGradient()` and the existing extraction are
**unchanged** — §13 does not require otherwise. G's iterative uncertainty (≈ 1e-2 absolute,
≈ 0.17 % of `G_exact`; W8-INV-001 log 08) is **printed, not gated**.

## 5. W8-R4 — order-gate validity precondition

§13 verbatim:

> Any future observed-order assertion on these cases must first demonstrate that the quantity is
> **iteratively converged**: |q(gate) − q(20000 iterations)| ≤ 10 % of |q − q_exact|. Measured today:
> StructuredQuad velocity **42 %** ✗, dp/dx at 96×12 **1083 %** ✗, MultiBlock G **140–507 %** ✗,
> MultiBlock velocity **0.1–0.5 %** ✓. This is why only the MultiBlock velocity order survives.

**W8A-5.** The MultiBlock velocity order (W8A-3) is the **only** observed-order assertion the
amended W8 retains, so it is the only one this precondition governs. It is implemented as an
**executable runtime check**, not a comment, because a documented-only precondition cannot catch a
future regression:

```
|L2(gate) − L2(plateau)|  <=  0.10 · |L2(gate) − 0|        (velocity's exact value is 0)
```

`L2(plateau)` re-solves the **same** grid with the convergence tolerance made unreachable
(×1e-7) and a fixed iteration budget `B`, extracting the metrics unconditionally. The assertion is
ordered **before** `EXPECT_GE(velocityOrder, 1.5)` in the log so a contaminated field is reported as
the cause rather than the symptom.

**Pre-registered scope:** the check is applied at the **finest** grid (`18×45×3`) only. A fixed
iterative error floor is largest *relative to the discretisation error* on the grid whose
discretisation error is smallest, so the finest grid is the binding one; the coarse and medium
drifts are recorded from W8-INV-001 log 08 rather than re-measured, to keep the added runtime to one
solve.

**Pre-registered rule for `B`** — fixed **before** `logs/12_r4_calibration.log` was read, so `B` is a
measured consequence and not a tuned choice:

> `B` = the smallest budget in {5000, 10000, 20000} whose measured velocity-L2 drift at 18×45
> reproduces the 20000-iteration drift to within **20 % relative**. If none qualifies, `B` = 20000.

§13's criterion is stated at 20000 iterations; this rule can only select a `B` that reproduces that
same drift, and it never weakens the 10 % threshold.

## 6. Non-vacuity — pre-registered, must hold before amended W8 may be called PASS

§13 verbatim:

> | control | R1 | R2 | R3 |
> |---|---|---|---|
> | far-cell sign flipped | **rejects** (no convergence) | — | — |
> | far-cell ×2 | passes monotonicity | **rejects** (4.9× / 65× over) | **rejects** |
> | pre-DIFF-002 two-point | passes | **rejects** (1.15× / 2.2× over) | **rejects** (2.2× over) |
> | current DIFF-002 | passes | **passes** | **passes** |
>
> Monotone decrease alone is **insufficient** (the far-cell ×2 control still decreases), which is why
> every proposal pairs direction with magnitude.

**Provenance of §13's control cells — disclosed before running anything.** W8-INV-001 §11 measured
the corrupted-operator controls on the **StructuredQuad case only, at 64×8 and 96×12**
(`logs/09`, `logs/10`). Therefore:

- *measured:* sign flip does not converge (StructuredQuad, both grids); far-cell ×2 velocity L2
  5.3310e-02 at 64×8; the two-point baseline's StructuredQuad values (`logs/07`) and MultiBlock G
  (`logs/11`, 0.690 %).
- *not measured, stated in §13 by extrapolation:* far-cell ×2 on **MultiBlock** (R3 "✗"), far-cell
  ×2 dp/dx at **144×18** (R2 "≫ ✗"), and sign flip on **MultiBlock**.

So §13's table is a **prediction** for those cells, and this amendment must **measure** them. The
controls are run by building the **amended W8 test sources themselves** against each control
library — not a re-implementation — so they test exactly the assertions being frozen.

Required outcome:

| control | library | required |
|---|---|---|
| far-cell **sign flip** | `1e5fbb7b3e758280…` | **rejected** in both tests |
| far-cell **×2** | `078668bacc2d454e…` | **rejected** in both tests, by a **magnitude** criterion or by solve acceptance — *not* by monotone decrease alone |
| pre-DIFF-002 **two-point** | `719d0fc7ae48c027…` | **rejected** in both tests |
| current DIFF-002 | `143a1dda0680bc1d…` | **accepted** |

For each control, **every** amended criterion's individual outcome (W8A-1 per grid, W8A-2, W8A-4,
W8A-5, and the retained velocity order) is recorded, not only the test verdict, so that a rejection
is attributable. A control that is rejected **only** by a criterion other than the amended ones is
reported as such. If any control is **accepted**, **STOP**.

**Monotone error decrease is explicitly NOT an acceptance criterion on its own** — W8-INV-001 proved
that vacuous, because the far-cell ×2 control decreases monotonically too. Every replacement pairs
direction with magnitude.

## 7. What may not change

No production numerical change for this migration. No MESH-001 or MESH-003 gate modification. No
unrelated MESH-004 ASan fix. No new P12 phase. No commit. No push. No threshold loosened. No
reference replaced by production output. No previously failed result erased. The **GRAD-002
iterative-drift question stays a separate open item** and is not investigated here.

## 8. Verdict rule

Amended W8 is **PASS** only if all of: (a) only the three identified assertions changed, verified by
diff against the frozen sources; (b) the eight production files are at their frozen hashes; (c)
MESH-001's and MESH-003's gates are byte-identical; (d) both tests pass from a fresh authoritative
build, with binary/library hashes and exact test counts recorded; (e) all three non-vacuity controls
are rejected as tabulated. Otherwise **FAIL**, recorded as such, and **STOP** at the first failed
gate. If a non-vacuity control unexpectedly *passes*, **STOP**. If a criterion cannot be reproduced
exactly from §13, **STOP**. No further amendment is self-authorized.
