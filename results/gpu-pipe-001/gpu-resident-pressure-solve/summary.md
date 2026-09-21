# GPU-PIPE-001 — GPU-resident pressure solve

**Result: PASS.** The pressure-correction solve now runs directly against the device-resident
system. Nothing crosses PCIe between the assembly and the corrections except two host *decisions*
totalling twelve bytes.

```text
per pressure solve, steady state, 160x160     BEFORE            AFTER
  assemble -> host           4 D2H / 2,447,368 B        0 / 0
  solver uploads             5 H2D / 1,838,064 B        0 / 0
  solution -> host           1 D2H /   204,800 B        0 / 0
  host decisions                             -        2 D2H / 12 B
  host matrix rebuild (stable_sort)     6.04 ms          none
  steady-state allocations                   0             0

equivalence          BITWISE at 40x40, 80x80, 160x160, and a 60-iteration long run
GPU-DISC gates       15/15
rejection behaviour  7/7 cases match the host path
lifecycle            13/13 properties hold
negative controls    8/8 observable detected, all restored byte-exact
CUDA diagnostics     12/12 clean, non-vacuous, resident path confirmed examined
CPU-only backend     1932/1932, 0 nvcc invocations, no libcudart
full regression      1998/1998
known debt           UNCHANGED, fails at iteration 1845 on both paths
performance          640^2: 9.282 s -> 8.189 s (-11.8%); GPU speedup 9.72x -> 11.40x
```

---

## 1. What the audit found, and why it decided the design

`audit.md` is the Phase A record. One finding shaped everything:

> `toHostSystem()` was never a pure transport. It **drops every entry whose assembled value is
> exactly `0.0`**, so the solver has never seen the device matrix — it has seen a compacted copy.

To be fair to the original code, `toHostSystem`'s own comment already said it drops exact zeros.
What the audit added was **measuring how many, and recognising that it blocks residency** — the
comment records a behaviour, not its consequence for a solver reading the device matrix directly.

Measured, the drop was **exactly 2 entries at 40², 160² and 320² alike** — structural, not
data-dependent: the pinned reference row is assigned `1.0` on its diagonal and exactly `0.0`
everywhere else, and the reference cell is a corner with two internal-face neighbours.

That left two options, and they are not equally defensible:

1. **Build the structure without those entries**, so the device matrix *is* the host matrix.
2. Keep the explicit zeros and rely on `0.0 * x` contributing nothing.

Option 2 is **not provable**: adding `+0.0` to a running sum of exactly `-0.0` yields `+0.0`, so a
signed-zero difference can survive into the result, and `memcmp` sees it. A gate whose standard is
bitwise cannot rest on an argument with a known exception. **Option 1 is what was built.**

It has a second virtue: it left the *existing* host path byte-identical. `toHostSystem()` simply
finds nothing to drop. That was verified before any solver work began — 15/15 gates green with only
the structural change in place.

```text
  grid     full pattern   device nnz   host nnz   dropped
  40²             7,840        7,838      7,838         0      IDENTICAL
  160²          127,360      127,358    127,358         0      IDENTICAL
  320²          510,720      510,718    510,718         0      IDENTICAL
```

## 2. One solver, two entry points

The brief said not to create a second pressure solver. That is satisfied structurally rather than
by discipline.

Every exit path in `GpuBiCGSTAB::solveImpl` — converged, breakdown, iteration limit, non-finite
residual — ended with the same two lines, and **no path modified `x_` after the download**
(verified by reading all 629 lines). So the download was hoisted to a single point and the
algorithm lifted out verbatim into `bicgstabCore`:

```text
  solve(LinearSystem, guess)   upload A, b, x0   ->  core  ->  download x
  solveBiCGSTABResident(...)   adopt A, b, x0    ->  core  ->  x stays resident
```

There is exactly one copy of the BiCGSTAB iteration, one set of breakdown tests, one convergence
test. Neither entry point owns any numerics, so they cannot drift apart. The core's parameters
deliberately keep the trailing underscores of the members they replaced, so the lifted body is the
original text and reviewing the change is mechanical.

