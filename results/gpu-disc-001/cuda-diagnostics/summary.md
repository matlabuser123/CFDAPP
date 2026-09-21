# GPU-DISC-001O — CUDA diagnostics

**Result: PASS.** The integrated production GPU SIMPLE path is clean under all four sanitizers
across a six-mode workload matrix — **24 runs, 0 errors, 0 hazards, every one non-vacuous**.

```text
memcheck    6/6 modes   0 errors
initcheck   6/6 modes   0 errors
synccheck   6/6 modes   0 errors
racecheck   6/6 modes   0 hazards
workload    13 production cases · 364 outer iterations · 53,138 kernel launches
repeatability  3 workloads x 2 runs: identical exit code, summary and result
API audit   every launch checked, every allocation and copy checked, fallbacks logged and counted
GPU-DISC gates  15/15 green
full regression 1998/1998 passed, 0 failed, 1292.5 s (45 disabled of 2043 registered)
```

No defect was found, so no fix was made and no regression test was needed.

## 1. The finding that shaped this gate

Phase A (`audit.md` §1) mapped all 15 `.cu` files and 32 kernels:

```text
__shared__       only cuda/kernels/DeviceVectorOpsKernel.cu
__syncthreads    only cuda/kernels/DeviceVectorOpsKernel.cu
atomics          NONE anywhere
streams          NONE (default stream throughout)
warp intrinsics  NONE
```

Every discretization kernel is a per-cell or per-face **gather** — the design decision taken in
GPU-DISC-001F to avoid atomics entirely. But **racecheck is a shared-memory hazard detector** and
**synccheck examines barriers**, so a kernel with neither gives both tools nothing to report.

Every gate from 001B through 001N ran **CPU** linear solvers. Their `racecheck: 0 hazards` and
`synccheck: 0 errors` lines were therefore true but **structurally vacuous for those two tools** —
no code under test contained a barrier or a shared array. They were genuinely non-vacuous for
memcheck and initcheck, which examine the global-memory traffic those kernels do plenty of.

This is precisely why this checkbox could not be ticked from the union of per-gate runs, and it
dictated the workload design: **one mode puts the linear solver on the device too**, bringing in
the reduction kernels — the only code in the whole GPU path with shared memory and barriers.

The contrast is visible in the counters:

```text
CPU-solver modes    sync=0        kernels ~1000-5400
GPU-solver mode     sync=6265     kernels 37932        <- reductions actually executing
```

## 2. Environment

`environment.txt`

```text
GPU            NVIDIA RTX 5000 Ada Generation Laptop GPU, compute capability 8.9, driver 580.97
CUDA toolkit   12.9.86
sanitizer      NVIDIA Compute Sanitizer 2025.2.1.0
host compiler  g++ 11.4.0 (Ubuntu 22.04, WSL2)
build type     Release, CFDAPP_ENABLE_CUDA=ON
architectures  sm_80 and sm_89 (native)
CUDA flags     -O3 -DNDEBUG -std=c++17 -fmad=false   (12 kernels carry -fmad=false)
line info      -g on the workload translation unit only
```

**No numerical flag was changed.** `-fmad=false`, `-O3` and `-DNDEBUG` are exactly as production
builds them, so the diagnostic build *is* the production build. Only the workload's own
translation unit gains `-g`, which the brief explicitly permits for actionable reports.

## 3. Workload — the production path, not isolated kernels

`tools/diagnostic_workload.cpp` drives `SIMPLE::solve` with `enableGpuDiscretization`, i.e. the
integrated path qualified by 001M and 001N. It prints what it executed and **fails itself as
VACUOUS if zero kernels ran**, so non-vacuity is provable from the sanitizer log rather than
asserted.

| mode | cases | outer | what it exercises |
| --- | --- | --- | --- |
| `cavity2d` | closed cavity 12², 40 outer | 40 | reference pin, wall + moving wall, U/V, multi-iteration buffer reuse |
| `inletoutlet2d` | channel 12², 40 outer | 40 | open pressure boundary, pin **suppressed**, through-flow, Symmetry |
| `case3d` | cavity 5³ (GG), cavity 5³ (LS), channel 5³ | 74 | U/V/W, 3D gradients, **3D cofactor/adjugate path**, 3D BC storage, 3D flux |
| `schemes` | QUICK, LinearUpwind, Central, LS | 100 | deferred-correction convection; **2D packed least-squares layout** |
| `nonorthogonal` | warped 12² (GG and LS) | 60 | skew sweeps, **oblique-Neumann**, non-orthogonal pressure coupling |
| `gpusolver` | cavity 8², channel 8², **GPU linear solver** | 50 | **the reduction kernels: shared memory and barriers** |

Every workload runs **tens of outer iterations**, so buffers are reused, plans persist across
iterations, and a stale-buffer, lifetime or delayed-uninitialised-read defect has room to appear.
No single-iteration run is used as evidence.

### Known sensitive paths, deliberately executed

* **2D `gradW` absence** — the GPU-DISC-001D initcheck incident, where 2D meshes allocated `gradW`
  buffers no kernel wrote and the harness then downloaded them. Every 2D mode covers it under
  initcheck; all clean.
* **2D packed least-squares layout** (`c11=Syy, c12=Sxy, c22=Sxx`) — `schemes` and `nonorthogonal`.
* **3D cofactor/adjugate path** — `case3d` with `LeastSquares`.
* **Conditional pressure reference pin** — pinned in `cavity2d`, suppressed in `inletoutlet2d`.
* **Non-orthogonal pressure-correction coefficients** — `nonorthogonal`.
* **Vector BC lookup** — Wall, MovingWall, Inlet, Outlet, Symmetry across the modes.
* **Face-flux owner/neighbour orientation** — every mode.

