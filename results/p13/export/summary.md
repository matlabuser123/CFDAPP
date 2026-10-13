# P13-004 — Results / export: summary

**Two findings, one of them a production defect: on a full disk, a run reported success over empty
files.**

```text
gate        results/p13/export/acceptance_gate.md   (frozen before any change)
baseline    the tree that closed P13-003
environment WSL2 Ubuntu 22.04, gcc 11.4, CUDA 12.9, RTX 5000 Ada (sm_89)
```

---

## 1. Finding 1 — every writer silently reported success when writes failed

The writers checked the **open** and never the **write**. `std::ofstream` goes into a failed state
on a full disk, exhausted quota or I/O error, and its destructor closes it without complaint — so
the writer returned normally, `ResultExporter` returned a populated summary, `ProjectRunner`
reported success, and the CLI printed a `Results:` block listing files that were never written,
with exit code 0.

`openDeterministicOutput`'s own comment claimed it rejected an unwritable path "rather than
silently producing a truncated/partial file". It could not: it inspected the open and then handed
the stream away.

**Measured, not argued.** `/dev/full` opens successfully and fails every write with `ENOSPC` —
exactly a full disk, without needing one:

```text
BEFORE                                          AFTER
CSVWriter::writeFields      RETURNED NORMALLY   throws IOError naming the path
CSVWriter::writeResiduals   RETURNED NORMALLY   throws IOError naming the path
JSONWriter::writeMetadata   RETURNED NORMALLY   throws IOError naming the path
VTKWriter::writeCellFields  RETURNED NORMALLY   throws IOError naming the path
                        4 silent successes                    0
```

Same instrument both times, so the two numbers are directly comparable rather than two different
measurements. `logs/00_partial_write_BASELINE.log` and `logs/01_partial_write_AFTER.log`.

The fix is `requireDeterministicOutputWritten` — flush, then check the sticky failure bit — applied
at all five `openDeterministicOutput` sites plus `JSONWriter`'s own stream. The same pattern
P13-001 already established for `CheckpointWriter`, so this is applying a known fix, not inventing
one.

**What it does not promise, stated in the header:** the bytes are in the operating system's hands,
not necessarily on the device. That needs `fsync`, which `std::ofstream` cannot express. The
distinction matters for a power cut, not for a full disk, and a full disk is the failure this
closes.

## 2. Finding 2 — a result could not be reproduced from its own metadata

`metadata.json` said which case and which solver, and nothing about **how** it was solved.
Measured: 0 occurrences each of version, build identity, backend, convection scheme, gradient
scheme and tolerances in `JSONWriter.cpp`.

Added:

```text
version                              0.2.0-dev
build_identity                       3bd32f918656-modified
solver.convection_scheme             upwind
solver.gradient_scheme               green_gauss
solver.non_orthogonal_corrections    0
solver.linear_solver_backend         CPU        (configured)
solver.gpu_discretization_used       false      (actually used -- not always the same)
solver.max_iterations                15
solver.tolerances.{velocity,pressure,continuity}
```

`linear_solver_backend` and `gpu_discretization_used` are both there deliberately: a GPU request
falls back to CPU when no device is available (P6-GPU-002), and a reader needs to know which one
actually ran, not only which was asked for.

### 2.1 Build identity tells the truth about itself

Captured at configure time, with two honest answers rather than a guess:

```text
no git, or an exported tarball   ->  "unknown"
uncommitted changes in the tree  ->  "<sha>-modified"
```

The current build reports `3bd32f918656-modified`, which is correct: HEAD is `3bd32f9` and the
P13 work is uncommitted. A metadata file that claimed a clean commit the binary does not match
would be worse than one admitting it does not know.

`ERROR_QUIET` plus the fallback means a machine without git configures exactly as before.

### 2.2 No timestamp — and that is a decision, not an omission

The brief lists "timestamp if appropriate". It is not appropriate here. `JSONWriter` deliberately
writes no per-run-varying field so repeated exports compare byte-for-byte, and **P13-003's
criterion E8 now depends on that** — it compares the CLI's and the GUI's exported files. Adding a
timestamp would have silently broken a criterion established one item earlier.

Every field added is fixed per binary. Verified both ways: a direct two-run comparison, and the
pre-existing `ResultExportIntegrationTest.RepeatedExportIsByteIdenticalAcrossAllFiles`.

## 3. Results

```text
W1  writers that silently succeed on /dev/full       4 -> 0
W5  an unwritable directory still rejected           preserved
M1  version                                          present
M2  build identity, honest about "unknown"/"modified" present
M3  configured backend AND the one used              present
M4  convection / gradient / corrector count          present
M5  tolerances and the iteration budget              present
M6  metadata.json byte-identical across runs         YES

P1  existing export tests                            59 / 59
N3  a healthy write still succeeds                   PASS (in the new test file)
N1  the write check removed -> W1 fails              see logs/final.log
G   clang-format, Release+CUDA, Debug+GUI            see logs/final.log
```

New permanent tests: `tests/integration/io/test_export_write_failure.cpp` — three tests covering
"every writer reports a failed write", "the failure names the file", and the non-vacuity half.

## 4. A note on the `/dev/full` tests

They **skip loudly** where `/dev/full` does not exist rather than passing silently. A test that
quietly checks nothing on half its platforms is worse than one that says it did not run. CI runs
Linux, so the coverage is real where it is relied on. The limitation is stated rather than hidden.

## 5. What this item did not do

* **No `fsync`.** See §1 — out of scope, and the brief's failure list does not require it.
* **No new export format.** The brief says not to duplicate a good exporter; CSV, JSON and VTK are
  all already sound and were left alone apart from the write check.
* **No change to the failed-solve export policy.** Metadata and residuals are still always written,
  fields and VTK only when finite (P1 §29). That policy is orthogonal to write failure and was
  preserved.
