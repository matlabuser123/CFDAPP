# P17.10 — the equivalence measurements

Every number here is from a log in `logs/`. The requirement throughout is **BITWISE**, set in
`tolerance_policy.md` before any CUDA source for this path existed, and no fallback tolerance was
written down to retreat to.

```text
GATE      acceptance_gate.md, sha256 c5b86211dce2128152f5fe8d5a93b8ba739fc596da562f485af18d3ce6efbb51
POLICY    tolerance_policy.md, sha256 e69d8ef2b8cb5f8c7bac320ab7b153dd0b914a7f658718f2f774845863b8d116
HARDWARE  NVIDIA RTX 5000 Ada Laptop (compute capability 8.9), CUDA 12.9, WSL2 Ubuntu-22.04
```

---

## 1. The instrument, qualified before it was trusted

`logs/01_dryrun.log`, run **before the gate was frozen**.

```text
A NAMING COLLISION, flagged so nothing here is read as something it is not. The dry-run's TESTS are
named D1..D6 and the frozen gate's GROUP D criteria are also D1..D6. THEY ARE UNRELATED. Below,
D1..D6 are dry-run test names (the instrument and the mesh invariant). The gate's Group D --
trajectories and benchmarks -- is section 5 of this document.
```

```text
D2  180 values compared, 0 differing                                      -> EQUAL
D3  ONE ULP in each of the five conserved components, at interior cell 17 -> ALL FIVE DETECTED
      density  momentumX  momentumY  momentumZ  totalEnergy       1 of 180 values each
      momentumY is the sharpest: it is exactly 0.0 there, so one ULP up is the smallest
      subnormal (~4.9e-324). A RELATIVE tolerance would miss it entirely. A bit comparison does not.
D4  five ways a comparison can silently not happen, each producing "zero differences found":
      never ran / gpu unpopulated / cpu unpopulated / both empty / size mismatch
      ALL FIVE report NOT EQUAL. ComparisonReport's DEFAULT STATE IS FAILURE.
D5  two identical NaN bit patterns are EQUAL (same computed result) and are counted separately as
    non-finite, so a matching pair of NaNs can never read as a clean pass on its own.
D6  the CPU baseline itself: 34 steps, run twice, state and every dt BITWISE identical.
```

## 2. Level A — the arithmetic

### 2.1 The restatements against production (`logs/04_thermo_equiv.log`)

Runs in **both** build configurations, because the shared header compiles under gcc too.

```text
A1  the recovery Q -> (u, T, p, a) vs TotalEnergyState::recover()
      10 states x 6 quantities x 2 gases (zero and NONZERO enthalpy reference) = 120 comparisons
      ALL BITWISE.
      The nonzero-reference gas matters: with h_ref = T_ref = 0 the recovery would pass with those
      terms deleted.

A1 CONTROL  regrouping (rho*R)*T into rho*(R*T) -- the same number in exact arithmetic --
            differs in 1 of 20 values.
            REPORTED AS MEASURED: 1 of 20 is a weak detector, and it is weak for a real reason --
            two groupings differ only when the intermediate rounding differs, which is
            data-dependent. It is non-zero, so A1 is demonstrably sensitive to operand order. It is
            not strong, and calling it strong would be a claim the number does not support.

A2  the REFUSALS, and the reason for each
      non_positive_density   cpu refused  device=NonPositiveDensity
      negative_density       cpu refused  device=NonPositiveDensity
      nan_density            cpu refused  device=NonFiniteDensity
      nan_momentum           cpu refused  device=NonFiniteMomentum
      nan_energy             cpu refused  device=NonFiniteTotalEnergy
      kinetic_exceeds_total  cpu refused  device=InternalEnergyBelowFloor
      Six for six, same reason each time -- a divergence in the ADMISSIBLE SET is not visible in
      any field comparison, so the reasons are compared and not merely the fact of refusal.

A2 TRAP 7   the temperature range check, which the CPU performs as `(void)enthalpy(T)` purely for
            its exception. Both refuse an out-of-range state; with the check removed the device
            ACCEPTS T = 4730.93 K against a property-model ceiling of 2500 K.
            That is the control: the line that looks like dead code is the admissible set.

A6  exteriorState, all 3 boundary kinds x 6 normals (3 axis-aligned, 3 not) = 144 values, ALL
    BITWISE.
A6 CONTROL  carrying rhoE over unchanged instead of rebuilding it from the mirrored velocity
            changes it on 2 of 3 non-axis-aligned normals -- and NOT on the axis-aligned one, which
            is exactly right: mirroring along x leaves |U| unchanged bit for bit. The control
            fires where the physics says it should and is silent where it says it should be.
```

