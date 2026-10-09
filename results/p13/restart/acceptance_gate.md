# P13-001 — Restart / checkpointing: acceptance gate

Frozen before any production change. Baseline `3bd32f918656f471e72b5f6ac7cb6c2cbf75a274`.

Criteria run in the order written. **Stop at the first failure.** No threshold here may be
weakened; a criterion found to be mis-derived is preserved verbatim and amended in a separate
`acceptance_gate_AN.md` with its own dry-run and non-vacuity check (`CLAUDE.md` §5.3).

Each criterion is labelled by kind, because the dry-run means different things for each
(the lesson recorded in `results/gpu-pipe-001/final-residency/acceptance_gate_A1.md`):

```text
PRESERVATION  must PASS on the unchanged baseline and must still pass afterwards.
DETECTION     cannot run on the baseline (the feature does not exist). Its non-vacuity is
              proved by a named control in group D that MUST FAIL.
```

---

## Group A — format and identity  (DETECTION)

| id | criterion |
| --- | --- |
| A1 | The production checkpoint declares its own format version, distinct from `cfd::solver::kRestartFormatVersion = 1`. Reading a checkpoint whose version is not the supported one is rejected with `InvalidArgumentError`, not silently accepted. |
| A2 | A written checkpoint carries **all** of: format version, CFDApp version string, case fingerprint, mesh fingerprint, cell count, face count, mesh dimension, completed iteration count, solver backend, and the field data. A missing key is a read rejection, never a default. |
| A3 | Write → read round-trips every stored floating-point value **bitwise**: velocity x/y/z, pressure, face mass flux, and every residual-sample history entry. |
| A4 | Writing the same in-memory checkpoint twice produces **byte-identical** files. |
| A5 | The checkpoint contains no device pointer, CUDA handle, stream id, allocator state or Krylov scratch. Checked structurally: the schema's key set is exactly the documented one. |

## Group B — continuous vs checkpoint/resume equivalence  (DETECTION)

The property: for the same case and the same total iteration budget `N`,

```text
one solve of N   ==   a solve stopped at N1, checkpointed, resumed, and run to N
```

| id | criterion | bound |
| --- | --- | --- |
| B1 | final velocity (x, y, z per cell) | **bitwise** |
| B2 | final pressure (per cell) | **bitwise** |
| B3 | final face mass flux (per face) | **bitwise** |
| B4 | `status` and total `iterations` | exactly equal |
| B5 | `uResidualHistory`, `vResidualHistory`, `wResidualHistory`, `pressureResidualHistory`, `continuityHistory` — full length `N`, entry by entry | **bitwise** |
| B6 | `finalContinuityResidual` and `globalMassImbalance` | **bitwise** |
| B7 | the `robustness` normalized histories, convergence-distance history and relaxation histories | **bitwise** |

Bitwise, not a tolerance, because both arms run the identical algorithm over the identical bits
on the identical hardware. This is the standard GPU-DISC-001 and GPU-PIPE-001 already hold their
equivalence claims to; a tolerance here would hide exactly the defect B exists to catch.

Coverage — every row must pass:

```text
B-cpu-2d       CPU backend, 2D cavity
B-cpu-2d-io    CPU backend, 2D inlet/outlet channel
B-cpu-3d       CPU backend, 3D cavity
B-gpu-2d       GPU backend, resident SIMPLE loop, 2D cavity
B-gpu-3d       GPU backend, resident SIMPLE loop, 3D cavity
B-robust       CPU, normalized criterion + stagnation + divergence + adaptive relaxation
B-converged    a solve that reaches Converged on its own, split before convergence
B-multi        the same budget split into 4 chunks, not 2
B-boundary     split at N1 = 1 and at N1 = N-1
```

`B-robust` is the row that decides whether the monitor-state reconstruction is real.
`B-converged` is the row that decides whether convergence classification survives a restart.

## Group C — rejection matrix  (DETECTION)

Every row must be rejected **before any solver work**, with an exception whose message names the
offending thing. A bare "invalid checkpoint" fails the criterion.

| id | input | expected |
| --- | --- | --- |
| C1 | file does not exist | `IOError` |
| C2 | not well-formed JSON (truncated mid-file) | `IOError` |
| C3 | format version above or below the supported one | `InvalidArgumentError` |
| C4 | missing required key | `CaseConfigurationError` |
| C5 | wrong JSON type for a key | `CaseConfigurationError` |
| C6 | different mesh, same cell and face counts (fingerprint differs) | `InvalidArgumentError` |
| C7 | different cell count | `InvalidArgumentError` |
| C8 | different mesh dimension (2D checkpoint into a 3D mesh) | `InvalidArgumentError` |
| C9 | different case configuration (any solver / physics / boundary change) | `InvalidArgumentError` |
| C10 | truncated field array (length ≠ cell or face count) | `CaseConfigurationError` |
| C11 | NaN in a field value | rejected |
| C12 | Inf in a field value | rejected |
| C13 | residual-history length inconsistent with the recorded iteration count | `CaseConfigurationError` |
| C14 | a case running a transport-equation turbulence model (`k_epsilon` / `k_omega` / `sst`) | refused at **write** and at **resume**, naming the model |

