# P12-DIFF-002-UF-001 — Summary

Two tests fail on one assertion — `EXPECT_LT(r.vMaxError, 0.12)`, measured **0.1303** on the
10×10 grid. This investigation determines why, without changing the de Vahl Davis reference.

**Evidence classes are kept apart throughout.** Literature value / literature definition /
CFDApp quantity / CFDApp sampling convention / existing test tolerance are labelled separately in
every section below, and no CFDApp output is ever used as a reference.

---

## UF-001.2 — The benchmark definition, established independently

Re-fetched the open source the repo's own provenance names
(`https://users.math.msu.edu/users/wei/paper/p59.pdf`, Wan, Patnaik & Wei 2001,
sha256 `8a7ca469e4abf2fc90bfcfb6abf7a67af04679b404814b68d4bc4591a0dd1985`, extracted with
`pdftotext -layout` to `data/wan_2001.txt`) and read the table headers verbatim:

| item | established value / definition |
|---|---|
| Rayleigh number | `Ra = g β ΔT L³/(ν α)`, case Ra = 10³ |
| Prandtl number | `Pr = ν/α = 0.71` |
| geometry | square cavity, `Lx = Ly = L = 1` |
| hot / cold walls | left `θ=1`, right `θ=0`, both no-slip |
| horizontal walls | adiabatic, no-slip |
| formulation | Boussinesq |
| nondimensionalisation | velocity `U = u L/α` (thermal-diffusivity scale, **not** viscous); `θ = (T−T_cold)/(T_hot−T_cold)` |
| `Nu_avg` | Table 5 / eq. (55): `Nu = ∫₀¹ Nu_local dy` |
| `u_max` | **Table 3 title: "maximum horizontal velocity (u) at the mid-width (x = 0.5)"** → `3.634` at `y = 0.813` |
| `v_max` | **Table 2 title: "maximum vertical velocity (v) at the mid-height (y = 0.5)"** → `3.679` at `x = 0.179` |

So the literature convention is **mid-plane extrema**, not global field extrema, and CFDApp's
`extractUProfileAtMidWidth` / `extractVProfileAtMidHeight` sample at exactly those lines — the
**location** convention matches. The repo's stored values match the source verbatim.

**One further fact, decisive for how the values may be used.** de Vahl Davis's tabulated numbers
are **h→0 Richardson-extrapolated**, not values on any particular mesh. Independently sourced
from the spectral-element study arXiv `physics/0305049`, whose Table 1 is captioned "Computed
Nusselt numbers … compared to the **extrapolated** results of the reference solutions of de Vahl
Davis (1983) and Hortmann et al. (1990)" and whose text states it compares "with the
'grid-independent' values from the reference solutions **obtained by Richardson extrapolation**".
The same paper records de Vahl Davis's method as a second-order finite-difference
stream-function/vorticity scheme on a regular mesh, consistent with Wan et al.

Benchmark definition is therefore **established**, not uncertain.

---

## UF-001.3 — Nondimensional equivalence, derived and then verified

CFDApp's case sets `ρ=1`, `μ=Pr` (so `ν=Pr`), `k=cp=1` (so `α=1`), `L=1`, `T_hot=1`, `T_cold=0`,
`T_ref=0.5`, `|g|=1` in `−y`, `β=Ra·Pr`. `BoussinesqBuoyancy::source` returns
`f = −ρβ(T−T_ref)g`, i.e. `f_y = +Ra·Pr·(T−T_ref)` — hot fluid rises. Hence CFDApp solves

```
(u·∇)u = −∇p + Pr ∇²u + (0, Ra·Pr·(T − 0.5))        (u·∇)T = ∇²T
```

against the benchmark's

```
(U·∇)U = −∇P + Pr ∇²U + (0, Ra·Pr·θ)                (U·∇)θ = ∇²θ,   θ = T
```

Derived checks: `Ra = 1·(Ra·Pr)·1·1/(Pr·1) = Ra` **exactly**; `Pr = ν/α = Pr`; velocity scale
`α/L = 1`, so the raw solver velocity already *is* the benchmark's nondimensional velocity;
`θ = (T−0)/(1−0) = T`. The two momentum sources differ only by the **constant** `−Ra·Pr·0.5` in
`y`, a uniform body force absorbable into the pressure.

