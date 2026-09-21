# GPU-DISC-001R — Full regression

**Result: PASS.** The completed CUDA discretization pipeline is qualified on a **from-scratch
production build**. One real defect was found and fixed; it could not have been found by any
earlier gate.

```text
clean build          configure + build from scratch, 0 warnings, 0 errors, sm_80 + sm_89
CTest (CUDA)         1998/1998 passed, 0 failed   (2043 discovered, 45 disabled)
CTest (CPU-only)     1932/1932 passed, 0 failed   -- after fixing a REAL DEFECT
GPU-DISC gates       15/15 green on the FINAL binaries, 51,176 cases, bitwise
validation outputs   2685 files compared: VALUES 0 -- no numerical change anywhere
determinism          bitwise on all four backend combinations
sanitizers           16/16 clean and non-vacuous, RE-RUN on the final build
production smoke     CLI converged, mass imbalance 0, NaN/Inf no, 4 outputs
performance smoke    no timing regression, no exact-counter change
known debt           reproduced at iteration 1845 -- UNCHANGED, not broadened
repository           unintended/generated changes: 0
```

## 1. The defect this gate existed to find

**`src/gpu/GpuSimpleDiscretization.cpp` — the CPU-only stub — never compiled.**

```text
src/gpu/GpuSimpleDiscretization.cpp:41
  return cfd::algebra::LinearSystem(cfd::algebra::SparseMatrix{}, cfd::algebra::Vector{});
                                                               ^
error: no matching function for call to 'SparseMatrix::SparseMatrix()'
```

`SparseMatrix` has only a five-argument constructor plus copy/move. `SparseMatrix{}` is ill-formed.

**Why six completed gates missed it.** `src/CMakeLists.txt:313-319` adds that file only inside
`if(NOT CFDAPP_ENABLE_CUDA)`. Every gate from GPU-DISC-001M onward built with CUDA **ON**, where
`cuda/kernels/GpuSimpleDiscretizationCuda.cpp` replaces it entirely. And because the GPU-DISC work
is uncommitted, CI never compiled it either. **Phase G is the first time that translation unit was
ever put through a compiler.** Every earlier gate was green and correctly green — none of them
could see this file.

**What it meant.** Anyone building CFDApp in its documented CPU-only configuration would have hit a
hard compile error. A shipping blocker, caught by the last gate before closure.

**Classification: introduced regression, inside GPU-DISC-001 scope** (001M added the file), so the
failure policy directs a fix. The original failure log is preserved at
`cpu-only/build.log`.

**The fix throws rather than returns.** Making it compile was one line — return an empty `0x0`
system. That would be worse than the bug: an empty momentum or pressure system handed to a linear
solver produces a *wrong answer silently*, and this file's own header says it "never silently
pretends". Both methods are unreachable by construction (`prepare()` returns false, so SIMPLE never
dispatches), so they now throw a named `InvalidArgumentError` identifying a dispatch defect.

After the fix: **CPU-only build succeeds, 1932/1932 tests pass.**

## 2. Phase A — final-state audit

`repository-status/`

```text
HEAD                 d7d74f51  "docs(ci-perf-001): record the exact-SHA CI qualification"
tracked modified     65 before cleanup -> 15 after
untracked            46 entries (44 new production sources, 2 evidence trees)
staged / deleted     0 / 0
'MUTATED' in src/ include/ cuda/ apps/     0
negative-control build options defined     none, anywhere
```

The GPU-DISC implementation is intentionally uncommitted, which the brief permits. What it requires
is that every change be explained, and §8 shows the final classification.

## 3. Phase B — source integrity

`source-integrity/`

The GPU-DISC-001P campaign recorded `baseline-sha256.txt` over the 31 production sources its
mutations could touch. Comparing the final tree against it:

```text
30 of 31 files  IDENTICAL to the post-restoration baseline
 1 of 31 files  differs: src/pressure_velocity/SIMPLE.cpp
```

