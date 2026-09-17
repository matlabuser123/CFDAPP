# P12-DIFF-002-INV-002 — sub-class F production impact: findings

Investigation only. Production numerical files and `libcfdcore.a` verified **byte-identical** before
and after (`logs/00_state_freeze.log`, `logs/09_state_after.log`). No test, threshold, benchmark
reference, golden output, case or acceptance gate was modified.

## Classification summary

| # | test | classification | basis |
| --- | --- | --- | --- |
| F1 | `SpeciesConservationTest.OpenChannelWithVolumetricSource…` | **F-C** | the solver conserves to 3.5e-08; the 1.587e-03 is the test's own first-order flux estimator |
| F2 | `LowMachRegressionTest.GlobalMassImbalanceIsSmall` | **F-C** | the metric does not converge under refinement **in the pre-DIFF-002 build either**, and already exceeds 1e-4 there at 64×12 |
| F3 | `NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3` | **F-C** (was F-F; resolved by Step 1) | the momentum wall shear alone causes it; the operator is verified exact for linear/quadratic and improves every analytically referenced quantity |
| F4 | `…Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3` | **F-C** (was F-F) | same root cause AND mechanism as F3, both demonstrated |

All four **pass on the isolated pre-DIFF-002 baseline** and fail on the current build, so all four are
DIFF-002-caused, not pre-existing (rules out F-E for every one of them).

---

## 1. INV-F8 conservation identity audit — the DIFF-002 boundary stencil IS conservative

`logs/01_F8_conservation.log`. This was run first because it is the one check that could directly
prove a defect. Two independent checks, on tiny hand-derivable systems and on production-scale
meshes, for thermal and species:

- **C1 constant field with matching Dirichlet values** → every cell's net flux must be exactly zero.
  Measured max |Aφ − b| ≤ **2.13e-14** on every mesh (1×1 all-fallback, 8×1 mixed fallback, 2×1, 3×3,
  Cartesian 10/20/40², sheared 20² s=0.45, graded 20² r=1.2, Cartesian 3D 8³).
- **C2 telescoping** → summing all rows must equal the independently summed boundary flux. Measured
  mismatch ≤ **4.97e-14 absolute, ≤ 1.10e-15 relative** on every mesh.

The far-cell coefficient appears **only** in the owner's row and is part of the boundary *flux
expression*, not an extra internal coupling, so it cannot double-count. **A broken conservation
identity is ruled out as the cause of any sub-class F failure.**

## 2. INV-F3 species conservation — F-C, decisively

`logs/03_F3_species.log`. Deterministic: three consecutive runs give bit-identical values, and the
test's own number is reproduced exactly (1.587336e-03).

The failing assertion computes its own global balance whose boundary **diffusive** term is written
inline as `ρD|S|/d · (Y_P − Y_b)` — the pre-A2 two-point wall flux — while the solver assembled the
three-point one. The same balance, computed three ways on the identical converged field:

| mesh | (1) the test's two-point estimator | (2) the flux production actually assembled | (3) Σ(AY − b), no per-face reconstruction |
| --- | --- | --- | --- |
| 30×2 | 2.564e-03 **FAIL** | 1.765e-08 PASS | −1.77e-09 |
| **60×4 (the failing case)** | **1.587336e-03 FAIL** | **3.479e-08 PASS** | **−3.48e-09** |
| 120×8 | 9.010e-04 PASS | 7.653e-08 PASS | −7.65e-09 |
| 240×16 | 4.832e-04 PASS | 1.331e-07 PASS | −1.33e-08 |

- With the **consistent** operator the imbalance is **3.48e-08 — 45 600× smaller** than the test
  reports, and at the solver's own residual level (max per-cell residual 9.8e-10).
- The fully independent route (sum the assembled residual; no per-face flux reconstruction at all)
  agrees at −3.5e-09.
- The test estimator's error **converges at first order** (2.564e-03 → 1.587e-03 → 9.010e-04 →
  4.832e-04; ratio ≈ 1.8 per halving) and passes its own bound by 120×8 — the signature of a
  first-order flux estimator applied to a second-order solution, not of a conservation defect.

**Mechanism: the validation instrument's flux estimator, identical to A4 sub-class B and to the
defect A3-2 already fixed in the sector test.** Not the DIFF-002 boundary reconstruction, not the
outlet treatment, not source integration, not convergence tolerance.

## 3. INV-F4 low-Mach mass imbalance — F-C

