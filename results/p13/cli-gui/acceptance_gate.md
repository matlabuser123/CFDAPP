# P13-003 — CLI / GUI consistency: acceptance gate

Frozen before any production change, on the tree that closed P13-002.

Criteria run in the order written. **Stop at the first failure.** No threshold may be weakened; a
mis-derived criterion is preserved and amended in a separate `acceptance_gate_AN.md` with its own
dry-run and non-vacuity check (`CLAUDE.md` §5.3).

```text
PRESERVATION  must PASS on the unchanged baseline and must still pass afterwards.
DETECTION     cannot pass on the baseline. Non-vacuity is a named control in group N.
```

**A lesson from P13-002 applied here in advance:** every baseline figure below is **measured on the
unchanged tree and recorded in the dry-run**, never inferred. A criterion whose baseline equals its
target is vacuous and must be withdrawn, not reported as a pass.

---

## Group A — one execution path  (PRESERVATION, structural)

| id | criterion |
| --- | --- |
| A1 | Neither `apps/cli` nor `apps/gui` constructs a pressure-velocity solver. Checked by grep, exit-code asserted. |
| A2 | Every solver entry from either client reaches `cfd::app::ProjectRunner::run`. |
| A3 | `CaseSession::run` forwards `ProjectRunOptions` unmodified except `cancellationCheck`, whose wrapping is documented. |

These pass today. They are here so that a later change cannot quietly introduce a second path
while the numerical criteria still pass.

## Group E — numerical equivalence, CLI vs GUI  (DETECTION)

The brief's list, in full. For the **same case directory**, a run driven through the GUI's
controller and a run driven through the CLI's entry point must agree on:

| id | quantity | bound |
| --- | --- | --- |
| E1 | final velocity (x, y, z per cell) | **bitwise** |
| E2 | final pressure | **bitwise** |
| E3 | final face mass flux | **bitwise** |
| E4 | `status` and `iterations` | exactly equal |
| E5 | u/v/w/pressure/continuity residual histories, entry by entry | **bitwise** |
| E6 | `globalMassImbalance`, `finalContinuityResidual` | **bitwise** |
| E7 | solver backend actually used, and the resolved face-flux scheme | exactly equal |
| E8 | the exported `fields.csv`, `residuals.csv`, `metadata.json` | **byte-identical** |

Bitwise, because both arms run the identical `ProjectRunner::run` on identical inputs on one
machine. Anything weaker would not distinguish "same path" from "same answer by luck" — which is
precisely the weakness of the existing check (audit §3).

Coverage — every row:

```text
E-2d          2D cavity
E-3d          3D duct
E-thermal     a case with a thermal block (a second solver after SIMPLE)
E-export      the three exported files, compared byte for byte
E-gui-edited  a case EDITED in the GUI, saved, then run both ways
```

`E-gui-edited` is the row that matters most: it is the only one where the GUI did something the
CLI cannot, and then both must still agree.

## Group P — capability parity  (DETECTION)

| id | criterion |
| --- | --- |
| P1 | Checkpointing is reachable from the GUI's controller: a run can be given a checkpoint path and interval, and can be resumed from one. |
| P2 | The GUI's checkpoint/resume goes through the **same** `ProjectRunOptions` → `CaseSession` → `ProjectRunner` path as the CLI's. No second implementation: asserted structurally, by grep, not by comparing outputs. |
| P3 | A GUI-driven checkpointed run and a CLI-driven checkpointed run of the same case produce **byte-identical** checkpoint files. |
| P4 | A checkpoint written by the CLI resumes in the GUI, and one written by the GUI resumes in the CLI. |
| P5 | The GUI reports what happened — resumed-from iteration and checkpoints-written — as data, not as a log line. |
| P6 | CLI cancellation is **not** added. The asymmetry is documented in `docs/user_guide/` as intentional, with the reason. |

P6 is a criterion, not an omission: the gate records that this asymmetry was considered and
deliberately left, so a later reader does not mistake it for an oversight.