**Verified rather than asserted** (`logs/04_equivalence.log`): re-solving with `T_ref = 0`
(θ = T, the benchmark's own source) changes the velocity field by `2.7e-13` and the temperature
field by `7.3e-15` — `8.6e-14` relative on a velocity scale of 3.20. The problems are equivalent.

*Disclosed:* the secondary prediction that the two runs' **pressure** difference is exactly the
continuum linear field `−Ra·Pr·0.5·y` does **not** hold discretely (worst residual 71 against a
355 range). That is a property of the collocated pressure–velocity coupling, not of the
equivalence; the benchmark compares velocity, temperature and Nusselt number, all of which are
invariant as measured.

---

## UF-001.5 — Field error versus extrema-sampling error

The existing instrument is `findMax`, a **discrete maximum over the cell-centre samples** of the
mid-plane profile (plus the two no-slip wall anchors). On 10×10 the v-profile samples sit at
`x = 0.05, 0.15, … 0.95`, while the benchmark's peak is at `x = 0.179`. A discrete sample maximum
is a **structurally biased, always-low** estimator of a smooth profile's peak, and the bias
depends on where the peak happens to fall between samples — so it is not a smooth function of `h`.

Same converged fields, five conventions (`tools/uf001_grids.cpp`), current library:

| n | v_max discrete (current) | parabolic sub-grid | spline | global field | literature |
|---|---|---|---|---|---|
| 10 | 3.199792 @ x=0.150 → err **0.1303** | 3.296679 @ x=0.187 → err 0.1039 | 3.232642 → 0.1213 | 3.201133 | 3.679 @ 0.179 |
| 15 | 3.449694 @ x=0.167 → 0.0623 | 3.465113 @ x=0.180 → 0.0581 | 3.457017 → 0.0603 | 3.454079 | |
| 20 | 3.505618 @ x=0.175 → 0.0471 | 3.507015 @ x=0.180 → 0.0467 | 3.505623 → 0.0471 | 3.523318 | |

At 10×10 the sampling convention alone accounts for **2.6 of the 13.0 percentage points** of
`v_max` error (0.1303 → 0.1039), and the recovered peak *location* moves from `x = 0.150` to
`x = 0.187` against the literature's `0.179`. The remaining ~10.4 points are genuine field
discretisation error. The bias shrinks fast with refinement (2.6 points at n=10, 0.4 at n=15,
0.04 at n=20), which is why it is invisible on the finer grids.

So the sampling convention is a **real, quantified contributor** but **not** by itself the cause
of the failure.

---

## UF-001.7 — The momentum-wall attribution, re-verified

INV-002's factorial attributed the coarse-grid velocity-extrema change to the momentum wall shear.
That attribution is preserved. What UF-001 adds is whether that change is *correct*, tested
independently of any CFD solve: an analytic field `u_x = f(y)` is imposed, the wall's prescribed
value set to `f(y_wall)` exactly, and the flux the production
`boundaryFaceDiffusionTerms` represents is compared against the exact `μ f'(y_w)|S|`
(`tools/uf001_wallshear.cpp`, run against both libraries).

Worst relative wall-flux error over the bottom wall:

| field | two-point (pre-DIFF-002) | order | DIFF-002 (current) | order |
|---|---|---|---|---|
| constant | 0.000e+00 | exact | 3.331e-16 | exact |
| linear | ~1e-15 | exact | ~1e-15 | exact |
| quadratic | 7.059e-02 → 4.412e-03 | **1.000** | ~1e-15 | **exact** |
| cubic | 6.897e-02 → 4.405e-03 | **0.998** | 4.853e-03 → 1.896e-05 | **2.000** |
| sine | 2.649e-02 → 1.528e-03 | **1.008** | 6.236e-03 → 2.575e-05 | **1.995** |

Identity `cP − cF = cB`: 4.441e-16. Constant-field boundary flux (conservation): 4.996e-16, on
80 of 80 boundary faces carrying a far-cell term (0 fallback).

**Classification: `CORRECTED_DISCRETIZATION`.** The momentum wall shear goes from first to second
order and becomes exact for quadratics; it is *more* accurate than the two-point form at every
resolution tested (4.2× better on the sine field at n = 10). Correctness is established from the
analytic wall shear and the order study, **not** inferred from conservation having improved.

*Sign convention, disclosed:* the first run of this probe reported a relative error of exactly
`2.000000` for the linear and quadratic fields — the magnitudes already agreed to the last digit
and the probe's comparison sign was wrong. `NonOrthogonalDiffusion` defines `dφ/ds` with `s`
inward and calls `Γ|S|(−dφ/ds)` the "flux into the owner" (the sign that keeps `+Γ|S|cP` on the
diagonal, as a diffusion operator requires), which is minus the physical inward viscous flux. The
probe was corrected, not the expectation.

---

## UF-001.4 — Raw grid data, both trajectories

Five grids per library, every quantity recorded raw before any error was computed
(`logs/01,02` current; `logs/05,06` baseline). All ten runs converged, with the outer Picard loop
meeting its own 1e-8 tolerance; **mass imbalance 0.0e+00 and max wall-normal flux 0.0e+00 on every
grid of both libraries**; heat imbalance ≤ 1.3e-07 (current) and ≤ 1.1e-11 (baseline).

Relative error against the FIXED literature value:

| n | Nu_avg cur / base | u_max cur / base | v_max cur / base |
|---|---|---|---|
| 10 | 0.02246 / 0.04856 | 0.13703 / 0.10442 | **0.13026** / 0.06714 |
| 15 | 0.01580 / 0.02721 | 0.05199 / 0.02607 | 0.06233 / 0.03703 |
| 20 | 0.01218 / 0.01846 | 0.05375 / 0.03861 | 0.04713 / 0.03145 |
| 30 | 0.00815 / 0.01083 | 0.02768 / 0.02102 | 0.02653 / 0.01977 |
| 40 | 0.00588 / 0.00735 | 0.01785 / 0.01412 | 0.01874 / 0.01501 |

DIFF-002 is **better for `Nu_avg` at every grid** (ratio 0.463 → 0.800) and **worse for the
velocity extrema at every grid** (`v_max` ratio 1.940 → 1.499 → 1.248). Both ratios move toward 1
under refinement.

---

## UF-001.6 / .8 — Convergence and the continuum limit

The scheme's documented formal order is **1** — `SIMPLESettings::convectionScheme` defaults to
first-order `Upwind` and this case never overrides it. Pairwise observed orders on the
parity-consistent even family (current): Nu_avg 0.883 / 0.991 / 1.134; u_max 1.350 / 1.636 /
1.525; v_max 1.467 / 1.417 / 1.208.

**Is 10×10 asymptotic? No** — 13.0 % velocity error, drifting pairwise orders, and the literature
value is itself an `h→0` value.

Richardson on the only constant-ratio triplet available, **10/20/40**, against the fixed
literature value:

| quantity | current: p / extrapolated / GCI / distance | baseline: p / extrapolated / GCI / distance |
|---|---|---|
| Nu_avg | 0.708 / 1.115449 / 0.01236 / **0.00406** | 1.438 / 1.120958 / 0.00806 / **0.00086** |
| u_max | 1.214 / 3.667949 / 0.03461 / **0.00934** | 1.426 / 3.635447 / 0.01841 / **0.00040** |
| v_max | 1.550 / 3.664218 / 0.01875 / **0.00402** | 1.118 / 3.675424 / 0.01782 / **0.00097** |

Every credible extrapolation lands **within its own GCI** of the literature value. One baseline
sequence (`v_max` parabolic, p = 0.062) is reported as *not computed* rather than printing its
artifact extrapolation of 5.19 — a GCI from a non-asymptotic sequence would be meaningless.

**There is no continuum-limit regression.** Both libraries extrapolate to the literature value
within uncertainty, and the gap between them closes monotonically under refinement — the
signature of a common continuum limit reached with different error constants. A coarse-grid
difference of this kind is categorically different from a continuum-limit difference, and the two
are not conflated here.

---

## UF-001.9 — Classification

Both tests: **`REDESIGN_GRID_CONVERGENCE`**.

Ruled out, each on evidence:

- **`PRODUCTION_DEFECT`** — the momentum wall treatment is second order and exact for quadratics
  (UF-001.7), and there is no continuum-limit regression (UF-001.8).
- **`BENCHMARK_DEFINITION_MISMATCH`** — the mid-plane *location* convention matches the source
  (UF-001.2); the peak-*extraction* bias is real but quantified at 2.6 of 13.0 points and is not
  the cause.
- **`UNCERTAIN`** — the definition is established verbatim from the source.
- **`KEEP_BOUND`** — see the two findings below.

### Why the existing criterion is not a valid application of the benchmark

1. **The bound is a lock on old production output.** The test recorded its thresholds as "locked
   from diagnosed evidence (coarsest grid, largest error): u_max 10.1 %, v_max 6.7 %, Nu_avg
   4.9 % — margin above each". The pre-DIFF-002 baseline measures **6.714 %** and **4.856 %** at
   10×10 — the quoted figures to the digit shown. The literature value entered only as the point
   from which that error was computed; the *bound* came from the code. Any correct change to the
   discretisation necessarily breaks it.
2. **The failing assertion is anti-correlated with wall-operator correctness at this grid.** The
   deliberately far-cell-doubled operator scores `v_max` error **0.01855** at 10×10 against the
   correct code's **0.13026** — 7× "better" on the very assertion that fails. (The old test as a
   whole does still reject that control, through its `Nu_avg` bound of 0.10 against a measured
   1.92; it is the `v_max` assertion specifically that is inverted.)

