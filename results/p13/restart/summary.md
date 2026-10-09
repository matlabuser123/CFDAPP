# P13-001 — Restart / checkpointing: summary

**Result: the gate passes.** A steady SIMPLE solve can be stopped and resumed, through the
production CLI, on CPU and GPU, in 2D and 3D, and the resumed run is **bitwise** the run that was
never interrupted.

```text
gate            results/p13/restart/acceptance_gate.md   (frozen before any production change)
baseline        3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
environment     WSL2 Ubuntu 22.04, gcc 11.4, CUDA 12.9, RTX 5000 Ada (sm_89)
```

---

## 1. What already existed, and why it was not enough

`TODO.md` listed this item as unstarted. A restart subsystem existed anyway — `RestartSnapshot`,
`RestartWriter`, `RestartReader`, three test files, `kRestartFormatVersion = 1`. It is the restart
of a **transient PISO time step**: parameterised by physical time, step index and `delta_t`, 2D
only by explicit refusal (`src/solver/RestartSnapshot.cpp:37`), reachable from no application, and
carrying no configuration identity.

The P13 production solver is **steady SIMPLE**, which has no physical time, runs in 2D and 3D, and
must know whether the case it is resumed into is still the same case.

So format version 1 is untouched and keeps its tests. P13-001 adds a second, separately versioned
artifact. `audit.md` §1 has the full comparison.

## 2. The one finding that made this a solver change

```cpp
// SIMPLE.cpp, before
SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
```

`SIMPLE::solve()` accepted an initial velocity and pressure and **always regenerated the face mass
flux by interpolating the cell velocity**. F is loop-carried state — the next iteration's momentum
assembly reads it — so a resume that handed back only U and p would reinterpolate F and continue
down a different iteration path.

That is exactly the inconsistency this project had already identified and refused for PISO:

> "a restart that only saved U/p and rebuilt F afterward would silently reintroduce exactly the
> inconsistency PISO was built to avoid" — `RestartSnapshot.hpp`

This is the whole numerical content of the item. Everything else is plumbing.

**The gate proves the finding was real, not theoretical.** Control D1 reintroduces exactly that
regeneration and breaks the equivalence on **25 of 25** rows — every case, every split, both
backends, both dimensions. The same control is a permanent regression test
(`SIMPLECheckpointTest.TheCheckpointedFaceFluxIsUsedRatherThanRegenerated`).

## 3. What is stored, and what is deliberately reconstructed

```text
stored      velocity, pressure, the authoritative corrected face mass flux,
            the completed iteration count, and the outer residual SAMPLE history
            (u, v, pressure, continuity, global imbalance, w in 3D, turbulence when reported)

identity    format version, CFDApp version, backend, case fingerprint,
            mesh fingerprint, cell count, face count, dimension
```

Not stored, because none of it is solver state: device pointers, CUDA handles, streams, allocator
state, the Krylov workspace, any linear-solver iterate, stage timings.

