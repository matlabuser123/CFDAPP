# GPU-DISC-001B Phase 1 — CPU gradient audit

Written before any CUDA. The CPU implementation is the reference; this records exactly what must be
reproduced.

## 1. Entry points

```text
gradient(mesh, field, boundaries, scheme = GreenGauss)      Gradient.hpp:48
  GreenGauss   -> greenGaussGradient(mesh, field, boundaries, kGreenGaussSkewCorrectionSweeps = 4)
  LeastSquares -> leastSquaresGradient(...)                 (separate formulation)
```

This phase ports **GreenGauss**, the production default. `LeastSquares` is a different scheme with
its own entry point and is out of scope here.

## 2. The algorithm, exactly

`greenGaussGradient` (`Gradient.cpp:534`):

```text
1  faceValues = interpolate(mesh, field, boundaries)         // all faces
2  result     = greenGaussSweep(mesh, field, faceValues, nullptr)
3  skewedFaces  = { internal f : ownerNeighborCrossing(f).skewVector != 0 }
4  obliqueFaces = { boundary f : obliqueNeumannFace(mesh, f, boundaries).applies }
5  boundaryTransfer = boundaryTransferNeeded(mesh)
6  repeat `sweeps` times, only if (skewedFaces or obliqueFaces or boundaryTransfer):
       for f in skewedFaces : faceValues[f] = interpolateInternalFaceSkewCorrected(mesh,f,field,result)
       for f in obliqueFaces: faceValues[f] = scalarConditionForFace(...)-based oblique value
       result = greenGaussSweep(mesh, field, faceValues, &result)
```

`greenGaussSweep` (`Gradient.cpp:478`) is **not** a plain sum. Per cell:

```text
a  for each BOUNDARY face bf of the cell:
     c = boundaryConsistentFaceValue(mesh, cell, bf, field, faceValues, ownerGradient)
     -- c targets the OPPOSITE INTERIOR face, not bf itself
     -- record claim (c.faceId, c.value, c.antiParallelAlignment, bf)
b  conflict resolution: if two boundary faces claim the same opposite face,
     the winner is min by (antiParallelAlignment, claiming boundary face id)
c  sum = Σ_f  Sf_cell · (claimed value if f was claimed, else faceValues[f])
     Sf_cell = +areaVector if owner == cell, else -areaVector
d  result[cell] = sum / volume
```

`boundaryConsistentFaceValue` (`Gradient.cpp:104`), the P12-GRAD-002 treatment:

```text
oppositeFaceId = oppositeInteriorFace(mesh, cell, boundaryFace)      -- topology
farCellId      = the cell across that opposite face
d              = centroid(farCell) - centroid(cell);  farDistance = |d|;  inward = d/|d|
crossing       = ownerNeighborCrossing(oppositeFace)                 -- w = crossing.t
intersection   = boundaryLineIntersection(mesh, boundaryFace, inward) -- point, distance
phiBoundary    = faceValues[bf] + ownerGradient · (intersection.point - centroid(bf))
phiP, phiFar   = field[cell], field[farCell];  backDistance = intersection.distance
secondDeriv    = 2 [ (phiBoundary-phiP)/backDistance + (phiFar-phiP)/farDistance ]
                   / (backDistance + farDistance)
correction     = -0.5 w (1-w) farDistance² secondDeriv
value          = faceValues[oppositeFaceId] + correction
alignment      = unitNormal(bf) · (±unitNormal(oppositeFace), sign by ownership)
```

Any of these returning "no" (`oppositeInteriorFace` empty, `farDistance == 0`, `crossing` empty,
`intersection` invalid) makes the claim not apply.

## 3. The decisive structural observation

**Almost every hard quantity above is field-independent.** Split them:

| immutable (mesh only) | field-dependent (per evaluation) |
| --- | --- |
| `oppositeFaceId`, `farCellId` | `phiP`, `phiFar` |
| `farDistance`, `backDistance`, `w` | `faceValues[bf]`, `faceValues[oppositeFaceId]` |
| `intersection.point − centroid(bf)` (offset vector) | `ownerGradient` (previous sweep) |
| `antiParallelAlignment`, and hence the **conflict winner** | `secondDerivative`, `correction` |
| skewed-face list, per-face crossing `t`/`skewVector` | skew-corrected face values |
| oblique-face list, per-face normal/tangential split | oblique face values |
| interior linear interpolation weights | interior face values |
| boundary `(a, b)` with value = `a + b·φ_P` | the resulting boundary value |

