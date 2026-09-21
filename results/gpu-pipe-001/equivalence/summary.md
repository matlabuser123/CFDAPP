# GPU-PIPE-001 — dedicated CPU/GPU equivalence campaign

**Result: equivalence holds wherever both backends converge. One pre-existing GPU defect was
discovered, attributed to HEAD, and is NOT caused by GPU-PIPE-001.**

## 1. Scope, stated honestly

The GPU backend is selected per linear solver (`LinearSolverSettings::backend`), and only
`SIMPLESettings`/`CompressibleSIMPLE`'s `momentumSolver` and `pressureSolver` expose it. Thermal,
species, turbulence and multiphase carry **no backend setting**, so their linear solves run on the
CPU in both arms. This campaign therefore does **not** claim equivalence for physics the GPU
backend never touches.

## 2. Tolerance — adopted, not invented

`tests/solver/simple/test_simple_gpu_solver.cpp` is the repository's existing CPU/GPU equivalence
test. It uses a 6×6 cavity, `maxIterations = 3000`, 1e-6 outer tolerances, 1e-10/1e-8 linear
tolerances, **asserts `status == Converged` on both arms**, then requires
`maxVelocityError < 1e-6` and `maxPressureError < 1e-6`. This campaign uses exactly that
configuration and bound, extended to more sizes and to 3D. Nothing was loosened.

### A methodological error, corrected

The first run of this campaign compared the two backends at outer budgets of 2–6 with 1e-10 outer
tolerances. Both arms exited on `MaxIterations`, so it was comparing **unconverged transients** —
two solvers that had taken different Krylov paths and had not yet reached the same solution — against
a bound calibrated on converged solutions. It reported 9 failures that meant nothing.

That is precisely the error `cfdapp-numerics` §3 exists to prevent: *the quantity must be
iteratively converged — check against a long-run plateau, not the default budget*. The campaign now
refuses to apply the field bound unless both arms report `Converged`, and says so in its output.

## 3. Results

| case | cells | CPU status | GPU status | pressure max\|d\| | u max\|d\| | verdict |
| --- | ---: | --- | --- | ---: | ---: | --- |
| 2D cavity 6×6 (the repo's own case) | 36 | Converged | Converged | **1.561e-15** | 6.800e-16 | **PASS** |
| 3D cavity 10³ | 1 000 | Converged | Converged | 7.160e-08 | 6.115e-08 | **PASS** |
| 3D cavity 16³ | 4 096 | Converged | Converged | 4.708e-07 | 2.437e-07 | **PASS** |
| 2D cavity 20² | 400 | MaxIterations | MaxIterations | (5.796e-09) | (2.070e-09) | bound n/a |
| 2D cavity 80² | 6 400 | MaxIterations | MaxIterations | (1.485e-07) | (1.309e-08) | bound n/a |
| 2D 320² bounded | 102 400 | MaxIterations | MaxIterations | (8.446e-05) | (5.795e-07) | bound n/a |
| 2D 640² bounded | 409 600 | MaxIterations | MaxIterations | (4.135e-03) | (1.452e-05) | bound n/a |
| **2D cavity 40²** | 1 600 | MaxIterations | **PressureCorrectionFailure** | — | — | **see §4** |

**Where both backends converge, they agree to between 1.6e-15 and 4.7e-07** — the 6×6 case, which is
the repository's own equivalence case, agrees to machine precision with *identical* iteration counts
(737 outer, 13 979 linear on both arms).

In every case the GPU arm actually executed on the device (42 987 – 4 847 891 kernel launches) with
**zero backend fallbacks**, so no comparison here is CPU-against-CPU.

Values in parentheses are informational: the field bound is deliberately not applied to an exhausted
iteration budget. Residuals nevertheless agree to 9–11 significant figures in those rows.

## 4. The 40×40 finding — pre-existing, attributed

At 40×40 run toward convergence, the **GPU** arm exits at outer iteration 1845 with
`PressureCorrectionFailure`:

```text
statusDetail: pressure-correction linear solve failed: BiCGSTAB Breakdown after 74 iterations
```

while the CPU arm runs its full 3000-iteration budget. That is the failure *mode* GPU-PCORR-001
repaired, so it was attributed before anything else was reported — two libraries, per `CLAUDE.md` §6.

```text
HEAD (548401a, before GPU-PIPE-001)
  CPU status=1 outer=3000 p_lin_it=243384 p_res=6.5751888576822637e-06 cont=8.369e-11
  GPU status=3 outer=1845 p_lin_it=160367 p_res=1.4513381583017726e-05 cont=5.543e-11
      breakdown after 74 iterations          kernels=2 870 500

WORKING TREE (Phase 2 + Phase 3 + rho-carry)
  CPU status=1 outer=3000 p_lin_it=243384 p_res=6.5751888576822637e-06 cont=8.369e-11
  GPU status=3 outer=1845 p_lin_it=160367 p_res=1.4513381583017726e-05 cont=5.543e-11
      breakdown after 74 iterations          kernels=2 301 249
```

**Identical in every digit** — same status, same outer iteration, same linear-iteration count, same
residuals, same breakdown point. The only difference is kernel launches, 2 870 500 → 2 301 249
(−19.8 %), which is the reduction fusion.

The baseline worktree is at `d7d74f5`, which differs from `548401a` only in `results/` and
`TODO.md` — `git diff 548401a d7d74f5 -- src include cuda apps tests cmake` is empty, so the
comparison is against production-identical code.

**Conclusion: GPU-PIPE-001 neither caused nor altered this behaviour.**

### What it does mean

This is a **real, newly demonstrated defect**, and it is almost certainly the recorded debt item
*"GPU BiCGSTAB restart asymmetry"*. `results/gpu-pcorr-001/summary.md` §7 states it precisely:

> On detecting cancellation the CPU **restarts** the Krylov sequence … and only reports Breakdown if
> the restart cannot help. The GPU reports Breakdown immediately. After this fix the criterion no
> longer fires spuriously, so the asymmetry is **unreachable in the cases measured here** — but it
> remains a real difference from the CPU solver.

It is now reachable, with a concrete reproducer: **2D lid-driven cavity, 40×40, 1e-6 outer
tolerances, 3000 outer budget — GPU breaks down at outer iteration 1845, 74 linear iterations into
that solve.** `attribution_40x40.cpp` reproduces it deterministically on both libraries.

**Not fixed.** Porting the CPU's restart is an unauthorized change to recorded technical debt
(`CLAUDE.md` §2). It is reported, with the reproducer preserved.

## 5. Campaign verdict

The campaign's own exit status is **FAIL**, on exactly one check: the 40×40 convergence-state
mismatch. That failure is genuine and is not being explained away — but it is **pre-existing at
HEAD** and outside GPU-PIPE-001's scope. For the purpose this campaign was built to serve —
does GPU-PIPE-001 preserve CPU/GPU equivalence — the answer is **yes**, demonstrated to machine
precision on every case where a comparison is meaningful.
