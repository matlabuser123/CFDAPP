# P17.10 — implementation

What was built, what was deliberately not built, and the two places where the implementation
departed from the frozen design document rather than quietly conforming to it.

```text
NEW PRODUCTION SOURCE
  include/cfd/gpu/DeviceThermodynamics.hpp        shared host/device: the thermodynamic closed
                                                  forms, exteriorState, outwardNormal, meanForCfl,
                                                  and the DeviceControls flag set
  include/cfd/gpu/DeviceDensityBasedSolver.hpp    the CUDA-only facade (no CUDA types in it)
  cuda/kernels/DeviceDensityBasedKernel.cu        the kernels and the host-side orchestration
  cuda/CMakeLists.txt                             one source entry, one --fmad=false property

NEW TEST SOURCE
  tests/integration/compressible/EquivalenceCompare.hpp        the comparison instrument
  tests/integration/compressible/test_p17_10_dryrun.cpp        the pre-freeze dry-run + the finding
  tests/unit/compressible/test_device_thermodynamics_equivalence.cpp   restatement vs production
  tests/unit/gpu/test_density_based_gpu_equivalence.cpp        CPU vs GPU
  three CMakeLists.txt entries

PRODUCTION SOURCE MODIFIED: none.
  DensityBasedSolver.cpp, HllcFlux.hpp and TotalEnergy.cpp are at their P17.8/P17.9 sha256 values,
  re-verified at the head of every step of this phase. §2 forbids changing a CPU formula for GPU
  convenience; nothing here needed one, which is the point of having read the CPU source line by
  line first rather than porting it.
```

---

## 1. The kernels, as built

```text
K1   recoverKernel          one thread per cell    Q -> primitive + a per-cell status code
K2a  faceFluxKernel         one thread per FACE    reconstruct, exterior state, HLLC, x area
K2b  gatherResidualKernel   one thread per cell    ascending gather, +owner / -neighbour
K3   localTimeStepKernel    one thread per cell    sum (|u.n| + a) A, then CFL V / sum
K4   minReduceKernel        tree reduction         global dt -- a MIN, so order-independent
K4'  minIndexReduceKernel   tree reduction         the FIRST offending cell, also a min
K5   applyUpdateKernel      one thread per cell    Q - (dt/V) R
K5'  fillKernel             one thread per cell    broadcasts the global dt into the per-cell array
                                                   so GlobalPhysical reuses K5 unchanged rather
                                                   than having a second update path that can drift
K6   (the validity half of K1, re-run on the candidate state after K5)

ATOMIC OPERATIONS: zero. Verified by source and, more to the point, by racecheck -- which reported
0 hazards on the real kernels and 1 hazard on a deliberately racy control kernel, so the zero means
"nothing wrong" rather than "nothing instrumented".
```

## 2. Two departures from `kernel_inventory.md`, recorded rather than smoothed over

The inventory was frozen before implementation (sha256
`953efdeeaa91e67195a6472d206476d0fe5e5f946c435b9134c4a11fba1e7536`) and is **not edited**. Where
the build had to differ from it, the difference is recorded here — a frozen design document that
gets quietly reworded to match what happened is worth nothing.

### 2.1 The boundary mapping is reimplemented, not shared

```text
THE INVENTORY SAID (trap 6): "The device upload builds its per-face treatment array THROUGH that
same host function, so the refusal is inherited rather than reimplemented."

WHAT IS TRUE: it cannot. boundaryByFace() has INTERNAL LINKAGE -- it lives in an anonymous
namespace inside src/compressible/DensityBasedSolver.cpp and is not reachable from another
translation unit. Making it reachable means editing P17.8 production code, which §2 and the
phase's "no production source modified" property both argue against for a convenience.

WHAT WAS DONE INSTEAD: upload() performs the SAME TWO REFUSALS -- a patch with no treatment, and a
boundary face in no patch -- and the equivalence suite proves the two agree by DRIVING BOTH with an
unmapped patch and requiring both to refuse, then driving both with a complete map and requiring
both to accept. That second half matters: without it, "the device refused" would also be satisfied
by an upload that always fails.

  measured (logs/05_gpu_equiv.log):
    trap6: cpu refused=YES, device refused=YES
           ("boundary patch \"right\" has no density-based boundary treatment; every patch must be
             given one explicitly (an unmapped patch is not assumed to be a wall)")
    and the complete map is accepted by both.

HONEST ASSESSMENT: a shared call cannot drift; two implementations proved equivalent by test can,
if someone later changes one. This is weaker than what the inventory promised, and it is the
weakest link in the phase. It is named here so it is visible rather than discovered.
```

### 2.2 `DeviceThermodynamics` carries nine scalars, not eight

```text
The inventory's first draft named eight and then listed nine (tMin and tMax arrived with trap 7).
The built POD carries nine: R, cp, cv, gamma, T_ref, h_ref, T_min, T_max, eFloor. No behavioural
difference; recorded because a count in a frozen document should match the thing it describes.
```

