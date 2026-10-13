# P16 GPU equivalence -- Phase A audit

Read-only. No production code, test, build file, case file or kernel was
changed while this was written.

---

## Headline

**There is no GPU turbulence implementation at all.** This checkbox is not a
validation pass over existing CUDA turbulence code -- it is a port that has not
started. Three independent confirmations below, none of which relies on
counting filenames.

**And there is no GPU thermal path either**, so the turbulent heat transfer
validated earlier in P16 (`k_t`, `k_eff`, the `T+` wall law, `q_w`) has no CUDA
counterpart to be equivalent to.

---

## 1. What actually executes on device

### 1a. The GPU-resident SIMPLE loop REFUSES every turbulence model

`src/pressure_velocity/SIMPLE.cpp:458`:

```cpp
const bool useResidentSimpleLoop =
    useResidentPressureSolve && activeModel->name() == "laminar" &&
    settings_.momentumSolver.backend == cfd::algebra::LinearSolverBackend::GPU &&
    settings_.momentumSolver.type == cfd::algebra::LinearSolverType::BiCGSTAB;
```

`activeModel->name() == "laminar"` is a hard precondition, and the code states
its own reason (`SIMPLE.cpp:440-447`): a resident loop deliberately leaves the
host velocity stale, `LaminarModel::correct()` is a documented no-op so that is
harmless, but *"A transport model genuinely reads it, so that configuration
keeps the per-iteration download it has today."*

### 1b. `activeModel->correct()` is unconditionally a HOST call

`SIMPLE.cpp:547`. Every turbulence transport equation -- k, epsilon, omega --
plus F1/F2, cross diffusion, the eddy-viscosity limiter and the entire wall
treatment runs on CPU in **every** configuration, GPU backend or not.

### 1c. No turbulence kernel exists

18 `.cu` files under `cuda/kernels/`:

```
CsrSpmv                  DeviceFaceFlux            DeviceMomentumResponse
DeviceBoundaryConditions DeviceFaceFluxCorrection  DevicePersistentFields
DeviceConvection         DeviceGradient            DevicePressureCorrection
DeviceDiffusion          DeviceLeastSquaresGradient DeviceScalarTransport
DeviceMomentumAssembly   DeviceMomentumConvection  DeviceVectorOps
DeviceVelocityCorrection GpuPreconditioner         GpuResidentSolve
```

None is a turbulence model. The `cfd::gpu` headers that mention "omega" or
"turbulen" (`BoundaryEncoding`, `DeviceBoundaryConditions`, `DeviceDiffusion`,
`DeviceConvection`, `DeviceGradient`, `GpuSimpleDiscretization`) do so for
boundary encoding and the effective-viscosity input, not for model equations.

### 1d. Production consumers of `cfd::gpu`

Only `src/algebra/LinearSolverFactory.cpp` and
`src/pressure_velocity/SIMPLE.cpp`. Nothing in `src/turbulence/` or
`src/thermal/` touches the GPU module at all.

---

## 2. Capability matrix -- what is CPU-only today

| operation | device today | note |
|---|---|---|
| **k-epsilon** | | |
| k assembly | CPU only | via `solveRelaxedScalarTransport` -> `cfd::transport` |
| epsilon assembly | CPU only | same |
| source terms Su/Sp | CPU only | `applyImplicitScalarSource` |
| P_k | CPU only | `computeTurbulentProduction` |
| strain rate / gradients | **kernel exists** | `DeviceGradient`, `DeviceLeastSquaresGradient` -- for momentum, not wired to turbulence |
| mu_t = rho C_mu k^2/eps | CPU only | `computeTurbulentViscosityField` |
| floors | CPU only | inside the wrapper |
| **imposed wall cells** | **CPU only, NO device equivalent** | `SparseMatrixBuilder::fixRow`; `DeviceScalarTransport` has no counterpart |
| **wall treatment** | | |
| wall-face discovery | CPU only | `collectWallFaces` |
| wall shear / u_tau / y+ | CPU only | `WallFunctions` |
| epsilon wall value, P_k replacement | CPU only | `WallTreatment` |
| wall viscosity -> momentum | CPU only produced; **consumed by a device kernel** | `SurfaceField` uploaded as part of the momentum effective viscosity |
| **eddy-viscosity use** | | |
| momentum diffusion with mu_eff | **GPU capable** | `DeviceDiffusion` / `DeviceMomentumAssembly` |
| SIMPLE coupling | **GPU capable, but excluded for turbulence** | see 1a |
| **thermal turbulence** | | |
| k_t = mu_t cp/Pr_t | CPU only | **no GPU thermal path exists** |
| thermal wall function | CPU only | same |
| wallHeatFlux | CPU only | same |

