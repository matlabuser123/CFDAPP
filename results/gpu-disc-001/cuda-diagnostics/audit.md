# GPU-DISC-001O Phase A — diagnostic coverage audit

Written before the sanitizer runs. Its purpose is to prove coverage is **non-vacuous** — that each
tool has something real to examine — and it turns up one finding that changes how this gate has to
be run.

## 1. The finding: two of the four tools were structurally vacuous in every previous gate

```text
__shared__       only cuda/kernels/DeviceVectorOpsKernel.cu
__syncthreads    only cuda/kernels/DeviceVectorOpsKernel.cu
atomics          NONE anywhere
streams          NONE (default stream throughout)
warp intrinsics  NONE
```

Every GPU-DISC discretization kernel is a per-cell or per-face **gather**. That was a deliberate
design decision from GPU-DISC-001F onward — "every write to row P comes from a face incident to P,
so a per-row gather reproduces the accumulation order with no atomics" — and it is why there is no
atomic, no shared-memory workspace and no barrier anywhere in the discretization path.

The consequence matters:

* **racecheck** is a *shared-memory* data-access hazard detector.
* **synccheck** detects barrier and warp-synchronization misuse.

A kernel with neither shared memory nor barriers gives both tools **nothing to report**. So the
`racecheck: 0 hazards` and `synccheck: 0 errors` lines in GPU-DISC-001B…001N were true but
*structurally vacuous for those two tools* — every one of those harnesses ran CPU linear solvers,
so no kernel under test contained a barrier or a shared array. They were genuinely non-vacuous for
**memcheck** and **initcheck**, which examine global memory traffic that those kernels do plenty
of.

This is exactly why the checkbox deserved its own audit instead of being ticked from the union of
per-gate runs, and it dictates the workload design in §4: **the diagnostic runs must also exercise
the GPU linear solver**, whose reduction kernels are the only code in the whole GPU path with
shared memory and barriers.

## 2. Production CUDA stages, kernels and buffers

