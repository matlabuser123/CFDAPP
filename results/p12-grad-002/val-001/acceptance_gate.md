# P12-GRAD-002-VAL-001 — Acceptance Gate (FROZEN before the test was modified)

Frozen: 2026-09-17. Evidence: `results/p12-grad-002/val-001/`.
Library under test: `build/release/src/libcfdcore.a`
sha256 `143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a`.

Scope: **one assertion in one test** —
`GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`.
**No production numerics. No other GRAD-002 test. No W8. No MESH-007.**

## 1. The preserved failure (unchanged)

`results/p12-diff-002/final-w7/logs/02_greengauss.log`
sha256 `5247098ee6ee7940b67e33f914f8054ef6a3ca91c76cd39c64c9bc6a22d13dc6`:

```
8x8    0.125      0.0522508    --
16x16  0.0625     0.0136592    1.93558
32x32  0.03125    0.00348864   1.96913
64x64  0.015625   0.000881911  1.98396
Expected: (p) < (maxObservedOrder), actual: 1.9355810765555042 vs 1.9
```

## 2. The assertion's original purpose, recovered (step 3)

From the test's own comment (`test_grid_refinement.cpp`, unchanged):

> GreenGauss on a DISTORTED mesh **loses its Cartesian-specific boundary advantage** (the
> paired-face-area assumption is no longer exact) — **its global rate drops to ~1.58–1.72**,
> similar to least-squares' global rate on either mesh. Recorded honestly (task requirement 8:
> "also test Green-Gauss on distorted meshes… **record its actual performance rather than
> assuming second order**").

The band `[1.4, 1.9]` is therefore a **descriptive envelope around a measured degradation**, not a
derived correctness property. Its upper bound encodes "the boundary ring must remain first order".

## 3. Independent derivation (step 4) — stated before measuring

Green–Gauss: `grad(phi)_P ≈ (1/V_P) Σ_f phi_f S_f`.

- **Interior cell.** Linear interpolation has the exact error
  `phi(x_f) − phi_interp(x_f) = −½ w(1−w) L² ∂²phi/∂ξ² + O(h³)` = O(h²). Opposite faces carry
  opposite `S_f` and equal leading errors, so the **pair cancels**; O(h³) per face × |S|~h ÷ V~h²
  ⇒ **interior order 2**.
- **Boundary cell, plain (pre-GRAD-002).** The boundary face value is *exact*; the opposite
  interior face is still interpolated and keeps its O(h²) error. The cancellation is broken:
  O(h²)×h ÷ h² ⇒ **ring order 1**.
- **Global volume-weighted L2.** The ring is ~4n cells of volume ~h², i.e. volume fraction ~4h:
  `L2² ~ C_i²h⁴ + 4C_b²h³` → the h³ term dominates ⇒ **plain global order → 1.5**.
- **Boundary cell, GRAD-002.** The correction removes that leading O(h²) interpolation bias from
  the opposite face, restoring the cancellation ⇒ ring O(h²), `L2² ~ C_i²h⁴ + 4C_b'²h⁵`
  ⇒ **global order → 2, approached strictly from below**.

## 4. Measured (step 5, 6) — `logs/01_refinement.log`, grids 16…256

| series | 16→32 | 32→64 | 64→128 | 128→256 | predicted |
|---|---|---|---|---|---|
| **plain** boundary ring L2 | 1.0604 | 1.0230 | 1.0088 | **1.0036** | 1 |
| **plain** global L2 | 1.6393 | 1.5802 | 1.5440 | **1.5233** | → 1.5 |
| **plain** global L∞ | 0.9784 | 0.9945 | 0.9986 | **0.9997** | 1 |
| **GRAD-002** boundary ring L2 | 1.9415 | 1.9728 | 1.9870 | **1.9936** | 2 |
| **GRAD-002** global L2 | 1.9691 | 1.9840 | 1.9918 | **1.9958** | → 2 from below |
| interior L2, **both** | ~2.02 | ~2.00 | ~2.00 | **2.0002 / 2.0008** | 2 (unchanged) |

Every prediction confirmed. The interior is second order in **both** treatments — the change is
confined to the boundary ring, exactly where GRAD-002 acted.

**Control fidelity:** the plain-Green–Gauss reconstruction gives 1.7204 / 1.6393 / 1.5802 on the
test's own grids — reproducing the comment's documented "~1.58–1.72" range, so the historical
control is faithful. (On this distorted family the pre-GRAD-002 code's aligned paired-fit branch
never applied — it required `cross(d, S_f) == 0` — so plain Green–Gauss *is* the historical
treatment here.)

## 5. Does the existing upper bound detect a correctness property? (step 6)

No. Evaluated on the same variants (`logs/02_controls.log`), the superseded band `[1.4, 1.9]`:

