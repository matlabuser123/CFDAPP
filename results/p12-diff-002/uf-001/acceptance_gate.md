# P12-DIFF-002-UF-001 — Acceptance Gate (FROZEN before any authoritative test was modified)

Frozen: 2026-09-17. Investigation: `results/p12-diff-002/uf-001/`.
Library under test: `build/release/src/libcfdcore.a`
sha256 `143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a`.

**No production source is modified by UF-001.** Only the two authorized U-F validation tests change.
**The de Vahl Davis reference values are not changed, redefined or replaced.**

## 1. Literature source and FIXED values

de Vahl Davis, G. (1983), *Natural convection of air in a square cavity: A bench mark numerical
solution*, IJNMF 3(3):249–264. Cross-check source re-fetched in this investigation:
Wan, Patnaik & Wei (2001), NHT-B 40(3):199–228 — `data/wan_patnaik_wei_2001.pdf`
sha256 `8a7ca469e4abf2fc90bfcfb6abf7a67af04679b404814b68d4bc4591a0dd1985`, text
`data/wan_2001.txt`.

Ra = 10³, Pr = 0.71. **Unchanged, and used only as the comparison target:**

```
Nu_avg = 1.12        u_max = 3.634 at y = 0.813        v_max = 3.679 at x = 0.179
```

## 2. Benchmark definitions, established from the source (UF-001.2)

| item | definition (verbatim from the source where quoted) |
|---|---|
| geometry | square cavity `Lx = Ly = L = 1`; left hot `θ=1`, right cold `θ=0`, both no-slip; top/bottom adiabatic, no-slip |
| formulation | Boussinesq; gravity in `−y` |
| nondimensionalisation | velocity `U = u L/α` (thermal-diffusivity scale); `θ = (T−T_cold)/(T_hot−T_cold)`; `Ra = gβΔT L³/(να)`; `Pr = ν/α` |
| `Nu_avg` | Table 5 / eq. (55): `Nu = ∫₀¹ Nu_local dy` |
| `u_max` | Table 3 title: **"maximum horizontal velocity (u) at the mid-width (x = 0.5)"** |
| `v_max` | Table 2 title: **"maximum vertical velocity (v) at the mid-height (y = 0.5)"** |
| convention | **mid-plane extrema**, not global field extrema |
| grid provenance | **h→0 Richardson-extrapolated** ("the 'grid-independent' values from the reference solutions obtained by Richardson extrapolation", arXiv `physics/0305049` Table 1 and text) |

## 3. CFDApp definitions and sampling convention

| item | CFDApp |
|---|---|
| `Nu_avg` | `computeAverageNusselt(computeLocalNusseltAtHotWall(...))`, `Nu_local = (T_wall − T_P)/(h/2)` — a one-sided half-cell difference |
| `u_max` | `findMax(extractUProfileAtMidWidth(..., x = L/2, ...))` |
| `v_max` | `findMax(extractVProfileAtMidHeight(..., y = H/2, ...))` |
| location convention | **matches** the literature (mid-planes) |
| peak convention | `findMax` = **discrete maximum over cell-centre samples** — a structurally always-low, grid-phase-dependent estimator of the continuous profile peak the literature reports |
| existing tolerance | `uMaxError < 0.15`, `vMaxError < 0.12`, `nuAvgError < 0.10`, all at 10×10 |

## 4. Nondimensional equivalence (UF-001.3)

`ρ=1`, `μ=Pr`, `k=cp=1`, `L=1`, `T_hot=1`, `T_cold=0`, `T_ref=0.5`, `|g|=1`, `β=Ra·Pr`; buoyancy
`f_y = +Ra·Pr·(T−T_ref)`. Hence `Ra = 1·(Ra·Pr)·1·1/(Pr·1) = Ra` exactly, `Pr = ν/α = Pr`,
velocity scale `α/L = 1`, `θ = T`. The only difference from the benchmark's source is the
**constant** `−Ra·Pr·0.5`, absorbable into pressure.

**Verified** (`logs/04_equivalence.log`): re-solving with `T_ref = 0` changes velocity by
`2.7e-13` and temperature by `7.3e-15` (`8.6e-14` relative). *Disclosed:* the secondary
prediction that the pressure difference is exactly the continuum linear field does not hold
discretely (residual 71 of a 355 range) — a collocated-coupling property, not an equivalence
failure.

## 5. Raw grid data (UF-001.4) — relative error against the FIXED literature value

`logs/01,02` (current) and `logs/05,06` (pre-DIFF-002 baseline, library
`719d0fc7ae48c027756430a8473a9b47ef3f516167aa7eb7319fa3eb468c50fe`, one block removed,
`logs/03`). All grids converged; mass imbalance 0.0e+00 and max wall flux 0.0e+00 everywhere.

