# P13-004 — Results / export: acceptance gate

Frozen before any production change, on the tree that closed P13-003.

Stop at the first failure. No threshold may be weakened; a mis-derived criterion is preserved and
amended separately (`CLAUDE.md` §5.3). Every baseline below is **measured** and recorded in the
dry-run — never inferred (the lesson of P13-002's amendment A3).

---

## The two findings this gate is built around

**F1 — every writer silently reports success when writes fail.** Measured against `/dev/full`,
which opens successfully and fails every write with `ENOSPC`:

```text
CSVWriter::writeFields      RETURNED NORMALLY
CSVWriter::writeResiduals   RETURNED NORMALLY
JSONWriter::writeMetadata   RETURNED NORMALLY
VTKWriter::writeCellFields  RETURNED NORMALLY
                            -> 4 writers silently succeeded
```

On a full disk a run prints its `Results:` paths and exits 0 with empty files. The brief forbids
exactly this ("Do not silently report success"). The writers check the **open**, never the
**write** — and `openDeterministicOutput`'s own comment claims it prevents "silently producing a
truncated/partial file", which it cannot.

**F2 — `metadata.json` cannot reproduce or interpret its own result.** It carries none of: solver
version, build identity, backend, convection/gradient schemes, tolerances. Measured: 0 occurrences
of each in `JSONWriter.cpp`.

---

## Group W — write failures are detected  (DETECTION)

| id | criterion |
| --- | --- |
| W1 | Each of the four writers **throws `IOError`** when its writes fail, naming the path. Measured with `/dev/full`. Baseline: 4 silently succeed. Target: 0. |
| W2 | `ResultExporter::write` propagates that failure rather than returning a summary. |
| W3 | `ProjectRunner` reports it as `ApplicationError` with a message naming the file — the path it already uses for an export failure. |
| W4 | The CLI exits non-zero, and does **not** print a `Results:` block listing files that were not written. |
| W5 | An unwritable **directory** is still rejected as it is today (preservation — this path already works). |

## Group M — metadata completeness  (DETECTION)

| id | criterion |
| --- | --- |
| M1 | `metadata.json` carries the application version. |
| M2 | It carries build identity where available, and says so honestly when not. |
| M3 | It carries the backend actually used (not the one configured). |
| M4 | It carries the convection scheme, the gradient scheme and the non-orthogonal corrector count. |
| M5 | It carries the tolerances the run was judged against. |
| M6 | Every added field is **deterministic**: two runs of the same case with the same binary produce byte-identical `metadata.json`. |

M6 is the constraint that makes the rest safe. `JSONWriter` deliberately writes no timestamp,
runtime or absolute path so repeated runs compare byte-for-byte, and **P13-003's criterion E8 now
depends on that** (it compares the CLI's and GUI's exported files). A per-run-varying field would
break a criterion established one item ago.

## Group P — preservation

| id | criterion | baseline |
| --- | --- | --- |
| P1 | Every existing export test passes unchanged. | measured in the dry-run |
| P2 | `fields.csv`, `residuals.csv` and `solution.vtk` are **byte-identical** to before, for a case whose run is unchanged. Only `metadata.json` may differ, and only by the added keys. | measured corpus |
| P3 | The failed-solve export policy is unchanged: metadata and residuals always written, fields/VTK only when finite. | existing tests |
| P4 | P13-001, P13-002 and P13-003 gates still pass. | existing tests |

P2 is the counterpart of P13-002's V3: the change adds fields to one file and must not perturb the
other three.

## Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Remove the write-failure check from one writer | W1 for that writer |
| N2 | Make one added metadata field vary per run (e.g. a timestamp) | M6 |
| N3 | Point the `/dev/full` probe at a normal file | the probe must report 0 silent successes, i.e. it must not fire on a healthy path |

N3 guards the instrument itself: a probe that reported failure everywhere would prove nothing.

## Group G — regression

Release+CUDA, Debug+GUI, CPU-only, clang-format. Counts recorded, not inherited.

---

## Dry-run record

```text
F1 baseline   4 writers silently succeed on /dev/full   logs/00_partial_write_BASELINE.log
F2 baseline   version/build/backend/schemes/tolerances: 0 occurrences each in JSONWriter.cpp
W1..W5        DETECTION -- baseline is the 4 above; target 0. NON-VACUOUS.
M1..M5        DETECTION -- baseline 0 fields; target 5. NON-VACUOUS.
M6            PRESERVATION of an existing property; measured before and after.
P1..P4        PRESERVATION -- existing suites, counts recorded at close.
```

## Gate verdict

P13-004 closes only when every criterion in W, M, P and G passes and every control in N fails.
`Diagnostics / logging` does not begin until this closes.
