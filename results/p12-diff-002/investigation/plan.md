
# P12-DIFF-002-INV-001 — plan and derivation

Investigation only. No production source is modified; every formulation below lives in a probe under
`tools/`. Previous evidence (GRAD-001, GRAD-002, A1, GRAD-002-INV-001, DIFF-001, MESH-007) is
preserved unchanged.

## 1. What DIFF-001 established (the starting point)

- `boundaryFaceDiffusionTerms` already uses the wall-normal distance: `|S_orth|/|d| = |S|²/(d·S) =
  |S|/d_n`, verified to 4.5e-16.
- The first-order wall-flux error exists on a **perfectly orthogonal** mesh, where it equals 0.5 h to
  five significant figures — it is the standard half-cell one-sided difference.
- The previously proposed tangential boundary-value transfer is effectively a no-op (≤0.05 %, order
  unchanged at 1.00).

So the only way to remove it is a genuinely higher-order one-sided reconstruction of ∂φ/∂n **at the
face**, which is what this investigation derives and tests.

## 2. Derivation from the finite-volume geometry

Let m̂ = −n̂ be the inward unit normal at a boundary face with centroid x_f, and let the inward ray be
x(s) = x_f + s m̂, s ≥ 0. Available data: the prescribed φ_b at x_f, the owner value φ_P at x_P, and
the value φ_F at the centroid of the cell across the owner's opposite interior face.

**Normal projections** (these are what a normal-direction stencil needs):

```text
h1 = (x_f - x_P) · n̂          s-coordinate of the owner
h2 = (x_f - x_F) · n̂          s-coordinate of the far cell
```

**Tangential offsets**, which do NOT vanish on a non-orthogonal mesh (DIFF-001 measured
|d_t|/d_n up to 0.724), so the cell values must be transferred onto the ray before a normal stencil
may use them:

```text
delta_P = x_P - (x_f + h1 m̂)          (purely tangential)
phi_P_ray = phi_P - grad(phi)_P · delta_P
```

and likewise for F. Unlike DIFF-001's proposal, this transfer is **essential** here: the stencil
being built is a normal-direction one, whereas the existing scheme's over-relaxed decomposition
already handled the tangential direction exactly for linear fields.

**Three-point one-sided derivative at s = 0.** With φ(s) = φ_b + a s + b s² through
(0, φ_b), (h1, φ̃_P), (h2, φ̃_F):

```text
(φ̃_P - φ_b)/h1 = a + b h1
(φ̃_F - φ_b)/h2 = a + b h2
=>  b = [ (φ̃_F - φ_b)/h2 - (φ̃_P - φ_b)/h1 ] / (h2 - h1)
     a = (φ̃_P - φ_b)/h1 - b h1
```

`a = ∂φ/∂s|_0 = ∂φ/∂m̂ = −∂φ/∂n`, so the diffusion contribution to the owner's balance is

```text
flux_into_owner = Gamma (∂φ/∂n) |S| = -Gamma a |S|
```

**Uniform 1D check** (h1 = h/2, h2 = 3h/2), which the probe verifies numerically:

```text
a = [ -8 φ_b + 9 φ_P - φ_F ] / (3h)
```

exact for φ = 1, s, s² (verified by substitution: for φ = s², a = (9h²/4 − 9h²/4)/(3h) = 0 ✓), with
leading error O(h²) φ''' — hence a **second-order** wall flux where the existing two-point form is
first order. Nothing is assumed: the coefficients follow from h1, h2 and are re-derived per face.

**Degenerate cases** (no unique inward neighbour) fall back to the existing two-point form; §4 of
`summary.md` enumerates them and measures how often they occur.

## 3. What is measured

| step | probe | question |
| --- | --- | --- |
| 1D stencil | `diff2_stencil.cpp` | exactness for constant/linear/quadratic, order for cubic, on uniform and graded 1D spacing |
| wall flux, 2D/3D | `diff2_wallflux.cpp` | observed order of the total wall force on orthogonal, translated, distorted, curved and 3D meshes, for constant/linear/quadratic/cubic fields, with both the exact and the computed gradient used for the transfer |
| production relevance | `diff2_channel1d.cpp` | the **discrete** fully developed channel solved with each wall treatment: does dp/dx and the velocity error actually improve? This is the quantity MESH-001 gates on |
| feasibility | `diff2_topology.cpp` | availability of a unique inward neighbour on structured, graded, distorted, multi-block and 3D meshes, and the size of the fallback set |

## 4. Classification

Exactly one of A (works, production-suitable), B (works, needs architecture change), C (does not
solve the production problem), D (insufficient evidence), chosen only from the measurements.
