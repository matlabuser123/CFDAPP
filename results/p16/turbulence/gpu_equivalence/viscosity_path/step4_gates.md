# Step 4 — the four closing gates

```text
1. frozen channel benchmark — CPU        PASS   5/5, build/final (CUDA OFF)
2. frozen channel benchmark — GPU        PASS   same gate body, resident GPU path
3. P13 restart / refusal re-verification PASS   33/33 + 9/9, refusal now has a test
4. production residency ledger           PASS   0 allocations, no field leaked
```

Two defects and one gap were found while closing them, all three by
non-vacuity controls, and all three are recorded below rather than quietly
fixed.

---

## 1. CPU frozen channel benchmark — PASS

`WallFunctionChannelHighRe.*`, 5/5, 105.8 s on `build/final`. The G1-valid
subset of the Re_tau = 590 family:

| mesh | y1+ | Re_tau | E_Re_tau | u+ L∞ | tau_w |
|---|---|---|---|---|---|
| ny06 | 92.511 | 555.373 | 0.0587 | 1.4903 | 2.614024e-03 |
| ny08 | 71.572 | 572.817 | 0.0291 | 0.7776 | 2.780813e-03 |
| ny12 | 49.109 | 589.533 | 0.0008 | 0.2662 | 2.945487e-03 |
| ny16 | 37.130 | 594.585 | 0.0078 | 0.6981 | 2.996181e-03 |

ny20/ny24/ny32 are G1-REJECT — outside the wall function's validity band, as
the frozen methodology requires them to be.

## 2. GPU frozen channel benchmark — PASS

The **same** benchmark, the **same** gate body, through the resident GPU
k-epsilon production path.

The gate is now a single function, `expectAdoptedGate`, that both arms call.
Two copies of an acceptance criterion is how two benchmarks drift apart, and
this file's header already says exactly that about the measurement harness.

### The adapter is four lines

`enableGpuDiscretization` has no case-file key (GPU-DISC-001R), so unlike the
linear-solver backend it cannot be selected through a case directory. The
adapter flips backend selection on the built `SimulationSetup` and nothing
else — geometry, mesh, forcing, Reynolds number, wall law, turbulence
constants, solver tolerances and every acceptance criterion still come from the
same case files, unchanged.

### Provenance, per mesh

```text
strain rate / production / k assembly / k solve /
epsilon assembly / epsilon solve / mu_t          GPU
wall treatment                                    CPU     (recorded P16 scope)
turbulenceGpuPathQualified()                      true
allOnGpu()                                        false   asserted, not tolerated
```

`allOnGpu()` is required to be FALSE. A true there would mean the recorded
decision to leave the O(sqrt(N)) wall treatment on the host had silently
changed.

### CPU vs GPU, side by side

```text
mesh       G1  Re_tau(C)  Re_tau(G)       E(C)       E(G)  u+Linf(C)  u+Linf(G)
ny06       ok   555.3727   555.3727    0.05869    0.05869     1.4903     1.4903
ny08       ok   572.8168   572.8168    0.02912    0.02912     0.7776     0.7776
ny12       ok   589.5333   589.5333    0.00079    0.00079     0.2662     0.2662
ny16       ok   594.5848   594.5848    0.00777    0.00777     0.6981     0.6981
ny20      rej   596.2053   596.2053    0.01052    0.01052     0.3281     0.3281
ny24      rej   597.1230   597.1230    0.01207    0.01207     0.5556     0.5556
ny32      rej   598.0430   598.0430    0.01363    0.01363     0.8294     0.8294
```

Identical to every printed digit, and the iteration counts match exactly
(366/369/393/416/434/445/442 on both). At full precision they do differ:

```text
mesh     it(C) it(G)   gpuOps  wallOps            tau_w(C)            tau_w(G)  rel|d|
ny06       366   366      366      365  2.61402388553700748e-03  2.61402388509658200e-03  1.685e-10
ny08       369   369      369      368  2.78081341422915275e-03  2.78081341124404319e-03  1.073e-09
ny12       393   393      393      392  2.94548671584416236e-03  2.94548671564147770e-03  6.881e-11
ny16       416   416      416      415  2.99618070314694885e-03  2.99618070336034499e-03  7.122e-11
ny20       434   434      434      433  3.01253465646849359e-03  3.01253465376962078e-03  8.959e-10
ny24       445   445      445      444  3.02181599547023724e-03  3.02181599543249312e-03  1.249e-11
ny32       442   442      442      441  3.03113512499911210e-03  3.03113512471789954e-03  9.277e-11
```

The GPU arm satisfies the frozen physical benchmark **independently**. The
comparison above is evidence, not the criterion: two arms can agree with each
other while both are wrong.

`gpuOps == iterations` exactly, and `wallOps == iterations - 1` — iteration 0
has no resident state to correct from, so it runs the host turbulence path.
That off-by-one is the signature of the real control flow, not a coincidence.

