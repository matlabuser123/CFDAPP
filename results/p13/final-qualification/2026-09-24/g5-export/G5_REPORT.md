# P13 Final Qualification — G5, Results / Export: final report

**Result: all criteria PASS. G5 → PASS.**

Date 2026-09-24 (UTC) / 2026-09-25 local. Tree `3bd32f918656-modified`.

**No production code was changed, and no test or harness source was changed.**
`SOURCE_FINGERPRINT_V2` is byte-identical before and after — there is no
authorized delta to account for. Everything new in this gate is evidence and
instruments under `results/`.

---

## 1. Scorecard

```text
Writer failures detected            PASS       6 of 6 production writers
/dev/full failure path              PASS       present, genuine ENOSPC
Deterministic exports               PASS       4 of 4 artifacts byte-identical
Reproducible metadata               PASS       5 non-default values propagated exactly
Correct build identity              PASS       3bd32f918656-modified, honestly dirty
CLI / GUI export equivalence        PASS       E8, 9 of 9 rows, provenance-asserted
Negative controls                   DETECTED   N1, N2 detected; N3 instrument sound
Per-control restoration             VERIFIED   2 of 2 mutating controls, by SHA-256
Unexpected RC=99 restore failures   0
Filesystem hygiene                  PASS       no strays; committed evidence untouched
SOURCE_FINGERPRINT_V2               MATCH      before == after, no delta

G5 -> PASS
```

## 2. Step 2 — writer failure detection, all six production writers

The failure mode is a write that fails **after a successful open**, not a failed
open. Measured against `/dev/full`, which opens successfully and fails every
write with `ENOSPC`.

```text
writer                        production call site        failure injected   detected
-------------------------------------------------------------------------------------
JSONWriter::writeMetadata     ResultExporter.cpp:56       yes                PASS
CSVWriter::writeResiduals     ResultExporter.cpp:60       yes                PASS
CSVWriter::writeFields3D      ResultExporter.cpp:68       yes                PASS
VTKWriter::writeCellFields    ResultExporter.cpp:74       yes                PASS
CSVWriter::writeFields        ResultExporter.cpp:101      yes                PASS
VTKWriter::writeSolution      ResultExporter.cpp:105      yes                PASS

writers that silently reported success: 0
```

Each throws `cfd::IOError` naming the path. The frozen probe, run unmodified,
reports 0 silent successes (`logs/G5_frozen_probe.log`), and the extended probe
reports 0 across all six (`logs/G5_all_writers_probe.log`).

### W2 / W3 / W4 — the failure reaches the user, through the real binary

Each expected output file was replaced by a **symlink to `/dev/full`**, so the
production `cfdapp` opens the real destination successfully and every write then
fails. This is the whole chain, not a unit test:

```text
CLI exit code                    1        non-zero, required
printed a "Results:" block       0        required: it must not list unwritten files
error names the failing file     yes

  Run error: Failed while writing output file (the file may be incomplete):
             /tmp/g5_cli.IKrZwm/full/results/metadata.json
```

`logs/G5_cli_failure_path.log`.

### W5 — an unwritable destination is still rejected

**The first invocation of this criterion was invalid and is preserved.** It used
`chmod a-w` on the results directory, and this shell runs as uid 0 — root
bypasses the permission bits, so the export succeeded, exit 0, and the criterion
measured nothing. Classified **environmental**, not a production regression. Kept
in `logs/G5_cli_failure_path.log` rather than deleted.

Re-tested with two destinations root cannot bypass
(`logs/G5_W5_unwritable_destination.log`):

```text
variant A   `results` exists as a regular FILE
            exit 1, no Results: block
            "Could not create output directory: ...: Not a directory"
variant B   an immutable output file (chattr +i)
            exit 1, no Results: block
            "Could not open output file for writing: .../metadata.json"
```

## 3. Step 3 — `/dev/full`

Available and genuine, not substituted:

```text
/dev/full            crw-rw-rw- 1 root root 1, 7
direct write         rc=1, "No space left on device"
```

Opens succeed, writes fail `ENOSPC`. No permission-denied or
nonexistent-directory test was used in its place; those are a different failure
mode and are covered separately by W5.

## 4. Step 4 — deterministic exports

Two runs of the same case, same binary, **separate isolated directories**, with
the non-aliasing checked rather than assumed:

