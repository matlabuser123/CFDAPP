# P16 BLOCKER — resident GPU BiCGSTAB breakdown on turbulence transport

Phases A, B, C, D and E complete. **Root cause demonstrated.**

> **RESOLVED.** The restart was implemented in the shared GPU solver and the
> ladder below is superseded. This file is kept as the investigation record --
> including its two disproved hypotheses -- and is the only surviving copy of the
> PRE-FIX measurements, which the reproducer that produced them can no longer
> reproduce. The contract that was matched is in
> [restart_contract.md](restart_contract.md); the before/after, the re-run of the
> historical `DEBT-GPU-001` reproducer and the new gates are in
> [before_after.md](before_after.md).
>
> One transcription error in the ladder below: the 128-cell k row's `r0` reads
> `2.594e-02`, which is the 192-cell value copied one row too high. The correct
> value is `3.171e-02`. Every other digit reproduces exactly.

No solver code has been changed **as of this document**.

---

## Conclusion (Phase E), stated without hedging

**Same mechanism as `DEBT-GPU-001` — GPU BiCGSTAB restart asymmetry.**

The CPU BiCGSTAB **restarts** the Krylov sequence on a rho or `(rHat, v)`
breakdown. The GPU BiCGSTAB **has no restart at all** and returns `Breakdown`
immediately.

```text
src/algebra/BiCGSTAB.cpp        20 references to restart/Restart
cuda/kernels/GpuLinearSolverCuda.cpp    0
```

CPU, on breakdown (`BiCGSTAB.cpp:161-165`):

```cpp
if (cancelledToRoundingLevel(rho, rHat, r, rHatNorm, rNorm)) {
  const Restart outcome = restart();          // rHat = r, bounded, deterministic
  if (outcome == Restart::Restarted) continue;
  if (outcome == Restart::Converged) return restartedConvergence();
  return breakdown();
}
```

GPU, same point (`GpuLinearSolverCuda.cpp:524-526`):

```cpp
if (!std::isfinite(rho) || cancelledToRoundingLevel(rho, rHat_, r_, rHatNorm, rNorm)) {
  return breakdown(iter - 1);                 // no restart
}
```

The restart is not an optional refinement. `SolverResult::restarts` exists and
is documented as *"BiCGSTAB only — how many times the Krylov sequence was
restarted after a numerical breakdown of its recurrence"*, so restarts are part
of normal CPU operation. The GPU solver is missing a step the reference
implementation performs.

### It explains every observation

| observed | explained by |
|---|---|
| identical input, GPU alone fails | the two solvers differ in control flow, not in data |
| status is `Breakdown`, never `MaxIterations` | the GPU exits at the breakdown branch |
| GPU stops at FEWER iterations than CPU (10 vs 23) | CPU restarts and keeps going; GPU stops |
| GPU residual small but not converged | it stops mid-descent, not at stagnation |
| failure is NOT monotonic in size | whether a breakdown is hit depends on the system, not on N |

---

## Phase B — input identity, settled by construction

The reproducer assembles the system **once on the device**, then feeds it to
both solvers:

```text
device CSR ──┬─→ solveScalarTransportResident      (adopted in place)
             └─→ download → host LinearSystem → cfd::algebra::BiCGSTAB
```

So "they disagree about the system" is excluded by construction rather than by
a comparison that could itself be wrong. Confirmed empirically: the initial
residual `r0` is identical on both arms to every printed digit, on every row.

**Answer: case B — an identical system enters both solvers and the GPU alone
fails.** The assembly is not implicated.

---

## Phase A / C — the frozen ladder

Production-default solver settings (`relativeTolerance` 1e-10,
`absoluteTolerance` 1e-12), uniform velocity, wall functions on, one correction.