### DEFECT FOUND — the backend trace is a process-lifetime accumulator

The non-vacuity control was to require the SAME provenance predicate to be
FALSE on the CPU arm. It failed: the CPU arm reported
`turbulenceGpuPathQualified() == true`, with 2865 executions — **exactly the
sum of the GPU sweep's iterations** (366+369+393+416+434+445+442).

`TurbulenceBackendTrace.hpp` claimed:

> Process-wide, same convention as gpuExecutionStats(). A solve resets it at
> entry; a test reads it afterwards.

No production path calls `reset()`, and none ever did. Worse, the host
k-epsilon path records **nothing** — only the device implementations call
`record()` — so a CPU solve leaves the trace exactly as the previous GPU solve
left it. Any reader that does not reset first is told the GPU path ran.

Fixed by correcting the header to say what the code does, and by resetting in
the harness immediately before each solve. With that, the CPU arm reports zero
executions and `qualified == false`, and the GPU claim stands on its own.

The GPU gate had been passing before this was found. It was passing for the
right reason — the GPU sweep ran first — but that was ordering luck, not
evidence.

## 3. P13 restart / refusal re-verification — PASS

```text
CFDRestartIntegrationTests                33/33
CheckpointProductionCaseTest               9/9   (6 existing + 3 new)
```

### Reachability of the test-only injection fixture

```text
KEpsilonModelTestAccess appears in:
  include/cfd/turbulence/KEpsilonModel.hpp             forward + friend declaration only
  tests/unit/gpu/KEpsilonModelTestAccess.hpp           the definition
  tests/unit/gpu/test_resident_channel_production.cpp  the one consumer
```

The class is DEFINED only under `tests/unit/gpu/`, a directory CMake adds only
when `CFDAPP_ENABLE_CUDA=ON`. Production carries a friend declaration, which
grants access to a type that does not exist there. There is no case-file,
checkpoint, CLI or GUI path to it.

### GAP FOUND — the refusal had no test at all

The P13 turbulence refusal was read and believed, never exercised. Its constant
and its trigger sat in `ProjectRunner.cpp` with nothing pinning them.

That matters more now than it did before Step 4: the mature-state injection
fixture's entire safety argument is that no production path reaches it, and a
refusal with no test is exactly what erodes unnoticed.

Three tests added, through the production path
(`case directory -> CaseReader -> CaseBuilder -> ProjectRunner`):

```text
CheckpointingATurbulentCaseIsRefusedByName        refused, by name, nothing written
ResumingATurbulentCaseIsRefusedByName             refused at the other end too
TheSameTurbulentCaseRunsWithoutCheckpointOptions  NON-VACUITY
```

The third is what makes the first two mean anything: they would pass just as
well if a turbulent case failed for any reason at all — a JSON typo in the
fixture helper, an unsupported model, a diverging solve. The same turbulent
fixture with the checkpoint options removed and nothing else changed completes
its 5 iterations, so the refusal is about checkpointing, not about turbulence.