```text
runA/results inode 50211      runB/results inode 73899      distinct

fields.csv       IDENTICAL  6613f382257d8be6
residuals.csv    IDENTICAL  6a935462e2d85e40
metadata.json    IDENTICAL  c0b11821213333e6
solution.vtk     IDENTICAL  9cb476b2fe274e82
```

No timestamps, random identifiers, unstable ordering, temporary paths or
host-specific noise appear in any of the four. The deliberate no-timestamp design
is preserved — and control N2 demonstrates that a single per-run-varying field
would break it.

## 5. Step 5 — reproducible metadata, proven non-vacuous

The run used a **non-default configuration chosen so no hard-coded value could
pass**, including three tolerances nothing would plausibly hard-code:

```text
criterion                      configured     in metadata.json   result
------------------------------------------------------------------------
solver.max_iterations          7              7                  MATCH
solver.convection_scheme       quick          quick              MATCH
solver.tolerances.velocity     3.7e-07        3.7e-07            MATCH
solver.tolerances.pressure     4.1e-07        4.1e-07            MATCH
solver.tolerances.continuity   5.3e-07        5.3e-07            MATCH
```

So the metadata describes **this** run, not a default or a stale configuration.
The frozen M1–M5 fields are all present:

```text
M1  version                            "0.2.0-dev"
M2  build_identity                     "3bd32f918656-modified"
M3  solver.linear_solver_backend       "CPU"        (configured)
    solver.gpu_discretization_used     false        (actually used)
M4  solver.convection_scheme           "quick"
    solver.gradient_scheme             "green_gauss"
    solver.non_orthogonal_corrections  0
M5  solver.tolerances.{velocity,pressure,continuity}
```

No field was added. This is qualification of the existing contract.

## 6. Step 6 — build identity

```text
user-visible build identity   metadata.json "build_identity": "3bd32f918656-modified"
                              CLI --version: "CFDApp 0.2.0-dev"
qualification provenance      SOURCE_FINGERPRINT_V2 8652dda0...
```

These are two different things and are not conflated. The first is application
metadata: it correctly refuses to claim the binary is clean commit `3bd32f9`, and
the `-modified` suffix is the honest dirty-state indication the frozen contract
requires. The second is this qualification's own provenance, and it is the only
one treated as a unique identity of the tree — precisely because
`3bd32f918656-modified` is **not** unique, which P13 established and the
G4 fingerprint amendment records in full.

## 7. Step 7 — CLI / GUI export equivalence

The frozen criterion is P13-003's **E8**: the CLI's and GUI's `fields.csv`,
`residuals.csv` and `metadata.json` compared **byte-identical**. Re-verified on
the current tree: **9 of 9** rows pass (`logs/G5_cli_gui_export.log`).

Aliasing is excluded structurally, not by hoping:

```text
test_cli_gui_equivalence.cpp:164  each arm snapshots its own exported bytes
                                  while they are the ones on disk
            :222  ASSERT_NE(gui.arm, cli.arm)  -- the G4 F4 provenance assertion,
                  fatal, before any value is compared
            :281  EXPECT_EQ(gui.metadata, cli.metadata)  -- two snapshots, never
                  one path read twice
```

`ResultExporter` always writes `<case>/results`, so the second arm to run
overwrites the first. The harness handles that by snapshotting per arm rather
than by using two directories. That is the frozen design, and after the G4 F4
repair it also proves the two arms are different clients — which is the stronger
half of the anti-aliasing requirement.

**Not done, and flagged rather than assumed:** a literal two-directory variant
(CLI into dir A, GUI into dir B, compared on disk) would need a new test in
`apps/gui/tests/`, i.e. a source change. It is not a frozen criterion, and the
frozen one is satisfied, so it was not written. Say the word if you want it.

## 8. Step 8 — non-vacuity controls

```text
N1  the write-failure check is removed from CSVWriter::writeResiduals
    PLANTED DEFECT     yes
    CRITERION          FAILED as expected
                       probe: 1 writer silently succeeded (baseline 0)
                       tests: 8 ExportWriteFailure test failures
    DETECTION          DETECTED
    RESTORE            VERIFIED by SHA-256
    RETURN CODE        probe=1 ctest=8, both expected non-clean

N2  an added metadata field is made to vary per run (a steady_clock timestamp)
    PLANTED DEFECT     yes
    CRITERION          FAILED as expected -- M6 broken
                       run A metadata.json 3006f03108987cf002185b8c
                       run B metadata.json 3c9c73720c0a6d28fddf6c25
    DETECTION          DETECTED
    RESTORE            VERIFIED by SHA-256

N3  the same probe pointed at a normal directory
    PLANTED DEFECT     n/a -- this control guards the INSTRUMENT
    CRITERION          0 writers threw on a healthy path; 6 non-empty files written
    DETECTION          INSTRUMENT SOUND -- the probe does not fire on healthy paths
    RESTORE            n/a, no source mutated
```

