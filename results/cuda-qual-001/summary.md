# CUDA-QUAL-001 — Ada CUDA toolchain qualification

**Status: 🔴 STOPPED AT A FAILED ACCEPTANCE GATE — CPU/GPU equivalence at production scale.**

Thirteen of the fifteen acceptance items pass. The phase stops at "CPU/GPU equivalence passes":
the GPU backend cannot complete a production SIMPLE solve at 320² and 640², where the CPU backend
completes normally. **The failure is not caused by this phase** — it reproduces identically on the
historical CUDA 11.5 / sm_52 toolchain (§7) — but it is a real divergence between the CPU and GPU
production paths, so the phase cannot be called complete.

Baseline preserved: HEAD = `origin/main` = `67b9e3e07a8d06330cd5845627ed03e5d07eda64`, working tree
clean at the start (`logs/00_baseline_and_environment.log`). Nothing is committed or pushed.

## 1. Acceptance

| # | item | result |
| --- | --- | --- |
| 1 | compatible CUDA toolkit active | **PASS** — CUDA 12.9.86, §2 |
| 2 | actual GPU correctly identified | **PASS** — RTX 5000 Ada, cc 8.9, UUID `991873eb…`, §2 |
| 3 | Ada-compatible architecture configured | **PASS** — `80;89`, §4 |
| 4 | independent CUDA smoke kernel executes | **PASS** — native sm_89, §3 |
| 5 | clean CFDApp CUDA build | **PASS** — 392 steps, 0 warnings, §4 |
| 6 | intended GPU architecture present in built code | **PASS** — sm_80 + sm_89 cubins, §4 |
| 7 | real CFDApp GPU execution demonstrated | **PASS** — §5 |
| 8 | CPU fallback distinguished from GPU execution | **PASS** — negative control, §5 |
| 9 | **CPU/GPU equivalence passes** | **FAIL at production scale** — §7 |
| 10 | NaN/Inf = 0 | **PASS** — §6 |
| 11 | determinism characterized | **PASS** — bitwise, §6 |
| 12 | CUDA diagnostics pass or dispositioned | **PASS** — 4 tools, 0 errors, §8 |
| 13 | CPU regression remains green | **PASS** — §9 |
| 14 | controlled performance baseline recorded | **PASS** — §10, and it is what exposed item 9 |
| 15 | evidence complete | **PASS** — this directory |

## 2. Environment and toolkit (`logs/00`, `logs/01`, `logs/02`)

Fresh evidence, not historical notes:

- GPU: NVIDIA RTX 5000 Ada Generation Laptop GPU, UUID
  `GPU-991873eb-7773-eb18-a19e-56959564a88d`, 15352 MiB, **compute capability 8.9**, 76 SMs.
- Driver 580.97, driver-supported CUDA 13.0; driver API reports 13000.
- Was: apt `nvidia-cuda-toolkit` 11.5.1, `/usr/bin/nvcc` V11.5.119, no `/usr/local/cuda*`.
- Now: **CUDA 12.9.86** (`/usr/local/cuda-12.9/bin/nvcc`), from NVIDIA's `wsl-ubuntu` repository,
  packages `cuda-nvcc-12-9`, `cuda-cudart-dev-12-9`, `cuda-sanitizer-12-9` and their dependencies
  (10 packages, 0 upgraded, 0 removed). **No NVIDIA driver package was installed or changed**, as
  NVIDIA's WSL guide requires ("Do not install any Linux display driver in WSL").
- The apt 11.5 toolkit is left in place and still owns `/usr/bin/nvcc`; 12.9 is activated
  explicitly per shell/script by `tools/env.sh`, so no other build on this machine changes.
- Host: g++ 11.4.0, clang 14, CMake 3.22.1, Ninja 1.10.1, Ubuntu 22.04.5, kernel 6.6.87.2-WSL2.