| n | Nu_avg cur / base | u_max cur / base | v_max cur / base |
|---|---|---|---|
| 10 | 0.02246 / 0.04856 | 0.13703 / 0.10442 | **0.13026** / 0.06714 |
| 15 | 0.01580 / 0.02721 | 0.05199 / 0.02607 | 0.06233 / 0.03703 |
| 20 | 0.01218 / 0.01846 | 0.05375 / 0.03861 | 0.04713 / 0.03145 |
| 30 | 0.00815 / 0.01083 | 0.02768 / 0.02102 | 0.02653 / 0.01977 |
| 40 | 0.00588 / 0.00735 | 0.01785 / 0.01412 | 0.01874 / 0.01501 |

`0.13026` is the failing measurement, against the existing `0.12`.

## 6. Extrema-sampling analysis (UF-001.5)

Same converged fields, five conventions. At 10×10 the sampling convention alone accounts for
**2.6 of the 13.0 percentage points** of `v_max` error (discrete 0.13026 → parabolic sub-grid
0.10392), and the recovered peak location moves from `x = 0.150` to `x = 0.187` against the
literature's `0.179`. The bias collapses with refinement (2.6 / 0.4 / 0.04 points at n = 10/15/20).
Real and quantified, but **not** by itself the cause of the failure.

## 7. Convergence and continuum limit (UF-001.6, .8)

Parity rule (carried from UC-001, and independently corroborated by this file's own
`GridConvergence` comment "u_max (a cell-centre-sampled extremum) oscillates"): the mid-plane is a
cell-centre line for odd `n` and interpolated for even `n`, so order estimates use
**parity-consistent** subsequences only. The even family is 10/20/30/40; the constant-ratio
triplet is **10/20/40**.

Pairwise observed orders (current, even): Nu_avg 0.883 / 0.991 / 1.134; u_max 1.350 / 1.636 /
1.525; v_max 1.467 / 1.417 / 1.208. The scheme's documented formal order is **1** (first-order
upwind convection, `SIMPLESettings::convectionScheme` default, never overridden).

Richardson on 10/20/40 against the FIXED literature value:

| quantity | current: p, extrapolated, GCI, dist | baseline: p, extrapolated, GCI, dist |
|---|---|---|
| Nu_avg | 0.708, 1.115449, 0.01236, **0.00406** | 1.438, 1.120958, 0.00806, **0.00086** |
| u_max discrete | 1.214, 3.667949, 0.03461, **0.00934** | 1.426, 3.635447, 0.01841, **0.00040** |
| v_max discrete | 1.550, 3.664218, 0.01875, **0.00402** | 1.118, 3.675424, 0.01782, **0.00097** |
| v_max spline | 1.266, 3.699829, 0.02788, **0.00566** | 0.881, 3.715820, 0.02841, 0.01001 |

Every credible extrapolation's distance from the literature is **within its own GCI**. One
baseline sequence (`v_max` parabolic, p = 0.062) is reported as NOT computed rather than printing
its artifact extrapolation of 5.19.

**Is 10×10 in the asymptotic range? No.** Its errors are 2.2 % (Nu_avg) and 13.0 % (v_max), the
pairwise orders still drift across the family, and the literature value is an `h→0` value.

**No continuum-limit regression.** The head-to-head error ratio between the two libraries shrinks
monotonically with refinement — `v_max` 1.940 → 1.499 → 1.248 at n = 10/20/40, `Nu_avg` 0.463 →
0.660 → 0.800 — the signature of a **common continuum limit with different error constants**.

## 8. Momentum-wall verification (UF-001.7)

Manufactured analytic field, production `boundaryFaceDiffusionTerms`, no CFD solve
(`logs/07,08`). Worst relative wall-flux error on the bottom wall:

| field | two-point | order | DIFF-002 | order |
|---|---|---|---|---|
| constant | 0.000e+00 | exact | 3.331e-16 | exact |
| linear | ~1e-15 | exact | ~1e-15 | exact |
| quadratic | 7.059e-02 → 4.412e-03 | **1.000** | ~1e-15 | **exact** |
| cubic | 6.897e-02 → 4.405e-03 | **0.998** | 4.853e-03 → 1.896e-05 | **2.000** |
| sine | 2.649e-02 → 1.528e-03 | **1.008** | 6.236e-03 → 2.575e-05 | **1.995** |

`cP − cF = cB` 4.441e-16; constant-field flux 4.996e-16; 80/80 faces higher-order.
**`CORRECTED_DISCRETIZATION`** — established from the analytic wall shear and the order study, not
inferred from conservation improving.

## 9. Why the existing criterion is not a valid application of the benchmark

Two independent findings:

1. **Threshold provenance.** The test states its bounds were "locked from diagnosed evidence
   (coarsest grid, largest error): u_max 10.1 %, v_max 6.7 %, Nu_avg 4.9 % — margin above each."
   The pre-DIFF-002 baseline measures **6.714 %** (v_max) and **4.856 %** (Nu_avg) at 10×10 — the
   quoted figures to the digit shown. The `0.12` bound is therefore a **regression lock on the
   then-current production output at one non-asymptotic grid**, with the literature value used
   only to compute that error. Any correct change of the discretisation necessarily breaks it.
