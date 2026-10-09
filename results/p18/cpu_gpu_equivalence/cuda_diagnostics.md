<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — CUDA diagnostics (E13)

**PASS.** compute-sanitizer 2025.2.1.0, four tools, with every clean result paired to a live control.

## Why a clean log is not the evidence

A clean compute-sanitizer log is byte-for-byte what a sanitizer that instrumented nothing prints.
So the harness (`tools/run_cuda_diagnostics.sh`) reports a clean run **only** alongside a control
that the same tool, same binary, same machine must flag, and it **fails closed** otherwise: a
missing control report is `BLIND`, a missing summary line is `NO RESULT`, never a pass.

## Kernel scope, measured

The filter launches **all five production kernels** in one process. The harness runs that filter
**natively first**, records that **13 tests** pass, and then requires every sanitizer run to report
the same 13. A sanitizer run that executed fewer tests is `NO RESULT` — because "0 errors" is also
what a sanitizer prints when the gtest filter matched nothing.

## Results

| Tool | Production kernels | Live control | Verdict |
| --- | --- | --- | --- |
| **memcheck** (`--leak-check full`) | **0 errors**, 13/13 tests | deliberate out-of-bounds `cudaMemcpy` → **2 errors** | **PASS**, tool proven live |
| **initcheck** | **0 errors**, 13/13 tests | a **production** kernel reading an uninitialised buffer → **2 972 errors** | **PASS**, tool proven live |
| **racecheck** | **0 hazards**, 13/13 tests | none possible | **CLEAN, SCOPE-LIMITED** |
| **synccheck** | **0 errors**, 13/13 tests | none possible | **CLEAN, SCOPE-LIMITED** |

The initcheck control routes through the **real** `equationOfStateKernel` and its real launcher with
one input deliberately left unwritten, so initcheck is proven live **on these kernels**, not on a
toy.

## Where the scope is limited, stated rather than glossed

`racecheck` detects **shared-memory** hazards; `synccheck` detects divergent or illegal
`__syncthreads`. These kernels have neither — measured from the source, 0 `__shared__`, 0
`__syncthreads`, 0 atomics, 0 cooperative groups, 0 warp shuffles, in both translation units. So a
clean result from these two tools follows from that absence and is **not** evidence about
cross-thread interference in general.

No live control exists for either, and manufacturing one would mean adding shared memory to a kernel
that has none — qualifying a different kernel than the one under test. Recorded as a limit instead of
papered over.

**What does cover cross-thread behaviour:** one thread per cell with no atomics and no cross-thread
reads (structural, measured), plus E10's bitwise equality across block sizes 64/128/256/512 and
across repeated runs.

## Three defects in the harness itself, each caught by it failing closed

1. It parsed only `ERROR SUMMARY`. racecheck emits `RACECHECK SUMMARY: N hazards displayed`, so
   racecheck was reported `NO RESULT` rather than silently as a pass. Both formats are parsed now.
2. It treated a **missing** summary as zero. A missing summary is now `NO RESULT`.
3. It never checked that any test **actually ran**. Added, cross-checked against a native run.

The controls are `DISABLED_` so an ordinary `ctest` run never performs a deliberate out-of-bounds
access; the harness invokes them explicitly with `--gtest_also_run_disabled_tests`.