Turbulence is switched on through the production case-file key
(`physics.json`'s `turbulence` object), not through a test hook.

## 4. Production residency ledger — PASS

```text
STEADY-STATE RESIDENT TURBULENT ITERATION (288 cells, 612 faces)
  D2H 30432 bytes in 648 calls
  H2D 2712 bytes in 13 calls
  allocations -7   reallocations 0

5 further corrections: 0 allocations, 0 reallocations
  D2H 64324 bytes in 928 calls = 12864 bytes per correction
  largest single D2H 2304 bytes, 15 copies of that size over 5 corrections
```

The `-7` is the instrument bottoming out, not seven allocations returned: it is
a difference between two whole solves, and once the steady state genuinely
allocates nothing the difference measures warm-up, not the steady state. The
exact-zero claim is owned by the per-correction gate.

### H2D is O(N_wall) — now measured, not argued

The ledger built a taller mesh and then discarded it — `(void)tallCase;` — so
the scaling claim in its own comment was prose. It is measured now, on a mesh
with twice the cells and the same nx:

```text
H2D at 48 wall faces: 2712 bytes on 288 cells, 2712 bytes on 576 cells
```

Byte-for-byte equal across a doubled cell count. No upload is O(N).

### The flux-recompute residency check, and two criteria that were wrong

The Step-4 flux fix made the resident path rebuild its own convection flux with
`calculateMassFluxDevice`. The question is whether that rebuild pulls the
velocity or the face flux back to the host first.

Two attempts to gate it failed, both in the same way:

```text
attempt 1: per-ITERATION D2H growth  <  one field over the added cells
           41160 vs 4608   FAIL
attempt 2: per-CORRECTION D2H total  <  one cell field
           12864 vs 2304   FAIL
```

Neither was a code defect. Both bounded a **total** by a **per-transfer** size:

```text
D2H calls        648  ->  1144     (doubling the mesh)
mean bytes/call  46.96 -> 62.58
```

The bytes grow because the CALL COUNT grows — a bigger mesh needs more Krylov
iterations, and each fetches a handful of scalars. A correction moves ~12.8 kB
in ~186 calls, ~69 bytes each: more than a field in total while containing
nothing remotely field-sized. A byte-total budget charges solver work against a
field-sized allowance and cannot tell "many scalars" from "one field".

The quantity that separates them is the size of the **largest single
transfer**, and no counter exposed it. Two were added to `GPUExecutionStats`:

```text
largestDeviceToHostBytes           high-water mark, cleared by reset
deviceToHostCallsAtLargestSize     how many copies were that size
```

The size alone is still not enough, because one velocity COMPONENT is also
exactly one cell field. The count settles it:

```text
largest single D2H     2304 bytes = one cell field   (a face field is 4896)
copies of that size    15 over 5 corrections = exactly 3 per correction
```

Three per correction is k, epsilon and mu_t — the host `ScalarField`s
`TurbulenceModel`'s interface publishes, a cost recorded and accepted at
`GpuKEpsilonCorrectionCuda.cpp`'s `runCorrection`. Two more copies would be a
velocity download, three more a full one.

So:

```text
no full face-flux transfer     largest D2H 2304 < 4896             PASS
no velocity D2H                exactly 3 field-sized copies        PASS
no per-iteration allocation    0 allocations across 5 corrections  PASS
no growing flux storage        0 reallocations                     PASS
```

### The instrument is controlled

A bound is worth exactly what the counter's ability to see a violation is
worth. A deliberate full-field download is performed and both counters are
required to register it — at full size, and at a count of exactly one. Without
that, a counter that had silently stopped working would report a perfect
residency contract.

---

# Closeout - exact-tree qualification

The qualification binaries are proven to correspond to the final source, and
the regression was run with the repository's own instrument.

```text
exact-tree rebuild
  BEFORE  source 06:31:13   libcfdcuda.a 06:17:25    stale by one comment-only edit
  AFTER   source 06:31:13   libcfdcuda.a 07:02:20    rebuilt
  PROOF   build/cuda   "ninja: no work to do."
          build/final  Built target ... with no compile or link lines
                       (Makefile generator -- the same evidence, different wording)

authoritative regression  (regress_both.sh p16, BOTH configs, exact tree)
  build/cuda   2525 passed   0 failed
  build/final  2326 passed   0 failed
```

Each Step-4 gate confirmed BY NAME inside that run, not inferred from the
totals:

```text
WallFunctionChannelHighReGpu.EveryG1ValidMeshPassesTheAdoptedGate     Passed
WallFunctionChannelHighRe.EveryG1ValidMeshPassesTheAdoptedGate        Passed
CheckpointProductionCaseTest.CheckpointingATurbulentCaseIsRefusedByName       Passed
CheckpointProductionCaseTest.ResumingATurbulentCaseIsRefusedByName            Passed
CheckpointProductionCaseTest.TheSameTurbulentCaseRunsWithoutCheckpointOptions Passed
ResidentChannelProduction.RepeatedCorrectionsAllocateNothing          Passed
ResidentChannelProduction.SteadyStateResidentIterationLedger          Passed
```

## An instrument error worth recording

A first regression attempt was run as a bare `ctest -E "DriverSeegmiller"`
rather than through `regress_both.sh`. It came back RED with four failures:

```text
SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate
SSTAutomaticWallChannel.StaysWellBehavedOnMeshesTheHighRePathRejects
SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590
SSTMomentumFluxChannel.FourWayComparisonAtReTau590
```

These are the four P20 known-expected-failure suites that `regress_both.sh`
excludes in BOTH of its modes, for the reason its own header states: *"Leaving
them in a regression makes it permanently red and MASKS a real regression."*
The ad-hoc filter also used ctest's default 1500 s timeout instead of the
required 7200 s.

Classified before anything was changed, and classified as category C
(preserved, deferred) on measurement rather than on the test names:

```text
                 SUMMARY.md (recorded)      measured during classification
ny06 automatic   0.0347                     0.0347
ny12 automatic   0.0483                     0.0483
ny16 automatic   0.0698                     0.0698
ny16 u+ Linf     2.565                      2.5654
ny06 HighRe      0.0556                     0.0556
ny12 HighRe      0.0003                     0.0003
ny16 HighRe      0.0059                     0.0059
```

Both arms reproduce the recorded table digit for digit, and the three SST
bitwise guards -- `TheDefaultCaseFileIsBitwiseTheHighRePath`,
`HighRePathIsStillBitwiseUnchanged`, `IsDeterministic` -- all pass, which is
what establishes that Step 4 did not touch the SST path.

`DoesNotRegressTheAdoptedHighReGate` is worth a note: the name reads like a
guard, but its body asserts that the AUTOMATIC treatment meets the adopted
gate, which SUMMARY.md documents it does not. A name is not a specification,
and this one would have been easy to mistake for a regression.