2. **The failing criterion is anti-correlated with wall-operator correctness at this grid.** The
   deliberately corrupted far-cell-doubled operator gives `v_max` error **0.01855** at 10×10
   against the correct code's **0.13026** — the corrupted operator looks **7× better** on the very
   assertion that fails. (The old test as a whole still rejects that control, via its `Nu_avg`
   bound of 0.10 against a measured 1.92; it is the `v_max` assertion specifically that is
   anti-correlated.)

## 10. Pre-registered replacement criteria and dry run (UF-001.10)

Evaluated on the parity-consistent pair **n = 10 and n = 20** (both even). `logs/13_gate_dryrun.log`.

| id | criterion | rationale |
|---|---|---|
| P1 | `err_20 < err_10` against the FIXED literature value, each of Nu_avg / u_max / v_max | refinement must move **toward** the benchmark |
| P2 | `err_10 / err_20 ≥ 1.5` | formal order 1 predicts a factor 2.0 per doubling; 1.5 admits a 25 % pre-asymptotic shortfall |
| P3 | `err_20 ≤ 0.08` (u_max, v_max), `≤ 0.05` (Nu_avg) | **this file's own already-written `DISABLED_Grid20x20` bounds** — the precision claim moves from the non-asymptotic 10×10 grid to 20×20 at thresholds already in the file. No threshold is loosened. |
| P4 | extremum **location** within one cell of the literature's own reported location: `\|y(u_max) − 0.813\| ≤ 1/n`, `\|x(v_max) − 0.179\| ≤ 1/n`, at both grids | uses literature data (`DeVahlDavis1983.hpp`'s `uMaxY`/`vMaxX`) that the current tests **never used** |

Every existing health assertion is **kept unchanged**: `flowConverged`, `thermalConverged`,
`globalMassImbalance < 1e-6`, `maxWallNormalFlux < 1e-6`, `heatImbalance < 0.05`,
`minTheta ≥ −1e-6`, `maxTheta ≤ 1+1e-6`.

Dry run — **all four behaved as pre-registered**:

| library | P1–P4 | note |
|---|---|---|
| CURRENT (DIFF-002) | **PASS 13/13** | |
| pre-DIFF-002 two-point | **PASS 13/13** | correct: a legitimate lower-order scheme, not a defect |
| far-cell coefficient **doubled** | **FAIL 5/13** | Nu_avg error 1.92 → 4.43 (**grows** under refinement) |
| far-cell coefficient **sign flipped** | **FAIL 6/13** | Nu_avg 2.91 → 6.46; u_max 0.296; v_max 0.311 |

**Honest scope limit, recorded rather than overclaimed:** P1–P4 are **not**
wall-operator-discriminating — the superseded two-point scheme passes them, correctly, because it
converges to the same continuum limit. The wall operator's correctness is pinned by §8's
wall-shear order study, not by these criteria.

## 11. Classification (UF-001.9)

| test | classification |
|---|---|
| `NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3` | **REDESIGN_GRID_CONVERGENCE** |
| `NaturalConvectionValidation.Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3` | **REDESIGN_GRID_CONVERGENCE** |

Not `PRODUCTION_DEFECT` (§8: the wall treatment is second-order and quadratic-exact; §7: no
continuum-limit regression). Not `BENCHMARK_DEFINITION_MISMATCH` (§2/§3: the location convention
matches; the peak-extraction bias is quantified at 2.6 of 13.0 points and is not the cause). Not
`UNCERTAIN` (§2: the definition is established from the source). Not `KEEP_BOUND` (§9).
The user's category **C**, with **B** as the mechanism and **D** as a quantified contributor.

## 12. Pre-modification hashes

```
ca7ab2b2297c2754b19e2e90dd83ada9db36fef76854e1ce90817bbd0299fa45  test_natural_convection_validation.cpp
d9f5adabf41f7638331161e5d4702199db4a50559733eab395339beb41c7a8e7  NaturalConvectionValidationUtils.hpp
7496fe907e343c8c828f5be4a3ed7c65053b53d2082d86cd0fae5210623d8232  NaturalConvectionValidationUtils.cpp
35cbb3a5ad2ac0d29f2f9c92a00f194b05d7c21ff340bb1934eca1bfb969f52e  DeVahlDavis1983.hpp   (NOT modified)
```

`DeVahlDavis1983.hpp` and `validation/literature/natural_convection/**` must be **byte-identical**
after UF-001.

## 13. Pass condition (UF-001.13, .14)

Fresh rebuild; P1–P4 hold and both tests pass; the natural-convection / heated-cavity / thermal /
momentum / boundary-diffusion / MMS / conservation suites show **zero new unexplained failures**;
then a fresh authoritative W7 with exact counts, remaining failures separated into
DIFF-002-related-unresolved / pre-existing-historical / unrelated-known.

## 14. Recorded limitations

- The two tests now also solve 20×20 (~36 s each), so each grows from ~4 s to ~40 s.
- `Nu_avg`'s P2 margin is the thinnest: ratio 1.844 against the 1.5 floor (1.23×).
- The 10×10 benchmark errors are **kept and reported** as diagnostics; they are no longer an
  acceptance bound.
- P1–P4 do not discriminate the wall operator (§10).
- The continuum extrapolations rest on 10/20/40; `n = 80` was not run (≈100 min per library).
