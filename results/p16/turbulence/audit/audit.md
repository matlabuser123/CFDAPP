# P16 — Turbulence: repository audit

Written for: the person deciding what P16 must actually do, before any
production code changes.

Nothing below is inferred from a symbol existing. Every claim names the file
that establishes it.

**Headline: the three RANS models are implemented and their equations are
largely correct, but they do not reproduce the log law — Re_τ is off by 27–33 %
— and the existing "log-law validation" gates on regression, not on physics.
The missing wall treatment is the cause, not merely a missing checkbox.**

---

## 1. Inventory

`src/turbulence/` — 11 sources, `include/cfd/turbulence/` — 14 headers, 2503
lines total.

| Component | File | Lines |
|---|---|---|
| `TurbulenceModel` (interface) | `TurbulenceModel.{hpp,cpp}` | 112 / 44 |
| `LaminarModel` | `LaminarModel.{hpp,cpp}` | 43 / 20 |
| `KEpsilonModel` | `KEpsilonModel.{hpp,cpp}` | 162 / 178 |
| `KEpsilonEquation` | `KEpsilonEquation.{hpp,cpp}` | 163 / 255 |
| `KOmegaModel` | `KOmegaModel.{hpp,cpp}` | 161 / 185 |
| `KOmegaEquation` | `KOmegaEquation.{hpp,cpp}` | 35 / 34 |
| `SSTModel` | `SSTModel.{hpp,cpp}` | 168 / 225 |
| `SSTEquation` | `SSTEquation.{hpp,cpp}` | 119 / 120 |
| `TurbulenceProduction` | `TurbulenceProduction.{hpp,cpp}` | 53 / 49 |
| `WallDistance` | `WallDistance.{hpp,cpp}` | 51 / 71 |
| `TurbulenceModelFactory` | `TurbulenceModelFactory.{hpp,cpp}` | 80 / 68 |
| Coefficients | `KEpsilon/KOmega/SSTCoefficients.hpp` | 28 / 27 / 52 |

Tests: `tests/unit/turbulence/` (15 files), `tests/integration/turbulence_channel/`
(4 files). `tests/integration/channel/` **is empty**.

---

## 2. What is correct

Worth stating plainly, because most of the model physics is sound and P16 must
not churn it.

### Coefficients — correct and correctly separated

`KEpsilonCoefficients` (Launder–Spalding 1974): `Cμ=0.09, C1ε=1.44, C2ε=1.92,
σk=1.0, σε=1.3`. `KOmegaCoefficients` (Wilcox): `β*=0.09, α=5/9, β=0.075,
σk=2.0, σω=2.0`. `SSTCoefficients` (Menter 1994): `β*=0.09, a1=0.31, κ=0.41`,
inner `σk1=0.85, σω1=0.5, β1=0.075`, outer `σk2=1.0, σω2=0.856, β2=0.0828`,
production limiter 10.0.

The three are **deliberately separate types**, with the headers explaining why
mixing them is wrong. That is right.

### Diffusion coefficients — the two forms are correctly distinguished

- k-ε: `Γ = μ + μt/σ` (`computeEffectiveDiffusivity`), explicitly *not* `(μ+μt)/σ`.
- k-ω: `Γ = μ + σ·μt` (`computeLinearEffectiveDiffusivity`), the linear form.

Both are commented with the distinction. Correct.

### Production

`P_k = μt · S²` with `S² = 2(∂u/∂x)² + 2(∂v/∂y)² + (∂u/∂y + ∂v/∂x)²`
(`TurbulenceProduction.cpp`). The cross-shear term is not dropped. Correct
**in 2-D** — see §3.1.

### Source linearisation — textbook Patankar

k: `Su = P_k`, `Sp = −ρε/k`. ε: `Su = C1ε(ε/k)P_k`, `Sp = −C2ε·ρ·ε/k`. Both
destruction terms implicit and negative, which is what keeps the matrix
diagonally dominant.

### SST — the hard parts are all present and correct

`SSTEquation.cpp` implements, and I checked each against Menter (1994):

- `α = β/β* − σω κ²/√β*` — the log-law consistency relation, correct;
- `F1 = tanh(arg1⁴)`, `arg1 = min(max(√k/(β*ωy), 500ν/(y²ω)), 4ρσω2 k/(CD y²))`;
- `F2 = tanh(arg2²)`, `arg2 = max(2√k/(β*ωy), 500ν/(y²ω))`;
- cross-diffusion `D_ω = 2(1−F1)ρσω2 ∇k·∇ω/ω`, with `CD` floored at `1e-10`;
- eddy-viscosity limiter `μt = ρa1k / max(a1ω, S·F2)`;
- production limiter `min(P_k, 10β*ρkω)`.

This is a genuine SST implementation, not a k-ω with a blend bolted on.

### Model update ordering is transactional

`KEpsilonModel::correct` solves both equations before touching any member, so a
failed solve leaves `k_`, `epsilon_`, `turbulentViscosity_` exactly as they were.
Good practice, and it is documented.

---

## 3. Findings

### 3.1 Turbulence is 2-D only — production silently ignores w

