# GPU-PCORR-001 — production GPU pressure-correction recovery

**Status: ✅ PASSED, uncommitted.** The failure CUDA-QUAL-001 stopped on is fixed at its
demonstrated root cause, and CUDA-QUAL-001's blocked gate now passes.

Baseline: the CUDA-QUAL-001 checkpoint `999d6a33170993afca2affbf25161dd029bc10db`
(`origin/main` is `67b9e3e…`; the checkpoint is local and unpushed).

## 1. Root cause

**The GPU BiCGSTAB's breakdown tests were absolute; the CPU's are scale-relative.**

`src/algebra/BiCGSTAB.cpp` decides that an inner product has vanished when it has cancelled to the
rounding level of its own terms, `|(x,y)| <= eps * sum_i |x_i y_i|` (`cancelledToRoundingLevel`,
introduced by P12-MESH-004). Its own comment records why: the earlier absolute test against
`constants::tiny = 1e-30` "misread a healthy iteration of a system whose residual was simply small
as a breakdown". That criterion is invariant under `(A, b) -> (A, beta b)`.

`cuda/kernels/GpuLinearSolverCuda.cpp` still tested `std::abs(rho) < constants::tiny`,
`std::abs(rHatDotV) < constants::tiny`, `tDotT < constants::tiny` and
`std::abs(omega) < constants::tiny` — the pre-P12-MESH-004 form, despite that file's header
claiming it mirrors the CPU "instruction-for-instruction ... same breakdown thresholds".

The pressure-correction system's residual scales down with the grid, so at 320² a perfectly healthy
iteration produced a `rho` below 1e-30 and the GPU declared Breakdown. SIMPLE then reported
`PressureCorrectionFailure` on its first outer iteration.

**Classification: GPU linear-solver integration defect** — specifically a solver-criterion defect in
the GPU mirror. Not an assembly defect, not a fallback/recovery defect, not a transfer or backend
state defect, and not a CUDA toolkit or architecture effect.

## 2. What the investigation ruled out, before any change

| hypothesis | evidence | verdict |
| --- | --- | --- |
| the CUDA 12.9 / sm_89 toolchain | the historical CUDA 11.5 / sm_52 build fails identically on today's source (`results/cuda-qual-001/logs/08`) | ruled out |
| the GPU linear algebra generally | GPU CG and BiCGSTAB converge and match the CPU at exactly the failing size, n = 102400 (`results/cuda-qual-001/logs/09`) | ruled out |
| the pressure assembly | `assemblePressureCorrection` is the same CPU code on both paths; only `LinearSolverSettings::backend` differs between the two runs (`src/pressure_velocity/SIMPLE.cpp`) | ruled out |
| a CPU-only fallback/recovery path | both backends build their solver through `makeLinearSolverWithFallback` with the same `robustness.linearSolverFallback` setting; `gpuBackendFallbacks` is 0 in every failing run, so no fallback was attempted or suppressed on either side | ruled out as the cause |
| solver settings differing by backend | the benchmark sets one `SIMPLESettings`; only `backend` differs. The reproducer prints them | ruled out |

The remaining CPU/GPU asymmetry — the CPU **restarts** its Krylov sequence when cancellation is
detected, the GPU has no restart — is real but is not this failure's cause and was **not** changed.
See §7.

## 3. Canonical reproducer (`logs/01`, `tools/pcorr_repro.cpp`)

The benchmark's own case and settings (lid-driven cavity; pressure BiCGSTAB, maxIterations 5000,
absoluteTolerance 1e-7, relativeTolerance 1e-5, preconditioner None; CPU and GPU differ only in
`backend`). Before the fix:

| grid | backend | status | outer it | p linear it | detail |
| --- | --- | --- | --- | --- | --- |
| 160² | CPU | MaxIterations | 2 | 1159 | — |
| 160² | GPU | MaxIterations | 2 | 1145 | 16476 kernel launches, 0 fallbacks |
| 320² | CPU | MaxIterations | 2 | 2265 | — |
| **320²** | **GPU** | **PressureCorrectionFailure** | **0** | **0** | **"BiCGSTAB Breakdown after 876 iterations"**, 12421 kernel launches, 0 fallbacks, 0 NaN/Inf |