`logs/07_F4_lowmach_current.log`, `logs/08_F4_lowmach_baseline.log`. The real test: **passes on the
baseline build, 1.034329e-04 on the current build** (3.4 % over a 1e-4 bound), deterministic.

**Disclosure:** my independent transcription of the metric does not reproduce the test's exact value
(mine 4.51e-05 vs the test's 1.03e-04 at 32×6). The test passes its in-place-mutated local
`velocity` field to `calculateCompressibleMassFlux` rather than `flow.velocity`; I did not replicate
that. So conclusions below rest on the *character* of the quantity, measured identically in both
builds, plus the real binaries' pass/fail — not on my absolute value.

| configuration | PRE-DIFF-002 | CURRENT | change |
| --- | --- | --- | --- |
| 32×6, the test's tolerances | 4.799645e-05 | 4.508602e-05 | −6.1 % (better) |
| 32×6, tolerances tightened 1e3× | 5.669899e-05 | 5.649838e-05 | −0.4 % |
| 64×12 | **1.031586e-04** | 1.060077e-04 | +2.8 % |
| 128×24 | **1.068163e-04** | 1.088202e-04 | +1.9 % |

Two findings, both from the **baseline** column:

1. **The metric does not converge under mesh refinement** — it *grows* 4.8e-05 → 1.03e-04 → 1.07e-04
   from 32×6 to 128×24, in the pre-DIFF-002 build. It is therefore not a discretization error that
   refines away; it is a compound consistency diagnostic dominated by the O(Δρ/ρ) re-weighting of an
   *incompressible* solution's face fluxes by per-cell EOS densities, which cannot be discretely
   divergence-free.
2. **The pre-DIFF-002 code already exceeds the 1e-4 bound at 64×12 and 128×24.** The threshold is
   satisfied only at the one grid the test uses, so it is a near-threshold condition that DIFF-002
   nudged across rather than a conservation property the code robustly holds.

Tightening the solver's stopping tolerances by 10³ does **not** reduce the metric (it rises slightly
and hits the 3000-iteration cap), so this is not incomplete iterative convergence (not F-D). Combined
with §1 and §2 — the assembled operator is conservative to 1e-15 relative, and species conservation
is 3.5e-08 — the conservation of the discretization itself is intact.

## 4. INV-F5/F6 natural convection — initially F-F; **resolved to F-C by Step 1** (see the Step 1 section at the end)

`logs/05_F5_F6_natconv.log`, `logs/06_F6_refinement.log`. The De Vahl Davis Ra=1e³ reference is an
independent literature benchmark and was **not** redefined, and no expected value was derived from
CFDApp. Full metric set, both builds, three grids:

| grid | metric | PRE-DIFF-002 | CURRENT | direction |
| --- | --- | --- | --- | --- |
| 10×10 | Nu_avg error | 0.048557 | **0.022464** | **improved 2.16×** |
| 10×10 | u_max error | 0.104420 | **0.137032** | worse 1.31× |
| 10×10 | v_max error | 0.067138 | **0.130255** | worse 1.94× (**the failure**) |
| 15×15 | Nu_avg error | 0.027206 | **0.015797** | **improved 1.72×** |
| 15×15 | u_max error | 0.026071 | **0.051994** | worse 1.99× |
| 15×15 | v_max error | 0.037029 | **0.062328** | worse 1.68× |
| 20×20 | Nu_avg error | 0.018459 | **0.012180** | **improved 1.52×** |
| 20×20 | u_max error | 0.038614 | **0.053747** | worse 1.39× |
| 20×20 | v_max error | 0.031449 | **0.047128** | worse 1.50× |

Supporting quantities: global mass imbalance **exactly 0** in both builds at every grid; heat
imbalance 6.2e-13 → 8.6e-08 (10×10) and 1.5e-12 → 1.0e-07 (20×20) — five orders larger but still
~1e-7 relative to q_hot ≈ 1.14, consistent with the lagged transfer term's residue at convergence,
not a conservation break; u_max and v_max **locations** unchanged (y=0.75, x=0.15 at 10×10);
θ range essentially unchanged; iteration counts comparable (flow 1→3, outer 51→50).

**What this shows.** DIFF-002 does exactly what it was designed to do — it improves the **wall heat
flux** (Nusselt) monotonically at every resolution — and it **degrades the interior velocity extrema
by a consistent 1.3×–2.0× at every resolution**. The degradation does **not** shrink with refinement,
so it is not a coarse-grid artifact that a tolerance review would dispose of. Both metrics still
converge (current v_max 0.130 → 0.062 → 0.047), and `Grid15x15` and the dedicated `GridConvergence`
study both still pass.