**The convergence monitor is not serialized either, and does not need to be.**
`OuterIterationMonitor::record()` is a pure function of the sample sequence, and
`AdaptiveRelaxationController` says so in its own header ("the factors depend only on the sequence
of measures"). A resume replays the stored samples into a fresh monitor and it arrives in exactly
the state it had — normalization references, stagnation and divergence windows, relaxation factors
and ceilings.

That is what "represent solver state, not raw device pointers" means here, and it is testable:
row **B-robust** runs with the normalized criterion, stagnation, divergence and adaptive relaxation
all enabled and still matches bitwise, while control **D2** (a history of the right length and the
wrong content) breaks it.

Two histories had to be added to `SIMPLEResult` for this: `globalMassImbalanceHistory` and
`turbulenceResidualHistory`. Both were already *inputs* to the monitor's per-iteration decision;
only their final values were being kept.

## 4. What this format deliberately cannot do

A case running `k_epsilon`, `k_omega` or `sst` carries k, epsilon/omega and mu_t across outer
iterations, and all three models can only be constructed from **uniform scalar** initial values —
there is no API that could restore a checkpointed field. Adding one means changing three turbulence
models and verifying each numerically, which is a numerics change, not restart plumbing.

So the checkpoint **refuses such a case at both ends**, writing and resuming, with a message naming
the models. It is never written silently and never resumed silently.

Verified specifically, not incidentally: the same k-epsilon case is required to **run normally**
first, so the refusal is shown to be about checkpointing rather than about the case
(`cli_checkpoint.sh`, section C14). Recorded as new technical debt.

## 5. The case fingerprint, and the one key it excludes

`cfd::io::computeCaseFingerprint` hashes the case's own JSON documents — parsed and re-dumped, so
formatting does not matter and content does. Hashing the **files** rather than the parsed
`CaseDefinition` is deliberate: a struct-by-struct hash has to be extended by hand for every new
config field, and the failure mode of forgetting is silent.

Excluded: `case.json`'s `name` and `description` (documentation — a typo fix must not invalidate a
running job), and `solver.json`'s `max_iterations`.

`max_iterations` is the only numerical key excluded, and excluding it is the feature. It is a
budget: it does not change what the stored state means or the trajectory of any iteration that
follows. Raising it to finish a run that hit its limit is the ordinary reason to resume. Every
other solver key — tolerances, relaxation, schemes, correction passes, backend, robustness — does
change the trajectory or the convergence decision, and is hashed.

Both halves are tested: `CaseFingerprintTest.IgnoresDocumentationAndTheIterationBudgetOnly` and
`IsInsensitiveToFormattingButNotToValues`.

## 6. Results, by gate group

### Group A — format and identity: 6/6

```text
A1  version independent of kRestartFormatVersion             PASS
A2  every documented key present                             PASS
A3  write -> read round-trips bitwise (425 values)           PASS
A3b 3D carries dimension 3 and w residuals                   PASS
A4  repeated writes byte-identical                           PASS
A5  no device pointer, handle, stream or scratch in the file PASS
```

### Group B — continuous vs resumed: 35 rows, 0 failures, 124,584 values bitwise

```text
B-cpu-2d       4 splits   PASS      B-gpu-2d       4 splits   PASS
B-cpu-2d-io    4 splits   PASS      B-gpu-3d       4 splits   PASS
B-cpu-3d       4 splits   PASS      B-robust       4 splits   PASS
B-schemes      8 splits   PASS      B-converged    3 splits   PASS
```

Every case is run at four split points — `N1 = 1`, mid, `N1 = N-1`, and a four-way split — because
an off-by-one in the segment budget or the iteration numbering survives a single mid-run split.

`B-converged` is the row that decides whether convergence *classification* survives a restart: a
solve that reaches `Converged` at iteration 1860 on its own still reaches it, at the same
iteration, when split at 1, at 400, and into quarters.

### Group C — rejection matrix: 17/17

Every row asserts the exception **type** the contract promises and requires the message to name the
offending thing; a bare "invalid checkpoint" fails the row.

```text
C1  missing file                    IOError                 C8  2D into a 3D mesh        InvalidArgument
C2  truncated mid-file              IOError                 C9  different case           InvalidArgument
C3  unsupported format version      InvalidArgument         C10 truncated field array    CaseConfiguration
C4  missing required key            CaseConfiguration       C11 NaN in a field           IOError
C5  wrong JSON type                 CaseConfiguration       C12 Inf in a field           IOError
C6  different mesh, same counts     InvalidArgument         C13 history length wrong     CaseConfiguration
C7  different cell count            InvalidArgument         C14 k-epsilon / k-omega / sst   refused both ways
```

Plus: a checkpoint claiming zero completed iterations is refused, the **writer** refuses an invalid
checkpoint, and a refused write leaves no file behind.

