# P17.10 — CPU/GPU Equivalence — SUMMARY

```text
STATUS   P17.10 COMPLETE
GATE     results/p17/cpu_gpu_equivalence/acceptance_gate.md
         sha256 c5b86211dce2128152f5fe8d5a93b8ba739fc596da562f485af18d3ce6efbb51
         FROZEN before any CUDA source for this path existed, and UNMOVED since.
POLICY   tolerance_policy.md, frozen before the gate. One word: BITWISE. No fallback clause.
SCOPE    the density-based compressible solver. G8 moves from NOT APPLICABLE to CLAIMED.
         G10 is P17.11's and is not claimed here.
```

---

## 1. P17.10 was an implementation phase, not only a comparison phase

P17.8 recorded, in its own header, that *"there is no GPU density-based solver in P17.8 … the
residual assembly, the CFL reduction and the update are not [qualified]"*. P17.9 inherited that,
which is why its gate lists G8 as NOT APPLICABLE. So there was nothing to compare until this phase
wrote one.

```text
BUILT   six kernels + two reductions, in one new .cu and two new headers
        NO ATOMICS anywhere in them
        no production CPU source modified -- libcfdcore.a came out BYTE-IDENTICAL to the archive
        P17.8 and P17.9 closed with
```

## 2. The one decision the whole phase rests on

```text
Face ids come from a monotonically increasing counter and Cell::addFace appends without sorting, so
EVERY CELL'S FACE LIST IS ASCENDING BY CONSTRUCTION. The CPU accumulates each cell's residual by
looping faces in ascending id order. A per-cell GATHER therefore adds exactly the same terms in
exactly the same order -- so the residual can be BITWISE rather than merely close, and no atomics
are needed, which is separately what makes the GPU result reproducible run to run.

The alternative -- a scatter with atomicAdd -- would have been both non-bitwise AND
non-deterministic. The gather was chosen for correctness; the fact that it is also faster is
incidental and is not claimed as a result.

MEASURED BEFORE ANY KERNEL EXISTED: 702 cell-face references across five mesh construction paths,
all ascending. And the check lives in PRODUCTION, not only in a test -- upload() refuses a mesh
whose face lists are not ascending, so a future mesh path cannot silently break the equality.
```

## 3. What was proven

```text
LEVEL A  arithmetic
  device recovery vs TotalEnergyState::recover()    120 comparisons, 2 gases, BITWISE
  the kernel's recovery vs production toPrimitive() 1736 values, BITWISE
  exteriorState, 3 kinds x 6 normals                144 values, BITWISE
  the REFUSALS and their REASONS                    6 of 6 agree, reason for reason

LEVEL B  the local operator
  per-face flux                                     472 faces x 5, BITWISE
  per-cell residual, uniform / graded / 3D          2495 values, BITWISE
    the graded mesh has a 6849:1 volume ratio, so a dropped 1/V cannot hide in it
  the gather in isolation (B5)                      510 values rebuilt from the device's own face
                                                    fluxes, BITWISE, 362 interior references

LEVEL C  one step and the controls
  local dt / global dt                              BITWISE   5.55587098248344881e-08 both sides
  one update from an identical Q0                   1025 values, BITWISE
  the REJECT path                                   3 kinds of inadmissible state, TWO offenders
                                                    each; both backends name cell 9, reasons 5/2/4

LEVELS D/E  trajectories and a benchmark
  1, 2, 5, 10, 25, 50 steps                         1025 values each, BITWISE; dt bitwise at every
                                                    one of the 50 steps
  first divergence                                  NONE
  Sod, 500 steps                                    3000 values BITWISE, 101 cells at an
                                                    intermediate state
  P17.9's own detectors on both solutions           shock at 0.7649115536763863 on both, BITWISE;
                                                    transition width 7 cells on both

DETERMINISM
  CPU repeated                                      BITWISE (34 steps, state and dt)
  GPU repeated, same process                        BITWISE (30 steps)
  GPU repeated, SEPARATE PROCESSES                  BITWISE -- complete stdout identical, sha256
                                                    6e44d998...; a 1-char perturbation is detected
  GPU at 64 vs 128 threads per block                BITWISE -- the criterion that actually tests
                                                    the order-independence argument

TRANSFER AND RESIDENCY
  upload/download pair                              460 values round-tripped unchanged
  a 20-step resident run                            180 kernel launches, 0 host->device per step,
                                                    40 device->host against 100 for a full
                                                    per-step round trip

CUDA DIAGNOSTICS
  memcheck / racecheck / initcheck / synccheck      0 errors, 0 hazards
  and the CONTROLS that make those zeros mean something:
    memcheck  on a deliberate out-of-bounds read    68 errors, 64 lines naming the kernel
    racecheck on a deliberate shared-memory race    1 hazard, naming the kernel
```