| variant | orders | `[1.4, 1.9]` |
|---|---|---|
| CURRENT (correct second-order boundary) | 1.9356 / 1.9691 / 1.9840 | **REJECTS** |
| old / discontinuous plain Green–Gauss | 1.7204 / 1.6393 / 1.5802 | **ACCEPTS** |
| first-order boundary value | 0.5517 / 0.5128 / 0.5032 | rejects |

It accepts the limitation and rejects its removal. Its **lower** bound (1.4) still carries real
content — it rejects a first-order collapse — and is **kept**.

## 6. Classification (step 8)

**`MIGRATE_VALIDATION`.** Not `PRODUCTION_DEFECT` (the interior order is unchanged at 2, the ring
improved 1 → 2, L∞ improved 1 → 2, and errors decrease at every level and every refinement). Not
`UNCERTAIN` (the derivation predicted every measured number before it was taken). Not `KEEP`
(§5).

## 7. The replacement criterion, derived (step 9/10)

The amended assertion changes **only the two band arguments at this one call site**. Structure,
harness, mesh, field, norm and the monotone-decrease assertion are untouched.

```
runGradientSchemeGridRefinement(GreenGauss, distorted=true, …, restrictToInterior=false,
                                1.4, 1.9)   ->   (…, 1.875, 2.15)
```

**Derivation of the band, not a widened guess.** The corrected treatment gives
`global L2 = C_i h² √(1 + k h)`, so the pairwise order is `p(n) = 2 − c/n + O(1/n²)`. Fitting `c`
to the measured deficits gives `c = 0.515, 0.494, 0.512, 0.525, 0.538` over pairs (8,16)…(128,256)
— stable at ≈ 0.5. With a **2× safety factor** (`c_max = 1.0`), the coarsest pair the test uses
(n = 8) predicts `p ≥ 2 − 1.0/8 = 1.875`. The order approaches 2 strictly from below, so the upper
bound only has to reject a spurious super-convergence artifact: `2 + 0.15 = 2.15`.

Predicted vs measured on the test's own grids, from `p(n) = 2 − 0.5/n`:

| pair | model | measured |
|---|---|---|
| 8→16 | 1.9375 | 1.93558 |
| 16→32 | 1.96875 | 1.96913 |
| 32→64 | 1.984375 | 1.98396 |

Agreement to ≤ 0.002 — the band follows the model, not the other way round.

## 8. Non-vacuity, pre-registered (step 7) — `logs/02_controls.log`

| variant | orders on {8,16,32,64} | `[1.875, 2.15]` | expected |
|---|---|---|---|
| CURRENT (GRAD-002) | 1.9356 / 1.9691 / 1.9840 | **PASS** | PASS |
| (a) old / discontinuous plain Green–Gauss | 1.7204 / 1.6393 / 1.5802 | **FAIL** | FAIL |
| (b) first-order boundary value (owner cell) | 0.5517 / 0.5128 / 0.5032 | **FAIL** | FAIL |
| (c) wrong boundary coefficient **sign** | −0.5083 / −0.5023 / −0.5006 (errors grow) | **FAIL** | FAIL |
| (d) O(h) boundary-value perturbation | 0.6053 / 0.5208 / 0.5043 | **FAIL** | FAIL |

All five behaved as pre-registered. Margin of the live criterion at the tightest pair:
1.93558 vs the 1.875 floor = **0.0606**, i.e. 1.2× the model's own deficit at that grid.

## 9. Pre-modification hashes

The only file VAL-001 may modify, at its pre-modification hash:

```
1d6816a1388a2cb736a7fd2779393c66a297c5149152f634c617b0f10b602bac  tests/unit/discretization/test_grid_refinement.cpp
```

Unchanged support files:

```
ee2a59a64a97a10d9b01b6e400d02601d97055c3af9c7de5a674d4e268990812  tests/unit/discretization/DistortedMesh.hpp
fcbc170f393c963288864f36e5447f18ce6dc67d57070b6624673f8a1561167b  tests/unit/discretization/CMakeLists.txt
```

The full authoritative list is in `logs/00_freeze.log`: the test file, `DistortedMesh.hpp`, the
gradient/boundary production files, `libcfdcore.a`, all GRAD-001/GRAD-002 gates, all 11 DIFF-002
gates and the MESH-007 gate. Every production file and every gate must be **byte-identical** after
VAL-001.

## 10. Pass condition

Fresh rebuild; the named test passes; the complete `GridRefinementTest` suite, the gradient tests
and the MMS tests show **no new failures**; then a fresh authoritative DIFF-002 **W7** from a clean
rebuild (the 1923/1919/4 result is **not** reused), and W8 only if W7 legitimately passes.

## 11. Recorded limitations

- The band is calibrated on this test's own distorted family and grid range; it is not a general
  statement about Green–Gauss on arbitrary meshes.
- `p(n) = 2 − c/n` is an empirical fit of the sub-leading term; only the leading order 2 and the
  "from below" direction are derived from first principles.
- The test's **lower** bound is raised 1.4 → 1.875, which *tightens* it; the upper bound moves
  1.9 → 2.15 because the quantity it measures is now genuinely second order.