## Group D — non-vacuity controls  (each MUST FAIL)

A criterion that cannot fail is not a criterion. Each control is a deliberate, temporary defect;
the gate requires the named criterion to **fail** while the control is active and to pass again
once it is removed.

| id | control | must break |
| --- | --- | --- |
| D1 | resume ignores the checkpointed face mass flux and lets `SIMPLE::solve()` regenerate it from velocity — i.e. exactly the pre-P13 behaviour identified in `audit.md` §2.2 | B1, B2, B3 |
| D2 | resume skips the residual-history replay into `OuterIterationMonitor` | B7, and B4/B5 on `B-robust` |
| D3 | resume ignores the checkpointed iteration count and runs the full budget again | B4 |
| D4 | the mesh-fingerprint check is removed from the reader | C6 |
| D5 | the case-fingerprint check is removed from the reader | C9 |
| D6 | the finiteness check is removed from the reader | C11, C12 |
| D7 | the equivalence harness compares an arm against itself | B1 must still pass — and the harness must report that the two arms genuinely differed in path, or the comparison is declared vacuous and the row fails |

D7 is the guard the GPU-PIPE gate learned to need: a comparison of two identical paths passes for
free. Every B row therefore asserts that the resumed arm really did resume
(`resumedFromCheckpoint == true`) and the continuous arm really did not.

## Group E — residency, no stale device state  (DETECTION)

| id | criterion |
| --- | --- |
| E1 | On the GPU backend the resumed arm reports the same `gpuDiscretization`, `residentPressureSolve` and `residentSimpleLoop` flags as the continuous arm. |
| E2 | A resumed GPU solve shows **0 steady-state device reallocations**, the property GPU-PIPE-001 established. A restart must not reintroduce per-iteration reallocation. |
| E3 | Two resumed GPU solves from the same checkpoint produce bitwise-identical results (determinism across the restart boundary). |

## Group F — production path  (DETECTION)

`CLAUDE.md` §9: a library API is not production capability.

| id | criterion |
| --- | --- |
| F1 | The CLI can write a checkpoint on a schedule and resume from one, through documented flags. |
| F2 | A CLI run split by a checkpoint produces a `metadata.json` and `fields.csv` identical to the continuous CLI run's, except for fields documented as run-dependent. |
| F3 | The resume path used by the GUI is `cfd::app::ProjectRunner` — the same function the CLI calls. Asserted structurally (no second code path), not by comparing outputs after the fact. |
| F4 | `cfdapp --help` documents the new flags; an invalid combination is rejected with a message, not a crash. |

## Group G — preservation  (PRESERVATION — these DO dry-run on the baseline)

| id | criterion | instrument |
| --- | --- | --- |
| G1 | `results/p13/restart/tools/solve_fingerprint.cpp` produces a **byte-identical** log, header lines excluded, before and after the change. Ten cases: CPU/GPU, 2D/3D, four convection/correction configurations, the full-robustness configuration, and a converged solve. | `run.sh fingerprint` |
| G2 | The P2 restart lineage (`kRestartFormatVersion = 1`) is untouched: `RestartSnapshotTest`, `RestartIOTest`, `RestartResumeTest` and `TimeControllerTest` all pass, with the same test count. | ctest |
| G3 | Full Release + CUDA CTest: 0 unexpected failures, test count ≥ the 1998 of the last qualification. | ctest |
| G4 | CPU-only build (`CFDAPP_ENABLE_CUDA=OFF`) compiles and its suite passes — the blind spot recorded in memory as `cpu-only-build-blind-spot`. | ctest |
| G5 | `clang-format-18 --dry-run --Werror` clean over `include/ src/ apps/ tests/`. | format |
| G6 | The known GPU BiCGSTAB restart asymmetry (`TODO.md` → Technical Debt) still reproduces at **exactly 1845 iterations**. A known failure stays known only while it reproduces its recorded value. | gate harness |

---

## Dry-run record

Run against the unchanged tree, before freezing. See `logs/00_dryrun.log`.

* **G1** — instrument executed on the baseline; 10 cases fingerprinted, exit 0. Frozen output
  `logs/fingerprint-baseline.log`, sha256 recorded in `logs/00_freeze.log`. The criterion is
  meaningful only if the instrument can also FAIL: control **D-G1** below.
* **G2** — executed: 194 focused tests, 194 passed.
* **G3/G4/G5/G6** — carried over unchanged from the GPU-PIPE-001 final qualification, which ran
  them on this exact HEAD. They are re-run for real at this item's close, not inherited.
* **A, B, C, E, F** — DETECTION. They cannot run on a tree without the feature. Their
  non-vacuity is group D's job, not the baseline's.

### D-G1 — the preservation instrument must be able to fail

`solve_fingerprint.cpp` would be worthless if its hash were insensitive to the fields. The
control: perturb one velocity component of one cell of one case by one ULP and require the
fingerprint of that case to change. Recorded in `logs/00_dryrun.log`.

---

## Gate verdict

P13-001 closes, and `TODO.md`'s `Restart / checkpointing` becomes `[x]`, only when **every**
criterion in A, B, C, E, F and G passes and **every** control in D fails as specified.

A failure anywhere stops the item. `Robust case validation` does not begin until this closes.