This is the user's category **C**, with **B** as the mechanism and **D** as a quantified
contributor.

---

## UF-001.10 — Non-vacuity of the replacement

Pre-registered before any test was edited (`logs/13_gate_dryrun.log`), on the parity-consistent
pair n = 10 / 20:

| library | result | note |
|---|---|---|
| CURRENT (DIFF-002) | **PASS 13/13** | |
| pre-DIFF-002 two-point | **PASS 13/13** | correct — a legitimate lower-order scheme, not a defect |
| far-cell coefficient **doubled** | **FAIL 5/13** | Nu_avg error 1.92 → 4.43, *growing* under refinement |
| far-cell coefficient **sign flipped** | **FAIL 6/13** | Nu_avg 2.91 → 6.46; u_max 0.296; v_max 0.311 |

**Recorded rather than overclaimed:** these criteria are *not* wall-operator-discriminating — the
two-point scheme passes them because it converges to the same continuum limit. The wall operator's
correctness is pinned by UF-001.7's order study, not by these criteria.

---

## UF-001.12 — What changed

Only the two authorized tests, in `test_natural_convection_validation.cpp`. **No production
source.** `DeVahlDavis1983.hpp` and `validation/literature/natural_convection/**` are
byte-identical.

The three hand-locked 10×10 bounds (`uMaxError<0.15`, `vMaxError<0.12`, `nuAvgError<0.10`) are
replaced by `expectApproachesDeVahlDavis(coarse, fine)` on the parity-consistent 10/20 pair:

