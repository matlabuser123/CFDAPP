# P12-DIFF-002-INV-001 — findings

**Classification: B — the higher-order reconstruction works mathematically, but production adoption
requires operator/architecture changes, and its benefit to the specific quantities that failed is
partly unproven.**

Investigation only: no production source, test, threshold or prior evidence was modified; nothing was
committed.

## 1. The answer to the critical question

> Does the proposed reconstruction change wall-flux convergence from approximately first order to
> approximately second order?

**Yes, unambiguously.** Per-face wall-flux error, exact transfer gradient
([logs/03](logs/03_perface.log)):

| geometry | field | two-point (shipped) | three-point (derived) |
| --- | --- | --- | --- |
| 2D orthogonal Cartesian | quadratic | **1.000** | **exact** (≤ 3e-16) |
| 2D orthogonal Cartesian | cubic | **1.000** | **2.000** |
| 2D distorted, 48° | quadratic | 1.000 / 1.000 / 1.000 | **exact** |
| 2D distorted, 48° | cubic | 1.001 / 1.000 / 1.000 | **1.998 / 1.999 / 1.999** |
| curved annulus, 270° | radial | 0.994 / 0.996 / 0.998 | **1.998 / 1.999 / 2.000** |
| 3D Cartesian | quadratic | 1.000 | **exact** |
| 3D Cartesian | cubic | 1.000 | **2.000** |

Constant and linear fields are reproduced exactly by both formulations (round-off), as required. The
reconstruction is **exact for quadratic fields**, which includes the Poiseuille profile DIFF-001
measured, so the 2.78 %-at-144×18 error that DIFF-001 established becomes **0**.

With the **computed** GRAD-002 gradient instead of the exact one for the tangential transfer, the
quadratic-field error on the 48° mesh is 5.0e-05 → 1.1e-05 → 2.4e-06 → … , converging at order ≈ 4
([logs/02](logs/02_wallflux.log)) — i.e. the reconstruction does not depend on having an exact
gradient.

## 2. Derivation (no formula assumed)

Full derivation in [plan.md](plan.md) §2. Summary: with m̂ = −n̂ the inward normal and the inward ray
x(s) = x_f + s m̂,

```text
h1 = (x_f - x_P)·n̂                     h2 = (x_f - x_F)·n̂
delta_P = x_P - (x_f + h1 m̂)           phi~_P = phi_P - grad(phi)_P · delta_P      (likewise F)
b = [ (phi~_F - phi_b)/h2 - (phi~_P - phi_b)/h1 ] / (h2 - h1)
a = (phi~_P - phi_b)/h1 - b h1         = dphi/ds at the face
flux_into_owner = -Gamma a |S|
```

