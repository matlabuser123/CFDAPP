# P16 — SST near-wall audit

Written for: a reviewer checking what was changed in SST and what was
deliberately left alone.

Traced end to end through `SSTModel::correct()`, `SSTEquation.cpp`,
`SSTCoefficients.hpp`, `WallDistance.cpp`, `CaseBuilder.cpp` and `SIMPLE.cpp`,
**before any edit**.

---

## Correct, verified, and NOT to be touched

Checked against Menter (1994) term by term. All of this predates P16 and is
preserved.

| Component | State |
|---|---|
| `α = β/β* − σ_ω κ²/√β*` | correct — the log-law consistency relation |
| `F1 = tanh(arg1⁴)`, `arg1 = min(max(√k/(β*ωy), 500ν/(y²ω)), 4ρσ_ω2 k/(CD y²))` | correct |
| `F2 = tanh(arg2²)`, `arg2 = max(2√k/(β*ωy), 500ν/(y²ω))` | correct |
| Cross-diffusion `D_ω = 2(1−F1)ρσ_ω2 ∇k·∇ω/ω` | correct, weighted by (1−F1) |
| `CD` floored at 1e-10 | standard, documented |
| Limiter `μ_t = ρa₁k / max(a₁ω, S·F2)` | correct |
| Production limiter `min(P_k, 10β*ρkω)` | correct, applied to the k source only |
| Blending of σ_k, σ_ω, β, α through F1 | correct |
| `Γ_k = μ + σ_k μ_t`, `Γ_ω = μ + σ_ω μ_t` | correct — the **linear** k-ω form, not k-ε's `μ + μ_t/σ` |
| ω production as `α ρ S²` | correct, and deliberately avoids dividing by μ_t |
| Transactional update (both solves before any member is written) | correct |
| Wall distance computed once at construction | correct |

One detail worth calling out as genuinely well done: the ω production is formed
as `blendedAlpha · ρ · S²` rather than `blendedAlpha · ρ/μ_t · P_k`. The two are
algebraically identical, and the chosen form never divides by a μ_t that the
limiter may have made small. That is the right call and it stays.

---

## Where it is inconsistent with the P16 wall-function framework

### 1. There is no wall treatment in the solve at all

`SSTModel::correct()` never touches a wall cell specially. The entire near-wall
handling lives in the boundary conditions `CaseBuilder` assigns:

```
k      FixedValue(0)                     (buildTurbulenceKBoundary, wallFunctions=false)
omega  WallOmega = 60 nu / (beta1 y^2)   (buildTurbulenceOmegaBoundarySST)
```

### 2. `WallOmega` is a LOW-Re treatment being used at wall-function spacing

`ω_wall = 60ν/(β₁y²)` is Wilcox's near-wall **asymptotic**, derived for a first
cell inside the viscous sublayer (y⁺ → 0). Applying it with a first cell in the
log layer is using it outside its own derivation — structurally the same defect
the k-ε audit found, with the opposite sign: k-ε used a high-Re model at
sublayer spacing; SST uses a low-Re expression at log-layer spacing.

Measured consequence, from the preserved baseline
(`pre_p16_baseline/channel_flow_verbatim/sst/medium/`): **Re_τ = 131.4 against
180, a 27.0 % error**, u⁺ L∞ = 9.22, first-cell y⁺ = 8.21.

### 3. k = 0 at the wall

The same over-constraint the k-ε work removed. With a wall function, k must be
zero-gradient and ω the imposed quantity.

### 4. No wall shear reaches momentum

`SSTModel` does not override `wallViscosity()`, so it inherits the base
`nullptr` and the momentum assembly feels a **laminar** wall — no matter what
the turbulence field does.

### 5. No y⁺ diagnostics

Nothing reports where the first cell actually landed, so a user cannot tell
whether the near-wall treatment in use is valid for their mesh.

---

## What the fix must be, and what it must not be

**Must reuse `WallTreatment`.** The equilibrium wall state is model-independent:

```
u_τ* = C_μ^¼ √k_P
τ_w  = ρ u_τ* κ U_P / ln(E y*)
ω_P  = u_τ* / (√C_μ κ y_P)
```

`wallAdjacentSpecificDissipation` already computes that ω_P, and
`WallFunctions.SpecificDissipationAgreesWithDissipation` already proves it
equals `ε_P/(C_μ k_P)` for the same state — derived independently on each side,
so the k-ε and SST wall cells describe the *same* near-wall physics rather than
two conventions.

**A second wall-function implementation would be the wrong fix**, and the
codebase says so in its own words (`BoundaryEncoding.hpp`: *"Moved, not copied:
two copies of this would drift"*).

### One SST-specific check the k-ε work did not need

Imposing a wall-function ω must not break F1's near-wall limit. Worked through
at y⁺ = 50 with the equilibrium state:

```
term1 = √k/(β*ωy)     = (u_τ/0.5477)/(0.09·u_τ/0.123) ≈ 2.49
term2 = 500ν/(y²ω)    = 61.5/y⁺                        ≈ 1.23
arg1  = max(2.49, 1.23) = 2.49   →  F1 = tanh(2.49⁴) = tanh(38.7) ≈ 1
```

So F1 → 1 at the wall is preserved: the imposed ω keeps SST on its k-ω branch
there, which is the whole point of the blending. This is asserted as a test
rather than left as an argument.

### The ω boundary condition must change too

With ω **imposed in the wall cell**, keeping `WallOmega` (a Dirichlet at
60ν/β₁y²) on the wall face would impose two different values for the same
physics. The face condition becomes zero-gradient when wall functions are on;
`WallOmega` stays for the low-Re path, unchanged.

---

## Scope boundary

`wall_functions` is currently rejected by the parser for anything but
`k_epsilon`. That rejection was correct when written and is now widened to
include `sst`. `k_omega` stays rejected — its near-wall work is not part of this
milestone, and a case asking for it should fail rather than silently run with a
different treatment than it asked for.
