# P16 — the adopted k-epsilon wall-function gates

Written for: a reviewer checking what P16's k-epsilon portion is held to, and
an engineer who later changes the near-wall code and needs to know what will
catch them.

Two channel cases, two gates, one methodology. Both derived after measurement
by the procedure frozen in `methodology.md` §2, before any wall-function code
existed.

---

## The gates

### Re_τ = 590 — the PRIMARY accuracy gate

| | Criterion |
|---|---|
| **G1** | `30 ≤ y⁺ ≤ 300` at **every** wall face — hard precondition |
| **G2** | `E_Reτ ≤ 0.062` |
| **G3** | `E_{u⁺,L∞} ≤ 1.6` |

Derived from the worst G1-valid mesh (ny06: 0.0587, 1.4903) plus 5 %.

### Re_τ = 180 — the low-Re STRESS gate

| | Criterion |
|---|---|
| **G1** | `30 ≤ y⁺ ≤ 300` at **every** wall face |
| **G2** | `E_Reτ ≤ 0.22` |
| **G3** | `E_{u⁺,L∞} ≤ 3.9` |

Derived from the worst G1-valid mesh (ny02: 0.2091, 3.7001) plus 5 %.
`ny02` was **not** excluded after the fact: it satisfied the pre-declared
validity criterion, and removing the mesh that sets a threshold in order to
report a better threshold is what makes a gate meaningless.

### Against the preserved baseline

| | baseline | Re_τ=180 gate | Re_τ=590 gate |
|---|---|---|---|
| E_Reτ | 0.270 | 0.22 (1.2× tighter) | **0.062 (4.4× tighter)** |
| E_{u⁺,L∞} | 9.22 | 3.9 (2.4× tighter) | **1.6 (5.8× tighter)** |

---

## Re_τ = 590 sweep

Re_bulk 21725 (derived — see `ChannelReTau590.hpp`), 64 × ny.

| mesh | ny | y₁⁺ | Re_τ | E_Reτ | u⁺ L∞ | samples | τ_w | u_τ | outside | floored | iters | G1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ny06 | 6 | 92.51 | 555.37 | 0.0587 | 1.4903 | 4 | 2.614e-3 | 0.05113 | 0 | 0 | 366 | **PASS** |
| ny08 | 8 | 71.57 | 572.82 | 0.0291 | 0.7776 | 4 | 2.781e-3 | 0.05273 | 0 | 0 | 369 | **PASS** |
| ny12 | 12 | 49.11 | **589.53** | **0.0008** | **0.2662** | 6 | 2.945e-3 | 0.05427 | 0 | 0 | 393 | **PASS** |
| ny16 | 16 | 37.13 | 594.58 | 0.0078 | 0.6981 | 8 | 2.996e-3 | 0.05474 | 0 | 0 | 416 | **PASS** |
| ny20 | 20 | 29.77 | 596.21 | 0.0105 | 0.3281 | 8 | 3.013e-3 | 0.05489 | 72 | 0 | 434 | reject |
| ny24 | 24 | 24.83 | 597.12 | 0.0121 | 0.5556 | 10 | 3.022e-3 | 0.05497 | 128 | 0 | 445 | reject |
| ny32 | 32 | 18.64 | 598.04 | 0.0136 | 0.8294 | 14 | 3.031e-3 | 0.05506 | 128 | 0 | 442 | reject |

Every mesh converged. Continuity residuals ~2–5e-11. **Zero floored-viscosity
faces anywhere**, including outside the band — unlike Re_τ = 180, where ny12
floored 16 faces.

`ny12` reaches **Re_τ = 589.53 against a target of 590 — a 0.08 % error** — with
a log-layer profile within 0.27 in u⁺.

---

## Why two cases, and what the second one fixed

At Re_τ = 180 accuracy and validity moved in **opposite** directions:

```
ny:        2      3      4      5      6      8     12
E_Reτ:  0.209  0.161  0.121  0.089  0.065  0.032  0.008   ← improving
y₁⁺:    71.1   50.3   39.5   32.7   28.0   21.8   14.9    ← leaving the band
```

The best number was obtained where the model's derivation does not hold. A
score-based gate would have selected it.

At Re_τ = 590 that tension is gone:

```
ny:        6      8     12     16     20     24     32
E_Reτ:  0.059  0.029  0.001  0.008  0.011  0.012  0.014
y₁⁺:    92.5   71.6   49.1   37.1   29.8   24.8   18.6
                       ^^^^ best accuracy, and INSIDE the band
```

The cause is arithmetic, not modelling. `y₁⁺ = Re_τ/ny` for a uniform mesh, so a
high-Re wall function permits `ny ≤ Re_τ/30`:

- Re_τ = 180 → ny ≤ 6 → **3 cells per half-channel**;
- Re_τ = 590 → ny ≤ 19 → **~9 cells per half-channel**.

`WallFunctionChannelHighRe.AccuracyAndValidityAreNoLongerInTension` asserts the
most accurate mesh lies inside the band, so if this case ever loses the property
it was chosen for, a test says so.

A second physical confirmation: E_Reτ is **non-monotonic**, bottoming at
y₁⁺ ≈ 49 — mid-log-layer, exactly where the wall function's own derivation is
most secure. The model is most accurate where theory says it should be.

---

## Roles

| Case | Role |
|---|---|
| **Re_τ = 590** | **Primary accuracy benchmark.** Validity and core resolution simultaneously achievable. |
| **Re_τ = 180** | **Low-Re stress / edge-of-validity benchmark.** Preserved, not discarded. Documents the structural tension and is where degradation at the edge of validity is measured. |

The Re_τ = 180 result is a **benchmark limitation, not a model failure**: at
that Reynolds number the channel is only 180 wall units tall, so the validity
band and the outer resolution are in direct conflict. The same model, given
wall-normal room at Re_τ = 590, reaches 0.08 %.

---

## Validation wall shear — corrected

`ChannelFlowValidationUtils::computeWallShearStress` formed the laminar secant
`μ·U(y₁)/y₁`, and its own comment justified it with *"CFDApp's turbulence models
have no velocity wall function"* — true when written, false after P16. On a
wall-function mesh it **under-reports τ_w**, i.e. errs in the direction that
flatters every derived metric.

It now takes a required `WallTreatmentKind` and enforces the pairing:

| treatment | model shear | result |
|---|---|---|
| `NoWallFunction` | nullptr | the secant, unchanged |
| `WallFunction` | supplied | the model's own τ_w |
| `WallFunction` | nullptr | **throws** |
| `NoWallFunction` | supplied | **throws** |

Four regression tests hold it, including
`TheSecantUnderReportsAWallFunctionWallShear`, which runs a real solve and
requires the two estimators to differ by **more than 2×** — if they agreed, the
enforcement would be pedantry rather than a guard.

---

## Evidence

| Path | Contents |
|---|---|
| `acceptance/methodology.md` | The gate, frozen before implementation |
| `acceptance/threshold_derivation.md` | Re_τ = 180 derivation |
| `acceptance/adopted_gate.md` | This file |
| `mesh_refinement/re_tau_180/sweep.csv` | Low-Re sweep, 7 meshes |
| `mesh_refinement/re_tau_590/sweep.csv` | High-Re sweep, 7 meshes |
| `mesh_refinement/<case>/<mesh>/profiles.csv` | y⁺, u⁺, log law, k, ε per mesh |
| `pre_p16_baseline/` | Pre-P16 results, verbatim |
| `logs/` | Raw run logs |