## Group V — validation and error parity  (PRESERVATION + DETECTION)

| id | criterion | kind |
| --- | --- | --- |
| V1 | An invalid case produces the same `ProjectRunStatus` and the same `caseDiagnostic` through both clients. | DETECTION |
| V2 | The GUI's `validateDraft` uses `CaseReader` + `CaseBuilder` — the same classes `ProjectRunner` uses — and no private validation logic. | PRESERVATION, structural |
| V3 | The `cfdapp` binary's checkpoint flag handling is covered by ctest, so CI exercises it: the flags are documented in `--help`, and five bad invocations exit non-zero. | DETECTION |

V3 is the coverage deferred from P13-001.

## Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Perturb one exported value in the GUI arm before comparing | E8 |
| N2 | Compare the GUI arm against itself instead of against the CLI arm | the harness must declare the comparison vacuous and fail, not pass |
| N3 | Remove the GUI's checkpoint plumbing | P1, P3, P4 |
| N4 | Point the CLI-flag ctests at a binary without the flags | V3 |

N2 exists because an equivalence harness that compares one arm with itself passes for free — the
failure mode the GPU-PIPE-001 and P13-001 gates both had to guard against. Every E row therefore
asserts that the two arms genuinely came from different clients before comparing any value.

## Group G — regression

| id | criterion |
| --- | --- |
| G1 | Full Release + CUDA CTest: 0 unexpected failures, count ≥ 2030. |
| G2 | Debug + GUI CTest: 0 unexpected failures. |
| G3 | CPU-only build and suite. |
| G4 | `clang-format-18 --dry-run --Werror` clean. |
| G5 | P13-001 and P13-002 gates still pass — checkpointing and diagnostics must not regress. |

---

## Dry-run record

To be completed against the unchanged tree **before** this gate is acted on. Every baseline below
is measured, and the measurement is recorded even when it is zero.

Measured on the unchanged tree by `tools/dryrun.sh`, recorded in `logs/00_dryrun.log`:

```text
A1  solver constructions in apps/cli + apps/gui        0     (target 0, already met)
A2  solver entry points from the clients               2     both reach ProjectRunner
A3  ProjectRunOptions fields CaseSession rewrites      1     cancellationCheck, documented
P1  GUI references to checkpoint/resume                0     target > 0 -- NON-VACUOUS
    CLI references to checkpoint/resume               34
V2  validateDraft refs to CaseReader/CaseBuilder      11     shared core
    GUI calls into the private parsers                 0     (target 0, already met)
V3  ctest entries for the CLI checkpoint flags         0     target 5+ -- NON-VACUOUS
E1..E8  no harness exists on this tree, so the baseline is NOT MEASURABLE.
        Recorded as such: neither a pass nor a failure. The harness IS the
        work, and its non-vacuity comes from group N, not from a baseline.
    the pre-existing "round-trip gate" assertions      1     status only (audit §3)
```

**Vacuity check, done here rather than after the fact.** P13-002's amendment A3 exists because a
criterion whose baseline already equalled its target got reported as a pass. Each criterion above
was checked against that:

* **P1 (0 → >0)** and **V3 (0 → 5+)** are non-vacuous: baseline measured, and it differs from the
  target.
* **A1, A3 and the private-parser count** already sit at their target. That is correct *only*
  because they are PRESERVATION criteria — their job is to fail if a later change breaks them, not
  to improve. They are labelled as such and are **not** counted as achievements of this item.
* **E1–E8** have no baseline, because no harness existed. Recorded as "not measurable" rather than
  written down as 0 and later quoted as "improved to 0".

## Gate verdict

P13-003 closes, and `TODO.md`'s `CLI / GUI consistency` becomes `[x]`, only when every criterion in
A, E, P, V and G passes and every control in N fails as specified.

A failure anywhere stops the item. `Results / export` does not begin until this closes.
