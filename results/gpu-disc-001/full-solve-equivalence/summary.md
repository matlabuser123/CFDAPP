# GPU-DISC-001N — full-solve CPU/GPU equivalence

**Result: PASS.** Complete production solves run to convergence on both arms and are **bitwise
identical at every outer iteration** — 58 cases, 23,416 values, and a maximum discrepancy of
**exactly 0** across every residual history and every final field, including three 3000-iteration
runs.

```text
cases                      11   2D and 3D, cavity and inlet/outlet, Upwind / QUICK /
                                LinearUpwind / Central, Cartesian and warped
both arms converged         8   (3 exit on MaxIterations -- reported as transients,
                                not used as acceptance results)
values compared        23,416
bitwise-identical      23,416   (100%)
max discrepancy over ALL histories and fields   0
longest solve            3000 outer iterations

observable controls       8/8 detected, ALL at the expected iteration
documented null             0
compute-sanitizer         4/4 clean, non-vacuous, on a 30-iteration production solve
GPU-DISC gates          15/15 green
full regression    1998/1998 passed, 0 failed, 1295.8 s (45 disabled of 2043 registered)
                   `ninja: no work to do` BEFORE and AFTER ctest; library and
                   source sha256 identical on both sides
```

## 1. Entry point and configuration identity

`SIMPLE::solve` (`SIMPLE.cpp:127`). Both arms are launched through the production solver from
identical initial state; the **only** difference is `SIMPLESettings::enableGpuDiscretization`. Mesh,
fluid properties, initial velocity/pressure, the initial face flux (always the linear one),
boundary conditions, convection scheme, relaxation factors, solver tolerances, outer budget,
reference cell and linear-solver initial guesses are shared by construction — the harness builds one
`Case` and flips one flag. There is no restart state, no warm-start cache and no seed.

Configuration and bounds are **adopted from the repository's own** CPU/GPU full-solve test,
`tests/solver/simple/test_simple_gpu_solver.cpp`:

```text
3000 outer iterations · 1e-6 outer tolerances · 1e-10/1e-8 linear tolerances
both arms must report Converged · maxVelocityError < 1e-6 · maxPressureError < 1e-6
```

Nothing was loosened and no "full-solve tolerance" was invented.

## 2. Cases and convergence

| case | scheme | CPU | GPU | outer CPU/GPU | final residuals (u, p, continuity) |
| --- | --- | --- | --- | --- | --- |
| cavity 2d 8 | Upwind | Converged | Converged | **1093 / 1093** | 3.45e-08, 9.98e-07, 4.31e-11 |
| channel 2d 12 (open) | Upwind | Converged | Converged | **2938 / 2938** | 5.52e-09, 1.00e-06, 6.93e-12 |
| cavity 3d 4 | Upwind | Converged | Converged | **23 / 23** | 1.07e-07, 8.73e-07, 7.49e-11 |
| cavity 2d 20 | Upwind | MaxIterations | MaxIterations | 3000 / 3000 | 4.76e-08, 1.45e-06, 5.11e-11 |
| cavity 2d 40 | Upwind | MaxIterations | MaxIterations | 3000 / 3000 | 1.80e-07, 6.58e-06, 8.37e-11 |
| cavity 2d 16 | **QUICK** | Converged | Converged | **2682 / 2682** | 3.54e-08, 9.99e-07, 9.11e-11 |
| cavity 2d 16 | **LinearUpwind** | Converged | Converged | **2683 / 2683** | 3.65e-08, 9.99e-07, 8.26e-11 |
| cavity 2d 16 **warped** | Upwind | Converged | Converged | **2630 / 2630** | 3.36e-08, 9.98e-07, 7.27e-11 |
| channel 2d 16 (open) | **Central** | MaxIterations | MaxIterations | 3000 / 3000 | 1.57e-07, 2.51e-05, 5.73e-12 |
| cavity 3d 6 | Upwind | Converged | Converged | **25 / 25** | 2.21e-07, 8.27e-07, 9.77e-11 |
| channel 3d 6 (open) | Upwind | Converged | Converged | **26 / 26** | 5.14e-07, 9.52e-07, 5.83e-12 |

