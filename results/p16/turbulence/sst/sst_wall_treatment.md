# P16 — SST wall treatment and validation

Written for: a reviewer deciding whether SST's wall-function work can close.

---

## What changed

| File | Change |
|---|---|
| `include/cfd/turbulence/SSTModel.hpp` | `wallFunctions` flag (**default off**), coefficients, 7 diagnostic accessors |
| `src/turbulence/SSTModel.cpp` | wall faces collected once; treatment applied in `correct()`; `wallViscosity()` override |
| `src/io/case/PhysicsConfigParser.cpp` | `wall_functions` now accepted for `sst`; still **rejected** for `k_omega` |
| `src/io/CaseBuilder.cpp` | SST k and ω wall conditions switch with the flag |
| `tests/unit/turbulence/test_sst_wall_functions.cpp` | **new**, 18 tests (13 + 5 controls) |
| `tests/integration/turbulence_channel/test_wall_function_channel.cpp` | harness extended to both models; 6 SST tests |
| `tests/integration/backward_facing_step/test_sst_separation.cpp` | **new**, 5 tests — the discriminating benchmark |

**No second wall-function implementation.** SST reuses `WallTreatment` unchanged.

---

## The wall-cell treatment

Identical in structure to k-ε; only the imposed variable differs.

```
u_τ*  = C_μ^¼ √k_P                      (C_μ ≡ β*, tied and enforced)
y*    = ρ u_τ* y_P / μ
τ_w   = ρ u_τ* κ U_P / ln(E y*)
ω_P   = u_τ* / (√C_μ κ y_P)             IMPOSED via fixRow
P_k   = τ_w u_τ* / (κ y_P)              replaces μ_t S², then limitProduction
k                                        zero-gradient — not imposed
μ_wall= τ_w y_P / U_P                    per-face, to momentum
```

Two consistency points proved rather than asserted:

- **ω_P = ε_P/(C_μ k_P)** for the same equilibrium state, derived independently on
  each side. The SST and k-ε wall cells are the *same* physical state, not two
  conventions.
- **C_μ is tied to β\***, and a mismatch is refused at construction. In SST β*
  plays C_μ's role; two values would put the imposed ω on a different
  equilibrium than the model's own.

### The ω boundary condition had to change too

