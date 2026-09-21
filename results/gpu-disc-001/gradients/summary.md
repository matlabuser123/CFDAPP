# GPU-DISC-001B — CUDA gradients

**Result: PASS.** The CUDA Green–Gauss gradient is **bitwise identical** to the CPU reference on
every case run — 132 differential cases across 11 meshes, 3 boundary-condition sets and 4 fields,
plus the three benchmark sizes up to 409,600 cells. No tolerance was used anywhere in this gate.

```text
differential      132 cases   0 failures   bitwise (memcmp), not within a tolerance
non-vacuity       PASS        a one-ULP perturbation is detected by the same comparator
path coverage     PASS        every branch of the algorithm was reached by some case
negative controls 2/2 PASS    defect detected, source restored, rebuilt, freshness verified
compute-sanitizer 4/4 clean   memcheck, initcheck, synccheck, racecheck -- 0 errors
CPU regression    82/82       gradient / interpolation / boundary-consistency tests
full regression   1998/1998   0 failed, on a tree proven fresh before AND after ctest
device residency  0 D2H       zero device-to-host copies per gradient evaluation
```

## 1. Differential — the gate

`results/gpu-disc-001/gradients/differential/differential.log`

Meshes (the P12 gate families, reused rather than invented for the GPU):

```text
cartesian2d 16      aligned, orthogonal -- single sweep
cartesian2d 40      aligned, but boundary transfer fires -> sweeps
quad translated     structured quad at offset (1000, -250)
graded2d 16         stretched, geometric grading on both axes
distorted q16       P12-GRAD-002's distorted mesh -- non-orthogonal + skewed
q16 translated      the same distorted mesh at offset (1000, -250)
sheared 0.35        interior vertices sheared; boundary normals kept exact
multiblock L        3-block L-shaped domain, 2 internal interfaces
cartesian3d 8       3D
planar skew 3d 6    3D, planar faces, skewed and non-orthogonal at every boundary
warped 3d 6         3D, bilinear interior faces
```

Boundary conditions: all-Dirichlet, all-Neumann, and **mixed** (alternating patches, so a single
mesh carries both encodings and the per-face lookup has to be right per face). Fields: constant,
linear, quadratic, manufactured (`sin·cos` with a z ramp).

Every case reports `diff=0`, `maxAbs=0`, `maxRel=0`, `L2=0`. The `|grad|max` column is printed on
every line so it is visible that the comparison ran over real numbers (17–186, not a field of
zeros), and `bcells` gives the boundary-adjacent cell count, which is where every special treatment
in this operator lives.

### Path coverage

A wall of PASS lines proves nothing if a branch never executed, so the harness counts and gates on
it:

```text
sweep loop entered     21 mesh/BC pairs
skew-corrected faces   15      (up to 540 skewed faces on warped 3d)
oblique Neumann faces  12      (up to 216 oblique faces on planar skew 3d)
GRAD-002 claims        33      (up to 384 claims on cartesian3d 8)
boundary transfer      21
```

The harness **fails** if any of these is zero.

### Non-vacuity

An identical copy compares as 0 differing; the same value perturbed by one ULP compares as 1
differing (`maxAbs=2.22e-16`). The comparator can fail.

## 2. Negative controls

`results/gpu-disc-001/gradients/negative-control/`

Both follow the full procedure: inject → build → **verify detected** → restore → **rebuild** →
verify source sha256 matches → verify build freshness → **verify the differential passes again**.
No mutant binary survives either control.

| control | mutation | detected | restored |
| --- | --- | --- | --- |
| NC1 | interior face value uses `dPf`/`dNf` swapped | yes — first failure at `cartesian2d 40`, 1828 differing components, `maxAbs=6.6e-14` | sha256 match, `ninja: no work to do.`, differential PASSES |
| NC2 | `-fmad=false` removed from the gradient kernel | yes — 3080 differing components, `maxRel=0.125` | sha256 match, `ninja: no work to do.`, differential PASSES |

NC2 matters beyond being a control: it proves the bitwise result **depends on** disabling FMA
contraction rather than holding by accident.

NC1 is also informative about the mesh set — it is invisible on a perfectly uniform mesh where
`dPf == dNf`, and the first mesh to catch it is `cartesian2d 40`, where the two distances differ in
their last bits. The differential is sensitive at ULP level.

## 3. CUDA diagnostics

`results/gpu-disc-001/gradients/cuda-diagnostics/`

```text
memcheck    errors=0
initcheck   errors=0
synccheck   errors=0
racecheck   errors=0
```

Run over `--quick` mode (distorted q16 + warped 3d 6), which between them reach **every** kernel:
skewed faces, oblique Neumann faces, claims and boundary transfer, in both 2D and 3D. Each
sanitizer run's own output was checked to contain `GRADIENT EQUIVALENCE (quick): PASS`, so none of
the four runs was vacuous.

## 4. P12-GRAD-002 and P12-MESH-001 regression

Two independent statements:

1. The existing CPU gate tests still pass — 82/82 (`Gradient|Interpolation|BoundaryConsistency`).
   No production CPU file was modified by this phase.