## 4. The negative controls — the evidence the gate means something

```text
production, all controls off                        0 of 1085 residual values differ
V1  HLL -- the contact wave deleted, device only   868 of 1085
V3  the gather order REVERSED                       821 of 1085
V4  the neighbour re-evaluating with its own normal   3 of 1085
V6  CFL sound speed from the mean temperature       217 of 217 dt values
V8  the temperature range check removed             device accepts 4730.93 K vs a 2500 K ceiling
V2  (rho*R)*T regrouped as rho*(R*T)                  1 of 20 values
V5  rhoE carried over instead of rebuilt            2 of 3 non-axis-aligned normals
V7  --fmad=true                                     see below

V3 IS THE LOAD-BEARING CONTROL. Every term in the reversed gather is identical; only the ORDER
changes. 821 of 1085 values moving is the direct measurement that the ascending-order argument is
doing real work and is not a story told about an answer that would have come out right anyway.

V4 AND V2 FIRE WEAKLY -- 3 of 1085 and 1 of 20 -- and that is reported as measured rather than
dressed up. Both are cases where two mathematically identical expressions usually round the same
way. "Usually" is why they are traps.
```

## 5. V7 — what `--fmad=false` is worth, and why bitwise rather than a tight tolerance

```text
                          --fmad=false    --fmad=true
  recovery                 0 of 1736      133 of 1736
  face flux                0 of 2360      713 of 2360
  residual (3 meshes)      0 of 2495     1295 of 2495
  equivalence tests         15 of 15        8 of 15

  the trajectory, under contraction:
     step  1   106 of 1025 differ
     step  5   319
     step 25   629
     step 50   704 of 1025

A last-bit difference does not stay a last-bit difference: it spreads through the stencil at one
cell per step and compounds. That is the concrete answer to "why not just use a tight tolerance" --
the tolerance that holds at step 1 does not hold at step 50, so choosing one is choosing how long
you intend the agreement to last. The flag is not new: it has applied to the twenty-odd existing
kernels since before P17.7, and the experiment restored cleanly, proved by sha256.
```

## 6. What P17.10 FOUND, which is not an equivalence result

```text
Toro's pressure-based wave-speed estimate DOES NOT BOUND a strong symmetric head-on collision --
which is exactly what an inviscid wall produces when the flow is normal to it.

Above a normal Mach number of ~1.76:
  * the required ordering S_L <= S* <= S_R is VIOLATED
  * the star densities go NEGATIVE
  * HLLC selects the LeftSupersonic branch and returns the UNDISTURBED UPSTREAM FLUX for a
    configuration in which a strong shock is forming
  * and hllcFlux() still reports valid = true

  Ma 2.02:  estimate S_L = +58.0   exact S_L = -264.9   exact p*/p1 = 8.84
  The exact column is closed form, from the normal-shock relations, with no solver involved.

REPORTED, NOT FIXED. It is in P17.7's flux layer, which P17.10 is not authorized to rewrite, and a
wave-speed estimator is a numerical-scheme change with its own verification surface -- P17.7 needed
a 14-case bounding study and a 60-digit reference to qualify the current one.

CPU AND GPU AGREE BITWISE ON THIS CASE TOO. That is the sharpest illustration available of why
equivalence is not correctness, and why P17.9's validation is not superseded by this phase.

Details, impact, and three options for whoever authorizes a fix: BLOCKING_FINDINGS.md.
Reproducer: P17_10_Finding.DOCUMENTS_A_DEFECT_ToroWaveSpeedsDoNotBoundAHeadOnCollision.
```

## 7. Four criteria that nothing measured, found by mapping the frozen gate afterwards