**Why F-F and not F-C or F-A.** To call it F-C I would have to assert the new discretization remains
numerically valid for the velocity field, and a persistent 1.3×–2.0× accuracy loss against an
independent benchmark is not something this evidence establishes. To call it F-A I would have to
identify a defect mechanism, and I cannot: the boundary operator is independently verified (W1
quadratic exactness ≤1.9e-16 and cubic order 1.963–2.000; W4's hand-derived coefficients), and §1
proves conservation. The shared root cause of F3 and F4 **is** demonstrated rather than assumed —
identical v_max error 0.130255 to every printed digit, same case, same grid, the only difference
being the constant-property model path.

**Exactly what evidence is missing:** a momentum-specific near-wall accuracy verification — an
analytic or manufactured buoyancy/shear solution with a known near-wall velocity profile, refined,
comparing the two wall treatments — to determine whether the second-order wall *shear* is
implemented correctly for the momentum equation specifically (as opposed to the scalar diffusion
operator, which is verified), and whether redistributing near-wall momentum error is an inherent
property of the scheme or an implementation fault. That requires production-level probing of the
momentum wall path, which this authorization does not cover.

## 5. INV-F7 same-field operator vs converged-solution comparison

Kept distinct, as required.

- **Same field, two operators** (§2, species): on one identical converged field the two-point
  estimator gives 1.587e-03 and the DIFF-002 operator 3.479e-08 — a pure *operator* effect, no
  solution change involved.
- **Converged solutions from each treatment** (§3, §4): the baseline and current builds each solve to
  convergence with their own wall treatment. Species/low-Mach shift by a few percent; natural
  convection shifts Nusselt down 1.5–2.2× in error and velocity extrema up 1.3–2.0×.

The species case shows the operator difference is four to five orders larger than any solution shift,
which is why its failure is attributable to the estimator alone.

## 6. INV-F9 sensitivity

| quantity | mesh resolution | solver tolerance | notes |
| --- | --- | --- | --- |
| species imbalance (test estimator) | strongly sensitive, 1st-order convergent | insensitive (residual 1e-9) | passes its own bound by 120×8 |
| species imbalance (consistent operator) | insensitive, always ~1e-8 | insensitive | at solver-residual level |
| low-Mach metric | **strongly sensitive and non-convergent** (grows with refinement in both builds) | insensitive to a 10³ tightening | already >1e-4 pre-DIFF-002 at 64×12 |
| Nu_avg error | convergent, improved by DIFF-002 at every grid | — | 0.0486→0.0225, 0.0272→0.0158, 0.0185→0.0122 |
| u_max / v_max error | convergent but degraded by a ~constant factor | — | the unresolved finding |

No setting was tuned to make any test pass; the sweeps exist for attribution only.

## 7. Proposed next actions (nothing implemented)

- **F1 species (F-C):** amend the validation instrument to evaluate the boundary flux with the
  operator production uses and sum conservation independently — the pattern already proven in A3-2
  (`derivations.md` §4). Fold into the A–E migration scope; do not change the 1e-3 bound.
- **F2 low-Mach (F-C):** the 1e-4 bound is met only at one grid and the metric diverges under
  refinement in the pre-DIFF-002 code too. Recommend reviewing the metric's *formulation* (it
  re-weights an incompressible solution by EOS densities) rather than its numeric value, as a
  separate item. Do not change the threshold.
- **F3/F4 natural convection (F-F):** authorize a focused momentum near-wall accuracy investigation
  as described in §4 before any decision. Do not amend the benchmark, the 0.12 bound, or production.

---

# Step 1 (authorized 2026-09-16) — natural-convection F-F resolved

`logs/10_variant_momentum_old.log`, `logs/11_natconv_factorial.log`. Two further **isolated** builds
restore the pre-A2 wall gating in ONE equation only, giving a 2×2 factorial with the existing
baseline and the authoritative tree. No production, test or benchmark tolerance was changed; the
authoritative files are hash-verified unchanged in every variant log.

## Factorial attribution

