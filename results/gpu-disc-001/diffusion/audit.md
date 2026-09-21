# GPU-DISC-001C Phase A — CPU diffusion audit

Written before any CUDA. The CPU implementation is the numerical specification; this records what
must be reproduced, and resolves one scope question the brief leaves open.

## 1. There are TWO diffusion paths, and only one is production

This matters enough to lead with, because the brief asks for "matrix diagonal, off-diagonal, RHS"
comparisons and only one of the two produces a matrix at all.

### 1a. Explicit operator — `cfd::discretization::diffusion()`

`src/discretization/Diffusion.cpp:251`. Returns a `ScalarField`, namely `div(Gamma grad phi)`
per unit volume. `laplacian()` (`Laplacian.cpp:10`) is a one-line wrapper over it with
`diffusivity = 1.0`. Constant scalar diffusivity only.

It has **no production caller**. Grepping `src/` and `apps/` finds only `Laplacian.cpp`. This is
already on record — `results/p12-mesh-005/summary.md`:

> The explicit `diffusion()`/`laplacian()` operators have no production caller in `src/` or
> `apps/`: the solvers use the implicit assemblies. These defects affect verification and API use
> only.

It is also where the **MESH-005 two-cells-across defect** lives (§6).

### 1b. Implicit operator — the shared face-terms core

`include/cfd/discretization/NonOrthogonalDiffusion.hpp` states its own status plainly:

> the ONE implementation of the non-orthogonal correction for every IMPLICIT (matrix-assembling)
> diffusion term in the code: momentum viscous terms (`physics::assembleDiffusionContribution`),
> thermal conduction (`thermal::assembleThermalDiffusionContribution`), species diffusion
> (`species::assembleSpeciesDiffusionContribution`) and the k / epsilon / omega diffusion of every
> turbulence model (`turbulence::assembleScalarDiffusionContribution`).

This is the operator that produces a diagonal, off-diagonals and an RHS; that supports a
**spatially varying** diffusivity field; and that every downstream GPU-DISC stage (momentum
assembly 001F, pressure correction) actually consumes.

### Scope decision

**001C ports the implicit path (1b).** It is the production operator, it is what the brief's
comparison list describes, and it is what the rest of GPU-DISC-001 needs. The explicit operator
(1a) is **deliberately out of scope** and is not ported: it is unreachable from production, and
porting it would mean porting the MESH-005 defect's cubic path for no downstream consumer. This is
a scope statement, not a silent omission — §6 covers what happens to the known defect.

The concrete assembly mirrored is `turbulence::assembleScalarDiffusionContribution`
(`src/turbulence/KEpsilonEquation.cpp:73`), the generic scalar one. The other three assemblers
differ only in their own diffusivity and boundary-VALUE evaluation, which the header says is
legitimate and per-caller; the face geometry and coefficients are shared and are what is ported.

## 2. The production assembly, exactly

```text
gradPhi = gradient(mesh, phi, boundaries, nonOrthogonal.gradientScheme)   // ALWAYS built
internalGradPhi = nonOrthogonal.enabled ? &gradPhi : nullptr              // gates INTERNAL faces only

for faceId = 0 .. numberOfFaces-1:            <-- FACE-ID ORDER. This is load-bearing (§4).
  if boundary:
    owner    = face.owner()
    distance = MeshGeometry::distance(cell(owner).centroid, face.centroid)
    gammaFace = diffusivity[owner]                       // owner value, no interpolation
    terms    = boundaryFaceDiffusionTerms(mesh, face, gammaFace, distance, &gradPhi,
                                          prescribesBoundaryValue(bc.type()))
    phiB     = scalarBc->boundaryValue(phi[owner], distance)
    rhs[owner] += terms.explicitFlux
    A(owner,owner) += terms.coefficient
    rhs[owner] += terms.boundaryValueCoefficient * phiB
    if terms.farCellCoefficient != 0.0:  A(owner, terms.farCell) += -terms.farCellCoefficient
  else:
    o, n      = face.owner(), *face.neighbor()
    dPN       = MeshGeometry::ownerNeighborDistance(mesh, face)
    gammaFace = interpolateInternalFace(mesh, face, diffusivity)   // SCALAR overload
    terms     = internalFaceDiffusionTerms(mesh, face, gammaFace, dPN, internalGradPhi)
    if internalGradPhi: rhs[o] += terms.explicitFlux;  rhs[n] -= terms.explicitFlux
    A(o,o) += c;  A(o,n) += -c;  A(n,n) += c;  A(n,o) += -c        // c = terms.coefficient
```

