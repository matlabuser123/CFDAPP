# GPU-DISC-001A — device mesh / geometry foundation

**Status: PASS.**

## What was built

`include/cfd/gpu/DeviceMesh.hpp` + `cuda/kernels/DeviceMeshCuda.cpp` — the **immutable** half of the
mesh, mirrored once into persistent device memory and separated from mutable solution fields.

Layout decisions, and why:

* **Structure-of-arrays**, one flat `DeviceBuffer` per attribute. Every kernel that reads a centroid
  or an area vector reads all three components across a contiguous range of ids, so SoA gives
  coalesced access; `cfd::Vector2` is an alias for `Vector3` (24 bytes), so an array-of-structs
  layout would also straddle cache lines badly.
* **CSR connectivity** for cell→faces, the same shape `DeviceCsrMatrix` already uses:
  `cellFaceIds[cellFaceOffsets[c] .. cellFaceOffsets[c+1])`.
* **A sentinel for "no neighbour"** (`kNoNeighbor = Index(-1)`) instead of an optional, so kernels
  branch on a value rather than consulting a side table. `Index` is `std::size_t`, so the sentinel
  can never collide with a real cell id.
* **`faceArea()` precomputed** from `Face::area()` on the host — the one derived quantity. It is
  copied, not recomputed, so the device sees exactly the magnitude the CPU operators use.
* **Boundary patches as slices** of one flat face-id array, in mesh order, so a host
  `BoundaryConditionSet` entry maps to its device slice by name.

## Gate: bitwise equality against the CPU mesh

Geometry is copied, not recomputed, so the requirement is **bitwise** equality — any difference
would be a layout or indexing defect, not rounding.

| mesh | cells | faces | attributes compared | result |
| --- | ---: | ---: | --- | --- |
| Cartesian 2D 8×8 | 64 | 144 | all 16 arrays + patches | **bitwise** |
| Cartesian 2D 40×40 | 1 600 | 3 280 | all 16 arrays + patches | **bitwise** |
| Cartesian 2D non-square 32×8 | 256 | 552 | all 16 arrays + patches | **bitwise** |
| Cartesian 3D 6×6×6 | 216 | 756 | all 16 arrays + patches | **bitwise** |
| Cartesian 3D 12×8×4 | 384 | 1 328 | all 16 arrays + patches | **bitwise** |

Compared per mesh: cell volumes; cell centroid x/y/z; face owner; face neighbour (including the
boundary sentinel); face centroid x/y/z; face area vector x/y/z; face area magnitude; cell-face
offsets; cell-face ids; boundary face ids; and every patch's name, slice offset and length.

```text
checks: 96   failures: 0
DEVICE MESH: PASS
```

**Non-vacuity proven.** The comparison is required to detect a deliberately injected **one-ulp**
difference, and does. Without that, "bitwise equal" would be an untested claim.

## Persistence

`allocations` is **16 per mesh upload** — one per buffer — and **re-uploading the same mesh
performs 0 reallocations**, verified by `GPUExecutionStats::reallocations` in the probe. Immutable
geometry is therefore uploaded once and reused, which is the point of separating it from the
solution fields.

Resident bytes (reported by `DeviceMesh::residentBytes()`, computed from the buffers, not
estimated):

```text
2D   8x8      14.9 kB        3D   6x6x6      73.4 kB
2D  40x40    344.4 kB        3D  12x8x4     129.1 kB
2D  32x8      57.4 kB
```

## CUDA diagnostics

```text
memcheck    errors=0
initcheck   errors=0
synccheck   errors=0
racecheck   errors=0
```

## Scope note

Mesh kinds exercised here are Cartesian 2D and 3D, which is what `MeshGeometry` exposes as direct
factories. Stretched, non-orthogonal, skewed and multi-block meshes are the *same* `Mesh` type with
different cell/face geometry values — `DeviceMesh` copies whatever those values are and is agnostic
to how they were produced — but they are **not separately exercised here**, and that is stated
rather than implied. They will be covered when an operator that is sensitive to them (gradients,
001B) is verified against them.

Derived geometry that the CPU computes on demand — `unitNormal`, `ownerNeighborDistance`,
`decomposeFaceArea`, `skewness`, `ownerNeighborCrossing`, `boundaryInwardStencil` — is **not**
mirrored. Those are functions of the primitives above, and each will be ported with the operator
that needs it rather than speculatively.
