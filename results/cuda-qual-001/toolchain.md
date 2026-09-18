# Toolkit selection and activation — `logs/01`, `logs/02`

## Requirement, from the vendor documentation

NVIDIA Ada Compatibility Guide: "With version 11.8 of the CUDA Toolkit, `nvcc` can generate cubin
native to the NVIDIA Ada GPU architecture (compute capability 8.9)", and PTX "is supported to run on
any GPU with compute capability higher than the compute capability assumed for generation of that
PTX", with the caveat that "PTX JIT-compiled kernels often cannot take advantage of architectural
features of newer GPUs". Recommended flags for Ada: `-gencode=arch=compute_89,code=sm_89` plus
`-gencode=arch=compute_89,code=compute_89`.

CUDA on WSL guide: the Windows driver provides `libcuda.so` inside WSL, and "users must not install
any NVIDIA GPU Linux driver within WSL 2".

## Choice: CUDA 12.9.86

- ≥ 11.8, so native sm_89 — the requirement above.
- Below the driver's CUDA 13.0 ceiling, so no PTX-JIT-version risk from a toolkit newer than the
  driver.
- Supports Ubuntu 22.04, and configures cleanly under the project's CMake 3.22.1 (verified before
  installing anything else, so no CMake upgrade was needed).
- Matches the Windows-side 12.9 toolkit, useful if native-Windows CUDA is qualified later.

## Installation

From NVIDIA's `wsl-ubuntu` repository via `cuda-keyring_1.1-1_all.deb`. Dry run first: 10 newly
installed, 0 upgraded, 0 removed. Installed `cuda-nvcc-12-9`, `cuda-cudart-dev-12-9`,
`cuda-sanitizer-12-9` and dependencies (`cuda-crt`, `cuda-nvvm`, `cuda-cccl`, `cuda-cudart`,
`cuda-driver-dev`, config-common). **No driver package installed or changed** — the
`libnvidia-compute-*` and `nvidia-kernel-common-580` versions are identical before and after.

## Activation

`tools/env.sh` sets `CUDA_HOME`, `PATH`, `LD_LIBRARY_PATH` and `CUDACXX` explicitly per shell or
script, because the project's builds run through non-interactive `wsl.exe -- bash script.sh`. The apt
11.5 toolkit is left in place and still owns `/usr/bin/nvcc`, so no other build on this machine is
affected. With `env.sh` sourced, `which nvcc` → `/usr/local/cuda-12.9/bin/nvcc`, `nvcc --version` →
release 12.9, V12.9.86.