The single difference is **expected**: GPU-DISC-001Q legitimately added stage timers after that
baseline was taken. Rather than force a match against an obsolete hash, a **new final baseline** is
recorded (`final-baseline-sha256.txt`), which the brief explicitly prefers.

Each of the four files 001Q changed was diffed against HEAD and checked for the one thing this gate
forbids moving:

```text
git diff src/ include/ apps/ | grep for a tolerance/criterion ASSIGNMENT   ->  no matches
```

**No tolerance, convergence criterion, relaxation factor or iteration limit is assigned differently
anywhere in the tracked source.**

## 4. Phase C — clean build

`clean-build/`

A **new** directory, configured from scratch. `build/cuda` was isolated rather than deleted.

```text
cmake -S . -B build/final -G Ninja -DCMAKE_BUILD_TYPE=Release -DCFDAPP_ENABLE_CUDA=ON
      -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.9/bin/nvcc -DBUILD_TESTING=ON

configure     rc=0   "cfdcuda: CUDA 12.9.86, architectures 80;89"
build         rc=0   1278 build edges, 235 phony targets
warnings      0
errors        0
architectures compute_80/sm_80 and compute_89/sm_89, both with PTX
rebuild       "ninja: no work to do."  -- AFTER a real build, not as the only evidence
discovery     2043 tests
```

The CUDA compiler is pinned explicitly because `/usr/bin/nvcc` on PATH is the apt CUDA 11.5, which
cannot compile this project with gcc 11 — a trap this project has hit before.

### Build configurations qualified

| configuration | result |
| --- | --- |
| CUDA Release (`build/final`) | configure + build + 1998/1998 tests |
| CPU-only Release (`build/cpuonly`, `CFDAPP_ENABLE_CUDA=OFF`) | configure + build + 1932/1932 tests |
| OpenMP Release (`build/omp`, from GPU-DISC-001Q) | built and measured, CPU baseline evidence |

Native Windows CUDA remains separate technical debt and was not attempted.

## 5. Phase D — complete CTest

`ctest/`

```text
discovered   2043
disabled       45    pre-existing long-running DISABLED_ validation cases
executed     1998
passed       1998
failed          0
time      1330.36 s
freshness    "ninja: no work to do" after ctest
```

Counts match the prior recorded baseline exactly, so no test appeared or vanished. The 45 disabled
break down as CavityGhiaValidation 11, MeshQualityCampaign 8, Duct3DProductionCase 5,
LidDrivenCube3D 4, ChannelFlowValidation 4, BackwardFacingStep 3, Poiseuille 2 and six singles —
the project's manual `--gtest_also_run_disabled_tests` set, none of them disabled by this work.

**No test was disabled or weakened by GPU-DISC-001.** The only modified test file,
`tests/unit/gpu/test_device_buffer.cpp`, *tightened* an assertion (GPU-PIPE-001 Phase 3:
`synchronizations` 1 → 0), so reintroducing a removed synchronization now fails rather than
silently costing a round trip per SpMV.

## 6. Phases E–L — results

| phase | result |
| --- | --- |
| **E** 15 GPU-DISC gates, final binaries | **15/15 green**, 51,176 cases, 0 failures, bitwise |
| **F** solver validation | **0 VALUES differences** across 2685 generated outputs |
| **G** CPU-only | defect found and fixed; **1932/1932**, `cfdapp` links no cudart |
| **H** production smoke | CLI converged 6159 iterations, continuity 8.72e-09, mass imbalance 0 |
| **I** determinism | **bitwise** on all four backend combinations |
| **J** sanitizer freshness | **16/16** clean, non-vacuous, re-run on the final build |
| **K** negative-control restoration | 30/31 bitwise, 0 mutation markers, no mutation option |
| **L** performance smoke | no timing regression, no exact-counter change |

### E — every gate on the final binaries

