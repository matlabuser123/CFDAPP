# P13 Final Qualification — G9, P14–P16 Preservation: final report

**Result: all criteria PASS. G9 → PASS.**

Date 2026-09-25 (UTC) / local. Tree `3bd32f918656-modified`.
**No production, test or harness source was changed.** `SOURCE_FINGERPRINT_V2` is
byte-identical before and after.

---

## 1. Scorecard

```text
P14 generic transport              PASS       104 tests
P14 CPU/GPU equivalence            PASS       included above
P15 thermal / energy               PASS       248 tests
P16 turbulence                     PASS       363 tests
P16 GPU equivalence                PASS       126 tests
P13-001 SIMPLE fingerprint         IDENTICAL  10 cases, 130 lines, bitwise
Known exclusions                   PRESERVED  same 5, verbatim from regress_both.sh
Known limitations                  PRESERVED  P20 deferral, sanitizer scope, Windows CUDA
SOURCE_FINGERPRINT_V2              MATCH      before == after, no delta

G9 -> PASS
```

**841 tests across 11 suites, zero failures**, plus the 4 required P20 failures
and the bitwise fingerprint.

## 2. The decisive instrument: the SIMPLE fingerprint is bitwise unchanged

Everything else in G9 proves the *new* capabilities still work. Only this proves
the *old* ones produce identical numbers.

```text
cases fingerprinted   10
frozen baseline       results/p13/restart/logs/fingerprint-after-histories.log
lines compared        130
result                BITWISE IDENTICAL
```

P14 added a generic transport layer and edited `TimeDerivative`; P15 and P16 added
solvers that run after SIMPLE. A passing CTest shows those additions work — it
cannot show SIMPLE still emits the same bits. This does, and it does not: **P14,
P15 and P16 moved not a single bit of the SIMPLE solution.**

## 3. Per-area results

All in `build/qual-final` (Release + CUDA ON + OpenMP ON), with the documented
exclusions applied.

```text
P14   p14_transport          68 tests   100% passed   transport, LegacyMatrix equivalence
      p14_gpu_transport      36 tests   100% passed   device scalar-transport semantics,
                                                      max-abs-difference, device fields/buffers
P15   p15_energy             96 tests   100% passed   energy equation, enthalpy, thermal
                                                      properties, temperature-dependent props
      p15_thermal_bc         53 tests   100% passed   fixed temperature, heat flux, convective,
                                                      adiabatic, conjugate, interface conductance
      p15_validation         99 tests   100% passed   thermal validation, heated cavity, natural
                                                      convection, Boussinesq, turbulent
                                                      conductivity, variable properties
P16   p16_unit              329 tests   100% passed   eddy viscosity, wall functions, wall
                                                      treatment, Spalding, momentum wall flux,
                                                      k-epsilon, k-omega
      p16_channel            14 tests   100% passed   channel-flow validation (the adopted
                                                      Re_tau gates), channel boundaries
      p16_thermal            20 tests   100% passed   turbulent thermal coupling, turbulent
                                                      heat transfer
P16   p16_gpu_turb           51 tests   100% passed   CUDA turbulence algebra, k transport,
GPU                                                   epsilon transport, k-epsilon correction,
                                                      effective viscosity, wall-face viscosity,
                                                      boundary encoding
      p16_gpu_resident       55 tests   100% passed   resident SIMPLE integration, backend
                                                      trace, production k-epsilon backend
      p16_gpu_determinism    20 tests   100% passed   deterministic GPU repeats, Krylov restart
                                                      parity, residency manager, CPU fallback
```

## 4. The preserved P20 failures: RED, and exactly the documented set

Run separately and deliberately, because the frozen
`run_p20_known_failures.sh` states the contract: *"Expected result: RED. A GREEN
result here is itself a finding."*

```text
exit status 1          non-zero is REQUIRED
8 passed, 4 failed

FAILED  SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate            366 s
FAILED  SSTAutomaticWallChannel.StaysWellBehavedOnMeshesTheHighRePathRejects  375 s
FAILED  SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590                377 s
FAILED  SSTMomentumFluxChannel.FourWayComparisonAtReTau590                    594 s
```

Exactly the four documented deferred automatic-wall failures. **The set neither
grew nor shrank**, and it matches what G2's CPU-only regression recorded. The 8
passing tests in the same suites (including `WallArchitectureDiagnostic` and
`IsDeterministic`) also still pass, so the boundary between deferred-failing and
still-working has not moved.