2. The GPU gradient is bitwise identical to the CPU gradient on the GRAD-002 gate mesh families
   themselves — `distorted q16`, `q16 translated`, `sheared 0.35`, `quad translated`,
   `planar skew 3d`, `warped 3d` — under Dirichlet, Neumann and mixed conditions.

Because the two paths produce the same bits on those meshes, every P12-GRAD-002 and P12-MESH-001
property the CPU satisfies is satisfied by the CUDA path identically, including translation
invariance (the defect GRAD-002 fixed): the translated and untranslated distorted meshes are both
covered, and on each the GPU reproduces the CPU exactly.

## 5. Gradient operator benchmark

`results/gpu-disc-001/gradients/benchmarks/gradient_operator.log`

Green–Gauss gradient of one scalar field, 20 repeats, field already device-resident, result left
device-resident, `cudaDeviceSynchronize` inside the timed region so the number is device execution
time and not launch time.

```text
 160^2   25,600 cells   cpu   5.408 ms   gpu 0.172 ms    31.5x   plan   8.5 ms   8.5 MB   bitwise
 320^2  102,400 cells   cpu  22.451 ms   gpu 0.224 ms   100.0x   plan  29.1 ms  33.8 MB   bitwise
 640^2  409,600 cells   cpu  96.452 ms   gpu 0.850 ms   113.5x   plan 123.5 ms 134.8 MB   bitwise
```

**These numbers need two honest caveats.**

* **A large part of the speedup is precomputation, not parallelism.** The CPU re-derives
  opposite-face topology, crossing weights and boundary-line intersections *inside every sweep*;
  the device path builds them once into the plan. A host implementation with an equivalent cached
  plan would close much of this gap. The comparison is "production CPU call vs device operator with
  a prebuilt plan", which is the honest description of what a GPU-resident pipeline would actually
  do — but it is not a pure measure of GPU parallelism, and should not be quoted as one.
* **This is one operator, not a solve.** Momentum, pressure and the SIMPLE loop are still on the
  CPU. Nothing here says the application got faster. GPU-DISC-001's own performance qualification
  comes at the end of the operator sequence, not here.

Plan cost amortization at 640²: 123.5 ms ≈ 145 device gradient evaluations, ≈ 1.3 CPU evaluations.
Built once per mesh.

One bookkeeping note so the two logs do not look contradictory: `residentBytes()` includes the
plan's face-value and claim-value scratch, which is allocated on the first evaluation. The
differential prints it **before** any evaluation and the benchmark **after** one, so the benchmark's
figure is the larger of the two for the same mesh. Both are real device memory; they are measured at
different moments.

Correctness was re-checked *at benchmark sizes* on the very arrays being timed — `bitwise=yes` at
all three, up to 409,600 cells. A fast wrong answer would not have been reported as a speedup.

## 5b. Full regression, and a note on how it was run

`regression.log` — 1998/1998 passed, 0 failed, 395.8 s (45 tests disabled, the same 45 as the
known-good baseline at `548401a`).

`regression_freshness.log` — the run that counts, with `ninja` reporting **`no work to do` both
immediately before and immediately after** `ctest`, plus sha256 fingerprints of all five changed
files. That matters because the first two regression attempts were each built one comment-only
header edit behind the tree: the edit landed while their build step was already finished. Both
reported 1998/1998, but neither could honestly be described as testing the current source. Rather
than argue that a comment cannot matter, the suite was re-run against a tree whose freshness is
proven on both sides of the run. CLAUDE.md section 8's stale-binary hazard applies to evidence runs
exactly as it applies to negative controls.

## 6. What is NOT claimed

* `LeastSquares` gradients are untouched — a different scheme, its own entry point.
* Vector-field gradients (`computeVelocityGradient`) are not ported; they belong to 001F.
* The **Green–Gauss four-sweep limitation** is reproduced, not fixed. It remains recorded technical
  debt.
* A mesh carrying a boundary condition this path cannot reproduce bitwise is **rejected**
  (`usable() == false` with a reason), not silently approximated. Every condition in the codebase
  today is covered by one of the three encodings, but the rejection path is real and is what makes
  that claim safe.
* No end-to-end solve is GPU-resident yet. GPU-PIPE-001's remaining items stay blocked on the rest
  of the GPU-DISC-001 operator sequence.

## 7. Files

```text
include/cfd/gpu/DeviceGradient.hpp        plan + entry point
cuda/kernels/DeviceGradientPlan.cpp       host plan builder
cuda/kernels/DeviceGradientKernel.cu      six kernels (-fmad=false, this file only)
include/cfd/gpu/DeviceMesh.hpp            + boundaryFaceCount() accessor
cuda/CMakeLists.txt                       sources + the no-FMA property
```

Evidence:

```text
audit.md                  Phase 1 -- the CPU algorithm, exactly
implementation.md         Phase 2 -- what was built and the three decisive details
differential/             the gate log
negative-control/         both controls, mutated and restored runs
cuda-diagnostics/         four sanitizer logs
benchmarks/               operator benchmark
regression.log            full suite
tools/                    harness sources
```