After restore: both files byte-identical to baseline, all export tests pass
again, and W1 re-measured at baseline gives **0** silent successes.

## 9. Findings

### G5-F1 — control N2 had never been implemented

`N2` appears exactly once in the whole of `results/p13/export/` — in
`acceptance_gate.md` itself. No script, no log, and `summary.md`'s results table
lists only N1 and N3. Implemented here for the first time, per its frozen
definition, and it DETECTS. Same shape as G4's finding F3.

### G5-F2 — two of the six production writers had never been measured

Both frozen instruments — `tools/partial_write_probe.cpp` and the permanent test
`ExportWriteFailureTest.EveryWriterReportsAFailedWrite` — cover four writers:
`writeFields`, `writeResiduals`, `writeMetadata`, `writeCellFields`.
`ResultExporter` calls six. Never covered:

```text
VTKWriter::writeSolution    ResultExporter.cpp:105   the 2D solution.vtk path
CSVWriter::writeFields3D    ResultExporter.cpp:68    the 3D fields path
```

`writeSolution` is the ordinary 2D export path — the one that runs for almost
every case — and the test asserting that *every* writer reports a failed write
never touched it.

**Classification: qualification-harness coverage gap, not a production defect.**
The protection is present at all five `openDeterministicOutput` sites
(`CSVWriter.cpp:79,108,150`, `VTKWriter.cpp:172,277`) plus `JSONWriter`'s own
flush-and-check, and `tools/all_writers_probe.cpp` measures that rather than
inferring it: both previously-unmeasured writers throw `IOError` naming the path.
No production change was needed, and none was made.

### G5-F3 — the recorded `/dev/full` baseline no longer shows the defect

`results/p13/export/logs/00_partial_write_BASELINE.log` is headed *"BASELINE,
before any change"* and reports **0** silent successes — the post-fix state.
`01_partial_write_AFTER.log` reports 0 as well. So `summary.md`'s headline
measurement, `4 silent successes -> 0`, has no surviving log that shows the 4:
the probe was re-run against the already-fixed tree and its output overwrote the
baseline file.

**Classification: evidence-integrity defect in the historical record.** It does
not affect this gate's verdict, because non-vacuity today is established by
control **N1** — removing one writer's check produces 1 silent success and 8 test
failures, which is a live demonstration that the criterion can fail. Recorded so
the `4 -> 0` claim is not read as backed by a log that no longer shows it.

The same overwrite hazard is why every frozen log this gate touched was copied
into `logs/PRESERVED_*` before anything ran.

## 10. Step 9 — filesystem hygiene

```text
repo-root strays (x.ckpt, *.ckpt, *.csv, *.vtk)      none
untracked engineering file count                     122  (unchanged)
control markers left in src/ or apps/                none
committed P13-004 evidence                           5 files, all UNCHANGED
```

Every control wrote only into temporary directories it created and removed. The
one preserved artifact kept deliberately is the failed W5 invocation, inside the
qualification evidence tree rather than at the repository root.

## 11. Step 10 — source fingerprint

```text
V2 before G5   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 after  G5   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

**Equal. No authorized delta to attribute**, because G5 required no source or
harness edit at all. Components also unchanged: tracked diff `2f71bb14…`,
untracked engineering `750012b4…`, solver-output sub-hash `3bf622aa…`, 112
tracked and 122 untracked files.

## 12. Limits of this gate, stated

* `/dev/full` is POSIX-only. The permanent tests skip elsewhere and say so; no
  Windows-native equivalent is claimed.
* The writers guarantee the bytes reached the operating system, not the device.
  That needs `fsync`, which `std::ofstream` cannot express. The distinction
  matters for a power cut, not a full disk, and a full disk is what this closes —
  the frozen contract's own stated limit, unchanged.
* W5 was verified as uid 0 via a non-directory destination and an immutable file.
  The ordinary permission-denied path could not be exercised as root.