**Why 12.9.** NVIDIA's Ada Compatibility Guide: "With version 11.8 of the CUDA Toolkit, nvcc can
generate cubin native to the NVIDIA Ada GPU architecture (compute capability 8.9)." 12.9 is
comfortably inside the driver's CUDA 13.0 ceiling, supports Ubuntu 22.04, and configures cleanly
under the project's CMake 3.22.1 — verified, so no CMake upgrade was needed.

## 3. Independent smoke test (`logs/02`, `tools/cuda_smoke.cu`)

Compiled with `-gencode arch=compute_89,code=sm_89` plus the matching PTX, outside CFDApp:

- `cuobjdump`: two `sm_89` cubins and `sm_89` PTX;
- device 0 = the RTX 5000 Ada, cc 8.9, UUID matching `nvidia-smi`;
- the kernel reports **`__CUDA_ARCH__` = 890**, so native sm_89 SASS ran, not PTX JIT from an
  older virtual architecture;
- saxpy over 65536 elements: 0 mismatches, worst |error| 0.

## 4. Architecture configuration and clean build (`logs/03`)

**Finding: the documented sm_80 target had never taken effect.** `cuda/CMakeLists.txt` set its
default inside `if(NOT CMAKE_CUDA_ARCHITECTURES)`, but `enable_language(CUDA)` initialises that
variable from nvcc's own default first (**52**), so the guard was always false. The first clean
build in this phase confirmed it: `architectures 52`, `sm_52` cubins only. Every GPU build this
project ever produced was sm_52 SASS plus compute_52 PTX, reaching the Ada GPU only through the
driver's JIT — which also explains the stale `CMAKE_CUDA_ARCHITECTURES=52` in `build/perf`.

Fix (`cmake/CUDA.cmake`): choose the architectures **before** `enable_language(CUDA)`, from the
toolkit's own capability — `80;89` for CUDA ≥ 11.8, `80` for older toolkits, which cannot name
sm_89 at all. An explicit `-DCMAKE_CUDA_ARCHITECTURES=…` or `CUDAARCHS` still wins.

Clean build, `build/cuda` removed first so no stale object could survive:

