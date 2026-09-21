# GPU-DISC-001M Phase A — production dispatch audit

Written before any production change.

## 1. What production selects today

| decision | where | what it actually controls |
| --- | --- | --- |
| momentum linear solver backend | `SIMPLESettings::momentumSolver.backend` → `makeLinearSolverWithFallback` → `makeLinearSolver` (`LinearSolverFactory.hpp`) | **the linear solve only** |
| pressure linear solver backend | `SIMPLESettings::pressureSolver.backend`, same path | **the linear solve only** |
| GPU residency mirroring | `SIMPLESettings::enableGpuResidency` → `GpuResidencyManager::syncMatrix/syncField` (`SIMPLE.cpp:361`, `:545`) | **nothing numerical** — the source comments say it is "Read-only w.r.t. everything below -- never influences uResult/vResult" |
| assembly path | *not selectable* | always CPU |

`LinearSolverSettings::backend == GPU` is documented as a **request**: a CPU-only binary, or a CUDA
binary with no usable device, falls back transparently, logs once at Warning, and increments
`gpuExecutionStats().gpuBackendFallbacks`. `SolverResult::backendUsed` reports what actually ran.

## 2. Where production falls back to CPU discretization — the honest answer

**Everywhere.** Before this gate, *no* discretization stage has a GPU path in production:

```text
stage                                   production today
--------------------------------------  --------------------------------
momentum assembly        SIMPLE.cpp:319  CPU  assembleRelaxedMomentumComponent
momentum solve                     :385  CPU or GPU   (solver backend)
response coefficients              :474  CPU  computeMomentumResponseCoefficient
predicted face flux                :484  CPU  rhieChowMassFlux / calculateMassFlux
  its pressure gradient            :487  CPU  discretization::gradient
pressure-correction assembly       :531  CPU  assemblePressureCorrection
pressure-correction solve          :550  CPU or GPU   (solver backend)
pressure update                    :615  CPU  (a per-cell loop, not an operator)
velocity correction                :620  CPU  correctVelocity
face-flux correction               :623  CPU  correctFaceMassFlux
residual / continuity              :638  CPU  evaluateContinuity, rms
```

So a "GPU solve" today is exactly that — two linear solves on the device, with every operator that
*builds* those systems running on the host. Proving that production now uses CUDA **discretization**
rather than merely CUDA **linear algebra** is the whole point of this gate, and the transfer audit
in §5 is how it is proved rather than asserted.

## 3. Field ownership, transfers and synchronization today

Fields are owned by `SIMPLE::solve` as host `VectorField` / `ScalarField` / `SurfaceField`. The only
device traffic in a default solve is:

* the linear solvers' own uploads/downloads, when the backend is GPU;
* `GpuResidencyManager`'s mirroring, when `enableGpuResidency` is set — additional H2D with **no**
  D2H, since nothing reads it back.

There is no explicit `cudaDeviceSynchronize` in `SIMPLE::solve`; synchronization happens inside the
solver and inside `DeviceBuffer::downloadTo`. `gpuExecutionStats()` already counts
`hostToDeviceCalls/Bytes`, `deviceToHostCalls/Bytes`, `allocations`, `kernelLaunches` and
`synchronizations`, so the audit needs no new instrumentation.

## 4. The integration design

### The established pattern, followed exactly

`GpuResidencyManager` and `GPUBackend` show the codebase's own split for something `cfdcore` must
call but only `cfdcuda` can implement:

```text
include/cfd/gpu/X.hpp        CPU-includable facade, PIMPL, every method a safe no-op when inactive
src/gpu/X.cpp                the stub -- compiled ONLY when NOT CFDAPP_ENABLE_CUDA
cuda/kernels/XCuda.cpp       the real implementation -- part of cfdcuda
```

`GpuSimpleDiscretization` follows it. Nothing else would let `SIMPLE.cpp` (in `cfdcore`) call device
operators while keeping the CPU-only build compiling.

### The switch

```text
SIMPLESettings::enableGpuDiscretization   (new, default false)
```

A **request**, with the same semantics as `LinearSolverSettings::backend`: honoured when the binary
has CUDA, a device is usable, and every operator can reproduce this configuration; otherwise a
recorded fallback.

### All-or-nothing, never a mixed path

`prepare()` builds every device plan up front. If **any** of them reports it cannot reproduce the
configuration bitwise — an unsupported boundary condition, say — the whole solve falls back to the
CPU discretization path and records why. A partially-GPU iteration is never run. This matters
because every one of these operators was qualified as *bitwise* equal; silently substituting one CPU
stage into a GPU chain would produce a path that no gate has ever verified.

The decision is observable, not just logged:

```text
SIMPLEResult::gpuDiscretization                bool   -- did the discretization run on device
SIMPLEResult::gpuDiscretizationFallbackReason  string -- empty unless it fell back
```

### What stays on the host inside one iteration, and why

| work | why it stays on the host |
| --- | --- |
| the two linear solves | `LinearSolver` is a host interface taking a host `LinearSystem`; the GPU solver is qualified separately (GPU-PCORR-001) and is selected by its own setting |
| the assembled systems' D2H, and the solutions' H2D | forced by the above — the solver cannot take a device CSR |
| the pressure update `p + alpha*p'` | a per-cell loop in `SIMPLE.cpp`, not an operator; no kernel exists and inventing one is out of scope |
| residuals, continuity, the convergence verdict | `evaluateContinuity`, `rms`, `OuterIterationMonitor` — CPU bookkeeping this gate does not touch |
| the turbulence model | `activeModel->correct` is not part of the discretization chain this gate covers |

These are **documented, not hidden**: §5's measurement reports them, and the brief's own instruction
is not to claim GPU-PIPE-001 persistent residency while host ownership remains.

## 5. How "production really uses CUDA discretization" is proved

Three independent ways, none of which is "the numbers match":

1. **Dispatch is observable.** `SIMPLEResult::gpuDiscretization` says which path ran, and the tests
   assert it is `true` for a GPU-configured solve and `false` for a CPU one.
2. **The transfer counters move.** A GPU-discretization iteration must show a materially higher
   `hostToDeviceCalls` / `kernelLaunches` than the same case with the flag off, because the
   operators now run on the device. Measured before and after, per iteration, and recorded.
3. **Forcing any single stage back to the CPU is detected.** The negative controls do exactly that,
   one stage at a time.

## 6. Stage order is preserved exactly

The integrated path executes the order audited in GPU-DISC-001L (`../single-iteration/audit.md` §2)
unchanged. In particular the warm-started momentum solves, the zero-guess pressure solve, the
initial-residual bookkeeping, `velocityStar` (not `velocity`) into the velocity correction, and
`predictorFlux` (not `massFlux`) into the flux correction all stay as they are. This gate moves
*where* the arithmetic runs, never *what* it is or *when*.
