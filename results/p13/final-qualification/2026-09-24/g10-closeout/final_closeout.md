# G10 — Final Closeout: status report

**5 of 6 G10 criteria PASS. G10 is NOT closed, and P13 is NOT finished.**

Date 2026-09-25 (UTC). Two blockers, both stated below; neither is a CFD defect.

---

## Final verification table

| Gate | Result | Evidence |
|---|---|---|
| Release+CUDA regression | **PASS** — 2488/2488, 0 failed, 36.0 min | `logs/G10_release_cuda_ctest.log` |
| Debug+GUI regression | **NOT PASS** — 2547/2550, 3 Timeouts, 174.1 min | `logs/G10_debug_gui_ctest.log`, `failure_audit.md` |
| CPU-only regression | **PASS** — 2288/2288, 0 failed, 32.9 min | `logs/G10_cpu_only_ctest.log` |
| Documented exclusions preserved | **PASS** — 5 items, all REGISTERED + EXCLUDED | `exclusions_audit.md` |
| No unexplained failures | **PASS** — 3 of 3 classified and quantified | `failure_audit.md` |
| Source fingerprint matches | **PASS** — identical start to end | `source_fingerprint.md` |

---

## Source identity

```text
HEAD                    3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
branch                  main
working tree            DIRTY — P13/P14/P15/P16 uncommitted (commit not authorized)
tracked modified        163      untracked 106      (unchanged across all of G10)

SOURCE_FINGERPRINT_V2 at G10 start   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
SOURCE_FINGERPRINT_V2 at G10 end     8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
                                     MATCH
```

Unmoved across **six** consecutive gates (G4, G5, G6, G7, G9, G10). No production
source changed during closeout. 112 tracked and 122 untracked engineering files,
both unchanged.

Build identity (`3bd32f918656-modified`) and V2 are different things and are not
conflated: the first is application metadata, the second qualification provenance.

## Regression matrix

```text
                 configuration                              tests  failed  Not Run  Disabled  runtime
Release+CUDA     Release  CUDA=ON   GUI=OFF  OpenMP=ON        2488       0        0        98   36.0 min
Debug+GUI        Debug    CUDA=ON   GUI=ON   OpenMP=OFF       2550       3        0        98  174.1 min
CPU-only         Release  CUDA=OFF  GUI=OFF  OpenMP=ON        2288       0        0        94   32.9 min
```

Invocation, frozen: `OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 exclusions>`.

Release+CUDA's 2488 and CPU-only's 2288 match G2's recorded counts exactly, which
independently confirms the exclusion set was applied as G2 applied it.

**CPU-only carries no CUDA dependency:**

```text
nvcc invocations in the build log   0
libcudart references               0
cuda/libcfdcuda.a                  absent
```

## Documented exclusions — 5, enumerated

Copied verbatim from `regress_both.sh` (p16 mode), matching `PLAN_2026-09-24.md`.
Every one is **REGISTERED and EXCLUDED**, never NOT REGISTERED:

```text
SSTAutomaticWallChannel           deferred automatic wall treatment, attempt 2
SSTCompositeWallChannel           attempt 4, Spalding composite law
SSTMomentumFluxChannel            attempt 5, F_U = -rho u_tau u*
WallArchitectureDiagnostic        diagnostic for the three deferred treatments
DriverSeegmiller.CorrectedMeshSequence   preserved failing separated-flow benchmark
```

All deferred to **P20 (NOT AUTHORIZED)**. G9 re-ran the four automatic-wall suites
directly: exactly four failures, the documented ones, neither grown nor shrunk,
with the 8 passing tests in those suites still passing. **Unexpected exclusions: 0.**

## Qualification-specific tests, verified BY NAME

All 24 checked rows registered; the two added during G4 both ran and passed:

```text
CliGuiEquivalenceTest.AThermalCaseAgreesBitwise                 Passed  0.33 s
CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli  Passed  0.28 s
```

This mattered: an audit taken before the Debug tree was rebuilt showed both as
absent, because `gtest_discover_tests` populates the registry at build time and
`build/qual-debug` dated from G1, 18 hours before those tests were written. A
count-based check would have certified a Debug+GUI regression that never executed
them. Preserved at `logs/PREMATURE_exclusions_audit_stale_qual_debug.md`.

