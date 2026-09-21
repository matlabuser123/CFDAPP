# GPU-DISC-001M — integrated GPU SIMPLE discretization

**Result: PASS.** Production `SIMPLE::solve` now runs its discretization on the device, and the
committed state is **bitwise identical** to the CPU path — 12,708 values across 26 cases,
`maxAbs = 0`, including a five-outer-iteration run.

```text
equivalence cases           6   one production iteration, GPU discretization ON vs OFF
                                (+1 five-iteration case in the full run)
dispatch checks             6
transfer measurements       6
determinism checks          6
documented fallback         1
values compared        12,708
bitwise-identical      12,708   (100%)

observable controls     10/10 detected
documented null             1   (carried forward from 001L, with its proof)
compute-sanitizer         4/4 clean, non-vacuous, ON THE PRODUCTION PATH
GPU-DISC gates          14/14 green
full regression    1998/1998 passed, 0 failed, 1350.5 s (45 disabled of 2043 registered)
                   `ninja: no work to do` BEFORE and AFTER ctest; library and
                   source sha256 identical on both sides
```

This is the first gate that changes production code. Full-solve equivalence and GPU-PIPE-001
residency were **not** started, and no GPU-PIPE-001 residency item is marked.

## 1. Entry point and dispatch

`SIMPLE::solve` (`SIMPLE.cpp:127`), outer loop at `:271`. The new switch:

```text
SIMPLESettings::enableGpuDiscretization   (default false)
```

A **request**, with exactly the semantics `LinearSolverSettings::backend` already has. Honoured when
the binary has CUDA, a device is usable, and every operator can reproduce the configuration;
otherwise a recorded fallback.

```text
CPU path                         GPU path
--------------------------       ------------------------------------------
assembleRelaxedMomentumComponent  GpuSimpleDiscretization::assembleMomentum    (001F)
computeMomentumResponseCoefficient   ...::computeResponseCoefficients          (001G)
gradient + rhieChowMassFlux          ...::computePredictedFaceFlux             (001B + 001H)
  / calculateMassFlux
assemblePressureCorrection           ...::assemblePressureCorrection           (001I)
correctVelocity                      ...::correctVelocity                      (001J)
correctFaceMassFlux                  ...::correctFaceMassFlux                  (001K)
```

The two linear solves, the pressure update, the residual/continuity bookkeeping and the convergence
verdict are unchanged on both paths.

### All-or-nothing, and why

`prepare()` builds every device plan up front. If **any** of them refuses — an unsupported boundary
condition, say — the **whole** solve runs the CPU operators and records why. A partially-GPU
iteration is never executed: each operator was qualified as *bitwise* equal, and substituting one
CPU stage into a GPU chain would produce a path no gate has verified.

The non-orthogonal corrector passes (`nonOrthogonalCorrections > 1`) decline the GPU path
explicitly, because their pass loops call CPU-only helpers. Running pass 1 on the device and the
rest on the host would be exactly such a mixed chain, so it is declined with a reason rather than
half-applied.

### The decision is observable, not merely logged

```text
SIMPLEResult::gpuDiscretization                bool
SIMPLEResult::gpuDiscretizationFallbackReason  string
```

Same principle as `SolverResult::backendUsed` and `gpuBackendFallbacks`: a test asserts the dispatch
instead of parsing log output. The fallback is *also* logged at Warning, so it is never silent.

## 2. CPU fallback behaviour

| situation | result |
| --- | --- |
| `enableGpuDiscretization` false | `gpuDiscretization = false`, empty reason — never asked |
| CPU-only binary | the stub's `prepare()` refuses: "this binary was built without CUDA support" |
| CUDA binary, no usable device | "no usable CUDA device" |
| any operator refuses the configuration | that plan's own reason, e.g. "pressure-correction plan: ..." |
| `nonOrthogonalCorrections > 1` | "nonOrthogonalCorrections > 1 keeps the corrector passes on the CPU" |

In every case the solve produces **bitwise** the CPU result — verified, not assumed: the `F` layer
runs the declined configuration both ways and compares.

## 3. Files changed