**One stated difference between the entry points, and it is not numerical.** `GpuBiCGSTAB::solve()`
increments `gpuLinearSolves` for every call including rejected ones, because the counter lives in
the wrapper outside `solveImpl`. `solveBiCGSTABResident` returns early on a rejected system without
incrementing it — the rejection happens before any solve is attempted. This is recorded rather than
"fixed": it affects telemetry only, on a path production treats as a fatal error, and no gate,
budget or detector in this milestone reads that counter. Changing frozen code to make a defensible
difference symmetrical would have cost a full re-verification for nothing.

`DeviceCsrMatrix::adoptDevice` lets the solver borrow the assembly's own CSR buffers — no copy, no
allocation. The solver reads the exact bytes the assembly kernel wrote.

## 3. The controlled comparison

`comparison/comparison.log`. One binary, one build, one mesh, one set of physics; the arms differ
in exactly one thing — whether the pressure solve is resident. The toggle is the fallback wrapper,
which SIMPLE already declines the resident path for, and which is numerically inert when no solve
fails.

**Both arms are asserted to have taken the path they were meant to take.** A comparison between two
identical paths passes vacuously; `SIMPLEResult::residentPressureSolve` makes that falsifiable, and
negative control `rp8` exists to prove the assertion works.

```text
  case                         Krylov iters   fields   residual histories   reductions
  cavity 2d 40x40   (8 outer)   1522 / 1522   bitwise             bitwise    identical
  cavity 2d 80x80   (6 outer)   2183 / 2183   bitwise             bitwise    identical
  cavity 2d 160x160 (4 outer)   3010 / 3010   bitwise             bitwise    identical
  cavity 2d 40x40  (60 outer)  11115 / 11115  bitwise             bitwise    identical
```

The **60-iteration long run** matters on its own: residency state persists across outer iterations,
so a fault that accumulates — a buffer never reset, a guess quietly inherited — needs iterations to
surface. Bitwise equality after 60 of them is a far stronger statement than after 4.

Identical `reductionGroups` is the direct evidence that both arms did the *same Krylov work*, not
merely that they arrived at the same answer.

## 4. Transfers, measured at the boundary that changed

`transfers/guard.log`. The window is split into three, because a single combined number would hide
the one that matters.

```text
  window      H2D    D2H    D2H bytes   note
  assemble      0      0            0   was 4 calls / 2,447,368 bytes at 160x160
  solve         0   3368      4712808   3367 of those are Krylov reductions
  carry         0      1            4   the p' finiteness guard
  ------------------------------------------------------------------
  non-reduction D2H calls  2   (budget 2)
  allocations              0   (budget 0)
```

**Krylov reduction traffic is subtracted, not budgeted around.** BiCGSTAB reduces every iteration
and each reduction is a host round trip by nature — 3,367 of them in an 842-iteration solve. That
is the reduction pattern, not the round trip this gate removes, and the audit put it explicitly out
of scope. `GPUExecutionStats::reductionGroups` counts exactly those, so the budget can say what it
means: *besides reducing, the solve brings back one thing.*

Whole-solve figures, from the comparison:

```text
  grid       H2D calls/iter   H2D bytes/iter   D2H bytes/iter   allocations   syncs
  40²         50.4 vs 55.6     -10.2%           -25.2%          375 = 375    equal
  80²         62.8 vs 68.2      -8.6%           -23.4%          375 = 375    equal
  160²        87.8 vs 93.2      -6.6%           -19.6%          375 = 375    equal
  40² long    18.0 vs 23.0     -23.4%           -25.6%          373 < 375    equal
```

Explicit synchronizations are identical, and the resident path's *blocking* D2H copies per solve
fell from five to two — so host waiting went down, not up.

The measured saving reconciles with the audit's prediction to the byte, including one term the
audit did not separate out: the resident path also eliminates the **one-time** pressure-matrix
structure upload (1,223,672 bytes at 160² — `8*(nc+1)` row offsets plus `8*nnz` column indices),
which appears amortised across the run.

## 5. Failure behaviour

`comparison/rejection.log`. The resident entry point had to re-implement three rejections on the
device. Re-implementing a rejection is exactly where a performance change quietly becomes a
behavioural one, so every case is run through **both** entry points and required to agree.

```text
  healthy, Jacobi                  host Converged        resident Converged       match
  healthy, no preconditioner       host Converged        resident Converged       match
  zero diagonal                    host InvalidSystem    resident InvalidSystem   match
  near-zero diagonal               host InvalidSystem    resident InvalidSystem   match
  NaN diagonal                     host refuses to build resident NonFiniteInput  both reject
  infinite off-diagonal            host refuses to build resident NonFiniteInput  both reject
  NaN right-hand side              host NonFiniteInput   resident NonFiniteInput  match
```

