---
name: cfdapp-cuda
description: CFDApp CUDA/GPU verification process - the CPU-reference rule, execution-vs-fallback distinction, the equivalence ladder, compute-sanitizer, and what makes a GPU performance claim valid. Use for any change under cuda/ or include/cfd/gpu/, or any GPU correctness or performance question.
---

# CFDApp CUDA

**The CPU path is the reference.** A GPU path may not redefine CPU semantics; it must demonstrate
equivalence to them. Where the CPU encodes a criterion, mirror it exactly rather than inventing a
third behaviour — the whole of `results/gpu-pcorr-001/` is the consequence of one place where the
GPU did not.

**Correctness before performance.** A performance number from an invalid solve is not a result.

`CLAUDE.md` is authoritative throughout — §4 for the evidence standard and environment naming, §6
for CPU/GPU equivalence, §11 for commit/push authorization, and the Local Hardware / Resource Policy
for how the GPU and the machine are used. Nothing here overrides it.

## 1. Four different things — never conflate them

```text
CPU execution        the reference path ran
GPU execution        a kernel actually ran on the device
CPU fallback         the GPU backend was never used (e.g. CUDA_VISIBLE_DEVICES="")
GPU backend fallback the GPU path started, failed, and handed back to the CPU
```

A test that "passes with the GPU backend selected" may have run entirely on the CPU. Prove device
execution before claiming it — a kernel-launch count that is non-zero, or a probe that reports
`__CUDA_ARCH__`. `CUDA_VISIBLE_DEVICES=""` is the negative control for this.

## 2. Track these

```text
kernel launches · H2D transfers · D2H transfers · synchronizations
fallbacks (must be 0 unless the test is about fallback) · NaN · Inf
device memory errors · VRAM high-water mark
```

Report them with every GPU result. Zero kernel launches with a "pass" is a red flag, not a pass.

## 3. Verification ladder

```text
operation-level CPU/GPU equivalence   (SpMV, dot, axpy, norms — relative error bounds)
        ↓
solver-level equivalence              (CG, BiCGSTAB at the failing/target size)
        ↓
production-case equivalence           (full SIMPLE run; residuals matching the CPU)
        ↓
determinism characterization          (repeat the same solve; report bitwise or bounded)
        ↓
compute-sanitizer                     (memcheck, initcheck, synccheck, racecheck — all four)
        ↓
performance measurement               (only after everything above passes)
```

Climb it in order. Each rung's failure is diagnostic: operation-level failures mean kernels;
solver-level with clean operations means criteria or recurrences; production-case with clean
solvers means assembly, settings or integration.

**`CFDAPP_ENABLE_CUDA` defaults OFF, and none of the three presets enables it** — so `build/debug`,
`build/release` and `build/asan` contain **no GPU code and no `CFDGpuTests` binary**. A GPU build is
configured explicitly into its own directory (by convention `build/cuda`, Release + CUDA ON; the
CUDA-QUAL-001 and GPU-PCORR-001 evidence used `build/cuda` and `build/perf`):

```bash
cmake -S . -B build/cuda -G Ninja -DCMAKE_BUILD_TYPE=Release -DCFDAPP_ENABLE_CUDA=ON
cmake --build build/cuda -- -j20

# The GPU unit target is CFDGpuTests.  Its CTest label is `numerical` (the tier), NOT `gpu` —
# `ctest -L gpu` matches nothing; select it with -R CFDGpuTests.
GPU_TESTS=build/cuda/tests/unit/gpu/CFDGpuTests
for tool in memcheck initcheck synccheck racecheck; do
  compute-sanitizer --tool $tool "$GPU_TESTS"
done
```

Check `grep CFDAPP_ENABLE_CUDA <build-dir>/CMakeCache.txt` before trusting any "GPU test passed" —
in a CUDA-OFF tree the GPU tests do not exist to fail.

`racecheck` matters most for any on-device reduction — that is the construct that produces
shared-memory races.

Report each tool's error count separately, with the test count that ran under it.

## 4. Toolchain facts to record, separately

These are four different facts and are routinely confused:

```text
driver version · driver-supported CUDA version · installed toolkit (nvcc --version) · GPU architecture
```

The workstation GPU is an **RTX 5000 Ada, compute capability 8.9**. CUDA 11.8 is the first toolkit
emitting native `sm_89` cubin; older toolkits silently produce `sm_52` + PTX that JITs. Verify what
was actually emitted rather than what was intended:

```bash
cuobjdump --list-elf <library>     # native cubins present
cuobjdump --list-ptx <library>     # PTX (JIT) only
```

`CMAKE_CUDA_ARCHITECTURES` is initialized by `enable_language(CUDA)` from nvcc's default, so any
architecture selection must happen **before** that call (`cmake/CUDA.cmake`).

## 5. Environment separation

WSL2 + CUDA evidence does **not** establish native Windows + CUDA (`CLAUDE.md` §4). Name the
environment on every GPU result. **GitHub Actions runners have no GPU** — CI never validates device
execution, and must never be cited as if it did. Run 35352132866 passing 12/12 on `c1355ea`
qualifies CPU correctness and portability only.

## 6. Performance validity

A GPU speed-up claim is valid only if:

* the solve itself is valid — converged or a legitimate budget exhaustion, `ran_cleanly` true,
  0 NaN/Inf, 0 unexpected fallbacks;
* it is **end-to-end**, including transfers, not kernel-only;
* it spans a **size ladder** with the crossover point recorded;
* baselines are named (CPU serial, CPU OpenMP, GPU);
* it is Release, no sanitizers, no competing load, warm-up then repeats, **median and spread** —
  never the best run;
* hardware, toolkit, build configuration, threads, grid and iteration counts are recorded.

**Explicitly prohibited:** a speed-up computed against a run that did no work. The rejected
"18.8×" and "22.9×" in `results/cuda-qual-001/` came from GPU runs that failed at the first outer
iteration — the GPU looked fast because it had quit. Those numbers are rejected permanently and must
never be reused.

**The current end-to-end record** (`results/gpu-pcorr-001/` §9, post-fix, CUDA 12.9 / sm_89, WSL2):

```text
20²  0.024×    40²  0.057×    80²  0.208×
160² 0.506×  ← the GPU is ~2× SLOWER than the CPU here
320² 1.33×   ← crossover lies between 160² and 320²
640² 3.20×
```

Quote the whole ladder, not the favourable end of it. Before proposing work at a given grid, check
which side of the crossover it is on — below it, the honest deliverable is "less slow", not "faster",
and that expectation belongs in the plan rather than in the closeout. Older figures exist in
`results/performance/cuda_end_to_end/` from a CUDA 11.5 machine and **disagree**; treat a baseline
disagreement across phases as a stop condition, not something to average.

## 7. Residency

Keep persistent data on the device; avoid host↔device copies inside iterative loops; estimate VRAM
and keep headroom (an OOM run is not a result). Transfer share falling as the grid grows is the
expected signature of a transfer-bound path — the recorded transfer overhead debt is **open**, and
is GPU-PIPE-001's scope, not something to fix in passing.

## 8. Known open asymmetries — do not "fix" incidentally

* GPU CG still uses an absolute `|pAp|` breakdown test (the CPU CG does too, so they agree) —
  recorded debt, separately authorized.
* The CPU restarts its Krylov sequence on detected cancellation; the GPU reports breakdown
  immediately — a real, recorded asymmetry, deliberately not changed.
* GPU transfer overhead — recorded debt.

Each is listed in `TODO.md` → Technical Debt. Touching them needs its own authorization.
