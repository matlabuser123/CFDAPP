# GPU-DISC-001N Phase A — production full-solve audit

Written before the harness. The CPU implementation is the specification, and every threshold below
is **adopted from the repository**, not chosen here.

## 1. Entry point and lifecycle

```text
SIMPLE::solve                     src/pressure_velocity/SIMPLE.cpp:127
  validation / solver construction            :159-208
  initial state                               :210-220
  turbulence model selection                  :229-234
  OuterIterationMonitor                       :244-249
  GpuResidencyManager (read-only mirroring)   :269
  GpuSimpleDiscretization + dispatch          :271-301   (GPU-DISC-001M)
  outer loop                                  :302
  finalization                                :737-743
```

### Initial state

```text
velocity = initialVelocity            (caller-supplied)
pressure = initialPressure            (caller-supplied)
massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries)
```

The initial face flux is **always** the plain linear interpolation, never Rhie–Chow, whatever the
iteration's own predictor scheme resolves to. There is no restart state, no warm-start cache and no
seed: two `solve()` calls with the same arguments are independent and start identically. The only
state carried across outer iterations is `velocity`, `pressure`, `massFlux`, the monitor's residual
history and the turbulence model's internals.

### One outer iteration

Unchanged from GPU-DISC-001L (`../single-iteration/audit.md` §2) and GPU-DISC-001M; not repeated.
The stage order, the warm-started momentum solves, the zero-guess pressure solve and the
initial-residual bookkeeping are all as audited there.

### Termination

```text
Converged      monitor.record(...) returns Converged            -> break
Diverging      divergence detector                              -> break
Stagnated      stagnation detector                              -> break
MaxIterations  the loop runs out                                (finalStatus's initial value)
MomentumFailure / PressureCorrectionFailure   a linear solve did not converge
NonFiniteState a field failed allFinite()
Cancelled      the cancellation callback
```

`result.velocity / pressure / massFlux` are the fields of the last **completed** iteration; a break
never leaves a half-updated state, because the commit at `:685-687` is the last thing an iteration
does.

### What "converged" means, exactly

`OuterIterationMonitor::isConverged` (`SolverRobustness.cpp:361`). With the default
`ConvergenceCriterion::Absolute`:

```text
u          <= velocityTolerance
v          <= velocityTolerance
w          <= velocityTolerance      (3D only, when the sample carries it)
pressure   <= pressureTolerance
continuity <= continuityTolerance
globalImbalance <= continuityTolerance
turbulence <= turbulenceTolerance    (only when the model reports one)
```

where the residuals are the solves' **initial** residuals, not their final ones. **This gate does
not alter these criteria**, and both backends are held to exactly the same ones — they come from
`SIMPLESettings`, which the harness sets identically for both arms.

## 2. Backend dispatch, and what differs between the two arms

| axis | setting | what it moves |
| --- | --- | --- |
| discretization | `SIMPLESettings::enableGpuDiscretization` | the operators (GPU-DISC-001M) |
| momentum solve | `momentumSolver.backend` | that linear solve only |
| pressure solve | `pressureSolver.backend` | that linear solve only |

**The primary comparison in this gate varies only the first.** Both arms use the same CPU linear
solvers, so the *only* difference is where the discretization runs. Since every operator was
qualified **bitwise** and the solves are then identical, the two trajectories should be bitwise
identical at every iteration — a far stronger statement than the acceptance bound, and one that
makes any drift immediately visible.

The GPU **linear solver** is a separate axis, qualified by GPU-PCORR-001, and is exercised
separately in §5.

## 3. Tolerance — adopted from the repository

`tests/solver/simple/test_simple_gpu_solver.cpp` is the project's existing CPU/GPU full-solve
equivalence test:

```text
maxIterations              3000
velocity/pressure/continuity tolerance   1e-6
momentumSolver   maxIterations 500,  abs 1e-10, rel 1e-8
pressureSolver   maxIterations 2000, abs 1e-10, rel 1e-8
asserts status == Converged on BOTH arms
then   maxVelocityError < 1e-6   and   maxPressureError < 1e-6
```

This harness uses **exactly** that configuration and those bounds. Nothing is loosened, and no
"full-solve tolerance" is invented.

### The methodological error that evidence already records

`results/gpu-pipe-001/equivalence/summary.md` §2 records that campaign's own corrected mistake:
comparing the two backends at small outer budgets meant comparing **unconverged transients** against
a bound calibrated on converged solutions, producing nine meaningless failures.

This gate honours that: **a case counts only when both arms report `Converged`**. A case that exits
on `MaxIterations` is reported as such and its field comparison is labelled a transient comparison,
never used as an acceptance result.

## 4. Conservation and physical validation

* `evaluateContinuity` supplies `cellImbalance` and `globalNetFlux`; `globalNetFlux` sums over
  boundary **patches** in patch order, so both arms must use that same function rather than a
  re-derived traversal.
* The interior-face cancellation invariant is the exactly-representable one established in 001H and
  reused since: every interior face visited once as owner and once as neighbour, `(+F)+(−F) == 0.0`.
* The physical check reuses what already exists rather than adding a V&V programme: the lid-driven
  cavity's own converged result, compared between arms, plus the closed-cavity boundary-flux
  property the existing test asserts (`massFlux[boundary] ≈ 0`, bound 1e-8, from
  `test_simple_gpu_solver.cpp:154`).

## 5. The known GPU BiCGSTAB restart asymmetry

Recorded in `TODO.md:283` and attributed in `results/gpu-pipe-001/equivalence/`:

```text
2D cavity 40x40, outer tolerance 1e-6, outer budget 3000
GPU arm: PressureCorrectionFailure, BiCGSTAB breakdown, at outer iteration 1845
CPU arm: runs the full 3000-iteration budget
Present at baseline 548401a; NOT introduced by GPU-PIPE-001.
```

Note carefully: that is the **GPU linear solver**, not the GPU discretization. This gate therefore:

* re-runs the reproducer with the **solver** backend on GPU, and reports whether the classification
  is unchanged from baseline;
* does **not** use it as an acceptance case;
* does **not** fix it;
* treats any **new** breakdown on a previously healthy case as a failed gate — the healthy cases
  here all run CPU solvers on both arms, so a breakdown there would be a genuine regression, not
  this known debt.

## 6. Transfers and timing

`gpuExecutionStats()` already counts H2D/D2H calls and bytes, allocations, kernel launches and
synchronizations, and `resetGpuExecutionStats()` makes per-solve measurement possible. No new
instrumentation is needed. Timing uses wall clock around `solve()`.

These are **observations** feeding GPU-PIPE-001; this gate closes no residency item and optimizes
nothing.