So the port precomputes the left column **once per mesh** on the host — the same thing `DeviceMesh`
already does for primitives — and the kernels evaluate only the right column. **This is
precomputation of mesh-dependent quantities, not a change of formulation**: every arithmetic
expression above is preserved verbatim, including the conflict-resolution ordering, which is a
function of `(antiParallelAlignment, boundaryFaceId)` and therefore immutable too.

## 4. Boundary conditions

`ScalarBoundaryCondition::boundaryValue(Real ownerValue, Real normalDistance)` — six
implementations: `Adiabatic`, `FixedGradient`, `FixedTemperature`, `FixedValue`, `HeatFlux`,
`WallOmega`.

Device encoding: per boundary face, two reals `(a, b)` with

```text
boundary face value = a + b * phi_P
```

**Affineness is verified, not assumed.** For each boundary face the host samples
`boundaryValue(0, d)`, `boundaryValue(1, d)`, `boundaryValue(φ, d)` for a third φ and checks the
affine prediction reproduces the third sample **exactly**. A face whose condition fails that check
is marked **unsupported** and the whole mesh is rejected for the device path rather than silently
approximated — the authorization's "never silently fall back while claiming full GPU residency".

This also covers P12-MESH-001's oblique-Neumann faces: their value uses the normal/tangential split
`boundaryValue(φ_P, d_n) + grad(φ)_P · d_t`, which is `a + b·φ_P + grad·d_t` — still affine in φ_P,
with an extra gradient term whose `d_t` is immutable.

## 5. Device arrays required

Reusable from 001A: volumes, cell/face centroids, face owner/neighbour, area vectors, area
magnitudes, cell→face CSR, boundary face ids and patch slices.

New, all immutable, all uploaded once:

```text
faceInterpWeight[nf]          interior linear weight
faceIsSkewed[nf]              skew-correction participation
faceSkewCrossingT[nf]         w
faceSkewPointX/Y/Z[nf]        crossing point
boundaryA[nf], boundaryB[nf]  value = a + b*phi_P   (boundary faces only)
boundaryTangentX/Y/Z[nf]      oblique tangential offset d_t (zero when not oblique)
claimCount                    number of accepted boundary-consistent claims
claimTargetFace[nc_claims]    opposite interior face receiving the correction
claimCell[nc_claims]          the cell whose sum uses it
claimBoundaryFace[nc_claims]  source boundary face
claimFarCell[nc_claims]
claimFarDistance[], claimBackDistance[], claimW[]
claimOffsetX/Y/Z[]            intersection.point - centroid(bf)
```

Conflict resolution is applied **on the host while building the claim list**, so the device sees at
most one claim per (cell, target face) — the deterministic winner, identical to the CPU's.

## 6. Kernel decomposition

```text
K1  interiorFaceValues     per face: linear interpolation
K2  boundaryFaceValues     per boundary face: a + b*phi_P  (+ grad·d_t when oblique)
K3  greenGaussSum          per cell: Σ Sf_cell · value / volume, honouring claims
K4  skewCorrectFaces       per skewed face: phi_f' + grad_f'·(x_f − x_f')
K5  applyClaims            per claim: recompute correction from the current gradient
```

Sweep loop: `K1,K2 → K3` once, then `sweeps` times `K4,K2,K5 → K3`. All on the default stream, so
ordering is guaranteed without synchronization — GPU-PIPE-001 Phase 3's finding applies here too.
**No `cudaDeviceSynchronize` between kernels**, and no D2H at all: the gradient stays device-resident.

Accumulation is **per cell over that cell's faces** (gather), not scattered per face, so there are
**no atomics** and the summation order is the cell's own face order — the same order the CPU loop
uses. That is what makes bitwise agreement plausible rather than merely close.

## 7. Expected exactness

Every operation is the same arithmetic in the same order on the same inputs:

* face value: same weights, same operands;
* cell sum: same face order (the CSR row preserves `cell.faceIds()` order), same `±Sf`;
* division by volume: same;
* claims: same winner, same formula.

So the target is **bitwise equality**, and the differential test will require it. If any case is not
bitwise, that is a finding to investigate and report — not a reason to reach for a tolerance.

## 8. Out of scope, deliberately

* `LeastSquares` — different scheme, separate entry point.
* The **Green–Gauss four-sweep limitation** is recorded technical debt and is **reproduced, not
  fixed**: the device path uses the same `kGreenGaussSkewCorrectionSweeps = 4`.
* Vector-field gradients (`computeVelocityGradient`) — needed by momentum assembly (001F), not here.