**Outer iteration counts are identical on every case**, which follows from the arms being bitwise:
a differing count would itself be the divergence signal, so it is required rather than excused.

The three `MaxIterations` cases are reported as such and their field comparisons are labelled
**TRANSIENT — not an acceptance result**, honouring the correction
`results/gpu-pipe-001/equivalence/summary.md` §2 records against itself. They still contribute
strong evidence, because being bitwise for 3000 coupled iterations is exactly the long-run drift
test; they simply do not count toward the converged-field acceptance criterion, which the eight
converged cases satisfy.

The case matrix covers every axis the brief names: open boundaries with net through-flow and a
suppressed reference pin, 3D with U/V/W, three non-Upwind schemes, and a non-orthogonal mesh.

## 3. Iteration-by-iteration comparison

Not just final fields — two layers:

* **Residual history (`H`)**: every outer iteration's u, v, w, pressure and continuity residuals,
  both arms. 11 cases, up to 3000 iterations each. **Max history discrepancy 0, no first divergent
  iteration on any case.**
* **Per-iteration fields (`S`)**: on the small 2D and 3D cases, re-solving with
  `maxIterations = k` for k = 1…40 and comparing the complete pressure, velocity and face-flux
  fields at each k. **Max 0, no divergent iteration.**

So the trajectories are identical step for step, not merely convergent to the same answer. There is
no drift to quantify.

### First-divergence status

**None, on any case, in any layer.** When a defect is injected, the same layers name the exact
iteration — see §6.

## 4. Final fields and conservation

Every case: `pressure Linf = 0, L2 = 0`, `u/v/w Linf = 0`, `flux Linf = 0, L2 = 0`, and 100% of
values bitwise (e.g. cavity 2d 40: 1600/1600 pressure, 3280/3280 flux). The adopted 1e-6 bounds are
met with total margin.

Conservation, checked on both arms:

* **no NaN, no Inf** in any committed field;
* **interior-face cancellation** — every interior face `(+F) + (−F) == 0.0` exactly;
* **per-cell continuity** — max cell imbalance 2.4e-11 … 5.9e-10, identical between arms
  (`|dImbalance| = 0`);
* **global mass balance** — `globalNetFlux` identical between arms, computed by the production
  `evaluateContinuity` on both;
* **boundary accounting** — a closed cavity carries `maxBoundaryFlux = 0`, satisfying the existing
  test's own 1e-8 property; the open channel carries a genuine through-flow (0.148), as it must.

### Physical validation preserved

The cavity cases converge to the same solution the CPU produces, bitwise — so every physical
property the existing validation asserts of the CPU result holds identically of the GPU result.
That is the strongest possible preservation statement and needs no new V&V programme: there is no
numerical difference for a physical check to detect.

## 5. Determinism and long-run stability

**Determinism**: each arm run twice on all 11 cases — fields, outer iteration count and full
residual history bitwise identical. 11/11.

**Long-run**: 8 cases exceed 50 outer iterations, three run the full 3000. The maximum discrepancy
tracked over the *entire* solve — not just at convergence — is **0**. No accumulating drift, no
stale device state, no delayed NaN.

## 6. Negative controls — 8/8 detected, 8/8 at the expected iteration

`negative-controls/`. These break equivalence **across iterations**, which is what this gate adds
over 001L and 001M.

| control | mutation | expected | observed |
| --- | --- | --- | --- |
| N1 | every 5th iteration commits the previous mass flux | iteration 5 | **5** |
| N2 | p' upload skipped on the 3rd iteration | iteration 3 | **3** |
| N3 | the U predictor uploaded into the V slot | iteration 1 | **1** |
| N4 | response coefficients computed on iteration 1 only, then reused | iteration 2 | **2** |
| N5 | pressure update skipped on every 7th iteration of the GPU arm | iteration 7 | **7** |
| N6 | pressure correction assembled from the previous iteration's flux | iteration 1 | **1** |
| N7 | one iteration forced through a CPU/GPU mixed path | explicit error | **explicit error** |
| N8 | the V predictor never uploaded | explicit error | **explicit error** |

