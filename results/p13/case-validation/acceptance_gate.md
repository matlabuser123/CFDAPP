# P13-002 — Robust case validation: acceptance gate

Frozen before any production change, on the tree that closed P13-001.

Criteria run in the order written. **Stop at the first failure.** No threshold may be weakened; a
mis-derived criterion is preserved verbatim and amended in a separate `acceptance_gate_AN.md` with
its own dry-run and non-vacuity check (`CLAUDE.md` §5.3).

Labelled by kind, because the dry-run means different things for each:

```text
PRESERVATION  must PASS on the unchanged baseline and must still pass afterwards.
DETECTION     cannot pass on the baseline (the behaviour does not exist yet). Its
              non-vacuity is proved by a named control in group N that MUST FAIL.
```

---

## What the audit established, and what is therefore NOT the work

`audit.md`: **2537 mutations across 12 case families, 0 accepted.** Every automatically-generable
invalid case is already rejected before the solver runs, by ~151 existing rejection sites. This
item is therefore **not** about making invalid cases fail. They already fail.

It is about the two things the brief names that are genuinely missing:

1. **"Do not return only 'invalid case' when the failing field is known."** The parsers know the
   file, the field, the constraint and the received value — and then flatten all four into a
   string. `apps/gui/SimulationControllerEditing.cpp` recovers two of them by **string-scraping the
   message** (`fieldForMessage` searches for `field "`, `sectionForMessage` searches for
   `mesh.json` and friends). The CLI discards the structure entirely. A diagnostic that exists is
   being thrown away and guessed back.
2. **One message that does not follow the project's own convention.** A mistyped file reference in
   `case.json` produces `required geometry configuration not found: <path>` — it names the role and
   the resolved path, but not the file to edit or the key at fault. It is also exactly the class of
   error the GUI's scraper cannot read, which is the same root cause as (1).

---

## Group V — valid cases still run  (PRESERVATION — dry-runs on the baseline)

| id | criterion |
| --- | --- |
| V1 | The 17 `CFDAppCli*` ctests pass — every shipped fixture still loads, runs, or is rejected exactly as before. |
| V2 | Full Release + CUDA CTest: 0 unexpected failures, count ≥ 2023. |
| V3 | **Every rejection message is byte-identical** before and after, except the manifest rows S3 deliberately changes. Instrument: the mutation matrix's own normalised `message` column, 2537 rows over 12 families. |
| V4 | CPU-only build (`CFDAPP_ENABLE_CUDA=OFF`) compiles and its suite passes. |
| V5 | `clang-format-18 --dry-run --Werror` clean. |
| V6 | P13-001's gate still passes — checkpointing must not regress. |

V3 is the criterion that makes "add structure, change no text" checkable rather than asserted. The
corpus is proved reproducible: two independent sweeps produced **byte-identical** CSVs for all 12
families.

## Group M — the mutation matrix  (PRESERVATION, then DETECTION)

| id | criterion | kind |
| --- | --- | --- |
| M1 | 2537 mutations, 12 families, **0 accepted**. Every invalid case still rejected before the solver. | PRESERVATION |
| M2 | **0 vague rows.** A row is vague when the rejection message names neither the file to edit nor the field at fault. Baseline: 63. | DETECTION |

M2's 63 baseline rows are 60 manifest-reference rows (5 per family × 12) plus 3 cascade messages
that name a *different* file and field than the one mutated — correct messages about the real
downstream consequence, which the criterion must therefore not count. That reclassification is
part of the frozen criterion, not a later concession: M2 counts a row vague only when the message
names **no** file and **no** field.

## Group S — structured diagnostics  (DETECTION)

| id | criterion |
| --- | --- |
| S1 | A `CaseConfigurationError` raised by `throwConfigError` carries the file, the field, the constraint and the received value as **data**, not only inside its message text. |
| S2 | `ProjectRunResult` exposes that diagnostic for an `InvalidCase` run, so the CLI and any other client can read it without parsing prose. |
| S3 | The five manifest references (`geometry`, `mesh`, `physics`, `boundaries`, `solver`) produce a diagnostic in the same shape, naming `case.json` and the key. |
| S4 | `SimulationController` fills `section` and `field` from the diagnostic, not by scanning the message. |
| S5 | Every rejection either carries a diagnostic or belongs to a category documented as unable to (a mesh-quality refusal, a `CaseBuilder` construction error) — and that list is written down, not left implicit. |
| S6 | The exception **type** each site throws is unchanged. `IOError` stays `IOError`; a caller catching one still catches it. |

S6 exists because the manifest site currently throws `IOError` and the tempting fix is to change it
to `CaseConfigurationError`. Both map to exit 2, so nothing visible would break — which is exactly
why the change would be easy to make by accident. The fixture `tests/data/cases/missing_mesh`
exercises this path.

## Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Reword one `throwConfigError` message (e.g. `must satisfy` → `must be`). The **string-scraping** path loses the field; the structured path does not. | proves S1/S4 are worth doing, not decoration |
| N2 | Remove the payload from `throwConfigError`, keeping the text | S1, S2, S4 |
| N3 | Revert the manifest site to its current message | S3, M2 |
| N4 | Point the matrix at a case that is already invalid | the harness must refuse to report — a baseline that does not RUN makes every row meaningless |

N1 is the decisive control. If rewording a message does **not** break the current GUI, then the
scraping is not fragile and this item's premise is wrong — so N1 is run against the **unchanged**
tree first, as part of the dry-run, and its result is recorded either way.

## Group G — regression

Full Release + CUDA, CPU-only, format, and P13-001's gate. Same instruments, re-run, not inherited.

---

## Dry-run record

Run against the unchanged tree, before freezing. See `logs/`.

* **V1** — executed: 17 / 17 `CFDAppCli*` tests pass.
* **V3** — the corpus instrument executed twice on the unchanged tree: **byte-identical** across
  all 12 families. A preservation instrument that is not reproducible cannot detect a change, so
  this is the check that makes V3 usable.
* **M1** — executed: 2537 mutations, **0 accepted**. This is the baseline M1 must preserve.
* **M2** — executed: **63** vague rows. This is the baseline M2 must reduce to 0.
* **V2, V4, V5, V6** — carried over from P13-001's gate, which ran them on this exact tree. Re-run
  for real at this item's close, never inherited.
* **S1–S6** — DETECTION. They cannot pass on a tree without the behaviour; group N is their
  non-vacuity, not the baseline.
* **N1** — run on the unchanged tree during the dry-run, because its whole purpose is to test this
  item's premise before the item is built.

### The instrument was wrong twice, and that is on the record

The first sweep reported 5 findings and the second reported 5 more. All ten were the matrix
treating a documented optional default or a free-text label as a required, enumerated field.
Corrected by naming those keys explicitly, each citing the parser line that makes it so — never by
loosening the pass criterion. `audit.md` §2.2 has the table.

---

## Gate verdict

P13-002 closes, and `TODO.md`'s `Robust case validation` becomes `[x]`, only when every criterion
in V, M, S and G passes and every control in N fails as specified.

A failure anywhere stops the item. `CLI / GUI consistency` does not begin until this closes.