Writing this probe surfaced something worth recording: **`SparseMatrix` validates finiteness in its
constructor, but `Vector` does not.** A non-finite matrix is therefore refused before any host
solver sees it, which makes `GpuBiCGSTAB::solveImpl`'s own `A.allFinite()` guard unreachable through
normal construction — while a non-finite RHS reaches the solver on both paths. The resident path
adopts raw device buffers and has no constructor to refuse anything, so its device-side check is the
*only* thing standing between a poisoned system and a silently garbage solve. The first version of
this probe assumed both were refused at construction; the host cheerfully built the NaN RHS, and the
probe was corrected to match what the code does.

## 6. Lifecycle

`lifecycle/probe.log`. Persistent state is where "works once" and "works every time" come apart.

```text
PASS  three consecutive resident solves all converge
PASS  solves after the first allocate nothing (0, 0)
PASS  repeating an identical solve reproduces it exactly -- no state leaks between solves
PASS  no host solution is ever produced
PASS  the larger mesh converges after re-prepare; the workspace grew with it
PASS  both meshes converge across a shrink
PASS  a reused facade solves the SAME problem as a fresh one -- iterations, initial and final
      residual all bitwise
PASS  a restart reproduces the FIRST solve exactly (bitwise initial residual, same iterations)
```

The shrink case is the pointed one: `DeviceBuffer` never shrinks, so capacity from the larger mesh
is still there and a length bug would read it **without any allocation or transfer to give it
away**. The authority is a fresh facade that only ever saw the small mesh.

## 7. Negative controls

`negative-controls/driver.log`. Run through the GPU-DISC-001P engine, so the Phase D discipline
(record sha256 → one mutation → rebuild → narrowest detector → restore → verify sha → rebuild →
re-pass) is the one already qualified rather than a second implementation.

**Before any control ran, the detectors were required to pass on the clean tree.** A control suite
whose detectors do not pass unmutated proves nothing about the mutations — and this mattered: the
first attempt aborted with *"harness sparsity failed to compile — nothing was mutated"*, because a
Python-via-Bash edit had written literal newlines into a string literal. The engine refused to
mutate anything, which is exactly the behaviour that makes it trustworthy.

| control | defect | detector | result |
| --- | --- | --- | --- |
| `rp1` | pinned row keeps its explicit zeros | **sparsity** | DETECTED |
| `rp2` | Jacobi stores the diagonal, not its reciprocal | comparison | DETECTED |
| `rp3` | initial guess is all-ones, not all-zeros | comparison | DETECTED |
| `rp4` | the RHS never reaches the workspace | comparison | DETECTED |
| `rp5` | p' never reaches the corrections | comparison | DETECTED |
| `rp6` | the diagonal rejection is removed | **rejection** | DETECTED |
| `rp7` | the whole system is downloaded anyway | **transfer guard** | DETECTED |
| `rp8` | the resident path is silently disabled | comparison (non-vacuity) | DETECTED |

**8/8 observable detected, every one restored byte-exact with the baseline re-passing.** All four
touched sources hash to their recorded baselines afterwards.

Three of these are the reason this gate needed detectors beyond a numerical comparison:

* **`rp7` changes no number.** Downloading a system that is already resident produces bit-identical
  results; every equivalence gate in this project passes it. Only a transfer count can tell, which
  is what makes "the pressure solve is resident" a falsifiable claim rather than an assertion. This
  is `pf7`'s lesson from persistent fields, applied to a different boundary.
* **`rp8` also changes no number** — it just reverts to the host path. The comparison catches it
  *only* through its non-vacuity assertion. That control exists to prove the assertion works, since
  a vacuity guard nobody tests is itself untested.
* **`rp1` is the structural one.** Its extra entries are explicit zeros, and adding `0.0` to a sum
  almost always changes nothing — so the numerical comparison would likely have passed it. It is
  caught by asserting the *structure* directly. That is precisely why the design reproduces the host
  sparsity pattern instead of relying on zeros being harmless.

## 8. CUDA diagnostics

