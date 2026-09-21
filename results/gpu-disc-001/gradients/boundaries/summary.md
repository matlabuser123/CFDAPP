# GPU-DISC-001B — boundary handling, specifically

The authorization said not to postpone a gradient-specific boundary mismatch to the later general
BC phase. There was one, it was found by the plan's own verification rather than by a test, and it
is fixed here. This file is the boundary-only view of the gate.

## 1. What the gradient asks of a boundary condition

Three distinct things, all inside `greenGaussGradient`:

```text
1  interpolate()        every boundary face:  boundaryValue(phi_P, |x_f - x_P|)
2  oblique re-eval      P12-MESH-001 faces:   boundaryValue(phi_P, d.n) + grad(phi)_P . d_t
                        -- re-evaluated inside EVERY sweep, with the latest owner gradient
3  claim transfer       P12-GRAD-002:         faceValues[bf] + grad(phi)_P . (x_int - x_bf)
```

(2) and (3) are why this could not be deferred: both re-read boundary information *during* the
sweep loop, and both are live exactly on the stretched, non-orthogonal and skewed meshes the gate
requires. An implementation that took host-computed boundary face values as a fixed input array
would only match production on meshes where these paths are inert.

## 2. The mismatch that was found

The device cannot call a virtual `boundaryValue`, so the plan encodes it. The first encoding was
affine, `a + b*phi_P`, with `b` recovered as `boundaryValue(1,d) - boundaryValue(0,d)`.

For a Neumann condition the implementation is `ownerValue + (gradient * normalDistance)`, so `b`
should be exactly 1. It is not: `fl(1 + a) - a` differs from 1 whenever `a = g*d` carries bits below
one ULP of `(1 + a)`.

**The plan's own bitwise verification caught this and rejected the mesh** — 32 of 80 cases failed
as `plan unusable`, rather than producing a quietly-wrong boundary value. That is the "never
silently approximate" rule doing its job; the encoder failing closed is why this shows up as a
loud, specific failure instead of a small unexplained residual later.

## 3. The fix

Reproduce the condition's arithmetic, not its value in exact arithmetic. Three forms, chosen per
face, each **verified bitwise** against the condition itself over 11 probe values spanning
`0, ±1, 0.25, 3.5, -7.125, 1e3, -1e-3, 1e8, -3.7e-7, 273.15`:

```text
kBoundaryConstant   value = a             FixedValue, FixedTemperature, WallOmega
kBoundaryShift      value = phi_P + a     FixedGradient, Adiabatic, HeatFlux
kBoundaryAffine     value = a + b*phi_P   anything else still affine in phi_P
```

Every scalar condition in the codebase is covered by the first two. The third exists so a future
affine condition works without a code change, and the **rejection path** exists so a non-affine one
disqualifies the mesh (`usable() == false`, with the face id and condition name in
`unsupportedReason()`) instead of being approximated.

An oblique face carries a **second** encoding, built at the normal distance `d.n` rather than the
straight-line distance, because `interpolate()` and the sweep re-evaluation use different distances.
Conflating them would have been a real discretization change, silently.

## 4. Boundary-specific results

From `../differential/differential.log`. Three boundary-condition sets per mesh:

* **dirichlet** — every patch `FixedValue`, distinct values per patch.
* **neumann** — every patch `FixedGradient`, distinct gradients per patch. This is what makes
  oblique faces oblique: a value-prescribing condition is never oblique
  (`obliqueNeumannFace` returns early on `prescribesBoundaryValue`).
* **mixed** — alternating patches, so one mesh carries both encodings and the per-face `kind`
  lookup must be right *per face*, not per mesh. Also the only configuration where some boundary
  faces of a mesh are oblique and others are not.

Oblique-face counts actually exercised (Neumann sets):

```text
graded2d 16          10 oblique faces
distorted q16        64
q16 translated       64
sheared 0.35         64
planar skew 3d 6    216
warped 3d 6         216
```

All bitwise equal. Boundary-adjacent cell counts are printed per case (`bcells`), 60–156 in 2D and
152–488 in 3D — these are the cells where the claim substitution, the oblique re-evaluation and the
boundary transfer all act, and they are included in the same bitwise comparison as the interior.

## 5. What this does not settle

This covers the boundary conditions **as the gradient consumes them**. It is not 001E. Diffusion's
boundary coefficients (P12-DIFF-002), convection's boundary behaviour, and vector-valued conditions
(`VectorBoundaryCondition`, needed for momentum) are untouched and unverified here. A mesh whose
gradient plan builds successfully says nothing about whether those later operators can reproduce
the same conditions.