| variant | grid | Nu_avg err | u_max err | v_max err |
| --- | --- | --- | --- | --- |
| neither (pre-DIFF-002) | 10×10 | 0.048557 | 0.104420 | 0.067138 |
| **thermal reconstruction only** | 10×10 | 0.040417 | **0.103650** | **0.066313** |
| **momentum reconstruction only** | 10×10 | 0.029133 | **0.137642** | **0.130886** |
| both (authoritative) | 10×10 | 0.022464 | 0.137032 | 0.130255 |
| neither | 15×15 | 0.027206 | 0.026071 | 0.037029 |
| thermal only | 15×15 | 0.024550 | **0.025896** | **0.036856** |
| momentum only | 15×15 | 0.018095 | **0.052142** | **0.062474** |
| both | 15×15 | 0.015797 | 0.051994 | 0.062328 |

**The two effects separate completely.**

- **Boundary heat-flux reconstruction** (thermal): improves Nu_avg (0.0486 → 0.0404 at 10×10) and
  leaves u_max/v_max **unchanged to three digits** — in fact marginally better (0.10442 → 0.10365,
  0.06714 → 0.06631). It is not the cause.
- **Momentum wall-shear reconstruction**: reproduces the **entire** velocity-extrema degradation on
  its own (0.137642 / 0.130886 against "both"'s 0.137032 / 0.130255), and independently improves
  Nu_avg as well (0.0486 → 0.0291) through the changed velocity field advecting heat.
- The two Nu contributions are roughly additive (0.0486 → 0.0404 thermal, → 0.0291 momentum,
  → 0.0225 both).

**Mechanisms excluded by the same data:** the *temperature field* barely moves (θ range 0.03734 →
0.03804 / 0.96266 → 0.96196 across all four variants), so *buoyancy/source coupling* — which follows
θ — cannot be the driver; the *gradient reconstruction* (GRAD-002) is identical in all four builds;
*solver convergence* is intact everywhere (flow 1–6 iterations, outer 50–51, final ΔT ≈ 1e-8, heat
imbalance ≤ 1.3e-7, mass imbalance exactly 0). What remains is the **velocity–pressure response to
the momentum wall shear**.

## Why this is F-C, not F-A

The momentum reconstruction is the *same operator* the thermal path uses, and it is independently
verified correct rather than assumed:

- **exact for linear and quadratic fields** and second order for cubic (W1: quadratic exactness
  ≤ 1.9e-16, cubic order 1.963–2.000; W4's hand-derived coefficients). For a no-slip wall with
  `u_b = 0` and a linear near-wall profile `u = a y`, the uniform-limit reconstruction returns
  `[9(a h/2) − a(3h/2)]/(3h) = a` — exactly the analytic wall shear.
- **conservative** to ≤ 1.10e-15 relative (INV-F8), mass imbalance exactly 0 here.
- **On cases with an analytic reference the momentum path improves both the wall flux and the
  interior velocity**: distorted Poiseuille dp/dx error 0.6788 % → **0.207 %** (3.28×) with wall-flux
  L1 0.99 % → 0.63 % → 0.40 % under refinement (W6/A2-6), and the Cartesian Poiseuille centerline
  1.45455 → **1.46512** against the exact 1.5 (1.30× better, A4 sub-class C).

So the operator is right and is beneficial where an analytic reference exists. What degrades is two
**interior peak velocities** in a buoyancy-driven boundary layer that the coarse grids barely
resolve — v_max sits at x = 0.15, i.e. **1.5 cells** from the wall on a 10-cell grid. A more accurate
(and stiffer: `cP = 3/h` versus `2/h`, a factor 1.5 on the diagonal) no-slip wall flux pins the
near-wall cell harder toward `u = 0` and lowers the under-resolved peak, while the wall flux itself
and every analytically referenced quantity improve. Both extrema still converge under refinement
(v_max err 0.1303 → 0.0623 → 0.0471 at 10/15/20).

**Classification: F-C for both natural-convection tests** — a legitimate discretization change whose
coarse-grid velocity-extrema tolerances (`u_max < 0.15`, `v_max < 0.12` at 10×10) were calibrated
against the superseded wall treatment and now require review. The shared root cause of the two tests
was already demonstrated (identical v_max error to every digit); the factorial now also identifies
the shared *mechanism*.

**Not proposed here, and not done:** any change to the De Vahl Davis reference, to the 0.12/0.15
bounds, or to production. The residual honest caveat is that the degradation does not shrink with
refinement (1.39×/1.50× still at 20×20), so the review should decide the bounds on the basis that
the scheme trades interior coarse-grid peak accuracy for wall-flux accuracy.