`requireTwoDimensional` is asserted in `KEpsilonModel.cpp:67`,
`KOmegaModel.cpp:70`, `WallDistance.cpp:36` (SST is 2-D transitively, since its
constructor builds the wall-distance field).

`VelocityGradientField` **has** `gradW` (added in P12-MESH-006), and
`computeStrainRateMagnitudeSquared` reads only `gradU` and `gradV`. So the
2-D restriction is enforced by a runtime check rather than being a silent wrong
answer — that is the right call — but it means **no 3-D turbulent case can run
at all**, on a codebase that has `duct_3d`, `lid_driven_cavity_3d` and
`lid_driven_cavity_3d_re1000`.

The full 3-D strain invariant needs six terms rather than three.

### 3.2 No wall functions, no y+, no u_τ, no τ_w in production code

Confirmed by search: no `wallFunction`, `yPlus`, `uTau`, `frictionVelocity` or
`wallShear` symbol exists outside tests and recorded results. The TODO note is
accurate.

Current wall treatment, from `CaseBuilder.cpp:134-212`:

| Model | k at wall | ε / ω at wall |
|---|---|---|
| k-ε | `FixedValue(0)` | ε: `FixedGradient(0)` |
| k-ω | `FixedValue(0)` | ω: `FixedGradient(0)` |
| SST | `FixedValue(0)` | ω: `WallOmega` = `60ν/(β1 y²)` (Wilcox low-Re) |

The code says so itself — *"No validated wall-function value without near-wall
treatment … documented as a simplification, not a claimed high-Re wall-function
epsilon."* That honesty is good. The consequence is §3.3.

**A standard high-Re k-ε with `k=0` at the wall and zero-gradient ε is not a
valid formulation.** k-ε cannot be integrated to the wall; it requires wall
functions. What is in the tree is neither a wall-function model nor a low-Re
model.

### 3.3 The models do not reproduce the log law, and the gate does not require them to

From `results/validation/turbulence/channel_flow/*/validation.json` (Re_bulk
5600, target Re_τ = 180, 64×16):

| Model | Re_τ achieved | Re_τ rel. error | outer u⁺ L2 | u⁺ L∞ | first-cell y⁺ |
|---|---|---|---|---|---|
| k-ε | 120.0 | **33.3 %** | 12.18 | 14.56 | 7.50 |
| SST | 131.4 | **27.0 %** | 8.64 | 9.22 | 8.21 |

An L∞ error of 9–15 **in u⁺ units** is enormous: u⁺ in the log layer is ~15–20,
so this is a 50–70 % error on the velocity profile.

The thresholds in `test_channel_flow_validation.cpp:237-242`:

```cpp
kOuterL2ThresholdCoarse  = 18.0;   kReTauErrorThresholdCoarse = 0.45;
kOuterL2ThresholdMedium  = 15.0;   kReTauErrorThresholdMedium = 0.40;
kOuterL2ThresholdFine    = 12.0;   kReTauErrorThresholdFine   = 0.32;
```

A 40 % Re_τ error passes. The tests are named `…ConvergesAndMatchesLogLaw`, and
the comment does say the thresholds are *"locked from measured evidence"* with a
reference to a *"near-wall limitation"* section — so this is disclosed, not
concealed. But the name claims more than the gate checks, and **no test in the
repository would fail if the log law were abandoned entirely**, as long as the
numbers did not get worse.

**y⁺ ≈ 7.5–8.2 is the worst possible place to be**: too coarse to resolve the
viscous sublayer, too fine for high-Re wall functions (which want 30 ≲ y⁺ ≲ 300).
The first cell sits in the buffer layer where neither `u⁺ = y⁺` nor
`u⁺ = ln(y⁺)/κ + B` holds.

This is the central P16 finding. Wall functions are not a missing feature next
to four working ones — they are the reason the four do not work.

### 3.4 P14's generic transport has **zero** production consumers

`cfd::transport::` is referenced nowhere in `src/`, `include/` or `apps/` except
two comments in `include/cfd/gpu/DeviceScalarTransport.hpp`.

Meanwhile `cfd::turbulence::assembleScalarDiffusionContribution`,
`applyImplicitScalarSource` and `assembleScalarTransportEquation`
(`KEpsilonEquation.cpp:72-210`) are a **second copy of the same numerics** — the
very copy P14 promoted, proved bitwise-equivalent to, and was supposed to
replace.

So P14 delivered a validated generic foundation that nothing uses, and the
duplication it existed to remove is still in place. This is the same
capability-vs-reachability pattern P15 found in the thermal property path, at a
larger scale.

It also drags a cross-module dependency: turbulence convection calls
`cfd::thermal::assembleThermalConvectionContribution` with
`specificHeat = 1.0` (`KEpsilonEquation.cpp:196`). Turbulence depends on thermal
to transport a turbulence scalar.

### 3.5 No GPU turbulence of any kind

`cuda/` contains no turbulence kernel, and no turbulence symbol appears in
`include/cfd/gpu/`. `DeviceScalarTransport.hpp` exists from P14 — and, like its
CPU counterpart, has no consumer.

