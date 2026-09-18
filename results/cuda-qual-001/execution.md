# Real GPU execution, and telling it apart from CPU fallback — `logs/04`

The distinction this phase must make, and how each state is evidenced:

| state | evidence used |
| --- | --- |
| CUDA available | `cudaGetDeviceCount` → 1; `cfd::gpu::cudaAvailable()` → true |
| CUDA binary compiled for this GPU | `cuobjdump`: `sm_89` cubin in `libcfdcuda.a` (`build.md`) |
| GPU selected | `cudaGetDeviceProperties(0)` → RTX 5000 Ada, cc 8.9, UUID `991873eb…` |
| kernel actually executed | `GPUExecutionStats::kernelLaunches` > 0, with non-zero transfer bytes |
| CPU fallback | `gpuBackendFallbacks`, and the negative control below |

## Positive run (`tools/gpu_execution_probe.cpp`, no arguments)

Driving CFDApp's own production API:

- SpMV at 16, 4096 and 65536 unknowns: 1 kernel launch each, 0 fallbacks.
- GPU CG at 9216 unknowns: **646 kernel launches**, 646 synchronizations, 5 H2D calls / 952 328 B,
  278 D2H calls / 153 504 B, 10 allocations, 0.0200 s in kernels, **0 fallbacks**.
- GPU BiCGSTAB, same system: **909 kernel launches**, 13 allocations, **0 fallbacks**.

## Negative control (`--negative-control`, `CUDA_VISIBLE_DEVICES=""`)

The same binary, the only change being that no device is visible:

- `cudaGetDeviceCount` → "no CUDA-capable device is detected", count 0;
- `cfd::gpu::cudaAvailable()` → **false**;
- `cfd::gpu::makeGpuBiCGSTAB()` → **nullptr**;
- **0 kernel launches**, 0 transfers, 0 allocations.

The control would also expose the reverse error: a run that claimed GPU execution while the work
happened on the CPU would show kernel launches here too. It does not.

## The repository's own tests, built against this toolchain

- `ctest -R 'Gpu|GPU|Cuda|CUDA|Device'` in `build/cuda`: **83/83 passed**, including
  `SIMPLEGpuSolverTest.GpuBackendReproducesCpuCavitySolutionWithinTolerance` and
  `…RepeatedOuterIterationsReuseGpuSolverBuffersWithoutReallocating`.
- The GPU unit binary alone: **66/66 passed**.
