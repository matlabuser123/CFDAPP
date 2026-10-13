# P17.0 — toolchain and environment

Recorded 2026-09-26T04:03:32Z. Raw: `logs/03_toolchain.log`.

**Environment scope.** Everything below is WSL2 on Windows 11. Per `CLAUDE.md` §4,
evidence is environment-specific: **WSL2 CUDA does not establish native Windows
CUDA**, which remains unqualified and is listed under `TODO.md` technical debt.

---

## Host

```text
OS            Windows 11 Pro 10.0.26200, WSL2
kernel        6.18.33.2-microsoft-standard-WSL2
distro        Ubuntu 22.04.5 LTS          <- Ubuntu-22.04, not the "Ubuntu" alias
cores         32 visible to WSL
memory        31 GiB visible to WSL
```

## Compilers and build tools

```text
c++ / gcc        11.4.0  (Ubuntu 11.4.0-1ubuntu1~22.04.3)
clang-format-18  18.1.8
cmake            3.22.1
ctest            3.22.1
ninja            1.10.1
Qt6              /usr/lib/x86_64-linux-gnu/cmake/Qt6   (Debug+GUI only)
```

## CUDA

Two toolkits are installed and **the distinction matters**:

```text
/usr/local/cuda-12.9/bin/nvcc   12.9, V12.9.86   <- the one CMake is configured with
/usr/local/cuda/bin/nvcc        12.9, V12.9.86      (symlink to the above)
/usr/bin/nvcc                   11.5, V11.5.119  <- distro package; NOT used
```

`build/qual-final` records `CMAKE_CUDA_COMPILER=/usr/local/cuda-12.9/bin/nvcc`, and
the frozen regression harness exports `PATH=/usr/local/cuda-12.9/bin:$PATH` for
exactly this reason. Picking up `/usr/bin/nvcc` 11.5 instead would silently fall
back to `sm_52`, which cannot target this GPU natively.

```text
GPU                 NVIDIA RTX 5000 Ada Generation Laptop GPU
compute capability  8.9
memory              15352 MiB
driver              580.97   (nvidia-smi 580.76.04, reports CUDA 13.0 runtime ceiling)
architectures built 80;89    per cuda/CMakeLists.txt for CUDA >= 11.8 — native cubin
                             for Ampere and Ada plus compute_NN PTX
```

`cmake/CUDA.cmake` selects the architectures **before** `enable_language(CUDA)`,
because `enable_language` initialises the variable from nvcc's `sm_52` default and a
later "set if empty" can never fire (CUDA-QUAL-001, 2026-09-18).

## The three qualified build trees

All three were configured at P13 G1 and are reused unchanged.

```text
tree                  build type  CUDA  GUI  OpenMP  CMakeCache configured
build/qual-final       Release     ON    OFF  ON      2026-09-24T01:53:10Z   "Release+CUDA"
build/qual-debug       Debug       ON    ON   OFF     2026-09-24T01:59:31Z   "Debug+GUI"
build/qual-cpuonly     Release     OFF   OFF  ON      2026-09-24T02:09:39Z   "CPU-only"
```

`build/final` is **not** one of them — it has `CFDAPP_ENABLE_CUDA=OFF` and therefore
cannot be the Release+CUDA configuration. P13's `source_fingerprint.md` records this;
it is repeated here because the name invites the mistake.

## Timing caveat

WSL's wall clock jumps. Per `CLAUDE.md` §7, per-test timings are taken from gtest's
own `(N ms)` output; the harness's own `date`-derived totals are recorded as
indicative wall time only, not as performance evidence.