- configure exit 0; `cfdcuda: CUDA 12.9.86, architectures 80;89`;
- build exit 0, 392 compile steps in 149 s, **0 warnings, 0 errors** (nvcc's "architectures prior
  to sm_75 are deprecated" warning, present in the sm_52 build, is gone);
- `libcfdcuda.a` `ecd6a978…` contains, per kernel translation unit, **`sm_80` and `sm_89` cubins
  and PTX for both**; the linked `CFDGpuTests` binary likewise.

## 5. Real GPU execution, and the CPU-fallback control (`logs/04`, `tools/gpu_execution_probe.cpp`)

The probe drives CFDApp's own production GPU API and prints the project's `GPUExecutionStats`:

| run | `cudaAvailable()` | kernel launches | fallbacks | H2D |
| --- | --- | --- | --- | --- |
| positive (GPU visible) | true | 646 (GPU CG), 909 (GPU BiCGSTAB) | 0 | 5 calls / 952 328 B |
| **negative control** (`CUDA_VISIBLE_DEVICES=""`) | **false** | **0** | 0 | 0 |

In the control the same binary also gets `nullptr` from `makeGpuBiCGSTAB()`. So the positive run's
kernels cannot be a CPU path wearing a GPU label.

## 6. Equivalence, NaN/Inf and determinism (`logs/04`, `logs/09`)

Against the CPU implementations, with the repository's own tolerances:

| operation | size | abs error | rel error | tolerance | result |
| --- | --- | --- | --- | --- | --- |
| SpMV vs `SparseMatrix::multiply` | 16 | 4.44e-16 | 2.23e-16 | 1e-12 | PASS |
| SpMV | 4096 | 8.88e-16 | 4.44e-16 | 1e-12 | PASS |
| SpMV | 65536 | 4.44e-16 | 5.69e-16 | 1e-12 | PASS |
| GPU CG vs CPU CG | 9216 | 8.88e-16 | 6.78e-16 | 1e-8 | PASS |
| GPU BiCGSTAB vs CPU BiCGSTAB | 9216 | 1.04e-10 | 7.97e-11 | 1e-8 | PASS |
| GPU CG | 25600 / 102400 | 1.33e-15 / 1.78e-15 | 1.11e-15 / 8.90e-16 | 1e-8 | PASS |
| GPU BiCGSTAB | 25600 / 102400 | 2.82e-10 / 3.18e-9 | 2.34e-10 / 1.59e-9 | 1e-8 | PASS |

NaN = 0 and Inf = 0 everywhere. Iteration counts match the CPU exactly for CG (92, 90, 88) and
differ slightly for BiCGSTAB at larger sizes (65 vs 65, 66 vs 62, 55 vs 62), as expected from
floating-point ordering in the reductions.

**Determinism: bitwise**, within a process on this device — 5 repeated SpMV runs at 128² differ in
0 of 65536 entries, and a repeated GPU BiCGSTAB solve differs in 0 of 9216. Not claimed beyond
that: no cross-device or cross-driver determinism was tested.

Repository tests: **83/83** GPU/CUDA-labelled ctest tests pass in `build/cuda`, including
`SIMPLEGpuSolverTest.GpuBackendReproducesCpuCavitySolutionWithinTolerance`; the GPU unit binary
passes 66/66.

## 7. FAILED GATE — production-scale CPU/GPU equivalence (`logs/07`, `logs/08`, `logs/09`)

The end-to-end benchmark runs the production SIMPLE solve with the CPU and GPU backends, differing
only in `LinearSolverSettings::backend`:

| grid | cells | CPU | GPU |
| --- | --- | --- | --- |
| 20² … 160² | 400 … 25600 | completes the budget, `ran_cleanly=yes` | completes, `ran_cleanly=yes` |
| **320²** | 102400 | 20 iterations, fine | **0 iterations, `PressureCorrectionFailure`, `ran_cleanly=no`** |
| **640²** | 409600 | 8 iterations, fine | **0 iterations, `PressureCorrectionFailure`, `ran_cleanly=no`** |

The benchmark's own summary prints "18.8x" and "22.9x" speedups at those two grids. **Those numbers
are meaningless** — they compare a completed CPU solve against a GPU run that did no outer
iterations, and they are recorded here only to be dismissed.

**This is a regression against committed evidence.** `results/performance/cuda_end_to_end/summary.csv`
(P7) records 320² on the GPU completing 20 outer iterations, `converged yes`, speedup 2.0063.

**Attribution — two libraries, today's source (`logs/08`).** Rebuilt with the *historical*
toolchain (CUDA 11.5, `-DCMAKE_CUDA_ARCHITECTURES=52`, `sm_52` cubins confirmed by `cuobjdump`) and
reran the same benchmark: 320² and 640² fail **identically**, `PressureCorrectionFailure` with 0
iterations, on all repeats. All other grids agree between the two toolchains within run-to-run
spread. **CUDA-QUAL-001's toolkit and architecture change did not cause this.**

**Where the failure is not (`logs/09`).** The GPU Krylov solvers are healthy at exactly the failing
size: at 320² (n = 102400) GPU CG and BiCGSTAB both converge, match the CPU to 8.9e-16 and 1.6e-9
relative, launch 618/873 kernels, take 0 fallbacks, and repeat bitwise. So the defect is in the
production pressure-correction path under the GPU backend, not in the GPU linear algebra, and not
in the toolchain.

**Not investigated further and not fixed**, since diagnosing or changing the SIMPLE/pressure-
correction path is outside this phase's authorization. Two candidates worth testing first, for
whoever picks this up: whether the GPU backend participates in the linear-solver fallback/robustness
recovery that `src/algebra/LinearSolverFallback.cpp` gives the CPU path, and what the pressure
system's solver settings are at these grids.

## 8. CUDA diagnostics (`logs/05`)

compute-sanitizer 12.9 over the whole GPU unit-test binary, all tools, tests passing under each:

| tool | result |
| --- | --- |
| memcheck (`--leak-check full`) | **0 errors, 0 bytes leaked in 0 allocations** |
| initcheck | **0 errors** |
| synccheck | **0 errors** |
| racecheck | **0 hazards (0 errors, 0 warnings)** |

## 9. CPU regression (`logs/06`)

The change cannot reach a CPU build — `cmake/CUDA.cmake` and `cuda/CMakeLists.txt` are only read
when `CFDAPP_ENABLE_CUDA=ON`, which is OFF in `build/release` and `build/debug`:

- both CPU trees rebuilt as no-ops; `libcfdcore.a` unchanged at `143a1dda…`;
- `src/` + `include/` hash `42c3d9df…`, identical to the pushed baseline;
- **GCC Release 1932/1932**, **GCC Debug + GUI 1984/1984**, 0 failures;
- clang-format 0 violations of 568 files; `git diff --check` clean.

The suites rewrote 50 generated outputs: 45 runtime-only and 5 value-differing (the Debug build's
optimization-level round-off, the class recorded in `results/p12-grad-002/a2/logs/regr_05`), all
restored to their committed values. The 5 file names were not captured before restoring — a gap in
this log, not a change to the repository.

## 10. Performance baseline (`logs/07`, `data/`)

Release, OpenMP off, sanitizers off, machine otherwise idle (load 0.59 before), GPU 62 °C before and
56 °C after; run from a scratch directory so the committed `results/performance` evidence is
untouched. Cold GPU first use: 0.575 s, excluded from the per-grid numbers.

**End-to-end production solve, median of 3 repeats (1 at 640²):**

| grid | cells | CPU median | GPU median | speed-up | transfer % |
| --- | --- | --- | --- | --- | --- |
| 20² | 400 | 0.301 s | 13.63 s | 0.022× | 44.9 |
| 40² | 1600 | 1.437 s | 24.47 s | 0.059× | 43.9 |
| 80² | 6400 | 6.401 s | 32.75 s | 0.195× | 40.8 |
| 160² | 25600 | 11.41 s | 19.83 s | 0.576× | 33.1 |
| 320² | 102400 | 27.85 s | — | **invalid, see §7** | 27.2 |
| 640² | 409600 | 89.85 s | — | **invalid, see §7** | 15.8 |

**Finding: the GPU backend is slower than the CPU at every grid where it completes**, by 1.7× to
45×, and 33–45 % of its time is host↔device transfer. No crossover is demonstrated: the benchmark's
`gpu_break_even_grid: 320x320` is exactly the grid where the GPU run fails, so it is not a
crossover. This is recorded as a finding, not acted on: CUDA-QUAL-001 is correctness qualification,
and optimization is not authorized.

**SpMV microbenchmark** (30 repeats), which separates the costs:

| grid | n | CPU | GPU stateless | GPU persistent | persistent speed-up |
| --- | --- | --- | --- | --- | --- |
| 100² | 10000 | 1.80e-3 s | 2.85e-2 s | 8.70e-4 s | 2.07× |
| 200² | 40000 | 7.88e-3 s | 5.42e-2 s | 7.59e-4 s | 10.4× |
| 400² | 160000 | 3.55e-2 s | 1.18e-1 s | 1.66e-3 s | 21.4× |
| 800² | 640000 | 1.57e-1 s | 3.14e-1 s | 3.06e-3 s | **51.3×** |

The kernel itself is strong; the stateless path (allocate + upload + kernel + download per call) is
always slower than the CPU. The persistent path's reuse is confirmed by the stats: second and later
calls show 0 allocations, 0 transfers, 0 bytes.

## 11. Files changed

Production/build: `cmake/CUDA.cmake`, `cuda/CMakeLists.txt`. Everything else is new evidence under
`results/cuda-qual-001/`. No source under `src/` or `include/` changed, proven by hash (§9).

## 12. What a resumption needs

1. A decision on the §7 defect: it predates this phase and belongs to the GPU production path.
2. Re-run this phase's item 9 once that is resolved; items 1–8 and 10–15 stand on the evidence here.
3. If the GPU backend is to be used for performance, §10's transfer-bound end-to-end result is the
   starting point — a separate, authorized optimization phase.