## 4. The mechanism, shown directly (`logs/02`, `tools/scale_breakdown.cpp`)

One matrix, RHS scaled by beta, relative tolerance only — so the work required is identical at every
scale, and a scale-invariant criterion cannot change its answer:

| b scale | CPU BiCGSTAB | GPU BiCGSTAB (before) |
| --- | --- | --- |
| 1e0 | Converged, 374 it | Converged, 354 it |
| 1e-6 | Converged, 349 it | Converged, 375 it |
| 1e-12 | Converged, 318 it | **Breakdown, 25 it** |
| 1e-16 | Converged, 357 it | **Breakdown, 2 it** |
| 1e-20 | Converged, 349 it | **Breakdown, 0 it** |

This is the failure mode in isolation, independent of SIMPLE, the mesh and the case.

## 5. The minimal fix

Three files, GPU only:

- `include/cfd/gpu/DeviceVectorOps.hpp` + `cuda/kernels/DeviceVectorOpsKernel.cu`: **`absDot(a, b)`
  = sum |a_i b_i|**, dot()'s reduction over term magnitudes, which cannot cancel. Same kernel shape,
  buffer-reuse strategy and instrumentation as `dot`.
- `cuda/kernels/GpuLinearSolverCuda.cpp`, **GpuBiCGSTAB only**: the four absolute tests become the
  CPU's criteria — `cancelledToRoundingLevel` for `rho`, `(rHat, v)` and `(t, s)`, and the CPU's
  underflow bound `std::numeric_limits<Real>::min()` for `(t, t)`. The invented
  `|omega| < tiny` test, which the CPU does not have, is gone; `omega`'s finiteness is still checked.
  `rNorm` is now tracked across iterations, as the CPU does, to feed the criterion.

**Deliberately not changed:** GpuCG's `|pAp| < constants::tiny`. The CPU CG uses the same absolute
test, so CPU and GPU agree there; making CG scale-relative is the separate, recorded debt item
"CG absolute breakdown threshold", and it is not this failure.

## 6. Acceptance (`logs/03`, `logs/05`)

| requirement | result |
| --- | --- |
| 160² GPU | **PASS** — MaxIterations (the budget), 2 outer iterations, matches CPU |
| 320² GPU | **PASS** — 2 outer iterations, p-residual **1.343e-03, identical to CPU**, continuity 8.254e-08 vs CPU 7.780e-08 |
| 640² GPU | **PASS** — 1 outer iteration, p-residual **8.126e-04, identical to CPU**, continuity 3.464e-08 vs CPU 3.387e-08 |
| `ran_cleanly` | **yes** at every grid, all repeats |
| `PressureCorrectionFailure` | **none** |
| NaN / Inf | 0 |
| CPU reference cases | unchanged and still passing |
| scale experiment | GPU now converges at every scale (331, 309, 363 it), like the CPU |

**Negative control (`logs/04`).** A mutant tree with the three criteria reverted to the absolute form
reproduces the failure **exactly**: `PressureCorrectionFailure`, "BiCGSTAB Breakdown after 876
iterations" — the same iteration count as the original. The fix addresses the demonstrated cause
rather than perturbing convergence.

## 7. Known remaining CPU/GPU asymmetry (not fixed)

On detecting cancellation the CPU **restarts** the Krylov sequence (recomputes the true residual,
resets the recurrence, bounded and deterministic) and only reports Breakdown if the restart cannot
help. The GPU reports Breakdown immediately. After this fix the criterion no longer fires spuriously,
so the asymmetry is unreachable in the cases measured here — but it remains a real difference from
the CPU solver, and porting the restart is a separate, unauthorized change.

## 8. Regression and diagnostics (`logs/06`, `logs/07`)