### 2.2 Device codegen against host codegen (`logs/05_gpu_equiv.log`)

```text
A7  the recovery, computed BY THE KERNEL, against production's toPrimitive()
      1736 values compared, 0 differing.

A5  HLLC on device: not restated at all. The kernel calls the shared CFD_HLLC_HOST_DEVICE source,
    so there is nothing to compare beyond B1 below, which compares its output.
```

```text
WHY BOTH HALVES WERE MEASURED SEPARATELY. §2.1 compares production against the shared header
COMPILED FOR THE HOST -- it catches a mis-transcription. §2.2 compares the kernel's output against
production -- it catches a codegen difference. A single end-to-end comparison conflates the two and,
on a failure, names neither.
```

## 3. Level B — the local operator

```text
B1  per-face flux, all five components      472 faces x 5 = 2360 values, 0 differing

B2  per-cell residual, three meshes
      uniform_2d      217 cells   volume ratio    1.000    1085 values   0 differing
      graded_2d       102 cells   volume ratio 6849.147     510 values   0 differing
      cartesian_3d    180 cells   volume ratio    1.000     900 values   0 differing
```

```text
B5  the gather's sign convention and exact cancellation, isolated
      510 residual values rebuilt on the HOST from the DOWNLOADED device face fluxes, using the
      same ascending order and the same +owner / -neighbour rule, then compared with the device's
      own residual:  0 differing, over 362 interior cell-face references.

    This isolates K2b. B2 alone would fail if either the flux OR the accumulation moved and could
    not say which; B5 holds the fluxes fixed and tests only the gather. It was added after the
    coverage map found that nothing measured it -- the property was true by construction and
    checked by no test.

B4 IS THE GRADED ROW, AND IT IS NOT DECORATION. P17.8's N2 control found that a UNIFORM mesh makes
a dropped 1/V factor invisible, because a constant volume factors out of both sides of the
comparison. A 6849:1 volume ratio means the residual is being compared where V genuinely varies
cell to cell. Running the comparison only on the uniform mesh would have measured less than it
appeared to.
```

## 4. Level C — one step and the controls

```text
C1  per-cell local dt, on the graded mesh      102 values, 0 differing
C2  global dt (the min reduction)
      cpu 5.55587098248344881e-08
      gpu 5.55587098248344881e-08     BITWISE

    min is associative and commutative in IEEE-754 for non-NaN operands, so the reduction TREE
    cannot change the answer -- which is why a difference here would mean the LOCAL dt differs, not
    the reduction. R5 below tests that claim directly.

C3  Q after one update from an identical Q0    1025 values, 0 differing (see D1 step 1)

C4  the REJECT path, which every other test avoids by construction
      three kinds of inadmissible state, TWO offending cells each (ids 9 and 30):

        kinetic_exceeds_total   cpu names cell 9   gpu first = 9 of 2 offenders   status 5
        non_positive_density    cpu names cell 9   gpu first = 9 of 2 offenders   status 2
        non_finite_energy       cpu names cell 9   gpu first = 9 of 2 offenders   status 4

    TWO offenders, not one, on purpose: a device reduction that returned SOME offender rather than
    the FIRST in ascending id order would compare equal on a one-offender case and diverge here.
    The happy path is asserted first in the same test, so the agreement is not "this fixture always
    refuses".

C5  the retry LOOP is host control logic with no device counterpart by design (audit.md §6). The
    DECISION that triggers a retry is compared above. A run in which the CPU actually retries, with
    retry counts compared, was NOT performed -- see coverage_map.md.
```

## 5. Levels D and E — trajectories and a benchmark

```text
D1  the trajectory, compared at 1, 2, 5, 10, 25 and 50 steps
      step  1   1025 values   0 differing   dt 3.38038832493589418e-06
      step  2   1025 values   0 differing   dt 3.39237437508229838e-06
      step  5   1025 values   0 differing   dt 3.40273577357818650e-06
      step 10   1025 values   0 differing   dt 3.41482663654204133e-06
      step 25   1025 values   0 differing   dt 3.44106118050838639e-06
      step 50   1025 values   0 differing   dt 3.47554142476449206e-06
    and dt compared BITWISE at EVERY ONE of the 50 steps, not only the sampled ones.

D2  first divergence: NONE through 50 steps.

D3  Sod shock tube, 200x3, 500 steps
      3000 values BITWISE
      101 cells at an intermediate state; max centreline jump 0.0233

D4  P17.9's OWN detectors, run on both backends' final states
      shock position  cpu 0.7649115536763863   gpu 0.7649115536763863   BITWISE
      transition width  7 cells on both
      200 centreline cells; the wave must be FOUND, so "identical" cannot be two identical absences

    the developed profile, sampled every 20 cells:
      1.0000 1.0000 1.0000 0.9697 0.7062 0.4319 0.4038 0.2655 0.1251 0.1250
      -- the rarefaction fan, the star states either side of the contact (~0.43 / ~0.27) and the
      shock down to 0.125: the Sod structure, developed rather than assumed.
```

