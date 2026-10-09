# P18.0 — GPU species capability inventory (§23)

```text
ANSWER TO THE QUESTION §23 ACTUALLY ASKS -- "can existing passive species run end-to-end on GPU?"

    NO. Not one step of it.

  species-path GPU references, counted:  0
      grep -rn "gpu|Gpu|Device|cuda" src/species/ include/cfd/species/   ->  0 matches

  And §23 warns specifically against inferring capability from generic scalar kernels existing.
  They do exist -- and they are reachable ONLY from turbulence. See §2.
```

---

## 1. Classification

| Functionality | Class | Evidence |
|---|---|---|
| Species field upload / residency | **MISSING** | no device buffer for a concentration field anywhere |
| Species diffusion on device | **MISSING** | no species kernel; `assembleScalarTransportDevice` is not called by species |
| Species convection on device | **MISSING** | same |
| Species source on device | **MISSING** | `applyScalarSourceDevice` exists, unused by species |
| Species boundary treatment on device | **MISSING** | no species BC encoding |
| Species linear solve on device | **MISSING** | `solveScalarSystemDevice` exists (turbulence-only) |
| Species transient on device | **MISSING** | `applyTransientDevice` exists, and species has no transient at all even on CPU |
| **Generic** scalar transport assembly on device | **GPU AVAILABLE — but turbulence-coupled** | `assembleScalarTransportDevice`, `DeviceScalarTransportSystem` |
| Shared host/device numerics for species | **MISSING** | nothing in the species path carries `__host__ __device__` |
| Mass flux on device | **GPU AVAILABLE** | `DeviceFaceFluxKernel` etc. (GPU-DISC-001H) |

## 2. What the GPU scalar stack is, and why it is not a species path

```text
include/cfd/gpu/DeviceScalarTransport.hpp exposes a genuinely capable set:

    DeviceScalarTransportSystem                the device-resident linear system
    DeviceTransportTimeState                   transient state, device side
    assembleScalarTransportDevice(...)         full assembly
    applyScalarSourceDevice(...)               Su/Sp
    applyTransientDevice(...)
    applyImplicitUnderRelaxationDevice(...)
    DeviceImposedCells / uploadImposedCellsDevice / fixImposedRowsDevice
                                               ^^^ wall-imposed k and epsilon rows

EVERY CONSUMER IS TURBULENCE:
    cuda/kernels/DeviceKEpsilonCuda.cpp:30, 144, 161
    include/cfd/gpu/DeviceKEpsilon.hpp:62, 149, 165, 179
    include/cfd/gpu/DeviceTurbulence.hpp:48
  and nothing else. There is no generic, physics-agnostic entry point that a species solve could
  call, and no CPU-side dispatcher that would route a species system to the device.

THE `DeviceImposedCells` / `fixImposedRowsDevice` PAIR IS THE TELL. Imposed rows are a k-epsilon
wall-treatment concept -- forcing a cell's value rather than solving it. Its presence in what looks
like a generic API is evidence the API was factored out OF turbulence rather than designed for
arbitrary scalars. Reusing it for species is plausible but is NOT a drop-in, and this audit does not
claim it is.
```

## 3. What this means for P18.8

```text
CPU/GPU equivalence for species is not a porting task with an existing reference -- there is no
species GPU path to compare against, exactly as P17.8 left no GPU density-based solver for P17.10.
P18.8 would be an IMPLEMENTATION phase, and it should be planned as one.

THE HONEST SEQUENCE, if P18 pursues GPU species at all:
  1. establish whether assembleScalarTransportDevice is reachable without the turbulence-specific
     imposed-row machinery -- MEASURE it, do not assume from the signature;
  2. only then decide whether species reuses it or whether a species kernel is warranted;
  3. and note that P17.10's finding applies directly: bitwise CPU/GPU equality is achievable when
     the gather order matches the CPU's accumulation order and --fmad=false is set. The species
     assembly is a face loop of the same shape, so the same argument should transfer -- but it must
     be re-established, not inherited.

WHAT WOULD MAKE A GPU SPECIES CLAIM VACUOUS, recorded now so a later phase cannot slip into it:
  claiming "GPU species" because DeviceScalarTransport exists. It exists for turbulence. The gate
  (G9) therefore requires an end-to-end species solve on device compared against the CPU, not a
  kernel-level unit test.
```

## 4. One inherited constraint worth carrying forward

```text
P17.10 measured that --fmad=false is REQUIRED for bitwise CPU/GPU agreement: with contraction
enabled, 7 of 15 equivalence tests failed and trajectory divergence grew to 704 of 1025 values by
step 50. Any future species GPU kernel inherits that requirement, and the existing
cuda/CMakeLists.txt convention already applies the flag to the twenty-odd kernels in its main block.
A new species kernel must be added to that list, not omitted from it.
```