**Non-vacuity is structural**: every corrupted document is derived from a checkpoint that is first
asserted to read successfully (`C0`), so no row is testing an input that would have been rejected
anyway.

**Which mechanism guards which row is worth naming.** A literal `NaN` or `Infinity` in the file is
not valid JSON, so `readJsonFile` rejects it during the parse — rows C11 and C12 are guarded by the
parser, not by `validateSIMPLECheckpoint`, which is why their expected type is `IOError` and not
`InvalidArgumentError`. The validator's finiteness check guards the *other* door: a non-finite
value arriving through the in-memory API, which `RejectsInconsistentOrNonFiniteContent` covers.
Both doors are closed; they are closed by different things.

### Group D — the controls, each of which MUST fail

```text
D1  the face mass flux is regenerated from velocity      detected on 25 / 25 rows
D2  the residual history is replayed with wrong content  detected in all 7 sections, 19 / 25 rows
D3  the resume ignores what was already completed        detected on 25 / 25 rows
D4  the mesh-fingerprint comparison is removed              C6 stops throwing -- DETECTED
D5  the case-fingerprint comparison is removed              C9 stops throwing -- DETECTED
D6  the finiteness check is removed                         RejectsInconsistentOrNonFiniteContent
                                                            stops failing -- DETECTED
D7  arms that did not differ in path                     asserted per row, never triggered
D-G1 the preservation instrument's own sensitivity       1 ULP in 1 component of 1 cell detected
```

D4, D5 and D6 disable one production check each, so they are run with proof that nothing is left
behind: the file's sha256 is recorded before the edit, the edit is an exact unique-string
replacement that is asserted to have matched, and the file is restored and re-hashed afterwards.
Both files came back **IDENTICAL**, and A/C/E passes again on the restored tree
(`tools/controls_d456.sh`, `logs/D456_controls.log`).

They earn their cost. Without D4 one could not say whether C6 is caught by the fingerprint or by
something incidental; with it, disabling the fingerprint makes C6 stop throwing, and nothing else
catches a different mesh that shares both counts.

D2's six undetected rows are the `split@1` rows, where the history has a single entry and
"replace every entry with the first" is a no-op. Recorded rather than papered over; detection in
every section is what the control claims.

### Group E — residency across a restart: 4/4

```text
E1  the resumed segment used the same resident GPU path as the continuous run   PASS
E1b it reported resuming (the comparison is not vacuous)                        PASS
E2  0 device reallocations in the resumed segment                               PASS
E3  two resumes from one checkpoint are bitwise identical                       PASS
```

A restart does not reintroduce the per-iteration reallocation GPU-PIPE-001 removed.

### Group F — the production path: 31/31

Run through the shipped `cfdapp` binary against real case directories, in separate **processes**.

```text
A   continuous run, 400 iterations
B   run to 150 --checkpoint, edit max_iterations to 400, --resume
    fields.csv       identical
    residuals.csv    identical      <- the full residual history crosses the process boundary
    metadata.json    identical
C   --checkpoint-every 50 over the full run: 8 checkpoints, results identical
D   8 CLI rejections, each naming the cause
F3  one execution path: the orchestration exists in exactly one file, and neither the GUI
    nor the CLI contains a checkpoint implementation of its own
F4  --help documents all three flags; four bad flag combinations are rejected, not ignored
```

`B` is the genuine cross-process resume, and it is also what proves the `max_iterations` exclusion
is load-bearing: the edit between the two runs is exactly the one key the fingerprint ignores.

### Group G — preservation and regression

