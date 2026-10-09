<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Determinism on device (E10)

**BITWISE across repeats and across block sizes.**

## Measured

1 024 cells, 3 species, 2 reactions, one coupled step, comparing `Y`, `T` and `ω`:

| Variation | Result |
| --- | --- |
| 5 repeated runs at 256 threads/block | **bitwise identical**, all three fields |
| block size 64 | **bitwise identical** to the 256 reference |
| block size 128 | **bitwise identical** |
| block size 512 | **bitwise identical** |

The frozen criterion (§46) asks for two block sizes; four were run.

## Why this is expected to be exact rather than merely close

Every kernel is one thread per cell, writing only that cell's outputs. There is no cross-thread
accumulation whose order could vary with the launch geometry. Measured from the source, in
`logs/09_E13_cuda_diagnostics.log`:

| | `DeviceSpeciesKernel.cu` | `DeviceSpeciesFmadControl.cu` |
| --- | --- | --- |
| `__shared__` declarations | 0 | 0 |
| `__syncthreads` calls | 0 | 0 |
| `atomic*` calls | 0 | 0 |
| cooperative groups | 0 | 0 |
| warp shuffles | 0 | 0 |

That is also why G9's conditions (2) — accumulation order — and (5) — order-independent reductions —
are satisfied **vacuously** rather than by effort, and it is recorded that way: a vacuously satisfied
condition is not evidence.

## What this criterion is doing that the sanitizers cannot

`racecheck` detects shared-memory hazards and there is no shared memory; `synccheck` detects
divergent `__syncthreads` and there are none. So for these kernels those tools confirm an absence
that follows from the source, not the absence of cross-thread interference generally.

**Block-size independence is the empirical check that does cover it.** If any cell's result depended
on another thread, changing the block size would change which threads are co-resident and the
results would diverge. Four block sizes, bitwise identical, is a real measurement of that.