| stage | file | kernels | persistent buffers | temporaries | 2D/3D-specific |
| --- | --- | --- | --- | --- | --- |
| device mesh / geometry | `DeviceMeshCuda.cpp` | — (upload only) | owner, neighbour, area x/y/z, face area, centroids, volumes, CSR connectivity | — | area z is zero in 2D |
| gradients (Green–Gauss) | `DeviceGradientKernel.cu` | 6 | face dPf/dNf, boundary a/b/kind, skew arrays, oblique arrays, claim arrays, slot-claim | plan-owned `faceValues_`, `claimValues_` | **gradZ written but unused in 2D** |
| least-squares gradient | `DeviceLeastSquaresGradientKernel.cu` | 1 | entry offsets/kind/neighbour/a/b/encoding, wd x/y/z, cellThreeD, cellConditioned, c11..c33, det | — | **packed 2D layout: c11=Syy, c12=Sxy, c22=Sxx**; 3D uses all six cofactors |
| diffusion | `DeviceDiffusionKernel.cu` | 1 | geometry + boundary encodings | — | — |
| convection (scalar) | `DeviceConvectionKernel.cu` | 3 | plan arrays | deferred-correction face values | — |
| convection (momentum) | `DeviceMomentumConvectionKernel.cu` | 4 | plan arrays, vector BC encodings | face values | vector BC z component |
| boundary conditions | `DeviceBoundaryConditionsKernel.cu` | 1 | scalar/vector encodings | — | 11 BC type ids |
| momentum assembly | `DeviceMomentumAssemblyKernel.cu` | 1 | CSR row/col, sorted faces | values, rhs, diagonal | W component |
| momentum response | `DeviceMomentumResponseKernel.cu` | 1 | — | response buffer | dW only in 3D |
| predicted face flux | `DeviceFaceFluxKernel.cu` | 3 | plan arrays | flux, correction | dW pointer null in 2D |
| pressure-correction assembly | `DevicePressureCorrectionKernel.cu` | 2 | d x/y/z, distance, axis flags, isFixedValue, cell-face CSR, candidate CSR | values, rhs, faceCoefficient, explicitFaceFlux | areaZIsZero flag |
| pressure update | (host loop in `SIMPLE.cpp`) | — | — | — | — |
| velocity correction | `DeviceVelocityCorrectionKernel.cu` | 1 | both gradient plans | gradient x/y/z, corrected x/y/z | **corrected z forced to +0.0 in 2D** |
| face-flux correction | `DeviceFaceFluxCorrectionKernel.cu` | 1 | — (reuses 001I's coefficients) | corrected flux | — |
| SpMV | `CsrSpmvKernel.cu` | 1 | device CSR | — | — |
| vector ops / **reductions** | `DeviceVectorOpsKernel.cu` | 5 | — | partial-sum buffers | **`extern __shared__`, `__syncthreads`** |
| preconditioner | `GpuPreconditionerKernel.cu` | 1 | inverse diagonal | — | — |
| residual / continuity | (host, `evaluateContinuity`) | — | — | — | — |

**15 `.cu` files, 32 `__global__` kernels.** Twelve carry `-fmad=false`; those are exactly the
discretization kernels whose bitwise equality depends on it.

### Synchronization and transfers

* **Streams:** none — everything is issued on the default stream, so ordering between kernels is
  guaranteed by the stream itself and needs no explicit barrier.
* **Explicit `cudaDeviceSynchronize`:** only inside the reduction path, immediately before the
  blocking D2H the host cannot proceed without. `gpuExecutionStats().synchronizations` measured
  **0** across all GPU-DISC-001N full solves, because those used CPU solvers.
* **Transfers:** measured per solve and per iteration in GPU-DISC-001N §9 — 12–25 H2D and 16–20 D2H
  per outer iteration, all of them the forced round-trips the host `LinearSolver` interface
  imposes.

## 3. CUDA API error-checking policy

The repository has one: `checkCuda(status, what)` (`include/cfd/gpu/CudaCheck.hpp`) throws
`cfd::NumericalError` naming the failing operation. Verified below in §5 of `summary.md` that:

* every kernel launch is followed by `checkCuda(cudaGetLastError(), ...)` via each file's
  `recordLaunch`;
* `DeviceBuffer` allocation and both copy directions check;
* `GpuSimpleDiscretization` now also validates that required device state is resident before a
  stage consumes it (`requireResident`, added in GPU-DISC-001N after its negative controls found a
  skipped upload failing as an illegal memory access rather than an explicit error).

**No blanket synchronization is added for the diagnostics**, and no numerical flag is changed:
§1 of `environment.txt` records `-fmad=false` still on all twelve kernels and `-O3 -DNDEBUG`
unchanged, so the diagnostic build is the production build.

## 4. Workload matrix — designed so each tool has something to find

| workload | backend | purpose | which tools it makes non-vacuous |
| --- | --- | --- | --- |
| 2D closed cavity, multi-iteration | CPU solver, GPU discretization | reference pin, walls + moving wall, U/V | memcheck, initcheck |
| 2D inlet/outlet, multi-iteration | CPU solver, GPU discretization | open pressure boundary, pin suppressed, through-flow | memcheck, initcheck |
| 3D cavity + 3D channel | CPU solver, GPU discretization | U/V/W, 3D gradients, 3D cofactor path, 3D BC storage, 3D flux | memcheck, initcheck |
| QUICK / LinearUpwind / Central | CPU solver, GPU discretization | the deferred-correction convection paths | memcheck, initcheck |
| warped (non-orthogonal) mesh | CPU solver, GPU discretization | the non-orthogonal pressure-coupling branch, oblique-Neumann, skew sweeps | memcheck, initcheck |
| **2D cavity, GPU LINEAR SOLVER** | **GPU solver + GPU discretization** | **the reduction kernels — the only shared memory and barriers in the codebase** | **synccheck, racecheck** (and memcheck, initcheck) |

The last row is what §1 shows is required. Without it this gate would repeat the earlier gates'
structurally vacuous synccheck/racecheck result.

### Known sensitive paths the workload must execute

* **2D `gradW` absence** — the 001D initcheck incident: 2D meshes allocated `gradW` buffers no
  kernel wrote, then downloaded them. Covered by every 2D case under initcheck.
* **2D packed least-squares layout** (`c11=Syy, c12=Sxy, c22=Sxx`) — reached through the velocity
  correction when `gradientScheme = LeastSquares`.
* **3D cofactor/adjugate path** — the 3D cases.
* **Conditional pressure reference pin** — the cavity pins, the inlet/outlet suppresses it.
* **Non-orthogonal pressure-correction coefficients** — the warped mesh.
* **Vector BC lookup** — Wall / MovingWall / Inlet / Outlet / Symmetry across the cases.
* **Face-flux owner/neighbour orientation** — every case.

## 5. What each gate can and cannot conclude

Stated plainly so the result is not over-read:

* **memcheck** and **initcheck** examine global-memory traffic and are meaningful for every stage
  in §2. A clean result is a real result.
* **synccheck** and **racecheck** are meaningful **only** where barriers and shared memory exist,
  i.e. the reduction kernels. A clean result on a CPU-solver workload says nothing; a clean result
  on the GPU-solver workload says the reductions are sound.
* Neither racecheck nor "the numbers match" can substitute for the other. GPU-DISC-001N's bitwise
  agreement does not prove the absence of a race, and this gate does not treat it as if it did.