```text
new
  include/cfd/gpu/GpuSimpleDiscretization.hpp     CPU-includable facade (PIMPL)
  src/gpu/GpuSimpleDiscretization.cpp             the stub, ONLY when NOT CFDAPP_ENABLE_CUDA
  cuda/kernels/GpuSimpleDiscretizationCuda.cpp    the real implementation, part of cfdcuda
modified
  src/pressure_velocity/SIMPLE.cpp                the dispatch and the six stage branches
  include/cfd/pressure_velocity/SIMPLESettings.hpp  enableGpuDiscretization
  include/cfd/pressure_velocity/SIMPLEResult.hpp    gpuDiscretization + fallback reason
  src/CMakeLists.txt, cuda/CMakeLists.txt           source registration
```

The three-file split (facade / stub / CUDA implementation) is the codebase's own pattern, copied
from `GPUBackend` and `GpuResidencyManager` — it is the only arrangement that lets `cfdcore` call
device operators while a CPU-only build still compiles.

**No verified kernel was duplicated.** `GpuSimpleDiscretizationCuda.cpp` contains no numerics: every
line is a call into an operator qualified by 001B–001K, which is why the equivalence is bitwise
rather than approximate.

## 4. What still runs on the host inside one iteration

Reported, not hidden:

| work | why |
| --- | --- |
| the two linear solves | `LinearSolver` is a host interface taking a host `LinearSystem`; the GPU solver is selected by its own setting and was qualified separately (GPU-PCORR-001) |
| the assembled systems' D2H and the solutions' H2D | forced by the above |
| the pressure update `p + alpha*p'` | a per-cell loop in `SIMPLE.cpp`, not an operator — inventing a kernel is out of scope |
| residuals, continuity, convergence verdict | `evaluateContinuity`, `rms`, `OuterIterationMonitor` |
| the turbulence model | not part of the discretization chain this gate covers |

Removing the first two is GPU-PIPE-001's gate. Nothing here claims persistent GPU fields.

## 5. Transfer and synchronization audit

`differential/differential.log`, the `T` lines. Measured with `gpuExecutionStats()` around a whole
production solve, flag off then on.

```text
case            flag off                              flag on (1 iteration)
cavity 2d 16    h2d=0 d2h=0 kern=0 sync=0 alloc=0     h2d=303/1096048B d2h=16/81176B kern=25  sync=0 alloc=345
channel 2d 16   h2d=0 d2h=0 kern=0 sync=0 alloc=0     h2d=303/1096048B d2h=16/81176B kern=25  sync=0 alloc=345
cavity 3d 4     h2d=0 d2h=0 kern=0 sync=0 alloc=0     h2d=304/468976B  d2h=20/30112B kern=39  sync=0 alloc=360
warped 3d 3     h2d=0 d2h=0 kern=0 sync=0 alloc=0     h2d=362/234832B  d2h=20/11912B kern=143 sync=0 alloc=418

marginal per EXTRA outer iteration:  h2d=12-13  d2h=16-20  kern=25-143  alloc=0
```

**This is the proof the gate is really about.** With the flag off a default solve issues *zero*
device activity — the linear solvers are CPU here, so there is nothing. With it on, kernels and
transfers appear. Production is now running CUDA **discretization**, not merely CUDA **linear
algebra**, and the counters say so rather than a comment claiming it.

`sync = 0`: no explicit `cudaDeviceSynchronize` is issued on this path; the only host waits are
inside `DeviceBuffer::downloadTo`.

### A leak the marginal measurement caught

The first version measured only one solve, where the totals are dominated by `prepare()`'s one-time
plan upload. Splitting out the marginal per-iteration cost immediately showed **4 allocations per
extra iteration** — the two correction *output* buffers were declared function-locally in the stage
methods, so `DeviceBuffer`'s never-shrink contract bought nothing and each iteration allocated
afresh. Moved into the impl, the marginal figure is now **`alloc = 0`**.

A single total would have looked perfectly reasonable and hidden it. That is the argument for
measuring the marginal cost, not the total.

## 6. Functional verification

`differential/differential.log`, the `E` lines. Six cases — lid-driven cavity and inlet/outlet, 2D
and 3D, Cartesian and warped, pinned and open pressure — plus a five-iteration run.

