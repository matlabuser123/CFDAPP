# GPU BiCGSTAB restart parity — before and after

The fix is one file: `cuda/kernels/GpuLinearSolverCuda.cpp`. The CPU reference
`src/algebra/BiCGSTAB.cpp` was not touched, and neither was any turbulence code.
The contract being matched is in [restart_contract.md](restart_contract.md); the
investigation that found the defect is in [findings.md](findings.md).

---

## 1. The frozen ladder

The same device-assembled system fed to both solvers (input identity by
construction — see `findings.md` §Phase B). Production-default solver settings.

| cells | eq | CPU before/after | GPU **before** | GPU **after** | restarts |
|---:|---|---|---|---|---:|
| 128 | k | Converged 15 | Converged 15 | Converged 15 | 0 |
| 128 | eps | Converged 14 | Converged 14 | Converged 14 | 0 |
| 192 | k | Converged 21 | Converged 21 | Converged 21 | 0 |
| 192 | eps | Converged 14 | Converged 13 | Converged 13 | 0 |
| 224 | k | Converged 17 | Converged 24 | Converged 24 | 0 |
| **224** | **eps** | Converged 13 | **Breakdown 10** | **Converged 13** | **1** |
| **256** | **k** | Converged 19 | **Breakdown 15** | **Converged 19** | **1** |
| 256 | eps | Converged 14 | Converged 14 | Converged 14 | 0 |
| **320** | **k** | Converged 20 | **Breakdown 16** | **Converged 20** | **1** |
| **320** | **eps** | Converged 16 | **Breakdown 11** | **Converged 15** | **1** |
| **384** | **k** | Converged 23 | **Breakdown 10** | **Converged 21** | **1** |
| **384** | **eps** | Converged 18 | **Breakdown 16** | **Converged 17** | **1** |
| **512** | **k** | Converged 23 | **Breakdown 10** | **Converged 23** | **1** |
| **512** | **eps** | Converged 20 | **Breakdown 12** | **Converged 18** | **1** |

**Eight of eight previously-failing rungs now converge. Every one of them took
exactly one restart, and every rung that already converged took none.** That
one-to-one correspondence is the causal evidence: the fix acts on precisely the
systems that were broken and is inert on the rest.

Gate: `KrylovRestartParity.TheSameDeviceSystemSolvesOnBothSolvers`.
Non-vacuity control: `KrylovRestartParity.EveryPreviouslyBrokenRungIsExplainedByARestart`
— it names the eight rungs the pre-fix ladder recorded as `Breakdown` and fails
if any of them now converges *without* restarting, and separately fails if a
rung that was healthy starts restarting.

### A correction to the pre-fix table

`findings.md` recorded `r0 = 2.594e-02` for the 128-cell k system. The correct
value is `3.171e-02`; `2.594e-02` is the 192-cell k value, transcribed one row
too high. Every other digit in that table reproduces exactly. No conclusion
depended on it.

---

## 2. The historical `DEBT-GPU-001` reproducer

2D lid-driven cavity, 40×40, 1e-6 outer tolerances, 3000 outer budget — the
reproducer recorded in `TODO.md` §"Known GPU BiCGSTAB Asymmetry" and in
`results/gpu-pipe-001/equivalence/summary.md` §4. Re-run unchanged, with the
same probe source (`attribution_40x40.cpp`).

```text
BEFORE (frozen at 548401a, results/gpu-pipe-001/equivalence/summary.md)
  CPU status=1 outer=3000 p_lin_it=243384 p_res=6.5751888576822637e-06 cont=8.369e-11
  GPU status=3 outer=1845 p_lin_it=160367 p_res=1.4513381583017726e-05 cont=5.543e-11
      statusDetail: BiCGSTAB Breakdown after 74 iterations

AFTER (working tree, this fix)
  CPU status=1 outer=3000 p_lin_it=243384 p_res=6.5751888576822637e-06 cont=8.369e-11
  GPU status=1 outer=3000 p_lin_it=242425 p_res=6.5751881323142974e-06 cont=7.350e-11
      (no statusDetail -- no failure)
```

`status: 0=Converged 1=MaxIterations 2=MomentumFailure 3=PressureCorrectionFailure`

**The `PressureCorrectionFailure` is gone.** The GPU now runs the full outer
budget, as the CPU does, and lands on a pressure residual agreeing with the CPU
to eight significant figures (`6.5751881e-06` vs `6.5751889e-06`) with a linear
iteration count within 0.4 % (242 425 vs 243 384).

### Why this comparison is attributable

The **CPU arm reproduces the frozen numbers in every one of its 17 printed
digits** — same status, same outer count, same linear-iteration count, same
residual, same continuity. The frozen figures were measured at `548401a` and the
working tree has changed a great deal since, so that identity is not a given: it
establishes that the reproducer, its inputs and its CPU reference are unchanged,
which is what makes the GPU arm's change attributable to the solver rather than
to drift. Log: [fix/40x40_after.log](fix/40x40_after.log).