```text
G1  10 cases -- CPU/GPU, 2D/3D, four scheme configurations, the full-robustness
    configuration and a converged solve -- fingerprinted before and after.
    Every field value, every residual, every history: BYTE-IDENTICAL.

G2  the P2 restart lineage, untouched and still passing:
      RestartSnapshotTest  17     RestartResumeTest    2
      RestartIOTest        14     TimeControllerTest  21     = 54 tests, 0 failures

G3  full Release + CUDA CTest:  2023 / 2023 passed, 0 failed, 198.52 s
      of which this item's own:  SIMPLECheckpointTest        14
                                 SIMPLEContinuationTest       3
                                 CheckpointProductionCaseTest 6
                                 CaseFingerprintTest          2   = 25 tests
      2023 = the 1998 of the last qualification + these 25.

G4  CPU-only build, configured from scratch with CFDAPP_ENABLE_CUDA=OFF:
      nvcc invocations 0   libcudart linked 0   libcfdcuda.a not built
      1957 / 1957 passed, 0 failed
      all 25 of this item's tests run there too -- none of them needs a GPU
    (2002 discovered vs 2023 in the CUDA build: the 21 difference is the
     GPU-only tests, which that configuration does not register.)

G5  clang-format-18 --dry-run --Werror over include/ src/ apps/ tests/: 0 violations

G6  the known GPU BiCGSTAB restart asymmetry, re-run on these binaries:
      cpu        MaxIterations              3000
      gpu-pipe   PressureCorrectionFailure  1845
      gpu-disc   PressureCorrectionFailure  1845
    UNCHANGED -- exactly the recorded values. A known failure stays known only
    while it reproduces them.
```

`solve_fingerprint.cpp` hashes the raw IEEE-754 bits, and its own sensitivity is proved by control
D-G1. A default (no-continuation) solve is structurally identical to the pre-P13 one, not merely
numerically close.

## 6a. What is covered in CI, and what is not

CI has no GPU. The split is deliberate:

```text
tests/integration/restart/test_simple_checkpoint.cpp        12 tests, CPU
tests/integration/case/test_checkpoint_production_case.cpp   7 tests, CPU, production path
results/p13/restart/tools/                                   the GPU rows, the CLI binary, the controls
```

The CI tests include the decisive bitwise equivalence, the 3D case, the full-robustness case, the
D1 control as a permanent regression, the whole format contract, and the production
checkpoint/resume through `ProjectRunner` — the same entry point the CLI and the GUI both call.

**One gap, deferred deliberately.** CI does not exercise the `cfdapp` BINARY's own flag parsing —
`--checkpoint`, `--checkpoint-every`, `--resume` and their four rejection paths are covered only by
`cli_checkpoint.sh`, which runs here and not in CI. Everything below the flag parsing is covered,
so a break would have to be in the argument handling itself. The ctest entries for it are written
(`--help` mentions the flags; five bad invocations must exit nonzero) and are deferred to **P13-003
— CLI / GUI consistency**, whose gate already owns "error/validation parity" and where CLI coverage
belongs. Adding them here would have forced a re-run of G3 and a from-scratch G4 for a check that
item will re-run anyway. Recorded so it cannot be quietly dropped.

## 6a1. One defect found by reviewing the diff, not by the gate

A checkpoint is built by `makeSIMPLECheckpoint`, which validates finiteness. A solve that ends
**non-finite** therefore makes the write throw — and the first version of `ProjectRunner` turned
that into `ApplicationError: could not write checkpoint ...`, **masking the numerical failure that
was the real answer**. A user whose case blew up would have been told their disk was the problem.

Fixed: a checkpoint refusal on a segment that had already failed is skipped, `checkpointsWritten`
stays where it was, and the solve's own status is reported — the same policy `ResultExporter`
already applies when it declines to write `fields.csv` for a non-finite solution. A refusal on a
segment that *succeeded* is still a reported error, because there the state was resumable and the
checkpoint is genuinely missing.