The **only** difference between the two runs is `enableGpuDiscretization`; both use the same CPU
linear solvers. Because every operator is bitwise equal and the solves are identical, the committed
state must be bitwise identical, and it is:

```text
u[d=0]  p[d=0]  flux[d=0]  residuals[d=0]  maxAbs=0   status and iteration count equal
```

That covers, in production form, every stage the brief lists: the momentum systems (they feed the
same solver and produce the same solutions), the response coefficients, the predicted flux, the
pressure-correction system, p', the pressure update, the corrected velocity and face flux, and the
residual/continuity bookkeeping. The stage-by-stage *decomposition* of those quantities was the
subject of GPU-DISC-001L and is not repeated here.

**No stage silently routes back to the CPU**: the controls in §7 force exactly that, one stage at a
time, and every one is detected.

## 7. Negative controls — routing and integration

`negative-control/` — these mutate the **production integration**, not an operator's arithmetic.
Each: inject → rebuild → run → restore → sha256 → re-run. All 11 restored with a matching hash.

| control | mutation | detected |
| --- | --- | --- |
| M1 | the momentum assembly routed back to the CPU while the rest stays on device | yes |
| M2 | the velocity and face-flux corrections routed back to the CPU | yes |
| M3 | `beginIteration` skipped — the device runs on stale fields | yes |
| M4 | the solved predictor never uploaded — stale `velocityStar` | yes |
| M5 | p' never uploaded — both corrections use a stale correction | yes |
| M6 | the dispatch flag inverted — the wrong backend entirely | yes |
| M8 | the linear predictor built from the start-of-iteration velocity | yes |
| M9 | the Rhie–Chow predictor built from the start-of-iteration velocity | yes |
| M10 | the flux correction applied to the carried mass flux, not the predictor | yes |
| M11 | a fallback reporting `gpuDiscretization = true` — the record lying | yes |

**10/10 observable controls detected.**

### The documented null control

**M7** swaps which component's diagonal feeds which response coefficient. It is **provably null**,
and the proof was established in GPU-DISC-001L: the momentum matrix in this codebase is
component-independent — the component reaches only the RHS, via `selectComponent(uB, component)`,
while the diagonal contribution is added for every boundary face regardless of condition type
(`MomentumEquation.cpp:136`, `:236`, `:298`). So `dU == dV == dW` exactly, always.

It is carried forward here rather than dropped because the 001L harness asserts that property on
every case: if it ever stops holding, that assertion fires and this control stops being null the
same day.

## 8. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck: **0 errors** each; racecheck: **0 hazards**.
Each log was verified to contain `INTEGRATED SIMPLE EQUIVALENCE: PASS`, so none is vacuous — and
because this harness drives `SIMPLE::solve` itself, the sanitizers are exercising the **production
path**, not isolated unit kernels.

## 9. Gate chain and regression

`all_gates.log`, `regression.log`, `regression_freshness.log`

```text
libcfdcuda.a  83d55ab66bc4884361988f4b0b06b2f36e799bcd4e8be13c044dd9dd762a767a
libcfdcore.a  79b3162c3cfc810773e8c2ce0e3cdd07c5d58a425966a0aee460d4d5cd8c0a7e

mesh 96 · gradients 132 · diffusion 528 · convection 1684 · momentum convection 10352
boundary conditions 75 · momentum assembly 27744 · momentum response 260 · face flux 2628
pressure correction 4950 · velocity correction 894 · face-flux correction 1848
single iteration 42 · integrated SIMPLE 26
                                                        14/14 gates green

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998     (45 disabled of 2043 registered)
  Total Test time (real) = 1350.51 s
  `ninja: no work to do` BEFORE and AFTER ctest; library and source sha256
  identical on both sides
```

This is the first gate whose regression is testing a **production change** rather than confirming
nothing drifted: `SIMPLE.cpp`, `SIMPLESettings` and `SIMPLEResult` were all modified, and the whole
suite exercises them with the new flag at its default (false), i.e. the untouched CPU path.

## 10. What is NOT started

Full-solve CPU/GPU equivalence, GPU-PIPE-001 persistent residency. Nothing committed or pushed.