`WallOmega` (= 60ν/β₁y², Wilcox's **viscous-sublayer asymptotic**) is kept for
the low-Re path and replaced by zero-gradient when wall functions are on.
Keeping it would impose two different values for the same physics — they
disagree by orders of magnitude at log-layer spacing, which is the whole point.

### F1 must still reach the k-ω branch at the wall

The one SST-specific risk an imposed ω creates. Worked through at y⁺ = 50:
`arg1 = max(2.49, 1.23) = 2.49 → F1 = tanh(2.49⁴) ≈ 1`. Asserted by
`F1TendsToTheKOmegaBranchAtTheWall` (measured F1 > 0.9 at the wall and larger
there than at the centreline), not left as an argument.

---

## Channel sweeps

### Re_τ = 590 (primary)

| mesh | ny | y₁⁺ | Re_τ | E_Reτ | u⁺ L∞ | τ_w | floored | iters | G1 |
|---|---|---|---|---|---|---|---|---|---|
| ny06 | 6 | 92.81 | 557.21 | 0.0556 | 1.3072 | 2.631e-3 | 0 | 1051 | **PASS** |
| ny08 | 8 | 71.76 | 574.32 | 0.0266 | 0.6683 | 2.795e-3 | 0 | 991 | **PASS** |
| **ny12** | 12 | 49.13 | **589.85** | **0.0003** | 0.4784 | 2.949e-3 | 0 | 922 | **PASS** |
| ny16 | 16 | 37.06 | 593.50 | 0.0059 | 1.0443 | 2.985e-3 | 0 | 912 | **PASS** |
| ny20 | 20 | 29.61 | 593.28 | 0.0056 | 0.4498 | 2.983e-3 | 0 | 963 | reject (72) |
| ny24 | 24 | 24.62 | 592.25 | 0.0038 | 0.5183 | 2.973e-3 | 0 | 989 | reject |
| ny32 | 32 | 18.41 | 591.06 | 0.0018 | 0.5449 | 2.961e-3 | 0 | 1123 | reject |

**Re_τ = 589.85 against 590 — 0.03 % error.**

### Re_τ = 180 (low-Re stress)

| mesh | y₁⁺ | Re_τ | E_Reτ | u⁺ L∞ | floored | iters | G1 |
|---|---|---|---|---|---|---|---|
| ny02 | 71.14 | 142.37 | 0.2091 | 3.6990 | 0 | 659 | **PASS** |
| ny03 | 50.41 | 151.38 | 0.1590 | 2.9592 | 0 | 858 | **PASS** |
| ny04 | 39.62 | 158.68 | 0.1185 | 2.2387 | 0 | 916 | **PASS** |
| ny05 | 32.85 | 164.42 | 0.0865 | 1.6146 | 0 | 942 | reject (10) |
| ny06 | 28.10 | 168.80 | 0.0622 | 0.9094 | 0 | 897 | reject |
| ny08 | 21.79 | 174.44 | 0.0309 | 0.6922 | 0 | 830 | reject |
| ny12 | 14.85 | 178.37 | 0.0090 | 0.9301 | **14** | 743 | reject |

Same structural tension as k-ε, same cause. ny12 floors 14 faces — the
buffer-layer symptom, matching k-ε's 16.

### Adopted SST gates — derived, not inherited

| | Re_τ = 180 | Re_τ = 590 |
|---|---|---|
| worst G1-valid E_Reτ | 0.2091 | 0.0556 |
| worst G1-valid u⁺ L∞ | 3.6990 | 1.3072 |
| **G2** (+5 %) | **0.22** | **0.059** |
| **G3** (+5 %) | **3.9** | **1.4** |

The Re_τ=180 numbers coincide with k-ε's; the Re_τ=590 numbers are **tighter**
(0.059 vs 0.062, 1.4 vs 1.6). Both are results of SST's own sweep, not reuse.

Against the preserved SST baseline: **E_Reτ 0.270 → 0.0003**, u⁺ L∞ 9.22 → 0.478.

---

## The SST-discriminating benchmark

A channel cannot validate SST. SST and k-ε both reproduce the log law there to
a fraction of a percent, and so would an SST with its blending flattened, its
cross diffusion removed, or its limiter disabled — an equilibrium boundary
layer is the flow where those mechanisms have nothing to do.

Backward-facing step, Re_h = 5000, 80 × 8, reattachment read off the wall-shear
sign change by the harness's own algorithmic detector:

| configuration | x_r/h | max μ_t | converged | iters |
|---|---|---|---|---|
| **SST** | **7.429** | 9.004e-3 | yes | 1139 |
| k-ε | 5.066 | 1.391e-2 | yes | 233 |
| SST, limiter disabled (a₁ → ∞) | 4.550 | 1.358e-2 | yes | 820 |
| SST, no cross diffusion (σ_ω2 → 0) | 18.853 | 1.367e-3 | **NO** | 2000 |
| SST, blending inert (set 1 = set 2) | 7.958 | 7.855e-3 | yes | 1248 |

**The benchmark discriminates**: SST reattaches 2.36 step heights downstream of
k-ε. That is the textbook difference — k-ε over-produces μ_t in the shear layer
(1.39e-2 vs 9.00e-3) and reattaches far too early.

**And it detects every mechanism failure:**

- **Limiter disabled** → x_r/h collapses 7.43 → 4.55 and max μ_t rises to
  1.36e-2, *almost exactly k-ε's value*. SST degrades into a k-ε-like
  over-prediction, which is precisely the mechanism the limiter exists to
  prevent. This is the single clearest result in the file.
- **Cross diffusion removed** → the solve does not converge at all, μ_t collapses
  tenfold, x_r/h runs off to 18.9.
- **Blending flattened** → x_r/h moves 0.53 h. Smaller, and detected.

Controls are applied through the **public `SSTCoefficients`**, configurations a
user could legally write — not by patching production source. That is what
makes the detection meaningful rather than an artefact.

### What this does NOT claim

The step is coarse (80 × 8) and Re_h = 5000. The numbers are **qualitative
discrimination**, not a quantitative match to a specific experiment. Published
turbulent BFS reattachment sits around 6–7 h; SST's 7.43 is in the right
region and k-ε's 5.07 is short in the documented direction, but this benchmark
is not calibrated to a named dataset and no claim of quantitative agreement is
made. Its job is to detect mechanism failure, and it does.

---

## Verification

| Suite | Result |
|---|---|
| turbulence unit | **204/204** (186 → 204, +18 SST) |
| solver | **90/90** |
| physics | **98/98** |
| io / case parsing | **307/307** |
| channel validation | **31/31** (12 pre-P16 unchanged) |
| backward-facing step | **7/7** |

Deterministic: SST channel and step both reproduce iterations, τ_w, Re_τ, u⁺ L∞
and reattachment exactly across repeats.

---

## Evidence

| Path | Contents |
|---|---|
| `audit/sst_near_wall_audit.md` | The pre-edit audit |
| `sst/sst_wall_treatment.md` | This file |
| `mesh_refinement/re_tau_{180,590}/sst/sweep.csv` | SST sweeps |
| `mesh_refinement/re_tau_{180,590}/k_epsilon/sweep.csv` | k-ε sweeps |
| `logs/sstsweep2.log`, `logs/sstsep.log`, `logs/ssttests.log`, `logs/sstscope.log` | Raw runs |
| `pre_p16_baseline/` | Pre-P16 results, verbatim |