- **P1** each error must shrink toward the fixed literature value;
- **P2** by ≥ 1.5× per doubling (formal order 1 predicts 2.0; 1.5 admits a 25 % shortfall);
- **P3** the 20×20 errors must meet **this file's own already-written** `DISABLED_Grid20x20`
  bounds (0.08 / 0.08 / 0.05) — the precision claim moves to the grid that can support it, and
  **no threshold is loosened**;
- **P4** each extremum's **location** within one cell of the literature's reported location
  (`u_max` y = 0.813, `v_max` x = 0.179) — benchmark data `DeVahlDavis1983.hpp` carried that no
  test had ever used.

Every existing health assertion is unchanged. The 10×10 benchmark errors are **preserved and
printed** as diagnostics, no longer an acceptance bound.

Measured after the change, matching the frozen gate exactly: 10×10 Nu_avg 0.02246 / u_max 0.13703
/ v_max 0.13026; 20×20 0.01218 / 0.05375 / 0.04713; reduction factors 1.844 / 2.550 / 2.764. Both
tests **pass**. Each test now takes ~40 s (was ~4 s).

The second test's printed numbers are identical to the first's, which incidentally confirms the
drop-in equivalence of the variable-property code path that its comment claims.

---

## UF-001.13 — Focused verification

`logs/15_uf_suite.log`, `logs/16_focused.log`. Every binary was `chmod +x`'d and its executability
asserted before running, after the UC-001 harness lesson; a suite that could not be executed would
be reported `NOT RUN`, never counted as passing. None were.