The tangential transfer is **essential** here (it was a no-op in DIFF-001's proposal) because the
stencil being built is a normal-direction one: |d_t|/d_n reaches 0.659 on the committed distorted
case. On a uniform 1D spacing the coefficients reduce to `a = [−8φ_b + 9φ_P − φ_F]/(3h)`, verified
exact for 1, s and s².

## 3. Does it improve the production quantities, or only a manufactured flux test?

This is the question the authorization singled out, and the answer is **mesh-dependent and does not
change any order**. The fully developed region MESH-001 gates on reduces exactly to a 1D discrete
problem, which [logs/05](logs/05_channel1d_fixed.log) solves with each wall treatment:

**Uniform spacing** — the two-point scheme's dp/dx error reproduces the closed form 2/(ny²+2)
exactly, confirming the model is the right one (and confirming DIFF-001's measured 0.6135 % at
distortion 0):

| ny | wall flux 2pt → 3pt | dp/dx error 2pt → 3pt | velocity L2 2pt → 3pt |
| --- | --- | --- | --- |
| 8 | 6.2500e-02 → **0** | 3.0303e-02 → **7.7519e-03** | 1.5183e-02 → 8.4927e-03 |
| 18 | 2.7778e-02 → **0** | 6.1350e-03 → **1.5408e-03** | 3.1294e-03 → 1.6879e-03 |
| 72 | 6.9444e-03 → **0** | 3.8565e-04 → **9.6441e-05** | 1.9753e-04 → 1.0565e-04 |
| order | 1.000 → exact | 1.999 → 2.000 | 1.998 → 2.000 |

So **4.0× smaller dp/dx error and 1.85× smaller velocity error, with the order unchanged at 2** —
both schemes were already second order in the solution; the reconstruction improves the constant.

**Graded spacing** (a fixed smooth mapping clustering cells at both walls — a proper refinement
family):

| ny | wall flux 2pt → 3pt | dp/dx error 2pt → 3pt | velocity L2 2pt → 3pt |
| --- | --- | --- | --- |
| 18 | 1.4169e-02 → **0** | 8.3932e-03 → 7.2407e-03 | 2.2692e-03 → 2.1830e-03 |
| 72 | 3.4766e-03 → **0** | 5.3011e-04 → 4.5779e-04 | 1.4248e-04 → 1.3692e-04 |
| order | ~1.0 → exact | 1.998 → 1.998 | 1.999 → 1.999 |

**Only 1.16× on dp/dx and 1.04× on velocity.** With cells clustered at the wall the half-cell error
is already small and the solution error is dominated by the interior discretisation, so the wall
reconstruction buys almost nothing. MESH-002 exists precisely to cluster cells at walls, so this is
the regime a well-built production mesh is in.

An earlier version of this study used a fixed geometric ratio, whose total stretch grows without
bound under refinement; its output ([logs/01](logs/01_channel1d.log)) showed the error *stalling*
and is an artefact of that construction, not a property of either scheme. It is preserved and
labelled rather than deleted.

### 3.1 Would MESH-001's and MESH-003's failing assertions pass? Undetermined.

The failing assertions are not "is the error smaller" but: dp/dx **observed order** ≥ 1.5 (measured
1.181 with GRAD-002), the fine-grid error inside its **GCI** band, and MESH-003's rise
**monotonicity**. Decomposing MESH-001's measured dp/dx error against the 1D model term by term:

| ny | measured (GRAD-002, distorted) | 1D wall term (2pt) | residual |
| --- | --- | --- | --- |
| 8 | 2.2800 % | 3.0303 % | −0.750 |
| 12 | 1.0957 % | 1.3699 % | −0.274 |
| 18 | 0.6788 % | 0.6135 % | **+0.065** |

The residual (everything that is not the 1D wall term: distortion, entrance, 2D coupling) **changes
sign** and does not follow a clean power of h. Replacing the wall term with its 4×-smaller
counterpart (0.7752 %, 0.3460 %, 0.1541 %) would leave the total dominated by that irregular
residual, so the *observed order* over a three-grid family could improve, stay, or get worse. I
cannot resolve this with probes: it needs the reconstruction in the solver, which this authorization
forbids. This is the single largest reason the classification is B rather than A.

The same applies, more strongly, to MESH-003's rise-monotonicity assertion, which
GRAD-002-INV-001 showed is a difference of two ≈0.83 quantities whose reference moves with the grid.

### 3.2 What it does resolve structurally

Independently of any specific assertion: GRAD-002-INV-001 established that the old validation
benefited from a cancellation between a first-order gradient and a first-order wall flux. Making the
wall flux second order removes the *asymmetry* — both operators would then be second order and
mutually consistent — so the cancellation mechanism itself disappears. That is a real argument for
doing this work, separate from whether three specific assertions flip.

## 4. Production feasibility

### 4.1 Stencil availability — measured on every committed case ([logs/04](logs/04_topology.log))

All 19 buildable committed cases: **100 % of boundary faces have a usable stencil** (no-opposite 0,
bad-projection 0), including every mesh class the authorization named:

| case class | example | boundary faces | usable | min anti-parallel | max \|d_t\|/d_n | h2/h1 |
| --- | --- | --- | --- | --- | --- | --- |
| multi-block | `curved_channel_multiblock` | 204 | **204** | 0.999 | 2.0e-02 | 2.97–3.01 |
| multi-block, hole | `obstacle_channel_multiblock` | 248 | **248** | 1.000 | 0 | 3.00 |
| graded | `channel_transpiration_graded` | 144 | **144** | 1.000 | 3.3e-16 | 3.00–3.20 |
| distorted structured | `poiseuille_distorted` | 144 | **144** | 0.994 | **6.6e-01** | 2.96–3.05 |
| 3D | `lid_driven_cavity_3d_re1000` | 6144 | **6144** | 1.000 | 0 | 3.00 |
| 3D duct | `duct_3d` | 1664 | **1664** | 1.000 | 0 | 3.00 |

Constructed graded meshes at ratio 1.2 and 1.5 are also fully usable (h2/h1 = 3.20, 3.50). The
opposite-face choice is never ambiguous (min anti-parallel 0.994).

### 4.2 Where a unique inward neighbour does not exist

Exactly one condition: the cell has **no interior face across the boundary face**
(`oppositeInteriorFace` returns nothing), or the projections are unusable (h1 ≤ 0 or h2 ≤ h1). The
probe quantifies it on deliberately degenerate meshes:

| mesh | boundary faces | usable | fallback |
| --- | --- | --- | --- |
| 2D 1×1 | 4 | 0 | **100 %** |
| 2D 8×1 (one cell across) | 18 | 2 | 89 % |
| 2D 1×8 | 18 | 2 | 89 % |
| 3D 1×1×1 | 6 | 0 | 100 % |
| 3D 8×8×1 (one cell in z) | 160 | 32 | 80 % |

This is a **topological** condition, not a geometric threshold — it cannot flip under round-off, and
it is the same condition GRAD-002's boundary treatment already uses. The fallback is the existing
two-point form, so such meshes keep today's behaviour exactly. No new topology assumption is
introduced: the probe uses only `oppositeInteriorFace`, which works on the general face-based `Mesh`
including across multi-block interfaces (confirmed: the four multi-block cases are 100 % usable).

### 4.3 Operator and architecture interaction — the reason for B

| area | interaction |
| --- | --- |
| **matrix structure** | The stencil couples the boundary cell to its **far** cell. `FaceDiffusionTerms` exposes one implicit `coefficient` (owner↔boundary) plus an `explicitFlux`. An implicit far-cell coupling needs a matrix entry that corresponds to **no face of that cell** — CFDApp builds sparsity from the mesh face graph, so this is a genuine architecture change. The alternative, a lagged deferred correction, re-introduces exactly the failure mode GRAD-002 hit (a lagged boundary term slowing the outer iteration 3–4×), so it must not be chosen casually. |
| **data flow** | `boundaryFaceDiffusionTerms(mesh, face, gamma, distance, gradPhi, prescribedValue)` receives the gradient but **not the field φ nor the far-cell id**. All six call sites would need to pass them. |
| **call sites** | `MomentumEquation.cpp:116` and `:213`, `EnergyEquation.cpp:116` and `:196`, `SpeciesEquation.cpp:77`, `KEpsilonEquation.cpp:116` — momentum, thermal, species and turbulence. `Diffusion.cpp:196-216` contains a **second, independent** boundary-diffusion implementation that would have to change in step or the two would disagree. |
| **non-orthogonal correction** | The new form computes ∂φ/∂n directly and multiplies by \|S\|, so it **supersedes** both the implicit \|S_orth\|/d part and the explicit `S_nonorth·∇φ_P` part at value-prescribing faces. It must replace them, not be added to them, or the tangential contribution is double-counted. |
| **Green–Gauss / GRAD-002** | Both would then use the same stencil (boundary value, owner, opposite neighbour) — a consistency gain. GRAD-002 evaluates the fit's derivative at the **owner**; this evaluates it at the **face**. No conflict, and the shared geometry helper could be factored. |
| **boundary conditions** | Applies only where `prescribesBoundaryValue` is true (FixedValue, Wall, MovingWall, Inlet, FixedTemperature, WallOmega). Neumann-type faces prescribe the flux and must stay untouched. Wall functions (k-ε/SST) prescribe values at walls and would inherit the change — their wall treatment is calibrated, so this needs separate verification. |

## 5. Classification: B

- **It works** (§1): per-face wall flux goes from order 1.000 to 2.000, exact for quadratics, on
  orthogonal, translated, 48°-distorted, curved and 3D geometry, with the computed gradient as well
  as the exact one.
- **The stencil is available** (§4.1): 100 % of boundary faces on all 19 buildable committed cases,
  with a clean topological fallback (§4.2).
- **But production adoption needs architecture work** (§4.3): a far-cell matrix coupling or a lagged
  correction (with the convergence risk that carries), a changed signature/data flow through six
  call sites plus a duplicate implementation, and supersession rather than addition of the existing
  non-orthogonal boundary correction.
- **And the benefit to the failing quantities is partly unproven** (§3): 4× better dp/dx on uniform
  meshes but only 1.16× on wall-clustered meshes, **no order change** in either, and the specific
  failing assertions (observed order, GCI band, rise monotonicity) cannot be predicted from probes.

Not A (architecture work and unproven case-level benefit). Not C — the reconstruction demonstrably
fixes the wall-flux order and removes the cancellation asymmetry, so it is not true that it "does
not solve the observed production problem"; it solves the mechanism while leaving the specific
assertion outcomes open. Not D — the mathematical question the investigation was asked to settle is
settled with margin.

## 6. Smallest proposed production scope (NOT implemented)

1. Add a geometry helper returning the inward stencil for a boundary face — `{h1, h2, farCellId,
   deltaP, deltaF, valid}` — built on `oppositeInteriorFace`, shared with GRAD-002's existing use.
2. Extend `FaceDiffusionTerms` with a **second implicit coefficient and the far cell's id**
   (`farCoefficient`, `farCell`), and extend `boundaryFaceDiffusionTerms` to fill them for
   value-prescribing faces when the stencil is valid, superseding the `|S_orth|/d` + `S_nonorth`
   pair there; keep today's values exactly when it is not.
3. Let the assembler add the far-cell entry. If the sparsity pattern cannot admit it, stop and
   reconsider — **do not** substitute a lagged explicit correction without its own convergence gate.
4. Update the six call sites; make `Diffusion.cpp`'s duplicate delegate to the shared function.
5. Verification as in §7.

Scope boundary: momentum, thermal, species, turbulence Dirichlet **diffusion** only. No change to
convection, pressure correction, GRAD-002, or Neumann faces.

## 7. Proposed P12-DIFF-002 acceptance gate (NOT frozen)

Each criterion is stated with the measurement that makes it checkable, and every threshold below is
derived from a measurement already in this investigation — so the gate is satisfiable by
construction. It would be frozen only after a dry-run against the unchanged baseline, per the
standing rule.

| id | criterion |
| --- | --- |
| **W1** | Per-face wall-flux order ≥ 1.8 in L1 for a cubic manufactured field on 2D orthogonal, 2D translated, 2D distorted (48°), curved annular and 3D Cartesian families (measured here: 2.000, 2.000, 1.999, 2.000, 2.000; two-point baseline 1.000). |
| **W2** | Wall flux **exact** (≤ 1e-12 relative) for constant, linear and quadratic fields on all of the above (measured: ≤ 3e-16). |
| **W3** | Stencil availability reported for every committed case (expected 100 %, §4.1). Bit-identity must **not** be required on orthogonal meshes: the reconstruction deliberately differs there, being exact where the two-point form is first order. Instead, every committed case output is compared against the pre-change baseline, and every difference is bounded, explained, and shown to move the case **towards** its analytic or benchmark reference. |
| **W4** | 1D discrete channel: dp/dx error improves ≥ 3× on uniform spacing at ny = 18 (measured 4.0×) and does not regress on the graded family (measured 1.16× better). |
| **W5** | Outer-iteration cost: SIMPLE iteration counts on `poiseuille_distorted`, `curved_channel_multiblock` and `lid_driven_cavity` within 1.25× of the pre-change counts — the explicit guard against re-introducing GRAD-002's lagged-correction slowdown (GRAD-002 measured 4.1×). |
| **W6** | Conservation unchanged: global mass imbalance and per-column flow error no worse than the pre-change values on every committed case. |
| **W7** | Thermal and species Dirichlet diffusion verified against their analytic linear profiles, and k-ε/SST wall behaviour compared against the pre-change baseline with any change explained. |
| **W8** | `MESH-001` and `MESH-003` rerun **unchanged**; their historical gates are not amended. Their outcome is **reported, not assumed** — §3.1 shows it cannot be predicted, so the gate must not presuppose it. |
| **W9** | Focused suites, then full Release/Debug/GUI/CLI regression with exact counts; sanitizers at CI settings with the known MESH-004 test defect as the only permitted failure. |
| **W10** | Degenerate meshes (1×1, 8×1, 1×8, 3D 1×1×1, 3D 8×8×1) produce results **bit-identical** to the pre-change library, since every face there falls back. |

W8 is deliberately not a pass/fail on those two tests: if the reconstruction is correct and they
still fail, the right response is a disclosed gate amendment for them, not weakening this gate.