### The two structural gaps

1. **`DeviceScalarTransport` has zero production consumers and no imposed-cell
   or under-relaxation support.** All six CPU turbulence equations now depend
   on imposed cells for the wall treatment (P16) and on Patankar relaxation, so
   a faithful port is blocked on both.
2. **`include/cfd/gpu/` contains no thermal or energy header.** There is no
   `DeviceEnergyEquation`, no device thermal solver, nothing.

---

## 3. THE SCOPE QUESTION -- settled from history, not from preference

Phase A requires the interpretation of "GPU equivalence" to be frozen from
existing roadmap/docs/history before any coding. It was.

### The precedent: P14's `[x] CPU/GPU equivalence`

`TODO.md:433`, and its own explanation at `TODO.md:436-455`:

> *"on the GPU, the two hard halves were already qualified ports
> (`assembleScalarDiffusionDevice`, `assembleScalarConvectionDevice`) of the
> exact functions the host composes"*
>
> *"So P14 is a PROMOTION, not a construction"*

P14 checked its GPU box on the basis that **device kernels mirroring the host
operators exist and are qualified** -- NOT that production ran on device.
`cfd::transport` had zero GPU production consumers when that box was checked,
and still does.

### What the other checked GPU items covered

`CUDA-QUAL-001` (toolchain), `GPU-DISC-001` (discretization pipeline),
`GPU-PIPE-001` (residency). All three are **flow-side**: momentum, pressure
correction, face flux, velocity correction, linear solve. None covers a
turbulence model, and none covers thermal.

### Frozen interpretation

**Interpretation A -- turbulence/flow CUDA equivalence only.**

The historical precedent is unambiguous: every checked GPU item in this
repository is flow/solver-side, and the one that touched transport
(`P14`) was satisfied by kernel-level equivalence with the host operator, not
by device-resident production. Nothing in the roadmap has ever scoped a GPU
thermal solver.

So P16's `[ ] GPU equivalence` is frozen as:

```
P16 GPU equivalence covers turbulence/flow quantities.
ThermalSolver remains CPU-only.
Turbulent thermal transport and the thermal wall function therefore
remain CPU-only and are not claimed as CUDA-equivalent.
```

This is a **documented narrowing of an unstated scope, not a narrowing of a
stated one.** The checkbox never said "including thermal"; the surrounding
history says the opposite. Recorded here BEFORE any result was produced, per
Phase B, so it cannot be re-read afterwards.

**If a reader disagrees with this reading**, the honest statement is the one in
section 1: there is no GPU thermal solver, and building one is a real blocking
capability -- not something to be waved through as PASS.

---

## 4. What Phases C-H actually require, given the above

Phase C ("matrix/RHS equivalence first"), Phase E (k-epsilon), Phase F (wall
functions) and Phase H (benchmark) all presuppose device implementations that
**do not exist**. The stated order

```
audit -> identify CPU-only -> port k-epsilon -> matrix/RHS equivalence -> ...
```

is right, but the weight sits almost entirely in "port", which is a
milestone-sized construction:

* a device k and epsilon assembly (reusing `DeviceScalarTransport`,
  `DeviceDiffusion`, `DeviceConvection`);
* **imposed-cell support on the device** -- the P16 wall treatment cannot be
  ported without it;
* device Patankar under-relaxation;
* device production `P_k` and strain rate;
* a device `mu_t` update with floors;
* a device wall treatment (`u_tau`, `y+`, `tau_w`, `eps_P`, `P_k`, wall
  viscosity);
* lifting the `name() == "laminar"` residency precondition without breaking
  the stale-velocity invariant it protects.

## 5. Phase G is not optional here -- it is the central risk

Today **every** turbulent case silently runs its turbulence on CPU. A naive
CPU-vs-GPU equivalence test written now would pass perfectly and prove
nothing, because both arms execute the identical host code. Any equivalence
harness built for this checkbox must therefore ship with its
execution-evidence gate and fallback-detection control FIRST, not last.

---

## 6. Status of this phase

| phase | status |
|---|---|
| A -- audit | **COMPLETE** |
| B -- freeze scope | **COMPLETE** (section 3, Interpretation A) |
| C-M | **BLOCKED on the port described in section 4** |