```text
B5  the owner/neighbour cancellation   -- true by construction, tested by nothing
C4  the REJECT path                    -- every trajectory asserted admissible == true, so the
                                          refusal path had never been exercised at all
D4  the detected wave positions        -- covered only by the argument "the states are identical"
R3  separate-process determinism       -- R2 covered same-process repeats only

ALL FOUR WERE CLOSED before the final regression, not argued away -- three with new tests, R3 with a
script over the existing binary. None was found by a failing test; they were found by writing
coverage_map.md against the frozen gate line by line. Three gaps found the same way remain open and
are listed there rather than implied absent: C5 (a run in which the CPU actually retries), D5
(normal-shock jump ratios on the GPU), T4 (no-reallocation on mesh re-upload).
```

## 8. Probes that printed what success looks like while measuring nothing

```text
THE SANITIZER EVIDENCE, FIRST VERSION. It counted log lines naming our kernels and got 0 for all
four tools. That is what a CLEAN run looks like -- a sanitizer names a kernel only when something is
wrong -- and it is also what "the tool never instrumented this library" looks like. The two are
indistinguishable from the error count alone. Fixed with live control kernels; the zeros are now
measurements.

D3's "A DISCONTINUITY DEVELOPED" CHECK. Cell ids run j*nx+i, so a scan over all cells compares the
LAST cell of one row with the FIRST of the next -- across the initial discontinuity -- and reports a
jump of 0.8750 no matter what the solver did. The check written to rule out "nothing happened" was
measuring precisely the artefact it was meant to exclude, and the run really had barely developed:
at 120 steps the wave structure spanned ~20 cells. Now measured along one row, at 500 steps, with
101 cells at an intermediate state asserted.

BOTH ARE RECORDED RATHER THAN QUIETLY REPLACED, because a probe that prints what success looks like
is the failure mode this project has a standing finding about, and two more instances of it are
worth more as evidence than as tidy history.
```

## 9. Regression

```text
                    release_cuda        cpu_only
registered          2776  PRED 2776     2553  PRED 2553
EXECUTED            2714  PRED 2714     2493  PRED 2493
passed              100%                100%
FAILED                 0  PRED 0           0  PRED 0
staleness           0 stale of 50       0 stale of 49
runtime             2099 s              1449 s
COUNT VERDICT       MATCHES             MATCHES

src/libcfdcore.a    188e5039...         1305a82a...
                    BOTH IDENTICAL to the archives P17.8 and P17.9 closed with -- and the CPU-only
                    one is a FROM-SCRATCH rebuild that reproduced P17.9's bit for bit.
```

Two earlier runs are preserved rather than discarded: one failed closed on a staleness false
positive (`STALENESS_FAILURE.md`), one reported three "failures" that were a Debug build caused by
my deleting a build tree whose configuration existed only as cache state
(`HARNESS_FINDINGS.md` §2). Release+CUDA passed 2714/2714 in all three. Details in `regression.md`.

## 10. Gate verdict

```text
G1  baseline preservation          PASS   no CPU production source modified; libcfdcore.a
                                          byte-identical to P17.8's and P17.9's archive
G6  HLLC correctness in solver     PASS   inherited, re-run in both configurations
G7  shock validation               PASS   inherited from P17.9, re-run; and now measured on the GPU
                                          as well (D4)
G8  CPU/GPU solver equivalence     PASS   this phase -- BITWISE at every level, with a non-vacuous
                                          control set and live sanitizer controls
G9  deterministic repeatability    PASS   both backends, and across block sizes on the GPU
G10 full qualification             NOT CLAIMED -- P17.11's
```

## 11. What is NOT claimed

```text
PERFORMANCE            §32 forbids a speed claim without equivalence. This phase makes none even
                       with it. No timing number in this evidence is a claim.
CORRECTNESS            bitwise agreement says the two paths execute the same arithmetic in the same
                       order. §6 above is a case where they agree exactly on a wrong answer.
2D / 3D shock structure, viscous terms, subsonic open boundaries, higher-order accuracy
                       inherited scope limits from P17.8 and P17.9, unchanged.
normal-shock jump ratios on the GPU, a CPU-retry comparison, mesh re-upload allocation behaviour
                       NOT MEASURED -- coverage_map.md §gaps.
Debug+GUI, host sanitizers
                       NOT RUN -- deferred to P17.11 by the standing decision every P17 phase has
                       recorded. Not skipped for runtime.
native Windows         WSL2 Ubuntu-22.04 only.
```
