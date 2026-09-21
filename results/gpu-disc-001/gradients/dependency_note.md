> **SUPERSEDED — kept as the record of a recommendation that was overruled, and correctly so.**
>
> This note recommended reordering 001E (boundary conditions) before 001B (gradients), arguing that
> a gradient cannot be ported onto unported boundary conditions. The user rejected the reordering
> and directed that gradient-specific boundary handling be done **inside 001B** rather than deferred
> to the general BC phase.
>
> That was the right call, and this note's premise was wrong in a specific way. The gradient does
> not need the boundary-condition *machinery* ported; it needs each boundary face's
> `boundaryValue(phi_P, d)` reproduced. That is a small, closed problem — every scalar condition in
> the codebase is either constant in `phi_P` or `phi_P + c` — and it is solved in 001B by encoding
> and **bitwise-verifying** each face's condition, with an explicit rejection path for anything that
> does not fit. Option 2 below ("take boundary face values as an input array") was dismissed as
> unable to pass the gate because the oblique-Neumann and tangential-transfer paths re-read the
> conditions inside each sweep. They do — and the plan simply stores a second encoding at the normal
> distance plus the tangential offset, so those sweeps are reproduced exactly.
>
> Outcome: 001B passed with **bitwise** equality on 132 cases including stretched, non-orthogonal,
> skewed, multi-block and 3D meshes under Dirichlet, Neumann and mixed conditions. See `summary.md`.
>
> The body below is unchanged.

---

# GPU-DISC-001B — gradients depend on 001E, not the other way round

**Not implemented. The prescribed order has a dependency inversion, and stacking a gradient on
unported boundary conditions is the thing GPU-DISC-001's own rule forbids.**

## What the production gradient actually does

`greenGaussGradient` (`src/discretization/Gradient.cpp:534`) is not "sum `phi_f * Sf` over faces".
Reading the real implementation:

```cpp
SurfaceField faceValues = interpolate(mesh, field, boundaries);   // <- boundaries
VectorField  result     = greenGaussSweep(mesh, field, faceValues, nullptr);

// faces whose skew vector is non-zero            <- MeshGeometry::ownerNeighborCrossing
// oblique Neumann boundary faces (P12-MESH-001)  <- obliqueNeumannFace(mesh, face, boundaries)
// boundary tangential transfer (P12-GRAD-002)    <- boundaryTransferNeeded(mesh)

for (sweep = 0; sweep < skewCorrectionSweeps && (...); ++sweep) {
  for (skewed face)  faceValues[f] = interpolateInternalFaceSkewCorrected(...);
  for (oblique face) faceValues[f] = scalarConditionForFace(mesh, face, boundaries)...  // <- boundaries
}
```

Measured dependencies in `Gradient.cpp`:

```text
scalarConditionForFace    2 references
obliqueNeumannFace        3 references
boundaryTransferNeeded    2 references
interpolate(mesh, field, boundaries)   takes the BoundaryConditionSet
```

Every one is boundary-condition machinery, and the P12-GRAD-002 boundary-consistent face value —
a quadratic fit along the owner–neighbour direction whose third point lies **on** the boundary, not
at the face centroid — is the part of this operator that two prior phases were spent getting right.

## Why this blocks 001B as ordered

The authorization sequences `001B gradients` before `001E boundary conditions`, and states:

> Do not stack later CUDA operators on an unqualified earlier operator.

Implementing the gradient now means one of:

1. **Port the boundary machinery inside 001B** — which is 001E, done early and under the wrong
   heading, with its own gate skipped.
2. **Take boundary face values as an input array** computed on the host. That verifies the
   Green-Gauss sum and the internal skew sweep, but the oblique-Neumann re-evaluation and the
   tangential-transfer trigger both re-read the boundary conditions *inside each sweep*, so the
   result would only match production on meshes where those paths are inert — an orthogonal
   Cartesian mesh with simple BCs. The gate explicitly requires stretched, non-orthogonal and
   skewed meshes, which is precisely where they are not inert.
3. **Implement a plain Green-Gauss** without skew correction or the P12-GRAD-002 boundary value.
   That is a different numerical scheme. `FUNDAMENTAL RULE`: *do not redesign numerical schemes
   merely to make them easier to execute on the GPU.*

Option 2 is the only one that is not a rule violation, and it cannot pass 001B's stated gate.

## What is not in question

This is an **ordering** issue, not a numerical-semantics one. Nothing about the CPU gradient looks
wrong; it looks carefully built, and P12-NUM-003, P12-MESH-001 and P12-GRAD-002 are visible in it.
No CPU defect is being reported.

`DeviceMesh` (001A, PASS) already anticipated part of this: derived geometry such as
`ownerNeighborCrossing` was deliberately **not** mirrored speculatively, to be added with the
operator that needs it. The skew sweep needs per-face `FaceCrossing {t, crossingPoint, skewVector}`,
which is immutable and belongs in `DeviceMesh` when 001B proceeds.

## Recommendation

**Reorder to 001E → 001B.** Boundary conditions are a prerequisite of gradients, of diffusion's
boundary coefficients (P12-DIFF-002) and of convection's boundary behaviour — every operator in the
list consumes them. The sequence that respects the dependency graph is:

```text
001A mesh  ->  001E boundary conditions  ->  001B gradients  ->  001C diffusion
           ->  001D convection  ->  001F momentum assembly  ->  ...
```

001E's own brief already flags it as "a high-risk gate" and asks for an inventory of every boundary
condition required by GPU-supported production cases, with any unportable case explicitly marked
unsupported. That inventory is the natural next unit of work and unblocks everything after it.

Reordering changes no numerics and no gate — only the sequence in which they are met. Stopping here
rather than proceeding, because the alternative is to stack gradients on unported boundary
conditions.