All eight restored to a matching sha256 and re-passed.

### What the controls found — three errors of mine, and one production defect

**A missing residency guard — a real defect in code I wrote.** N3 and N8 skip an upload, and the
facade reached a kernel with an unsized buffer, failing as
`cudaMalloc(DeviceBuffer) failed: an illegal memory access was encountered` from inside CUDA. Every
other device entry point in this layer validates its sizes; `GpuSimpleDiscretization` did not.
Added `requireResident`, so a skipped stage now fails explicitly and names the missing state. The
brief's "keep failure handling explicit" is exactly this.

**Process-wide `static` counters.** My first "every Nth iteration" controls used function-local
statics, but the harness runs each case six-plus times per process, so they fired at unpredictable
iterations — three undetected, three crashing. Replaced with SIMPLE's own `outerIteration`
(`SIMPLE.cpp:290`), which resets per solve. The lesson is general: a control that depends on
process state is not testing what its name says.

**A mutation that changed both arms equally.** N5 originally skipped the pressure update in shared
code, so both arms moved identically and the differential harness correctly saw nothing. That is
not a gap — it is a mutation that is not a backend-routing defect. Gated on `useGpuDiscretization`
it became one, and is now detected at iteration 7 exactly.

**Two controls are detected by an explicit error rather than a numbered iteration, and that is the
design succeeding.** On the device path `dU`/`dV` are deliberately left empty — nothing there reads
a host copy — so forcing one iteration through a CPU/GPU mixed path throws
`correctVelocity: field size does not match mesh cell count` instead of quietly computing something
wrong. A half-GPU iteration cannot run; it can only fail. That is the all-or-nothing rule from
GPU-DISC-001M holding under an adversarial mutation.

## 7. The known GPU BiCGSTAB restart asymmetry

Re-run as recorded, with the **linear solver** backend on GPU (a different axis from the GPU
discretization this gate varies):

```text
2D cavity 40x40, outer tolerance 1e-6, outer budget 3000
GPU-solver arm: PressureCorrectionFailure at outer iteration 1845
CPU-solver arm: MaxIterations at 3000
```

**Classification UNCHANGED from the baseline** recorded at `TODO.md:283` and attributed in
`results/gpu-pipe-001/equivalence/`. It remains known technical debt and was **not** fixed here. It
is not used as an acceptance case.

No new breakdown occurred on any healthy case: all eleven qualification cases run CPU solvers on
both arms and none of them failed, so nothing has spread.

## 8. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck: **0 errors**; racecheck: **0 hazards**. Run
on `--controls`, a 30-outer-iteration production cavity solve, so the sanitizers see the whole
chain — assembly, solves, response coefficients, predicted flux, pressure assembly, pressure
update, both corrections — across **coupled iterations**, not one pass and not isolated kernels.
Each log was verified to contain `FULL SOLVE EQUIVALENCE: PASS`, so none is vacuous.

## 9. Transfer and residency observation

Recorded for GPU-PIPE-001; **no residency item is closed here.**

```text
case            gpu-arm totals                                    per iteration
cavity 2d 8     h2d=13407/7.7MB   d2h=17488/21.4MB  kern=27325    h2d=12 d2h=16 kern=25
channel 2d 12   h2d=35547/45.2MB  d2h=47008/132.6MB kern=73450    h2d=12 d2h=16 kern=25
cavity 2d 40    h2d=36291/507.6MB d2h=48000/1553MB  kern=147000   h2d=12 d2h=16 kern=49
cavity 3d 4     h2d=590/0.65MB    d2h=460/0.69MB    kern=897      h2d=25 d2h=20 kern=39
```