## 4. Results

`run_diagnostics.log`, and per-mode logs under `memcheck/`, `initcheck/`, `synccheck/`,
`racecheck/`. Each log records the exact command, the mode, the build sha256, the exit code and the
sanitizer summary.

```text
tool       mode            rc  vacuity      kernels  summary
memcheck   cavity2d        0   non-vacuous   1000    ERROR SUMMARY: 0 errors
memcheck   inletoutlet2d   0   non-vacuous   1000    ERROR SUMMARY: 0 errors
memcheck   case3d          0   non-vacuous   2911    ERROR SUMMARY: 0 errors
memcheck   schemes         0   non-vacuous   4925    ERROR SUMMARY: 0 errors
memcheck   nonorthogonal   0   non-vacuous   5370    ERROR SUMMARY: 0 errors
memcheck   gpusolver       0   non-vacuous  37932    ERROR SUMMARY: 0 errors
initcheck  (all six)       0   non-vacuous   same    ERROR SUMMARY: 0 errors
synccheck  (all six)       0   non-vacuous   same    ERROR SUMMARY: 0 errors
racecheck  (all six)       0   non-vacuous   same    RACECHECK SUMMARY: 0 hazards (0 errors, 0 warnings)
```

**memcheck**: zero invalid reads/writes, zero misaligned or out-of-bounds accesses, zero
use-after-free, zero CUDA API errors, across 53,138 kernel launches.

**initcheck**: zero uninitialised reads, including on every dimension-sensitive contract listed in
§3.

**synccheck** and **racecheck**: meaningful on the `gpusolver` row, where 37,932 launches include
the reduction kernels' `extern __shared__` arrays and `__syncthreads`. Clean there is a real
result; clean on the CPU-solver rows says only that those kernels contain no barriers, which §1
already establishes. **Neither result is claimed as more than it is**, and the bitwise CPU/GPU
agreement from GPU-DISC-001N is not treated as evidence about races.

## 5. CUDA API error-handling audit

`api_error_handling_audit.log`. The sanitizers prove the kernels are sound; this proves that when
the *runtime* fails, production surfaces it.

| check | result |
| --- | --- |
| every kernel launch followed by a launch-error check | **15/15 files ok** — 32 launches, all covered |
| every `recordLaunch` checks `cudaGetLastError` | **9/9** |
| allocation and both copy directions checked | `DeviceBuffer.hpp:70, :88, :105` — `cudaMalloc`, H2D and D2H all via `checkCuda` |
| what failure does | `checkCuda` throws `cfd::NumericalError` naming the failing operation |
| synchronization failures propagate | the one `cudaDeviceSynchronize` (`DeviceVectorOpsKernel.cu:242`) is checked; the other two sites are comments recording their deliberate removal in GPU-PIPE-001 Phase 3 |
| backend fallbacks are policy, not silence | counted in `gpuExecutionStats().gpuBackendFallbacks` **and** logged at Warning, for both the solver backend and the discretization backend |
| required device state validated before use | 10 `requireResident` guards in the production facade |

**No blanket synchronization was added** to quiet any tool, and nothing was suppressed.

## 6. Repeatability

`repeatability/`

| workload | run 1 | run 2 | verdict |
| --- | --- | --- | --- |
| memcheck `cavity2d` | rc=0, 0 errors, 1000 kernels | identical | **IDENTICAL** |
| memcheck `gpusolver` | rc=0, 0 errors, 37932 kernels | identical | **IDENTICAL** |
| racecheck `gpusolver` | rc=0, 0 hazards, 37932 kernels | identical | **IDENTICAL** |

Racecheck was repeated specifically on the only mode with shared memory, since that is the only
place an intermittent hazard could hide. Exit code, sanitizer summary, workload result and kernel
count all match.

## 7. Defects, fixes, regression tests

**None found.** No sanitizer produced a warning or error, so there was nothing to explain away,
suppress, or fix, and no regression test was required. The post-fix rule did not trigger.

## 8. Gate chain and regression

`regression/all_gates.log`, `regression/regression.log`, `regression/regression_freshness.log`

```text
15/15 GPU-DISC differential gates green

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998     (45 disabled of 2043 registered)
  Total Test time (real) = 1292.49 s
  `ninja: no work to do` BEFORE and AFTER ctest; library and source sha256
  identical on both sides
```

## 9. Which Integration checkbox is next

| checkbox | status | why |
| --- | --- | --- |
| **CUDA diagnostics** | **marked** | this gate |
| **Negative controls** | not ready | the per-gate control sets exist (8/8 here in 001N, 10/10 in 001M, 9/9 in 001L, 11/11 in 001K, …) but there is no consolidated GPU-DISC-001-wide inventory, and the documented **null** controls and their proofs are scattered across eight summaries. That consolidation is the checkbox's plausible scope and does not exist yet. |
| **Performance qualification** | not ready | GPU-DISC-001N §10 measured the GPU arm **slower** (12.1× at 8² falling to 1.24× at 40²), because every assembled system round-trips to the host solver. Nothing has been optimized, by instruction. |
| **Full regression** | not ready as a GPU-DISC-001-wide statement | 1998/1998 passes after every gate, but the checkbox's own scope has never been written down. |

The next Integration checkbox is **`Negative controls`**, and like this one it should begin by
auditing what its GPU-DISC-001-wide scope actually requires rather than assuming the per-gate
evidence adds up to it.

## 10. What is NOT started

Negative controls, Performance qualification, Full regression (as Integration checkboxes), and
GPU-PIPE-001 persistent residency. Nothing committed or pushed.
