# Phase 2 — the sparse wall-face viscosity override on GPU

The capability that was blocking resident turbulent SIMPLE. Without it, widening
the laminar guard would have assembled momentum with the interior cell viscosity
at wall faces and discarded the wall-function shear coupling — silently, while
still converging to a plausible answer.

---

## 1. The reference, and why it is face-local

```cpp
// src/physics/MomentumEquation.cpp:232-235, boundary-face branch
Real muFace = effectiveViscosity[ownerId];
if (wallViscosity != nullptr && (*wallViscosity)[faceId] != 0.0) {
  muFace = (*wallViscosity)[faceId];
}
```

The obvious shortcut — write the wall viscosity into the owner cell's `mu_eff`
and let the ordinary path pick it up — is wrong, and wrong in a way that a
field-norm comparison on a wall-bounded case would not reliably catch. A
wall-adjacent cell owns one wall face and several interior faces:

```text
cell P:    [wall face]  [interior]  [interior]  [interior]
correct:     mu_wall      mu_eff      mu_eff      mu_eff
folded:      mu_wall      mu_wall     mu_wall     mu_wall
```

That is a different finite-volume operator, not a different spelling of the same
one. So the override stays on the face.

## 2. Implementation

One optional parameter on the already-shared boundary term routine:

```cpp
// include/cfd/gpu/DeviceDiffusionTerms.hpp
cfd::Real gammaFace = gamma[o];
if (wallGamma != nullptr && wallGamma[f] != 0.0) gammaFace = wallGamma[f];
```

`wallGamma` defaults to `nullptr`, so the scalar-transport caller
(`DeviceDiffusionKernel.cu`) passes nothing and is bitwise unchanged. `nullptr`
and a stored `0.0` both mean "no override" — the same two escapes the host has.

The override travels as a `MomentumAssemblyView` member, so `diffusionPass` did
not need another parameter threaded through it, and
`assembleRelaxedMomentumDevice` gained one optional
`const DeviceBuffer<Real>* wallFaceViscosity` alongside the existing optional
`momentumSource`.

### Sparse in transfer, dense in storage

`GpuSimpleDiscretization::setWallFaceViscosity(faceIds, values)` takes
**compact** lists — one entry per wall face, `O(N_wall)` H2D, never a
face-length field. They are zero-filled and then scattered into a face-length
device buffer using `reassertImposedValuesDevice`, the already-qualified
"write these values at these indices" primitive rather than a second copy of it.

Dense storage is deliberate: it gives the kernel an `O(1)` lookup with exactly
the host's `!= 0.0` semantics, while the *transfer* — the thing that costs — stays
`O(N_wall)`.

Zero-filling before each scatter matters: a face that stops carrying an override
must revert to "no override" rather than keep the previous iteration's value.

`clearWallFaceViscosity()` empties the buffer, and the assembly then passes
`nullptr` and does not read it at all — which is what keeps a laminar assembly
byte-identical to what it was.

## 3. Gates

| test | asserts |
|---|---|
| `NoOverrideReproducesTheHostAssemblyBitwise` | with no override, the device system matches the host CSR **structure and values** exactly — the laminar baseline is untouched |
| `OverrideReproducesTheHostAssemblyBitwise` | every boundary face overridden, values far from any cell viscosity so a wrong lookup cannot land on the right number |
| `WallValueDoesNotLeakToInteriorFaces` | **one** face overridden with a value ~1000× any cell viscosity; matches the host, which applies it to that face alone |
| `TurningTheOverrideOffMeasurablyChangesMomentum` | disabling it moves real coefficients — a wiring check or transfer counter could pass while the assembly ignored the buffer |
| `ClearingTheOverrideRestoresTheLaminarAssemblyExactly` | set, assemble, clear, assemble → byte-identical to the baseline, not merely close |

Comparison is over the **raw CSR arrays** (`rowOffsets`, `columnIndices`,
`values`, `rhs`), so an assembly that produced the right numbers in the wrong
sparsity pattern cannot pass.

### The leak test carries its own vacuity guard

Two things could make it pass for the wrong reason, and both are checked:

* The chosen wall cell must actually have interior faces —
  `ASSERT_GT(interiorFacesOnOwner, 0)`, or a leak would be invisible.
* Folding the same value into the owner **cell** must produce a *different* row
  from applying it to the wall face. If those agreed, the mesh could not
  distinguish the two operators and the test would prove nothing.

## 4. Suites

```text
GPU            174/174     (169 before, +5)
SIMPLE         123/123     CUDA and CUDA-OFF
turbulence     246/246     CUDA and CUDA-OFF
CPU-only build clean
```

No production result changed. The resident path is still laminar-gated, nothing
calls `setWallFaceViscosity` yet, and with an empty buffer the assembly is
bitwise what it always was.

---

## 5. What this unblocks, and what it does not

The capability gap named in [audit.md](audit.md) §4 is closed: GPU momentum can
now consume a wall-function face viscosity with the host's exact semantics.

It does **not** widen the resident guard, and must not be read as making
turbulent resident SIMPLE available. That step additionally needs the wall
treatment's `wallViscosity` actually routed into `setWallFaceViscosity` inside
the resident loop, a capability query that refuses anything but laminar and
qualified k-epsilon, and the stale-state controls — none of which exist yet.