| suite | result |
|---|---|
| CFDNaturalConvectionValidationTests | **7 / 7** |
| CFDHeatedCavityValidationTests | 5 / 5 |
| CFDConjugateHeatTransferValidationTests | 6 / 6 |
| CFDBoussinesqCouplingTests | 5 / 5 |
| CFDThermalTests | 99 / 99 |
| CFDPhysicsTests | 98 / 98 |
| CFDDiscretizationTests | 168 / 169 — 1 failure |
| CFDMMSValidationTests | 19 / 19 |
| CFDSpeciesConservationValidationTests | 4 / 4 |
| CFDMultiphaseConservationValidationTests | 3 / 3 |
| CFDCavityValidationTests | 10 / 10 |
| **total** | **425 run, 424 passed, 1 failed** |

The single failure is `GridRefinementTest.`
`GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` — recorded in
`a4/inventory.md` as "PRE-EXISTING / GRAD-002-ERA, `KEEP_UNCHANGED`" and out of UF-001's scope.
**Zero new failures.**

---

## UF-001.14 — Fresh authoritative W7

Configure → build everything → verify → only then execute, on the A4 fail-closed rule
(`tools/run_w7.sh`, `logs/17_W7_fresh.log`, raw `logs/17_W7_ctest_raw.log`). Library
`143a1dda…`. Test binaries lacking the executable bit before the guard: **0** (the guard is in
place regardless, after the UC-001 incident).

```
99% tests passed, 4 tests failed out of 1923      Total Test time 203.77 s
1968 ctest entries, 45 disabled, 1923 run, 1919 passed, 4 failed
```

Against A6's fresh W7 of **1923 run / 1914 passed / 9 failed** — the same 1923 run, and the
difference is exactly **−5**: the 3 U-C failures resolved by UC-001 and the 2 U-F failures
resolved here. No test was added or removed.

The 4 remaining failures, separated as required:

| failure | class |
|---|---|
| `GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment` | **pre-existing historical gate** — GRAD-002-era, `a4/inventory.md` `KEEP_UNCHANGED`; it asserts the distorted-mesh order is *below* 1.9 and now measures 1.94–1.98, i.e. it fails because the scheme became second order |
| `StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence` | **pre-existing historical gate** — one of W8's own two tests |
| `MultiBlockProductionCase.CurvedChannelGridConvergence` | **pre-existing historical gate** — W8's other test |
| `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | **unrelated known defect** — A6 `PRE_EXISTING` (1.034e-04 vs 1e-4); INV-002 showed the metric *grows* under refinement pre-DIFF-002 too and that tightening solver tolerances 10³× does not reduce it: its formulation needs review, not its value |

**DIFF-002-related unresolved: 0.**

---

## UF-001.15 — Decision after W7

No unresolved DIFF-002-related failure remains, so UF-001 is not blocked on one.

**W7 does not legitimately pass**, applying its frozen wording exactly — *"Focused verification
from P12-NUM, MESH-001…006 and GRAD-002 passes, each against **its own** originally frozen
thresholds"* (`acceptance_gate.md` §2, sha256 `51079f6d…`). The GRAD-002-era
`GridRefinementTest` entry does not pass against its own frozen threshold. That it is explained,
pre-existing and classified `KEEP_UNCHANGED` does **not** make W7 pass, and it is not called a
pass here.

**Therefore W8 was NOT run.** The authorization permits `RUN ORIGINAL W8 UNCHANGED` only once W7
has legitimately cleared, and it has not.

**The exact blocker for W7 is the single GRAD-002-era assertion** `GridRefinementTest.`
`GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment`, which pins the pre-GRAD-002
first-order distorted-mesh boundary behaviour. Amending it is a GRAD-002 decision and is outside
every authorization granted so far (UF-001 is explicitly forbidden from modifying GRAD-002).
W8's own two tests remain W8's subject, reported and unamended.
