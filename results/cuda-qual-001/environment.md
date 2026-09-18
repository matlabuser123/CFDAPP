# Environment (fresh, 2026-09-18) — `logs/00_baseline_and_environment.log`

Collected before any change; historical notes were not trusted.

| item | value |
| --- | --- |
| GPU | NVIDIA RTX 5000 Ada Generation Laptop GPU |
| GPU UUID | `GPU-991873eb-7773-eb18-a19e-56959564a88d` |
| VRAM | 15352 MiB |
| compute capability | **8.9** |
| SMs / max SM clock | 76 / 3105 MHz |
| NVIDIA driver | 580.97 (`/usr/lib/wsl/lib/nvidia-smi`) |
| driver-supported CUDA | 13.0 (driver API reports 13000) |
| nvcc before | `/usr/bin/nvcc`, 11.5.119 (apt `nvidia-cuda-toolkit` 11.5.1) |
| CUDA toolkit dirs before | none under `/usr/local` |
| CMake / Ninja | 3.22.1 / 1.10.1 |
| host compilers | g++ 11.4.0, clang 14.0.0 |
| WSL kernel / distro | 6.6.87.2-microsoft-standard-WSL2 / Ubuntu 22.04.5 LTS |
| user | root (uid 0) — no sudo prompt needed for apt |
| `LD_LIBRARY_PATH` | unset |
| CUDA env vars | none set |
| disk free | 884 GB on `/`, 2.8 TB on `/mnt/c` |

Windows-side toolkits appear on `PATH` (CUDA v13.0, v12.9 under `/mnt/c/Program Files/...`); they are
Windows binaries and play no part in a WSL build.

In-tree CUDA assumptions found: `cmake/CUDA.cmake` (`check_language`, `find_package(CUDAToolkit)`,
`CMAKE_CUDA_STANDARD 17`) and `cuda/CMakeLists.txt` (architecture default, `CUDA::cudart`). The only
CUDA-enabled build directory was `build/perf`, cached at `CMAKE_CUDA_ARCHITECTURES=52` — explained in
`summary.md` §4. The kernels include only `cuda_runtime.h`; no cuBLAS, Thrust or CUB.