Per the instruction, these are **not** reinterpreted as a G9 failure: they are the
documented P20-deferred limitation, unchanged.

Cost note: these four took 28 minutes of the gate's 2h15m. That is precisely why
`regress_both.sh` excludes them from the routine regression — and why verifying
them rather than trusting them is worth doing once, here.

## 5. Exclusions and limitations, verified unchanged

```text
this gate's filter, copied verbatim from regress_both.sh:
  SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|
  WallArchitectureDiagnostic|DriverSeegmiller.CorrectedMeshSequence

regress_both.sh still declares the same set:
  line 54  EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|
                    SSTMomentumFluxChannel|WallArchitectureDiagnostic'
  line 56  DEFERRED_BFS='DriverSeegmiller.CorrectedMeshSequence'
```

Not widened, not narrowed. Other limitations confirmed still stated, not
silently upgraded:

```text
k-omega SST advanced near-wall / separated-flow   DEFERRED to P20   (TODO.md:31)
sanitizer scope, coverage-driven                  44/44 exercised P16 CUDA kernels
                                                  covered, 114/196 tests sanitized,
                                                  synccheck negative-control
                                                  detection remains unproven  (TODO.md:45)
native Windows CUDA                               separately unqualified
Robin BC on GPU                                   unsupported, as P15 documented
```

The sanitizer figures were **not** re-measured and are **not** claimed stronger
than recorded.

## 6. A build-tree substitution, and a link-flag error of mine

### The substitution

Every frozen P14/P15/P16 script hardcodes `build/final`, which was Release+CUDA
when they were written. It is now:

```text
build/final        CFDAPP_ENABLE_CUDA:BOOL=OFF    cuda/libcfdcuda.a ABSENT
build/qual-final   CFDAPP_ENABLE_CUDA:BOOL=ON     cuda/libcfdcuda.a present
```

So P14's `fingerprint.sh` could not have linked, and every GPU suite would have
been **absent rather than failing** — a silent pass by omission, which is the
worst outcome a preservation gate can have. G9 therefore ran against
`build/qual-final`, the G1 qualification tree. Same criteria, same tests, a tree
that can run them.

**This is the third consecutive gate in which `build/final` was not what a frozen
script assumed:**

```text
G6  its cfdapp binary dated from inside another session's control window,
    so the gate would have measured a control-contaminated executable
G8  CUDA OFF, so two GPU benchmarks were MISSING and the driver exited 1
G9  CUDA OFF, so the fingerprint could not link and GPU suites would be absent
```

Frozen scripts pin a build directory by name; build directories drift. Worth
carrying into G10.

### The error

G9's first fingerprint attempt **failed to link**:

```text
build/qual-final/src/libcfdcore.a(SparseMatrix.cpp.o):
  undefined reference to `omp_get_num_threads'
  undefined reference to `GOMP_parallel'
```

`build/qual-final` has OpenMP ON, so linking its `libcfdcore.a` requires
`-fopenmp`, which the frozen compile line omits — it was written against a tree
with OpenMP off. Classification: **invalid gate invocation in my adaptation**
(category 5), not a regression, no production code involved. The failed link is
preserved at `logs/G9_fp_build.log`; the corrected run is
`tools/G9_fingerprint.sh`.

This is the **second consecutive gate** in which the OpenMP difference between
those two trees caused a problem — in G8 it produced three false "REGRESSION"
verdicts because the comparator could not see the build configuration (G8-F1).
Same underlying fact, two different symptoms.

## 7. Source fingerprint

```text
V2 before G9   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 after  G9   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

Equal. Components unchanged, 112 tracked and 122 untracked files. G9 changed no
source. No repo-root strays; the only file modified at the repository root is
`TODO.md`, which is outside V2 by definition.

## 8. What this gate does not claim

* It re-ran the frozen preservation **subsets**, not the three-configuration full
  regression — that is G10's gate, and running it here would duplicate it and
  invite the contention that invalidated G3's first invocation.
* The sanitizer coverage figures are cited from P16, not re-measured.
* No advanced SST work was restarted, and no P20 test was enabled, disabled or
  reinterpreted.
* G8 remains open on findings G8-F1 and G8-F2. G9 was begun under explicit
  instruction; it does not close or affect G8.