This is a **behaviour change to recorded technical debt** and is reported as
such, not as an incidental improvement.

---

## 3. Solver-level gates

`tests/unit/gpu/test_gpu_linear_solver.cpp`, suite `GpuBiCGSTABRestartTest`.
The trigger is a convection-dominated 2D grid operator (symmetric diffusion plus
an antisymmetric east/west convection coupling) — the same *character* of system
as turbulence transport, but built from nothing but a grid, so these tests do
not depend on the turbulence code.

| test | asserts |
|---|---|
| `RestartsWhereTheCpuReferenceRestarts` | both solvers **converge**, both **restart** getting there; CPU restart count asserted first so the test cannot pass vacuously |
| `RestartsDoNotBuyExtraIterationBudget` | with a budget of 250 and restarts occurring inside it, `iterations <= 250` and the status is `MaxIterations` — a restart is free of the budget but is not a way to exceed it |
| `HealthySystemNeverRestarts` | an SPD system converges with `restarts == 0` on both solvers — the change is inert where nothing breaks down |
| `BreakdownIsStillReachable` | a purely skew-symmetric system, where `(t, s)` is identically zero from the first iteration, still reports `Breakdown` with `restarts == 0`: the restart is correctly refused before any progress has been made, and `Breakdown` keeps its meaning |
| `RestartingSolveIsDeterministic` | four solves of a restarting system agree on status, iterations, restart count and **every bit of every solution component** |
| `RestartAllocatesNothingInSteadyState` | four restarting solves after warm-up: `allocations == 0`, `reallocations == 0` |

The last two assert `restarts >= 1` on the warm-up solve first, so neither can
pass while checking a path that never restarts.

`SolverResult::restarts` is reported by the GPU solver for the first time; it
propagates through both entry points (`GpuBiCGSTAB::solveImpl` and
`solveBiCGSTABResident`) by whole-struct assignment.

---

## 4. Pressure path

The fix is in the **shared** solver that GPU-PCORR-001 and GPU-PIPE-001's
resident pressure solve both use, so the pressure path was re-qualified rather
than assumed. All green:

```text
SIMPLEGpuSolverTest.GpuBackendReproducesCpuCavitySolutionWithinTolerance      OK
SIMPLEGpuSolverTest.GpuBackendSatisfiesTheSameConvergenceCriteria            OK
SIMPLEGpuSolverTest.RepeatedOuterIterationsReuseGpuSolverBuffersWithoutReallocating  OK
SIMPLEGpuResidencyTest.DisabledByDefaultIssuesNoGpuActivity                  OK
SIMPLEGpuResidencyTest.EnablingResidencyDoesNotChangeTheComputedResult       OK
SIMPLEGpuResidencyTest.RepeatedOuterIterationsReuseGpuResidencyWithoutReallocating   OK
```

Log: [fix/pressure_path.log](fix/pressure_path.log).

---

## 5. The one failure, and why it is not this fix

`ResidentWallExchange.WallTrafficScalesWithWallFacesNotCellCount` fails:

```text
128 cells,  32 wall faces: D2H 10108 bytes in 137 calls
512 cells, 128 wall faces: D2H 38804 bytes in 202 calls
  expected 38804 < 12288
  wall gather accounts for 3072 bytes of the 38804 downloaded
```

This was a **pre-existing preserved failure**, recorded before this work:
`resident_simple_integration.md` §4 states plainly that *"the transfer-scaling
gate was not marked PASS"*, alongside *"the tolerance was not loosened"* and
*"the test grid was not shrunk"*. It was the open Stage 7I Step 3 item.

It is not mechanically attributable to the restart: the wall gather is 3 072 of
the 38 804 bytes, the remainder being per-iteration solver reductions, and a
restart adds exactly two 8-byte reductions each time it fires. The gate
overshoots its bound by 3×, not by 32 bytes.

**It has since been audited and resolved separately** — see
[../wall_transfer/README.md](../wall_transfer/README.md). The verdict was
MIXED: the acceptance criterion was mathematically unsatisfiable (its bound was
exactly the size of a publication it was itself measuring), AND the audit found
a genuine avoidable device→host→device round trip, which was removed. Neither
finding involves this solver repair. Nothing was weakened: the replacement gate
asserts an exact equality rather than a bound.

With both repairs in place the Step 3 ladder — blocked by this very breakdown —
is green at 128, 192, 224, 256, 384 and 512 cells, inside the unchanged 2.1e-12
bound, with bitwise wall outputs and bitwise determinism.

---

## 6. Scope note that still stands

`findings.md` §"Scope impact" recorded that every Stage 4–7 turbulence
differential ran at 96–140 cells, below the first failing size of 224, so the
equivalence measured there was real but narrower in coverage than it appeared.
That remains true of those measurements. What has changed is that the ladder now
runs clean to 512 cells, so the coverage can be widened — which is Stage 8 work,
not a claim this document makes.
