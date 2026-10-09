# P16 Step 5 — results against the frozen gate

```text
S5-1  CUDA diagnostics              PASS*   see step5_cuda_diagnostics/RESULT.md
S5-2  CPU/GPU equivalence breadth   PASS    three regimes
S5-3  determinism                   PASS    bitwise
S5-4  transfer / residency          PASS    27/27
S5-5  performance                   MEASURED  GPU slower at benchmark scale
S5-6  full normal regression        PASS    cuda 2528/0, final 2326/0
S5-7  the checkbox                  TICKED
```

`*` S5-1 carries a documented limitation: synccheck's negative-control
detection could not be demonstrated, so its clean result carries less weight
than memcheck/initcheck/racecheck, each of which was first shown to detect a
planted defect.

---

## S5-2 — CPU/GPU equivalence across three regimes

Step 4 qualified one family. The checkbox claims the P16 turbulence scope, so
the campaign covers what Step 4 did not.

### Regime 1 — Re_tau = 590, wall functions ON (re-verified)

Adopted gate 0.062 / 1.6. Both arms pass the frozen physical benchmark
independently; the GPU arm is not judged by agreement with the CPU arm.

```text
mesh   Re_tau(C)  Re_tau(G)    E(C)     E(G)  u+Linf(C) u+Linf(G)   rel|dtau_w|
ny06    555.3727   555.3727  0.05869  0.05869    1.4903    1.4903      1.685e-10
ny08    572.8168   572.8168  0.02912  0.02912    0.7776    0.7776      1.073e-09
ny12    589.5333   589.5333  0.00079  0.00079    0.2662    0.2662      6.881e-11
ny16    594.5848   594.5848  0.00777  0.00777    0.6981    0.6981      7.122e-11
```

### Regime 2 — Re_tau = 180, wall functions ON (NEW)

Adopted gate 0.22 / 3.9 — an order of magnitude looser than Re_tau 590, and a
genuinely different regime rather than the same case at another Reynolds
number: at Re_tau = 180 the channel is only 180 wall units tall, so the wall
function's validity band and the core resolution are in direct conflict
(ny <= 6).

```text
CPU arm   PASS    7.2 s
GPU arm   PASS  231.3 s
```

The gate body is a single shared function, `expectAdoptedGate180`, called by
both arms, so the criterion cannot drift between them.

### Regime 3 — no wall functions (NEW, amendment A1)

This path has no adopted physical gate — it IS the pre-P16 baseline
(E_Re_tau = 0.333) that the wall-function work improved on, and the frozen
methodology sets thresholds only after the sweep that measures them. Inventing
one mid-qualification is what a frozen gate exists to prevent, so amendment A1
judges this regime as CPU-reproduction at the unchanged `1e-6`.

```text
mesh            rel|dRe_tau|   rel|dtau_w|   outer iterations
ny02_yplus90      7.510e-10     1.502e-09    equal
ny03_yplus60      3.087e-09     6.173e-09    equal
ny04_yplus45      2.562e-09     5.124e-09    equal
ny05_yplus36      1.026e-09     2.052e-09    equal
ny06_yplus30      1.706e-12     3.413e-12    equal
ny08_yplus22      2.628e-09     5.255e-09    equal
ny12_yplus15      7.111e-10     1.422e-09    equal
```

Worst discrepancy 6.173e-09 against the unchanged 1e-6 bound — 162x inside it.
Equal outer-iteration counts on every mesh, which is a stronger statement than
the field bound: the two arms took the same nonlinear path, not merely a
similar endpoint.

### Non-vacuity, on every regime

Each GPU arm must report `turbulenceGpuPathQualified() == true` with
`strainRateExecutions == iterations`, and each CPU arm must report zero
executions. Without the CPU half, a provenance check that reported Gpu
unconditionally would pass — which is exactly the defect this control caught in
Step 4, where a process-lifetime trace made the CPU arm claim the GPU path had
run.

## S5-3 — Determinism

```text
GPU determinism (in-process, bitwise): tau_w 2.99618070336034499e-03
```

Asserted with `EXPECT_EQ`, not a tolerance: iterations, `tau_w`, `Re_tau` and
`u+ Linf` must be bit-identical across two solves. A reduction whose result
depended on block scheduling would show here and nowhere else. The value also
matches the `tau_w` recorded for ny16 in the Step-4 table in all 17 digits.