**Coverage, stated honestly.** The reported half is executed end to end
(`AnUnwritableCheckpointPathIsReported`: a successful solve, a checkpoint path that is a directory,
`ApplicationError` naming the file). The masked half is **NOT VERIFIED end to end**: no case
fixture deterministically produces a non-finite field, and manufacturing one by detuning viscosity
would not be reproducible across platforms. What *is* verified is the contract it rests on —
`makeSIMPLECheckpoint` and `validateSIMPLECheckpoint` reject NaN and Inf in velocity, pressure,
mass flux and every history entry (`RejectsInconsistentOrNonFiniteContent`, and rows C11/C12).

## 6b. Two instrument defects, preserved

Neither was a defect in the production code. Both are recorded because a gate is only as good as
the instrument running it.

**The rejection harness destroyed its own binary.** `run.sh` links each harness to
`/tmp/p13_<basename>`, and `restart_negative.cpp` created its scratch directory at
`temp_directory_path() / "p13_restart_negative"` — the same path. Its own `remove_all` +
`create_directories` replaced the executable with a directory, so the *next* link failed with
`cannot open output file ... Is a directory`. The first run of the gate passed; the second stopped
at A/C/E.

`gate.sh` failed closed, which is the behaviour that matters: a harness that cannot be built is a
gate failure, never a skipped check. Fixed by renaming the scratch directory, and verified by
running the phase twice in a row.

**The build-freshness check reported 405 stale binaries on a clean build.** It walked
`find build/final -type f -executable`, and on drvfs (the Windows filesystem mounted into WSL)
every file carries the executable bit — object files, CMake scripts, generated headers. The
replacement is ninja's own answer: a second build that finds nothing to do means every target is
up to date with respect to every input. `CLAUDE.md` §8's requirement is met by a check that can
actually fail for the right reason.

A regression run that had already started before two later source edits was **discarded rather
than reported**, for the same §8 reason: it did not test the tree it would have been quoted for.

## 7. Production changes

```text
include/cfd/pressure_velocity/SIMPLEContinuation.hpp   new -- the solver-state model
include/cfd/pressure_velocity/SIMPLECheckpoint.hpp     new -- the checkpoint model + validation
include/cfd/io/CheckpointWriter.hpp                    new -- the on-disk schema
include/cfd/io/CheckpointReader.hpp                    new
include/cfd/io/case/CaseFingerprint.hpp                new
src/pressure_velocity/SIMPLEContinuation.cpp           new
src/pressure_velocity/SIMPLECheckpoint.cpp             new
src/io/CheckpointWriter.cpp                            new
src/io/CheckpointReader.cpp                            new
src/io/case/CaseFingerprint.cpp                        new

src/pressure_velocity/SIMPLE.cpp                       accept F; replay the history; segment budget
include/cfd/pressure_velocity/SIMPLE.hpp               setContinuation / continuation
include/cfd/pressure_velocity/SIMPLEResult.hpp         resumedFromContinuation + two histories
include/cfd/app/ProjectRunner.hpp                      three options, three reported outcomes
src/app/ProjectRunner.cpp                              the segmented solve and the refusals
apps/cli/main.cpp                                      --checkpoint, --checkpoint-every, --resume
docs/user_guide/cli.md                                 the flags, the rules, and the limits
```

The `SIMPLE` change is additive and null by default: `continuation_` is a null, non-owning pointer,
exactly the idiom `setMomentumSource` already established, so every existing call site is
structurally unchanged — which G1 measures rather than assumes.

## 8. New technical debt

* **Turbulence state is not checkpointable.** `k`, `epsilon`/`omega` and `mu_t` cannot be restored
  because `KEpsilonModel` / `KOmegaModel` / `SSTModel` are constructible only from uniform scalar
  initial values. Refused explicitly at both ends; fixing it means adding state-injection APIs to
  three models and verifying each numerically.
* **Segmenting repeats per-solve setup.** Each segment rebuilds the derived fields and, on the GPU
  path, the device plans. Correctness is unaffected and no transfer is added inside a segment, but
  `--checkpoint-every 1` is legal and slow. A future checkpoint hook inside the outer loop would
  avoid it at the cost of one device download per checkpoint.
