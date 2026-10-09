# A pre-fix regression run, preserved before it was overwritten

`ctest` writes `Testing/Temporary/LastTest.log` in place, so the next run
destroys the previous one. These four files were copied out of `build/cuda` and
`build/final` at 15:02 on 2026-09-23, while the post-fix regression was still
running and had not yet replaced them.

```text
cuda_LastTest_1134.log          build/cuda   finished 11:34, 2426 tests
cuda_LastTestsFailed_1134.log
final_LastTest_1207.log         build/final  finished 12:07, 2328+ tests
final_LastTestsFailed_1207.log
```

## What this run is, and what it is not

It **predates the GPU BiCGSTAB restart fix**: `cuda/kernels/GpuLinearSolverCuda.cpp`
was modified at 14:42, and this run's longest test started at 11:04.

It is **not a controlled before/after for the whole tree**. It also predates the
Stage 7I step 3 work and the blocker investigation — none of
`KrylovBlocker`, `KrylovRestartParity`, `GpuBiCGSTABRestartTest` or
`ResidentWallExchange` appears anywhere in either log (2426 tests then, 2481
now). So it cannot be diffed test-for-test against the current run, and it is
not presented as if it could be.

Its provenance is not certain: this repository is shared with other sessions,
and this run was not launched by the session that preserved it. That is stated
rather than glossed over. What the files themselves show is not in doubt.

## The one thing it settles

**`DriverSeegmiller.CorrectedMeshSequence` was already failing, in BOTH
configurations, before the restart fix existed.**

```text
build/cuda  (11:34)  2421:DriverSeegmiller.CorrectedMeshSequence
build/final (12:07)  2328:DriverSeegmiller.CorrectedMeshSequence
```

It failed on physics, after 1 819 301 ms (30 min), on two counts:

```text
test_driver_seegmiller.cpp:552
  Expected: (validMeshes) > (0), actual: 0 vs 0
  NO mesh in the sequence satisfies G1

test_driver_seegmiller.cpp:582
  Expected: (outcomes[2].reattachmentError) <= (outcomes[1].reattachmentError)
  actual: 0.40095575115883442 vs 0.21132859698905682
  refinement made the answer WORSE

mesh      cells     x_r/H    E_x_r  y+ mean   out
coarse     1080    5.6461   0.0981   276.67    60
medium     2430    7.5829   0.2113   184.50     3
fine       4320    8.7700   0.4010   136.55     3
```

## Why it cannot be the restart fix, on mechanism as well as on timing

The timing alone would be suggestive. Two independent facts make it conclusive:

1. **The backward-facing-step tests never select a GPU backend.** There is no
   occurrence of `backend`, `LinearSolverBackend::GPU` or `gpu` anywhere in
   `tests/integration/backward_facing_step/`. They run on
   `cfd::algebra::BiCGSTAB`, the CPU reference, which this work did not touch.
2. **`build/final` is `CFDAPP_ENABLE_CUDA=OFF`.** The changed file,
   `cuda/kernels/GpuLinearSolverCuda.cpp`, is not compiled into that
   configuration at all — and the same test failed there too.

A change confined to a file that a configuration does not compile cannot cause a
failure in that configuration.

## How to use these numbers

The three `x_r/H` values above are the comparison point. If the post-fix run
reproduces them, the restart fix is inert on this path in the strongest
available sense — same physics, same digits. That comparison is recorded in the
regression summary, not here.

This is a **preserved pre-existing failure**. It is not being fixed, explained
away, or attributed to this work. P16's turbulence accuracy on the
Driver & Seegmiller sequence is a separate open question, and the standing
instruction for this phase is not to re-run or chase it.