`sync = 0` everywhere; `alloc = 345` regardless of iteration count, confirming **zero steady-state
allocation**. The CPU arm issues no device activity at all.

The per-iteration transfers are the forced ones: the assembled systems down to the host solver and
the solutions back up, plus the two corrected fields SIMPLE commits as host state. Removing them is
GPU-PIPE-001's gate.

## 10. Performance observation

Recorded to detect pathological overhead, not to qualify performance.

```text
case            cpu       gpu       outer   cpu/iter    gpu/iter    ratio
cavity 2d 8     0.269s    3.245s    1093    2.46e-4     2.97e-3     12.1
channel 2d 12   1.764s    8.941s    2938    6.00e-4     3.04e-3      5.07
cavity 2d 16    2.513s    9.555s    2682    9.37e-4     3.56e-3      3.80
cavity 2d 20    5.349s   13.243s    3000    1.78e-3     4.41e-3      2.48
cavity 2d 40   21.615s   26.728s    3000    7.21e-3     8.91e-3      1.24
```

**The GPU arm is currently slower**, and the reason is structural rather than pathological: every
assembled system round-trips to the host for the CPU linear solver, and these meshes are 64–1600
cells, where per-kernel launch overhead dominates real work. The ratio falls monotonically with
size (12.1 → 1.24) which is the expected trend, and the remaining gap is exactly what GPU-PIPE-001's
residency work exists to close. **Nothing was optimized here**; correctness was the objective and
the brief says not to chase a speed target in this gate.

## 11. Files changed

```text
modified
  cuda/kernels/GpuSimpleDiscretizationCuda.cpp   requireResident guards (see section 6)
```

No other production change. The harness, controls and scripts live under `tools/`.

## 12. Gate chain and regression

`regression/all_gates.log`, `regression/regression.log`, `regression/regression_freshness.log`

```text
libcfdcuda.a  36be92adfda2c3bf5721c41acf7e6edb8d76c3f6de18c8e3cee30fad51cb0890
libcfdcore.a  79b3162c3cfc810773e8c2ce0e3cdd07c5d58a425966a0aee460d4d5cd8c0a7e

15/15 GPU-DISC differential gates green

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998     (45 disabled of 2043 registered)
  Total Test time (real) = 1295.79 s
  `ninja: no work to do` BEFORE and AFTER ctest; library and source sha256
  identical on both sides
```

## 13. Which Integration checkbox is next — and why the others stay unchecked

The brief requires auditing each remaining Integration checkbox's **own** scope rather than ticking
it because this task happened to exercise it. Doing that:

| checkbox | this gate's contribution | ready? |
| --- | --- | --- |
| **CUDA diagnostics** | 4/4 clean on a multi-iteration production solve; also 4/4 in each of 001B–001M | **Possibly — but not audited.** Its GPU-DISC-001-wide scope has never been stated in one place, and it may intend a diagnostics sweep across *all* qualified paths and configurations, not the union of per-gate runs. Leaving unchecked pending its own audit. |
| **Negative controls** | 8/8 here; 10/10 in 001M; 9/9 in 001L; and so on per gate | **Not ready.** Same reason: the checkbox plausibly means a consolidated control inventory across GPU-DISC-001, including the documented null controls and their proofs. That inventory does not exist yet as a single artefact. |
| **Performance qualification** | §10 records timings, and they show the GPU arm slower | **Definitely not ready.** This gate explicitly did not optimize, and the numbers would not pass a performance bar. |
| **Full regression** | 1998/1998 here and after every gate | **Not ready as a GPU-DISC-001-wide statement** — see its own scope. |

So **only `Full-solve CPU/GPU equivalence` is marked**. The next Integration checkbox is
**`CUDA diagnostics`**, and it should begin with an audit of what its GPU-DISC-001-wide scope
actually requires, because the per-gate evidence may or may not satisfy it.

## 14. What is NOT started

GPU-PIPE-001 persistent residency and GPU-resident pressure solve. Nothing committed or pushed.