```text
mesh 96 | gradients 132 | diffusion 528 | convection 1684 | momentum convection 10352
boundary conditions 75 | momentum assembly 27744 | momentum response 260 | Rhie-Chow 2628
pressure-correction 4950 | velocity correction 894 | face-flux correction 1848
single-iteration 42 | integrated SIMPLE 26 | full-solve 17
```

Compiled against `build/final`, not the incremental tree, so the gate evidence comes from the
binaries this regression actually qualifies.

### F — the validation result worth stating plainly

P12-MESH-005's classifier compared every generated output against HEAD (which *is* the
pre-GPU-DISC state, since none of this work is committed):

```text
checked 2685 files; IDENTICAL 1841, RUNTIME-ONLY 50, VALUES 0, NEW 794
```

**Zero VALUES differences.** GPU-DISC-001 changed no validation number anywhere in the repository —
every lid-driven cavity, MMS, grid-convergence, turbulence and production case reproduces its
committed result exactly. The CPU reference is untouched.

### H — production smoke, and a finding

```text
H1  cfdapp --case lid_driven_cavity_40x40    (GPU linear-solver backend)
    exit 0   converged=yes   iterations=6159   continuity=8.7233e-09
    massImbalance=0   NaN/Inf=no   4 output files (metadata.json, residuals.csv,
    fields.csv, solution.vtk)

H2  integrated GPU discretization via production SIMPLE::solve
    cavity2d / inletoutlet2d / case3d -- all PRODUCTION GPU PATH EXERCISED
```

**FINDING: `enableGpuDiscretization` has no case-file key.** `solver.json` accepts 14 keys
(`SolverConfigParser.cpp:222-227`) and `rejectUnknownKeys` hard-errors on anything else. The only
GPU switch the case format exposes is `backend: "GPU"` on the linear solvers (P6-GPU-002). So the
integrated CUDA discretization qualified by GPU-DISC-001M is reachable **only through the C++
`SIMPLESettings` API** — a user with a case file cannot enable it.

This is reported, not worked around. Adding a parser key, a `CaseWriter` round trip and tests at the
final gate would expand scope and risk destabilising a green state, and the brief directs against
fixing unrelated debt. Phase H therefore does both halves honestly and dresses neither up as the
other. **It is the natural first item for the work that follows.**

### I — determinism

```text
CPU solver + CPU disc   status=MaxIterations/MaxIterations  iters=400/400  fields[d=0]  history[d=0]
GPU solver + CPU disc   status=MaxIterations/MaxIterations  iters=400/400  fields[d=0]  history[d=0]
GPU solver + GPU disc   status=MaxIterations/MaxIterations  iters=400/400  fields[d=0]  history[d=0]
CPU solver + GPU disc   status=MaxIterations/MaxIterations  iters=400/400  fields[d=0]  history[d=0]
```

Bitwise on every axis the brief names — convergence state, final fields, residual history, mass
imbalance, iteration count. This is the project's **existing** policy (001M layer R, 001N history
comparison), not a weaker one written for this gate.

### J — why the sanitizers were re-run, not referenced

GPU-DISC-001Q changed `DeviceBuffer.hpp`'s `resize()` and `release()` **after** the 001P sanitizer
campaign. That is GPU memory ownership, exactly the category the brief says forces a rerun. So all
four tools were re-run on the final clean build across four workload modes:

```text
memcheck / initcheck / synccheck / racecheck  x  cavity2d / case3d / nonorthogonal / gpusolver
16/16 runs, 0 errors, 0 hazards, every one non-vacuous
```

The `gpusolver` mode is included because it is the only one reaching the reduction kernels — the
only code in the GPU path with shared memory and barriers, and therefore the only place racecheck
and synccheck are non-vacuous at all.

### L — performance smoke

```text
160x160   cpu 12.23   gpu-pipe 14.75   gpu-disc  9.63   disc-only  8.81
320x320   cpu 29.00   gpu-pipe 13.83   gpu-disc  8.29   disc-only 24.16
both BITWISE equivalence pairs PASS on the final build
```

