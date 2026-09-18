# Architecture configuration and the clean build — `logs/03`

## The defect this phase found

`cuda/CMakeLists.txt` chose its architectures inside `if(NOT CMAKE_CUDA_ARCHITECTURES)`. By then
`enable_language(CUDA)` has already initialised that variable from nvcc's own default (**52**), so
the guard never fired and the documented sm_80 target never applied. First clean build of this
phase, before the fix: `architectures 52`, `sm_52` cubins only, plus nvcc's warning that offline
compilation for architectures before sm_75 will be removed.

Consequence for every GPU build this project shipped: sm_52 SASS and compute_52 PTX, running on the
Ada development GPU only through the driver's PTX JIT.

## The fix

`cmake/CUDA.cmake` decides **before** `enable_language(CUDA)`, from the toolkit's own version:
`80;89` for CUDA ≥ 11.8, `80` otherwise (older toolkits reject sm_89 outright). An explicit
`-DCMAKE_CUDA_ARCHITECTURES=…` or the `CUDAARCHS` environment variable still wins, and
`cuda/CMakeLists.txt` now only reports and applies the choice.

## Clean build of record

`build/cuda` removed first, so no stale CUDA object could survive.

| item | value |
| --- | --- |
| configure | `cmake -S . -B build/cuda -G Ninja -DCMAKE_BUILD_TYPE=Release -DCFDAPP_ENABLE_CUDA=ON -DCFDAPP_BUILD_GUI=OFF -DBUILD_TESTING=ON`, exit 0 |
| CUDA compiler identification | NVIDIA 12.9.86, `/usr/local/cuda-12.9/bin/nvcc` |
| CUDA toolkit found | 12.9.86 |
| architectures | `cfdcuda: CUDA 12.9.86, architectures 80;89` |
| host compiler / build type | g++ 11.4.0 / Release |
| build | exit 0, 392 compile steps, 149 s at `-j20` |
| warnings / errors | **0 / 0** |
| `libcfdcuda.a` | `ecd6a9780b32572ba050ab4b6842cb8c6cd58254229e99b121d0154fc9868e55` |

`cuobjdump --list-elf` on the library: `sm_80` **and** `sm_89` cubins for each of
`CsrSpmvKernel.cu`, `DeviceVectorOpsKernel.cu`, `GpuPreconditionerKernel.cu`; `--list-ptx` shows PTX
for both. The linked `CFDGpuTests` binary carries the same pair. So the intended Ada target is
present in the built code, not assumed.
