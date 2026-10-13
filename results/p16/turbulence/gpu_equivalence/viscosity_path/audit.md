# The viscosity path, as it stands today

Step 1 of the mu_t round-trip removal: the exact data flow, recorded before
anything is modified.

**It found a blocker that is not about the round trip at all.** The GPU momentum
assembly has no wall-viscosity support of any kind, so enabling resident
k-epsilon in `SIMPLE` today would silently drop the wall-function shear
coupling. That is a physics change, and it gates Stage 7I step 10 independently
of everything below.

---

## 1. Where each quantity lives

| quantity | representation | location |
|---|---|---|
| molecular `mu` | **a single scalar**, not a field — `fluid.dynamicViscosity()` | host, constant for the solve |
| turbulent `mu_t` | per-cell `ScalarField` | device (`DeviceKEpsilonState::turbulentViscosity`), mirrored to host by publication |
| effective `mu_eff` | per-cell `ScalarField` | host, rebuilt every outer iteration |
| wall `mu_wall` | per-**face** `SurfaceField`, zero except at wall faces | host only |

`mu_eff` is trivial: `TurbulenceModel.cpp:19-22` is

```cpp
for (Index i = 0; i < turbulent.size(); ++i) effective[i] = molecularViscosity + turbulent[i];
```

A scalar broadcast plus a per-cell add. Nothing model-specific, and deliberately
non-virtual so every RANS model shares it.

## 2. The round trip, exactly

```text
device mu_t
  --D2H (publication, 8N)-->  KEpsilonModel::turbulentViscosity_
                                       |
                    SIMPLE.cpp:548  effectiveViscosity(mu)  -> host mu_eff (8N host alloc)
                                       |
                    SIMPLE.cpp:573-575  if (!sameField(previousViscosity, *effectiveViscosity))
                                            gpuDiscretization.setViscosity(*effectiveViscosity)
                                       |
  <--H2D (8N)-------------------  GpuSimpleDiscretizationCuda.cpp:282  impl_->viscosity
                                       |
                    assembleRelaxedMomentumDevice(..., impl_->viscosity, ...)
```

`sameField()` suppresses the upload when nothing changed, which makes it free
for a laminar solve and **never** free once turbulence is active — mu_t moves
every iteration. So `8N` down and `8N` up, per outer iteration, for a field that
was already on the device.

This is the same defect shape as the wall-production round trip already removed,
one level up in the call stack.

## 3. Who consumes the host effective viscosity

| consumer | path | needs the full host field? |
|---|---|---|
| `gpuDiscretization.setViscosity()` | GPU | no — it immediately re-uploads it |
| `assembleRelaxedMomentumComponent` | **host** momentum only | yes, on the host path |
| `runNonOrthogonalCorrectionPasses` (`SIMPLE.cpp:781`) | **host** only | yes, when `nonOrthogonalCorrections > 1` |

`GpuSimpleDiscretization::assembleMomentum()` takes **no** viscosity argument —
it reads the resident `impl_->viscosity`. So on the GPU path the host field
exists only to be copied back.

There is also a genuine host consumer of full `mu_t` itself:
`validateTurbulentViscosityField(mesh, result.turbulentViscosity)`
(`KEpsilonModel.cpp:167`) walks every cell checking finite and non-negative.
Any deferral of mu_t publication must provide a device equivalent — the device
turbulence path already has this shape of check (`DeviceTurbulenceKernel.cu`'s
`invalid` flag), so it is available, not free.

## 4. THE BLOCKER — wall viscosity is host-only

At a wall face the host momentum assembly does **not** use the cell viscosity:

```cpp
// src/physics/MomentumEquation.cpp:232-235
Real muFace = effectiveViscosity[ownerId];
if (wallViscosity != nullptr && (*wallViscosity)[faceId] != 0.0) {
  muFace = (*wallViscosity)[faceId];
}
```

`wallViscosity` is the wall function's own effective viscosity at that face
(`WallTreatment.cpp:205`), zero everywhere else (`:182`) so "zero means no
override". This is how the high-Re wall function imposes `tau_w` on momentum at
all — it is not a correction, it is the mechanism.

```text
grep -rn "wallViscosity" cuda/ include/cfd/gpu/     ->  no matches
```

**The device momentum assembly has no concept of it.** It is reachable today
only because `SIMPLE.cpp:459` gates the resident path on
`activeModel->name() == "laminar"`, and a laminar model returns `nullptr` for
`wallViscosity()`. So there is no current defect — but there is a hard
prerequisite:

> Enabling resident k-epsilon in `SIMPLE` without porting the wall-viscosity
> override would assemble momentum with the interior cell viscosity at wall
> faces, discarding the wall function's shear entirely. Every wall-bounded
> turbulent result would change.

This is precisely the case Stage 7I step 4 anticipated — *"if wall treatment
modifies only sparse wall entries, apply those results sparsely on device"*. It
does: the field is face-length but has `O(N_wall)` non-zero entries, and it is
consumed only in the **boundary-face branch** of the diffusion assembly.

### What porting it needs

`assembleRelaxedMomentumDevice` already takes `effectiveViscosity` as a runtime
buffer rather than baking it into the plan, and the plan's boundary arrays
(`bDistance_`, `bCP_`, `bCF_`, `bCB_`, ...) are pure geometry. So the override
is additive: a sparse `(faceId, value)` pair list, and one branch in the
boundary-face path of the kernel — structurally the same as the
`DeviceImposedCells` pattern already qualified for imposed epsilon rows.

It is a new device capability, not a data-location change, and it is **not**
covered by the authorization for removing the mu_t round trip.

## 5. Ordering the CPU proves

```text
correct()                     -> mu_t (and, with wall functions, wallViscosity_)
effectiveViscosity(mu)        -> mu_eff = mu + mu_t          [cells]
assemble momentum             -> muFace = mu_eff[owner], OVERRIDDEN at wall faces
```

The wall override is applied **at assembly**, per face, not folded into the cell
field beforehand. So the target device ordering is:

```text
GPU mu_t  ->  GPU mu_eff = mu + mu_t  (cells)  ->  momentum assembly,
                                                   which applies the sparse
                                                   wall-face override itself
```

and **not** "apply wall results into the cell field then assemble", which would
change which faces see the override and is not what the CPU does.

## 6. Existing ownership mechanism to reuse

`GpuSimpleDiscretization` already tracks per-field authority
(`viscosityAuthority`, `FieldAuthority::Synchronized`), so the handoff has an
established vocabulary rather than needing a new one: a device-produced
effective viscosity sets the viscosity authority to device-owned and suppresses
both the download and the upload.

---

## Conclusion

Two separable pieces, and only the first is authorized here:

1. **`mu_eff` on device** — a scalar-plus-field op and an authority change.
   Removes `8N` D2H and `8N` H2D per outer iteration. Self-contained.
2. **wall-face viscosity override on device** — a new capability the GPU
   momentum assembly does not have. Required before resident k-epsilon may be
   enabled in `SIMPLE`, independently of (1).

Doing (1) without (2) is safe and useful: it removes the round trip while the
laminar guard still prevents the unsupported combination from ever running.
Doing step 10 without (2) would be a silent physics change.