`cuda-diagnostics/`. All four tools across 2D, 3D and the GPU-solver mode, on the resident path:

```text
memcheck / initcheck / synccheck / racecheck  x  cavity2d / case3d / gpusolver
12/12 runs, 0 errors, 0 hazards, every one non-vacuous
kernel counts 1080 / 3059 / 38282   (up from 1080 / 3059 / 38032 -- the new
                                     input-check and Jacobi-diagonal kernels)
```

**Non-vacuity for THIS gate specifically.** Only the `gpusolver` mode reaches the resident path;
`cavity2d` and `case3d` use a CPU linear solver and would have exercised none of the new kernels.
A clean sanitizer report on workloads that never ran the code under test proves nothing, so the
diagnostic workload now prints `gpuDisc=2` when a solve took the resident path, the runner records
it per row, and the phase **fails** if no diagnosed workload did. All three `gpusolver` rows read
`resident`.

Clean on exactly the categories this change threatens: adopted pointers whose storage another
object owns, buffers reused across solves, and the gate's own device-side atomics.

## 9. CPU backend

`cpu-backend/run.log`. A from-scratch CUDA-disabled configure, build and full test suite:

```text
nvcc invocations in build.ninja   0
cfdapp links libcudart            no
tests                             1932/1932 passed, 0 failed
```

The CPU-only stub gained four methods. GPU-DISC-001R established that this file is compiled by no
routine gate -- a latent non-compiling defect lived there for five milestones -- so building it is
not a formality. Three of the four throw rather than fabricate a result: a fake "successful" solve
would be checked by `pResult.converged()` and pass. `residentSolveBytes()` is the exception and
returns 0, because it is an observation and zero is the true answer.

## 10. Performance

`performance/cavity.log`, against the GPU-DISC-001Q qualified baseline.

```text
grid       before    after    change    spread     speedup vs CPU
20x20       8.239    8.382    +1.7%      4.7%       0.04x
40x40      14.201   13.968    -1.6%      3.3%       0.10x
80x80      17.319   17.510    +1.1%      3.5%       0.37x
160x160     9.666    9.492    -1.8%      4.1%       1.20x
320x320     7.064    7.479    +5.9%      6.6%       3.86x
640x640     9.282    8.189   -11.8%      5.4%      11.40x   <- outside the spread
```

**One grid improves measurably, and it is the one the arithmetic predicts.** The removed cost is
the assemble download plus the host `stable_sort`, both `O(nnz)`; at 640² that is ~2 million
entries, at 160² it is 127 thousand. Everything below 640² sits inside the measured run-to-run
spread and is reported as noise rather than quoted as a win.

The saving lands exactly where the audit said it would -- the discretization stage, which is what
the pressure-correction assembly is counted in:

```text
640x640 gpu-disc, discretization stage   3.454 s -> 2.397 s   (-30.6%)
```

GPU speedup at 640² rises from **9.72× to 11.40×**, and all 12 bitwise equivalence pairs across the
six grids pass, `gpu-pipe vs gpu-disc` among them -- the pair that compares the host round-trip
solver against the resident one on a converged production solve.

## 11. Known technical debt — unchanged

`regression/known-debt.log`. The recorded GPU BiCGSTAB restart asymmetry (TODO.md, baseline
`548401a`). This was the sharpest risk in the gate: the reproducer's `gpu-disc` arm now runs its
pressure solve **device-resident**, so a resident path that quietly altered breakdown behaviour
would surface here and nowhere else.

```text
  cpu      (CPU solver, CPU disc)   status=MaxIterations              iterations=3000
  gpu-pipe (GPU solver, CPU disc)   status=PressureCorrectionFailure  iterations=1845
  gpu-disc (GPU solver, GPU disc)   status=PressureCorrectionFailure  iterations=1845

  gpu-disc status matches gpu-pipe:            yes
  gpu-disc iterations match gpu-pipe:          yes (1845 vs 1845)
  reproducer actually triggered on gpu-pipe:   yes    <- non-vacuity
  KNOWN DEBT: UNCHANGED
```

The resident path fails at **exactly the same outer iteration** as the host round trip. The
non-vacuity line matters: GPU-DISC-001R found this probe passing vacuously once, printing
"UNCHANGED" while the failure had not been reproduced at all. It now asserts the reproducer fired
before it is allowed to conclude anything.