Tracks the GPU-DISC-001Q qualified values closely. The guard reports **0 timing regressions and 0
exact-counter changes** on the 8 points compared.

**One honest note about reading the guard.** Its headline verdict says "REGRESSION DETECTED"
because 16 of the baseline's 24 points are absent from a deliberate subset. That is correct
behaviour — in a *full* run a vanished point genuinely is a regression — so the smoke reads the
guard's per-finding sections rather than its headline. Stated here rather than relied on quietly.

## 7. Known technical debt — reproduced, unchanged

`validation/known_debt.log`

```text
cpu       (CPU solver, CPU disc)   MaxIterations              3000
gpu-pipe  (GPU solver, CPU disc)   PressureCorrectionFailure  1845
gpu-disc  (GPU solver, GPU disc)   PressureCorrectionFailure  1845

gpu-disc status matches gpu-pipe:          yes
gpu-disc iterations match gpu-pipe:        yes (1845 vs 1845)
reproducer actually triggered on gpu-pipe: yes
```

**UNCHANGED — not broadened.** `gpu-disc` matches `gpu-pipe` on both status and iteration count, at
exactly the 1845 recorded in the GPU-DISC-001N evidence. The debt is present, unfixed, and no
wider. Not fixed: not authorized, not in scope.

### The first version of this probe was vacuous, and that is worth recording

It used the *benchmark's* linear-solver tolerances instead of the reproducer's, all three arms
reached `MaxIterations`, and it printed **"KNOWN DEBT: UNCHANGED"** — a clean-looking pass that had
never triggered the documented failure at all. Corrected to the reproducer's settings
(`test_simple_gpu_solver.cpp:73-79`: momentum 500/1e-10/1e-8, pressure 2000/1e-10/1e-8), and a
**non-vacuity assertion added**: the probe now checks the failure actually fired on `gpu-pipe` and
returns `NOT REPRODUCED` instead of `UNCHANGED` when it does not. A criterion the baseline passes
vacuously proves nothing.

## 8. Final repository cleanliness

`repository-status/`

```text
                       before this gate    after
RUNTIME-ONLY churn           50              0
VALUES differences            0              0
tracked modifications        65             15
tracked under results/       50              0
```

The 50 timing-only validation files are restored from the HEAD snapshot; the restorer never touches
VALUES or NEW files, so a genuine numerical change could not have been silently reverted.

Final classification — the three classes the brief requires:

```text
intended GPU-DISC / GPU-PIPE implementation   15 tracked + 44 untracked production sources
intended evidence                              2 untracked evidence trees
unintended / generated                         0      <- the requirement
```

```text
'MUTATED' in production source            0 files
negative-control build option             does not exist
temporary instrumentation remaining       none
TODO.md [x] audit                         18/18 GPU-DISC items legitimately checked
GPU-PIPE-001 final gate                   7/7 still unchecked
```

## 9. Evidence

```text
results/gpu-disc-001/full-regression/
  audit-raw.txt            Phase A capture
  summary.md               this file
  source-integrity/        current + final baselines, diffs vs 001P and HEAD
  clean-build/             configure, build, warnings, targets, freshness
  ctest/                   full suite log and discovery
  gpu-gates/               15 gate logs from the final binaries
  cpu-only/                configure, build (incl. the PRESERVED failure), ctest
  validation/              known-debt probe
  production-smoke/        CLI run, outputs, production-path runs
  determinism/             four repeated-solve comparisons
  sanitizer-freshness/     16 sanitizer logs on the final build
  performance-smoke/       benchmark + regression-guard comparison
  repository-status/       git status/diff, classifications, restore record
  tools/                   every script and probe used above
```

## 10. What is NOT started

* **No GPU-PIPE-001 final-residency checkbox is checked.** All 7 remain `[ ]`.
* Nothing committed or pushed.
* The known BiCGSTAB debt is not fixed.
* The `enableGpuDiscretization` case-format gap is reported, not closed.