## S5-4 — Transfer and residency, on the final tree

```text
STEADY-STATE RESIDENT TURBULENT ITERATION (288 cells, 612 faces)
  D2H 30432 bytes in 648 calls
  H2D 2712 bytes in 13 calls
  allocations -7   reallocations 0

H2D at 48 wall faces: 2712 bytes on 288 cells, 2712 bytes on 576 cells
largest single D2H 2304 bytes, 15 copies over 5 corrections
5 further corrections: 0 allocations, 0 reallocations
```

27/27 across `ResidentChannelProduction` and `ResidentWallExchange`.

H2D is O(N_wall) by measurement, confirmed twice over: byte-identical across a
doubled cell count, and in the wall-exchange sweep, 1300 bytes at 32 wall faces
on 128, 256, 512 AND 1024 cells, rising to 2580 at 64 wall faces and 5140 at
128.

The `-7` allocations is the instrument bottoming out on a difference between
two whole solves, not seven allocations returned; the exact-zero claim belongs
to the per-correction gate, which requires 0 and gets 0.

## S5-5 — Performance, MEASURED

**No speedup is claimed, and none is required by the gate.** Rule 14 forbids
claiming an improvement without equivalent numerical solves; it does not
require the GPU to be faster.

Measured on identical work — the same meshes, the same gate, both arms:

```text
regime                        CPU        GPU       GPU/CPU
Re_tau = 180 adopted gate    7.2 s    231.3 s      32.3x SLOWER
Re_tau = 590 adopted gate   25.2 s    ~348 s       ~13.8x SLOWER
```

The turbulent GPU path is **slower than the CPU path on every benchmark mesh in
P16**, and the honest reason is mesh size, not a defect. These channels are
64x2 to 64x32 — 128 to 2048 cells. GPU-PIPE-001's recorded crossover is at
160^2 = 25 600 cells, an order of magnitude larger than anything here, and the
measured trend agrees: the slowdown falls from 32x to 14x as the meshes grow
from ny 2-12 to ny 6-32.

So the correct reading is that these meshes sit far inside the launch-overhead
regime. Extrapolating a crossover from two points would be exactly the kind of
unsupported performance claim Rule 14 prohibits, so it is not extrapolated —
what is recorded is that P16 turbulence on the GPU is numerically equivalent and
currently slower at the scales P16 validates.

## S5-6 — Full normal regression — PASS

`regress_both.sh p16`, both configurations, on a tree proven current by a
second build reporting no work:

```text
build/cuda    2528 passed   0 failed     LastTest.log 11:16:03
build/final   2326 passed   0 failed     LastTest.log 11:41:29

exact-tree proof   build/cuda  first AND second build: "ninja: no work to do"
                   build/final second build: no compile or link lines
```

`build/cuda` went 2525 -> 2528: the three new Step-5 tests. Confirmed BY NAME
rather than inferred from the count, because a count can rise for reasons that
have nothing to do with the tests one expects:

```text
WallFunctionChannelGpu.EveryG1ValidMeshPassesTheAdoptedGate          Passed
WallFunctionChannelGpu.NoWallFunctionPathReproducesTheCpuReference   Passed
WallFunctionChannelGpu.IsBitwiseDeterministic                        Passed
WallFunctionChannelHighReGpu.EveryG1ValidMeshPassesTheAdoptedGate    Passed
WallFunctionChannel.EveryG1ValidMeshPassesTheAdoptedGate             Passed
ResidentChannelProduction.RepeatedCorrectionsAllocateNothing         Passed
```

And the exclusions the tool is responsible for were verified to have held, since
a regression that silently stopped excluding them would read as four new
failures:

```text
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate   correctly EXCLUDED
DriverSeegmiller.CorrectedMeshSequence                       correctly EXCLUDED
```

Failure detection: each config's `LastTestsFailed.log` is OLDER than its
`LastTest.log` (06:56 and Sep 23, against 11:16 and 11:41). ctest rewrites that
file only when something fails, so its staleness corroborates the 0 count --
but the count itself comes from `Test Failed.` occurrences in the fresh logs,
not from the absence of a file.

**Reported separately from S5-1 and never conflated with it:**

```text
FULL NORMAL REGRESSION      exhaustive       every test, unsanitized
CUDA SANITIZER REGRESSION   coverage-driven  114 tests + kernel coverage matrix
```