```text
TWO CORRECTIONS TO THIS TEST, BOTH RECORDED RATHER THAN QUIETLY APPLIED.

FIRST, D3 ORIGINALLY RAN 120 STEPS AND ITS "a discontinuity developed" CHECK PASSED FOR THE WRONG
REASON. Cell ids run j*nx+i, so a scan over all cells compares the LAST cell of one row with the
FIRST of the next -- across the initial discontinuity -- and reports a jump of 0.8750 no matter what
the solver did. The check meant to rule out "nothing happened" was measuring exactly the artefact it
was meant to exclude, and at 120 steps the wave structure really did still span only ~20 cells. It
now measures along ONE ROW and requires 40+ cells at an intermediate state; at 500 steps there are
101.

SECOND, UNDER --fmad=true, D3 FAILS AND D4 PASSES. A crossing-position detector quantises, so it is
LESS sensitive than the state comparison: the states differ in 704 of 1025 values by step 50 while
the detected shock position is still bitwise identical. D4 is supplementary evidence, not a
substitute for D3, and that ordering is a measurement rather than a preference.
```

## 6. Determinism

```text
R1  CPU repeated            BITWISE   (dry-run D6: 34 steps, state and dt)
R2  GPU repeated            BITWISE   30 steps, same process
R5  GPU at a DIFFERENT BLOCK SIZE (64 vs 128 threads)   BITWISE, state and every per-step dt

R5 IS THE ONE THAT TESTS THE ARGUMENT. The claim is that the gather is order-independent of the
launch configuration and that the min reduction tree cannot change the answer. Changing the block
size changes both the gather's thread mapping and the reduction tree. The answer did not move.

R4  atomic operations in the new kernels: ZERO, by source AND by racecheck -- see §8.
```

## 7. Transfer and residency

```text
T2  upload then download, compared bitwise      460 values round-tripped unchanged

T1  a 20-step resident run
      kernel launches                    180
      host->device transfers PER STEP      0
      device->host transfers              40
      a per-step state round trip would be 100
      resident bytes                   50488

    The 40 downloads are the dt reduction's partials and the validity reduction's, two per step --
    the loop brings back a SCALAR and a flag, not the state. The assertion is written as
    "fewer than a full per-step state round trip" with the comparison number printed, rather than
    as "0 transfers", because 0 would be false and a probe that asserts a falsehood gets weakened
    until it passes.
```

## 8. CUDA diagnostics (`logs/24_sanitizers_summary.log`)

```text
compute-sanitizer 2025.2.1.0

  memcheck    on all 12 equivalence tests      ERROR SUMMARY: 0 errors      gtest 12 passed
  racecheck   on the residual + trajectory     RACECHECK SUMMARY: 0 hazards gtest  2 passed
  initcheck   on the residual + trajectory     ERROR SUMMARY: 0 errors      gtest  2 passed
  synccheck   on all 12 equivalence tests      ERROR SUMMARY: 0 errors      gtest 12 passed
```

```text
AND THE CONTROLS, WITHOUT WHICH THE FOUR ZEROS ABOVE MEAN NOTHING.

A clean sanitizer log names no kernels, so "0 errors" is indistinguishable from "the tool never
instrumented this library". The first version of this evidence counted log lines naming our kernels
and got 0 for all four tools -- which is what a clean run looks like, and also what a broken probe
looks like. So a deliberate fault was added to each of two control kernels IN THIS TRANSLATION
UNIT:

  memcheck  on a deliberate out-of-bounds read   ERROR SUMMARY: 68 errors
                                                 64 log lines naming sanitizerOobKernel
  racecheck on a deliberate shared-memory race   RACECHECK SUMMARY: 1 hazard
                                                 2 log lines naming sanitizerRaceKernel

Both tools report the planted fault by name, so their silence on the real kernels is a measurement.

NOT EQUALLY PROVEN, stated plainly: initcheck and synccheck have NO dedicated control. They share
compute-sanitizer's instrumentation with memcheck, which is demonstrated to be active on this
library, so their zeros are not baseless -- but they are weaker than memcheck's and racecheck's,
and this document does not pretend otherwise.
```