## Prior evidence — intact

All G1–G9 reports present, plus every preserved failure and amendment: G3's
invalid `-j16` run, G4's 7/11 partial and 10/11 pre-F4 report and the raw N2a
UNDETECTED log, the G4 fingerprint amendment, G5's premature/W5-invalid logs,
the 2026-09-21 P13-003 and P13-006 originals, G8's two open findings, and G10's
own invalid first invocation. Prior-phase trees untouched: p14 34 files, p15 82,
p16 329, p13/restart 42, case-validation 88, cli-gui 10, export 8, diagnostics 47,
packaging 27, benchmarks 26.

Repo root clean of qualification artifacts. The 21 `cmake_test_discovery_*.json`
files there date from 2026-09-11 and predate this qualification entirely.

---

## Blockers

### 1. Debug+GUI: three Debug-runtime timeouts

```text
DriverSeegmiller.CorrectedSequenceIsDeterministic       Timeout
DriverSeegmillerInlet.MappedInletPreservesTheMassFlow   Timeout
DriverSeegmillerInlet.MediumGridAB                      Timeout
```

Fully explained, not a regression: all three pass in Release+CUDA (1529–2030 s)
and CPU-only (1170–1480 s), and the Debug/Release ratio measured over 123 samples
is mean **5.0x**, median 5.4x — projecting 7582 / 9668 / 10063 s against a 7200 s
ceiling. Zero assertion failures in any configuration. Full derivation in
`failure_audit.md`.

I did not raise the timeout, add an exclusion, or modify a test. Options, all
requiring authorization:

* **(a) Raise the Debug-only timeout** to ~14400 s and re-run those three.
  `AMENDMENT_G3.md` states that timeout and concurrency are execution-environment
  settings, not test criteria — the same class as the `OMP_NUM_THREADS=1` and
  `-j 8` settings already in the frozen policy. This *runs* the tests rather than
  skipping them, and is the recommended option. Cost: ~3 h.
* **(b) Record a Debug-only documented exclusion** with the measured
  justification. Cheaper, but converts a measured limitation into an invisible one.
* **(c) Accept Debug+GUI as 2547/2550 with three documented Debug-runtime
  timeouts**, stated explicitly rather than as an unqualified PASS.

### 2. G8 is still open

G8 stands at 13 of 15 with two unresolved harness findings — **G8-F1**, the
benchmark comparator's environment guard cannot see build configuration and so
reported three false REGRESSIONs; **G8-F2**, control NV5 has always passed for the
wrong reason. No authorization to fix was given.

`P13 → FINISHED` cannot follow while a gate is open, so `TODO.md` is not being
updated to FINISHED and G8 is not being written as PASS.

---

## Gate status

```text
G1  Clean builds                  PASS
G2  Restart / checkpoint          PASS
G3  Case validation               PASS
G4  CLI / GUI consistency         PASS
G5  Results / export              PASS
G6  Diagnostics / logging         PASS
G7  Packaging                     PASS
G8  Production benchmarks         OPEN   — G8-F1, G8-F2
G9  P14–P16 preservation          PASS
G10 Final closeout                5 of 6 — Debug+GUI pending a decision

P13 — Production Maturity         NOT FINISHED
```

## Limitations, preserved unchanged

* advanced SST near-wall / separated-flow validation — **deferred to P20**, not authorized
* native Windows CUDA and native Windows packaging — **not claimed**
* DEB / RPM / macOS packaging — **not claimed**
* turbulence checkpointing — **unsupported**, refused by name at both ends
* P16 sanitizer scope — coverage-driven: 44/44 exercised P16 CUDA kernels covered,
  114/196 tests sanitized, synccheck negative-control detection unproven
* benchmark regression threshold 1.25x — cannot detect a slowdown below 25%
* no portable timing baseline across hardware; loaded-machine runs refused, not absorbed
* `/dev/full` write-failure testing is POSIX-only
* no library/header install and no CMake package config — recorded as debt
* 19 frozen technical-debt items — unchanged, not fixed