### `internalFaceDiffusionTerms` (`NonOrthogonalDiffusion.cpp:51`)

```text
if gradPhi != null and decomposeFaceArea(mesh, face).valid:
    gradFace    = interpolateInternalFace(mesh, face, gradPhi)     // VECTOR overload
    coefficient = gammaFace * |orthogonal| / distance
    explicitFlux= gammaFace * dot(nonOrthogonal, gradFace)
else:
    coefficient = gammaFace * face.area() / distance
    explicitFlux= 0
boundaryValueCoefficient = coefficient;  farCellCoefficient = 0;  higherOrder = false
```

### `boundaryFaceDiffusionTerms` (`NonOrthogonalDiffusion.cpp:64`)

```text
if gradPhi != null AND prescribedValue:
    stencil = MeshGeometry::boundaryInwardStencil(mesh, face)
    if stencil.valid:                                   // P12-DIFF-002 3-point reconstruction
        h1, h2    = stencil.h1, stencil.h2
        gammaArea = gammaFace * face.area()
        cP = h2 / (h1*(h2-h1));  cF = h1 / (h2*(h2-h1));  cB = 1/h1 + 1/h2
        coefficient              = gammaArea * cP
        farCellCoefficient       = gammaArea * cF
        farCell                  = stencil.farCell
        boundaryValueCoefficient = gammaArea * cB
        explicitFlux             = gammaArea * ( cP*dot(gradPhi[owner],  stencil.deltaP)
                                               - cF*dot(gradPhi[farCell], stencil.deltaF) )
        higherOrder = true
    else if decomposeBoundaryFaceArea(mesh, face).valid:  // two-point non-orthogonal fallback
        coefficient  = gammaFace * |orthogonal| / distance
        explicitFlux = gammaFace * dot(nonOrthogonal, gradPhi[owner])
else:
    coefficient = gammaFace * face.area() / distance;  explicitFlux = 0
```

`cP - cF = cB` identically, so a constant field gives exactly zero flux — brief test 1.

`prescribesBoundaryValue` (`NonOrthogonalDiffusion.cpp:14`): true for FixedValue, FixedTemperature,
WallOmega, Wall, MovingWall, Inlet; false for FixedGradient, HeatFlux, Adiabatic, Outlet, Symmetry.
A Neumann-type face's flux is prescribed and is **never** corrected.

## 3. Immutable vs field-dependent — the same split that made 001B bitwise

| immutable (mesh + BC type only) | field-dependent (per assembly) |
| --- | --- |
| `decomposeFaceArea` orthogonal/nonOrthogonal, `valid` | `gradPhi` (from the CUDA gradient) |
| `decomposeBoundaryFaceArea` likewise | `gradFace`, `explicitFlux` |
| `boundaryInwardStencil` h1, h2, deltaP, deltaF, farCell, valid | `gammaFace` (diffusivity is a field) |
| `ownerNeighborDistance`, owner→face `distance` | `phiB = a + b*phi_P` |
| `face.area()`, owner/neighbour, CSR connectivity | every coefficient (all scale with gammaFace) |
| `prescribesBoundaryValue(bc.type())` | |
| interior interpolation distances dPf, dNf | |
| **cP, cF, cB** — functions of h1, h2 only | |

`cP`, `cF`, `cB` are pure geometry and are precomputed. Everything else is a multiply by a
field-dependent `gammaArea` or a dot with a field-dependent gradient.

## 4. Accumulation order is part of the answer

`SparseMatrixBuilder::build()` (`src/algebra/SparseMatrix.cpp:143`):

* `std::stable_sort` by `(row, column)` — so columns within a row come out **ascending**, and
  triplets with the same `(row, column)` keep **insertion order**, which is face-id order;
* the per-entry sum is `first`, then `+=` each subsequent, **in that order**;
* an entry whose sum is **exactly 0.0 is dropped** — so the sparsity pattern is value-dependent and
  must be reproduced, not just the values.

Two consequences for the kernel:

1. Every write to row `P` comes from a face **incident to `P`** (an internal face writes rows
   `owner` and `neighbour`; a boundary face writes row `owner`, and its far-cell entry targets a
   column that is already `P`'s neighbour across the opposite interior face — confirmed in
   `boundaryInwardStencil`, which takes `farCell` across `oppositeInteriorFace`). So the assembly
   can be a **gather per row**, with no atomics.