## 3. The design decisions that are not obvious from the code

### 3.1 Two kernels for the residual, not one

```text
A single fused per-cell kernel would evaluate every interior face's Riemann problem TWICE, once
from each side. It would still be bitwise -- same inputs, same code -- but it would break the
property P17.8's own header states: "ONE hllcFlux evaluation per face produces all five components
at once", which §6's prohibition on recomputing the convective fluxes independently is about.

The split keeps one evaluation per face AND keeps the accumulation order identical to the CPU's. It
also halves the flux work. The saving is a side effect; the discipline is the reason.
```

### 3.2 The live state alternates by parity instead of being copied

```text
DeviceBuffer has no swap(). The alternatives were a device-to-device copy of five arrays per step,
or a parity flag selecting which of the two buffer sets is live. Parity was chosen: a copy per step
is five operations that change no number, and §24 asks the loop to stay resident, not merely to
avoid the HOST.

The cost is that getState()/setState() must go through liveDensity() and friends rather than the
named buffers. That is a place a future edit could get wrong, so T2 -- upload, download, compare
bitwise -- runs after a fresh upload AND the trajectory test downloads at steps 1, 2, 5, 10, 25 and
50, which exercises both parities repeatedly.
```

### 3.3 Every negative control is a flag, not a source edit

```text
DeviceControls carries seven switches, all false in production. Each re-introduces exactly one
named trap. This is stronger than editing the source for a control run, because the control and the
production path are THE SAME BINARY: a control that needed a rebuild could always have been
measuring a different build than the one the positive result came from.

The cost is seven branches in kernels that are otherwise clean. They branch on a uniform value, so
there is no warp divergence, and the positive case ("production, all controls off") is measured in
the same test that measures the controls -- 0 of 1085 residual values differ -- so the branches
cannot be quietly changing the production answer.
```

### 3.4 The sanitizer controls are separate kernels, and opt-in

```text
A clean compute-sanitizer log NAMES NO KERNELS, because kernels are named only when something is
wrong. So "0 errors" and "the tool never instrumented this library" produce identical output. The
first run of tools/sanitizers.sh made exactly that mistake: it counted log lines naming our kernels
and got 0 for all four tools, which looked like a failed probe and was in fact the normal
appearance of a clean run.

The fix is a live control: two kernels in THIS translation unit that commit one deliberate fault
each. memcheck must name the out-of-bounds one; racecheck must name the racy one. They are separate
kernels rather than flags inside real ones, so no production kernel carries a fault path, and they
SKIP unless CFD_P17_10_SANITIZER_CONTROL=1 -- an out-of-bounds device read can raise a sticky error
that poisons the CUDA context for the whole process, and CFDGpuTests runs ~200 GPU tests in one
process.
```

## 4. The refusals the device inherits

```text
unmapped boundary patch            REFUSES     proved equivalent to the CPU's refusal by test
boundary face in no patch          REFUSES     same
non-ascending cell face list       REFUSES     checked at upload; see below
cfl not finite or <= 0             REFUSES
empty mesh                         REFUSES
rho <= 0, non-finite Q             REFUSES     with the SAME status code the CPU's message names
e below the admissible floor       REFUSES     nothing is clamped, inherited from P17.5
T outside the property range       REFUSES     trap 7 -- the (void)enthalpy() range check
no cell with a finite positive dt  REFUSES     the same refusal globalTimeStep makes
```

```text
THE ASCENDING CHECK IS IN PRODUCTION, NOT ONLY IN A TEST. upload() walks the connectivity once and
refuses if any cell's face list is not ascending. The dry-run measured the invariant on five mesh
construction paths (702 cell-face references, all ascending) and the static reading of
MeshGeometry.cpp says it holds by construction -- but a future mesh path could break it, and the
failure mode would be a residual that silently stops matching the CPU. A refusal at upload turns
that into an error message naming the cell.
```

## 5. What was NOT built, and why

```text
residual norms on device       an order-dependent SUM. A parallel tree reduction cannot be bitwise
                               equal to the CPU's sequential sum, and making the device reduction
                               sequential to force a match would be engineering the answer rather
                               than implementing the operation. Computed host-side from the state,
                               exactly as the CPU does, so they agree by construction whenever the
                               state does. Recorded in audit.md §6 as a DECISION before any code
                               existed, not discovered later as a limitation.
conservation sums on device    same reasoning.
the retry / rejection loop     host control logic. The device reports the DECISION -- admissible or
                               not, and which cell -- and the suite compares those.
restart                        UNSUPPORTED on the CPU, and it refuses. §34 requires preserving the
                               refusal, not building a GPU restart the CPU has no counterpart for.
a performance measurement      §32 forbids a speed claim without equivalence and this phase makes
                               none even with it. No timing in this evidence is a claim.
```
