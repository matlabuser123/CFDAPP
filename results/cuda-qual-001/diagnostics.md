# CUDA-specific diagnostics — `logs/05`

Tool: **compute-sanitizer from CUDA 12.9** (`/usr/local/cuda-12.9/bin/compute-sanitizer`), run over
the whole GPU unit-test binary `build/cuda/tests/unit/gpu/CFDGpuTests` (`2af1fb5019c5eb0c…`), with
`--error-exitcode 9`.

| tool | what it checks | result | tests under the tool |
| --- | --- | --- | --- |
| `memcheck --leak-check full` | invalid, out-of-bounds and misaligned device memory access; leaks | **0 errors, 0 bytes leaked in 0 allocations**, exit 0 | 66/66 passed |
| `initcheck` | uninitialized device memory reads | **0 errors**, exit 0 | 66/66 passed |
| `synccheck` | synchronization errors | **0 errors**, exit 0 | 66/66 passed |
| `racecheck` | shared-memory race hazards | **0 hazards (0 errors, 0 warnings)**, exit 0 | 66/66 passed |

Kernel launch errors would surface as sanitizer errors and as failing tests; neither occurred.

Nothing was skipped, so no limitation needs dispositioning. What this does and does not cover: it is
the GPU unit-test binary's kernels, not the end-to-end production solve, and host-side ASan/UBSan
says nothing about device memory safety — these four tools are the device-side evidence.