| check | result |
| --- | --- |
| CPU/GPU equivalence probe at n = 102400 | PASS — SpMV ≤ 5.7e-16 relative, CG 8.9e-16, BiCGSTAB 1.6e-9, 0 NaN/Inf |
| determinism | bitwise — repeated GPU BiCGSTAB solve differs in 0 of 102400 entries |
| repository GPU/CUDA ctest | **83/83** |
| GPU unit binary | **66/66** |
| CPU CG/BiCGSTAB/solver unit tests | 42 tests pass — the CPU solvers are untouched |
| compute-sanitizer memcheck / initcheck / synccheck / racecheck | **0 errors each**, 66/66 tests under every tool |
| **GCC Release** | **1932/1932** |
| **GCC Debug + GUI** | **1984/1984** |
| clang-format, CI scope (`include src apps tests`) | 0 violations |
| clang-format, `cuda/` (outside CI's scope) | 17 violations — **pre-existing and unchanged**: 9 in `GpuLinearSolverCuda.cpp` and 4 in `DeviceVectorOpsKernel.cu` both before and after this change |
| `git diff --check` on the source changes | clean |
| generated outputs rewritten by the suites | 50 — 45 runtime-only, 5 value-differing (the Debug build's optimization round-off, the count and class recorded in `results/p12-grad-002/a2/logs/regr_05`), all restored. The five names were not captured before restoring — a gap in `logs/08`, not a change to the repository. |

**Line endings (`logs/07`).** Three files were edited through Windows Python, which rewrote them
CRLF; the repository is LF and `.gitattributes` stores bytes verbatim. They were converted back to
LF before any commit, the library rebuilt to the identical hash `2cf46094…`, and the 320² case
re-verified on those exact bytes. Content was otherwise untouched.

## 9. CUDA-QUAL-001's blocked gate, rerun (`logs/05`)

The authoritative end-to-end benchmark, all six grids, CPU and GPU:

| grid | cells | CPU median | GPU median | speed-up | transfer % | GPU status |
| --- | --- | --- | --- | --- | --- | --- |
| 20² | 400 | 0.311 s | 13.02 s | 0.024× | 45.9 | MaxIterations, clean |
| 40² | 1600 | 1.401 s | 24.47 s | 0.057× | 45.8 | MaxIterations, clean |
| 80² | 6400 | 6.815 s | 32.84 s | 0.208× | 41.5 | MaxIterations, clean |
| 160² | 25600 | 11.58 s | 22.90 s | 0.506× | 35.3 | MaxIterations, clean |
| **320²** | 102400 | 27.76 s | 20.82 s | **1.33×** | 23.7 | **20 iterations, clean** |
| **640²** | 409600 | 89.80 s | 28.03 s | **3.20×** | 11.7 | **8 iterations, clean** |

Every grid is `ran_cleanly=yes`. The rejected "18.8×" and "22.9×" from the zero-work runs are
replaced by genuine measurements, and a real crossover now exists at 320² — consistent with the P7
evidence this failure had regressed against (2.0× at 320²). Transfer share falls from 46 % to 12 %
as the grid grows, so the transfer pathology recorded in CUDA-QUAL-001 §10 stands and remains
unaddressed, for GPU-PIPE-001 to take up under its own authorization.

**CUDA-QUAL-001 acceptance item 9, "CPU/GPU equivalence passes", is therefore satisfied**, and with
items 1–8 and 10–15 already passed at its checkpoint, that phase's acceptance list is complete —
contingent on this fix, which is not yet committed.

## 10. Files changed

```text
include/cfd/gpu/DeviceVectorOps.hpp      absDot declaration
cuda/kernels/DeviceVectorOpsKernel.cu    partialAbsDotKernel + absDot
cuda/kernels/GpuLinearSolverCuda.cpp     GpuBiCGSTAB scale-relative breakdown tests
results/gpu-pcorr-001/                   this evidence
```

`src/` is untouched; the `src/ + include/` tree hash moves from `42c3d9df…` to `378e2b45…` solely
through the `DeviceVectorOps.hpp` declaration. Both CPU build trees rebuilt as no-ops, confirming no
CPU translation unit is affected.