```text
nx   cells  eq                    iters   r0          rN
16   128    k    CPU Converged     15     2.594e-02   ~1e-13
                 GPU Converged     15     2.594e-02   ~1e-13
16   128    eps  CPU Converged     14                 both converge
                 GPU Converged     14
24   192    k    CPU Converged     21     2.594e-02   2.083e-13
                 GPU Converged     21     2.594e-02   2.102e-13
24   192    eps  CPU Converged     14     8.175e-01   7.029e-11
                 GPU Converged     13     8.175e-01   6.317e-11
28   224    k    CPU Converged     17     2.403e-02   3.487e-13
                 GPU Converged     24     2.403e-02   1.485e-12
28   224    eps  CPU Converged     13     7.569e-01   7.076e-11
                 GPU Breakdown     10     7.569e-01   1.297e-08   <-- FIRST FAILURE
32   256    k    CPU Converged     19     2.250e-02   2.788e-13
                 GPU Breakdown     15     2.250e-02   1.835e-10
32   256    eps  CPU Converged     14     7.080e-01   5.923e-11
                 GPU Converged     14     7.080e-01   6.424e-11   <-- PASSES
40   320    k    GPU Breakdown     16     2.016e-02   3.163e-10
40   320    eps  GPU Breakdown     11     6.334e-01   3.078e-08
48   384    k    GPU Breakdown     10     1.843e-02   2.611e-07
48   384    eps  GPU Breakdown     16     5.783e-01   1.399e-10
64   512    k    GPU Breakdown     10     1.602e-02   6.155e-07
64   512    eps  GPU Breakdown     12     5.010e-01   9.517e-08
```

The CPU converges on **every** system in the ladder.

### Two corrections to the first report

1. **The first reproducible failure is 224 cells (nx=28, epsilon), not 256.**
   My earlier note said 256; that was the first size I happened to sample.
2. **It is not a threshold.** 256-epsilon CONVERGES while 224-epsilon breaks
   down. Failure tracks the individual system, not the cell count — consistent
   with a breakdown that is hit or not hit depending on the Krylov sequence.

---

## Phase D — where they diverge

The divergence is not a drifting scalar; it is a **branch**. Both solvers reach
the same `cancelledToRoundingLevel` test with the same inputs (the GPU's
implementation is a faithful mirror of the CPU's, including the `absDot` term
sum, and its own comment says so). They then take different paths: the CPU
restarts, the GPU returns.

So Phase F (reduction accuracy crossing a threshold) is **not** the cause here.
A reduction difference could change *when* the breakdown test fires, but both
solvers firing it is normal and expected — the defect is what happens next.

---

## Two hypotheses tested and DISPROVED before this one

Recorded because each looked plausible:

1. *The strict 1e-14 equivalence tolerance is too tight at larger sizes.*
   **Disproved** — fails identically at production-default tolerance.
2. *The synthetic velocity is not divergence-free, so the flux is
   non-conservative and the convection operator loses diagonal dominance.*
   **Disproved** — fails with a uniform, divergence-free velocity.

---

## Scope impact

**Every GPU turbulence differential in Stages 4–7 ran below the first failing
size**: 12×9 = 108, 12×8 = 96, 14×10 = 140, 14×8 = 112 — all under 224. The
equivalence measured in those stages is real, but its **coverage is narrower
than it appeared**. It says nothing about production grid sizes, and the
Re_tau = 590 channel is far larger.

This also means `DEBT-GPU-001` is not confined to the pressure path: the same
missing restart blocks turbulence, and the P16 milestone with it.

---

## Reproducer

`tests/unit/gpu/test_krylov_blocker_reproducer.cpp`,
`KrylovBlocker.TheSameDeviceSystemSolvesOnCpuButNotOnGpu`.

It asserts that a CPU-succeeds/GPU-fails case is still reproduced, so if the
defect is fixed the test fails and the ladder must be re-measured rather than
silently going green.

**That is exactly what happened.** On the first run after the fix the reproducer
failed with *"no CPU-succeeds/GPU-fails case was reproduced"* -- which is how the
fix was confirmed rather than assumed. The file now holds the opposite
assertion, as `KrylovRestartParity.TheSameDeviceSystemSolvesOnBothSolvers`, plus
the non-vacuity control that keeps the green honest.

---

## Not done -- AT THE TIME THIS WAS WRITTEN

No fix has been implemented. Phases G (negative control), H (narrow fix in the
shared GPU BiCGSTAB), I (re-qualify the ladder) and J (regression, including
the already-qualified pressure path) remain.

**All four were subsequently carried out under the authorized shared-solver
fix. See [before_after.md](before_after.md).**

The fix touches the **shared** solver that GPU-PCORR-001 and GPU-PIPE-001's
resident pressure solve both depend on, so Phase J's pressure regression is not
optional. The existing `DEBT-GPU-001` reproducer (2D cavity 40×40,
`PressureCorrectionFailure` after 74 iterations) must also be re-run, and any
change in its behaviour documented rather than treated as an improvement.