Not authorized to fix, not fixed, and still failing exactly as recorded.

## 12. Full regression

`regression/ctest.log`

```text
ctest --test-dir build/final --output-on-failure
  100% tests passed, 0 tests failed out of 1998   (2043 discovered, 45 disabled)
  Total Test time (real) = 1321.40 s
  `ninja: no work to do` after ctest
```

Counts match the GPU-DISC-001R and persistent-fields baselines exactly. The 15 GPU-DISC
differential gates are green on the same binaries, and were additionally run **twice during
development** -- once with only the structural change, isolating it, and once with the full wiring.

## 12. Defects found in my own instruments

Three, all caught by measurement rather than review, and all fixed before the gate closed:

* **An inverted finiteness check.** `readNonFiniteCounter` already returns *all finite*, not *any
  non-finite*. Negating it made every resident solve report `NonFiniteState` after one outer
  iteration. Caught immediately by the comparison, which refused to pass.
* **A per-solve counter allocation and five separate 4-byte reads.** Allocations rose 375 → 406 over
  8 outer iterations, and **D2H call count did not fall at all** even as D2H bytes fell 25% — the
  five new reads exactly replaced the five removed transfers. The counter moved into the persistent
  workspace and all four device checks were fused into one 8-byte read.
* **A transfer budget that would have failed a correct implementation.** The first guard budgeted
  the whole solve window at 2 D2H, which BiCGSTAB's per-iteration reductions can never satisfy. The
  budget was wrong, not the code; it was split per window and reductions subtracted explicitly.

## 13. Files changed

```text
new     include/cfd/gpu/GpuResidentSolve.hpp          workspace + resident entry point
new     cuda/kernels/GpuResidentSolveKernel.cu        device checks, Jacobi diagonal (-fmad=false)
edit    include/cfd/gpu/DeviceCsrMatrix.hpp           adoptDevice: borrow resident buffers
edit    include/cfd/gpu/DevicePressureCorrection.hpp  per-referenceCell CSR structure
edit    cuda/kernels/DevicePressureCorrectionPlan.cpp ensureStructure
edit    cuda/kernels/DevicePressureCorrectionKernel.cu call ensureStructure before assembling
edit    cuda/kernels/GpuLinearSolverCuda.cpp          core extraction + resident entry point
edit    include/cfd/gpu/GpuSimpleDiscretization.hpp   resident assemble/solve/guard
edit    cuda/kernels/GpuSimpleDiscretizationCuda.cpp  implementations
edit    src/gpu/GpuSimpleDiscretization.cpp           CPU-only parity
edit    include/cfd/pressure_velocity/SIMPLEResult.hpp residentPressureSolve
edit    src/pressure_velocity/SIMPLE.cpp              GPU branch only
edit    cuda/CMakeLists.txt                           register the kernel, -fmad=false (14 files)
```

`GpuCG` was deliberately not touched. The CPU branch of `SIMPLE::solve` is untouched.

## 14. When the resident path is declined

Engaged only where it is exactly equivalent to the round trip it replaces. Each condition is a real
difference, not caution:

| condition | why |
| --- | --- |
| GPU discretization active | the device system only exists on that path |
| pressure backend is GPU | a CPU solver must receive a host system — the `disc-only` benchmark arm |
| solver type is BiCGSTAB | only BiCGSTAB has a resident entry point; `GpuCG` was not touched |
| linear-solver fallback off | the wrapper re-solves on the CPU when a GPU solve fails; bypassing it would change failure behaviour, **including the known BiCGSTAB breakdown** |
| residency mirror off | `gpuResidency.syncMatrix()` reads a host matrix the resident path never builds |
| `nonOrthogonalCorrections <= 1` | SIMPLE already declines the whole GPU path above that |

Every other configuration runs exactly what it ran before.

## 15. What is NOT done

* **`GPU-resident SIMPLE loop` — not started.** The momentum solves still round-trip by the same
  mechanism this gate removed for pressure; that belongs to the next item.
* **`Final CPU/GPU equivalence` — not started.**
* **Krylov reduction traffic — untouched**, and out of scope by the audit.
* **The known GPU BiCGSTAB restart asymmetry — not fixed**, and required to stay reproducible.
* No numerics changed; no tolerance or convergence criterion moved.
* Nothing committed or pushed.