So "GPU equivalence" for P16 is not a matter of verifying an existing path.
Either turbulence routes through P14's device transport, or the item is scoped
out with evidence.

### 3.6 No turbulent heat transfer — P15 and P16 are uncoupled

No `Pr_t`, `turbulentPrandtl`, `α_t` or `k_eff` anywhere in `src/thermal/`,
`include/cfd/thermal/` or the case schema. The only `Prandtl` matches are a
laminar constant in the de Vahl Davis natural-convection test.

μt **is** used for momentum (`ProjectRunner.cpp:453` builds a
`MixtureViscosityModel` from it) but never for energy. A turbulent run with a
thermal block solves the temperature field with **molecular conductivity only**,
which under-predicts wall heat transfer by roughly the same factor μt/μ
under-predicts momentum diffusion — i.e. by a lot.

This must be classified, not silently claimed.

### 3.7 Turbulence recomputes its own mass flux instead of using SIMPLE's

`TurbulenceModel::correct(mesh, velocity, pressure)` receives only cell
velocity. `KEpsilonModel::correct` then calls
`calculateMassFlux(mesh, velocity, fluid_, velocityBoundaries_)`.

SIMPLE maintains a Rhie–Chow-corrected **face** flux that satisfies discrete
continuity. The re-interpolated flux is not that flux and its divergence is not
machine-zero, so k/ε/ω are transported by a field with a spurious
`φ·∇·(ρu)` source.

At convergence the continuity residual is ~3e-11, so the end state is barely
affected; the path there is. Worth **measuring** before calling it a defect —
recorded here as a question, not a verdict.

### 3.8 Floors are unconditional, silent, and undiagnosed

Three separate clipping sites:

- `solveRelaxedScalarTransport` ends with `solved[i] = std::max(solved[i], floorValue)`
  — applied to k, ε and ω after **every** solve;
- `computeTurbulentViscosityField` uses `max(k, kFloor)` and `max(ε, εFloor)`;
- SST's `computeF1Argument` / `computeF2Argument` / `computeSSTTurbulentViscosity`
  use `max(k, 0.0)`, and `computeCrossDiffusionCoefficient` floors CD at `1e-10`.

The last is standard and correct. The first two are the concern: nothing records
that a floor fired, how often, or in how many cells. A model that is quietly
floored in half the domain and one that never floors are indistinguishable from
the outside — which is exactly the "clipping that hides instability" the P16
brief prohibits.

The CD floor at `1e-10` is legitimate and documented; it should not be lumped in
with the other two.

### 3.9 No committed case uses turbulence

`grep -l turbulence cases/*/physics.json` → **no matches**.

The parser accepts `laminar | k_epsilon | k_omega | sst`, `CaseBuilder` builds
all three models and derives their boundary conditions — and no case file in the
repository exercises any of it. Every turbulence test constructs its models in
C++ directly.

So the case path is *implemented* but never *run* end to end. Same pattern as
§3.4 and as P15's property models.

---

## 4. What P16 must decide

| # | Question | Bearing |
|---|---|---|
| 1 | Implement wall functions, or keep low-Re and refine to y⁺≈1? | Determines whether the log-law error can close at all |
| 2 | After wall functions, tighten the Re_τ / u⁺ thresholds | A 40 % gate is not a physics gate. Tightening is strengthening, which is permitted |
| 3 | Migrate turbulence onto `cfd::transport` | Removes a duplicate assembler **and** gives P14 its first consumer |
| 4 | 3-D production (6-term strain invariant) | Unlocks turbulence on the 3-D meshes already in the tree |
| 5 | GPU: route through `DeviceScalarTransport`, or scope out with evidence | No device turbulence exists today |
| 6 | Turbulent heat transfer (Pr_t, k_eff) | P15↔P16 coupling; currently absent |
| 7 | Floor diagnostics | Make clipping observable instead of silent |
| 8 | A committed turbulent case | Makes the models reachable from a case file |

---

## 5. What must not be disturbed

- The three coefficient sets and their separation.
- SST's blending, limiter and cross-diffusion — verified correct above.
- The `Γ = μ + μt/σ` vs `Γ = μ + σμt` distinction.
- The transactional update in each `correct()`.
- `LaminarModel`'s no-op behaviour: SIMPLE's laminar path must stay bitwise
  identical, and the resident-GPU loop is gated on `name() == "laminar"`
  (`SIMPLE.cpp:459`).

---

## 6. Status entering P16

| Scope item | Audited state |
|---|---|
| Eddy-viscosity framework | Present and sound; 2-D only; μt not used for energy |
| k-epsilon | Equations correct; **invalid wall treatment**; 33 % Re_τ error |
| Wall functions / y+ | **Absent** |
| k-omega SST | Blending/limiter/cross-diffusion correct; 27 % Re_τ error |
| Turbulent benchmark suite | Channel only; thresholds gate regression, not physics; no pipe, no separated flow, no SST-discriminating case |
| GPU equivalence | **No device turbulence exists** |

Nothing is marked complete. The next step is the equations document and the
frozen acceptance gate, before any production change.