2. The gather must visit `P`'s incident faces in **ascending face id**, not in `cell.faceIds()`
   order, and must emit columns ascending. The plan therefore stores a per-cell face list sorted by
   face id. Assuming `cell.faceIds()` is already ascending would be an unchecked guess.

## 5. How the verified CUDA gradient is reused

`gradPhi` is **always** built by the production assembler — P12-DIFF-002 A2 deliberately decoupled
the Dirichlet wall-flux scheme from the `enabled` flag — so the diffusion port has a hard dependency
on a gradient, and it is exactly the one qualified in 001B:

* `greenGaussGradientDevice(plan, phi, kGreenGaussSkewCorrectionSweeps, gx, gy, gz)`, the
  bitwise-verified operator, called on the **device-resident** field;
* its output stays on the device and is consumed directly by the diffusion kernels — no download,
  no second gradient implementation;
* the diffusion plan **embeds** a `DeviceGradientPlan` rather than rebuilding any geometry, so the
  boundary encodings, skew data and claim data are shared, not duplicated.

`GradientScheme::LeastSquares` is **not** ported (001B scoped it out). A request for it is rejected
by the plan with a reason rather than silently substituting GreenGauss.

## 6. The MESH-005 two-cells-across defect — reproduced by exclusion, not by porting

TODO.md records `Two-cell diffusion() defect` as technical debt, and the brief forbids fixing it.

Its location is specific: `Diffusion.cpp`'s 4-point cubic path, via `nextInteriorFaceAwayFrom`
(`Diffusion.cpp:36`) accepting a **perpendicular** face when the mesh is only two cells across, so
the fit's far point is a transverse cell. `results/p12-mesh-005/summary.md` §28 measures it —
`laplacian(x²+y²)` on a mesh with two cells along x gives **2.1667** where the exact value is **4**,
in 2D and 3D alike; with three cells it is exact to 9.8e-15.

That code is in the **explicit** operator (§1a), which this phase does not port. So:

* the defect is **not** reproduced on the GPU, because the defective function is not ported;
* it is **not** fixed either — `Diffusion.cpp` is not touched;
* the implicit path being ported has its own boundary treatment (`boundaryInwardStencil`), which
  returns `valid = false` rather than reaching for a transverse cell when no usable inward stencil
  exists, and falls back to the documented two-point form.

The differential will include a two-cells-across mesh to show the implicit path behaves
identically on CPU and GPU there, and the evidence will state that this is **not** a fix for
MESH-005 and does not touch it.

## 7. Dimensionality and geometry assumptions

The implicit path is dimension-agnostic: it uses `face.area()`, centroids and area vectors, all of
which `DeviceMesh` already mirrors for 2D and 3D. 3D meshes are supported by the CPU assembler, so
the differential covers 3D.

Degenerate-geometry fallbacks are behaviour, not error handling, and each is reproduced:
`decomposeFaceArea` invalid → two-point form; `boundaryInwardStencil` invalid → two-point
non-orthogonal form; `decomposeBoundaryFaceArea` invalid → plain two-point.

## 8. Tolerances and existing tests

The relevant CPU tests are `DiffusionTest`, `NonOrthogonalDiffusionTest`, `GridRefinementTest`, and
the turbulence/thermal/species assembly tests. Their tolerances are **not** touched.

For the CPU/GPU differential itself the target is the same as 001B: **bitwise**. The port is
precomputation plus the same arithmetic in the same order, with a gather that reproduces the
builder's accumulation order, so there is no reason for a rounding difference. If a case is not
bitwise that is a finding to investigate, not a reason to introduce a tolerance. The brief's "use
existing project tolerances, do not weaken" is satisfied a fortiori by requiring exact equality.

## 9. Files

```text
CPU reference       src/discretization/NonOrthogonalDiffusion.cpp    face terms (the core)
                    src/turbulence/KEpsilonEquation.cpp:73           the assembly mirrored
                    src/algebra/SparseMatrix.cpp:143                 build() accumulation semantics
                    src/mesh/MeshGeometry.cpp                        decompose*/boundaryInwardStencil
out of scope        src/discretization/Diffusion.cpp                 explicit operator, MESH-005
```