## 9. The negative controls

```text
positive case first: production, all controls off      0 of 1085 residual values differ

V1  HLL -- HLLC with the CONTACT WAVE DELETED, on device only     868 of 1085 differ
V3  the gather visiting a cell's faces in REVERSE order            821 of 1085 differ
V4  the neighbour re-evaluating with its OWN outward normal          3 of 1085 differ
V6  the CFL sound speed recomputed from the mean temperature       217 of 217 dt values differ
V8  the temperature range check removed                            device ACCEPTS T=4730.93 K
V2  one uploaded thermodynamic scalar regrouped                      1 of 20 values differ
V7  --fmad=true                                                    see §10
```

```text
V3 IS THE LOAD-BEARING ONE. Every term in the reversed gather is IDENTICAL; only the ORDER changes.
821 of 1085 values moving is the direct measurement that the ascending-order argument this whole
phase rests on is doing real work and is not a story told about an answer that would have come out
right anyway.

V4 FIRES ON ONLY 3 OF 1085, AND THAT IS REPORTED AS MEASURED. Re-evaluating the neighbour's side
with the flipped normal and swapped states is the exact negation of the owner's flux in almost
every case -- the arithmetic is symmetric enough that the rounding usually agrees. It is not always,
which is precisely why trap 1 is a trap: a port that got this wrong would pass a coarse comparison
and fail a bitwise one in 0.3% of cells, then compound. 3 is a small number and it is not zero.
```

## 10. V7 — what `--fmad=false` is actually worth (`logs/34_fma_summary.log`)

The flag is not new and was not introduced for this phase: it has applied to the twenty-odd
existing kernels since before P17.7. One translation unit was flipped, rebuilt, measured, restored,
and the restore proved by sha256.

```text
                              --fmad=false        --fmad=true
  A7  recovery                0 of 1736           133 of 1736 differ
  B1  face flux               0 of 2360           713 of 2360 differ
  B2  uniform residual        0 of 1085           562 of 1085 differ
  B2  graded residual         0 of  510           232 of  510 differ
  B2  3D residual             0 of  900           501 of  900 differ
  equivalence tests passing   12 of 12            5 of 12

  AND THE PART THAT MATTERS MOST -- the trajectory, under contraction:
      step  1   106 of 1025 differ
      step  2   171
      step  5   319
      step 10   463
      step 25   629
      step 50   704 of 1025
  A last-bit difference does not stay a last-bit difference. It spreads through the stencil at one
  cell per step and compounds. This is the concrete answer to "why demand bitwise rather than a
  tight tolerance": the tolerance that holds at step 1 does not hold at step 50, and choosing one
  means choosing how long you intend the agreement to last.

  RESTORE: cuda/CMakeLists.txt sha256 identical before and after
           (98e8ce7a96cf12751985979d85d343c56daf6e226e111800f888174fcea82069),
           and the restored build passes 12 of 12 again.
```

```text
ONE PROBE IN THIS EXPERIMENT READ ZERO FOR THE WRONG REASON, and it is recorded rather than
trimmed: the script counted build-log lines mentioning "fmad=true" and got 0, because the ninja
build log does not echo compile flags. The flag demonstrably took effect -- the results changed and
then changed back -- but that particular line of the log is not the evidence for it, and a reader
scanning for a zero should know which zeros mean what.
```

## 11. Trap 6 — the unmapped patch

```text
cpu refused    YES
device refused YES, with the message naming the patch and saying an unmapped patch is not assumed
               to be a wall
complete map   accepted by both -- so the refusal is not simply "upload always fails"
```

## 12. What these measurements do NOT establish

```text
THAT EITHER BACKEND IS CORRECT. Bitwise agreement says the two paths execute the same arithmetic in
the same order on the same data. It says nothing about whether that arithmetic is right.
BLOCKING_FINDINGS.md documents a case where both backends agree exactly on an answer that is
physically wrong, which is the sharpest illustration available of why equivalence and correctness
are separate claims and why P17.9's validation is not superseded by this phase.

ANYTHING ABOUT PERFORMANCE. §32 forbids a speed claim without equivalence; this phase makes none
even with it.

2D OR 3D SHOCK STRUCTURE, VISCOUS TERMS, SUBSONIC OPEN BOUNDARIES. All inherited scope limits from
P17.8 and P17.9, unchanged.
```
