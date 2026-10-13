# TD-000 — the documentation-only correction, recorded separately

Campaign item 1 requires reconciling stale project-status text **before** touching physics, with the
authoritative state taken from the latest final-qualification evidence, and the correction recorded
separately from any physics work. This file is that record.

**Authoritative source:** `results/p18/FINAL_QUALIFICATION.md` — `P18 OVERALL: PARTIAL`, G8 NOT
CLOSED, fingerprint `83d43668…`.

**No historical phase evidence was rewritten.** Only current status / execution-order summaries were
corrected, and only where they contradicted the qualification report.

---

## 1. Corrections APPLIED to `TODO.md`

### 1.1 The execution order said P18.8 was not authorized, while the same file said it was complete

```text
before:   └─ P18.8   NOT AUTHORIZED YET
after:    ├─ P18.8   COMPLETE  (CPU/GPU thermochemistry equivalence; G9 PASS scoped, amendment A2)
          └─ P18.9   EXECUTED IN FULL -> P18 OVERALL: PARTIAL (G8 NOT CLOSED is the single blocker)
```

This was a **self-contradiction inside one document**: §1 and the P18.8 section heading both say
COMPLETE, and §2 records P18.9 as EXECUTED IN FULL, while §4's execution-order summary still said
P18.8 was unauthorized. P18.9 was absent from the summary entirely. Corrected to match
`FINAL_QUALIFICATION.md`.

### 1.2 "NOTHING AUTHORIZED" and "P18 is the only active new development phase"

Both were true when written and are now stale: a technical-debt campaign is authorized. Replaced
with a statement that names what is authorized (debt resolution, TD-000…TD-018) and what is
explicitly not (P19, P20, unrelated roadmap features, any new development phase).

### 1.3 "Each needs its own authorization. **None is authorized.**"

Stale in the same way — the three items it refers to are now TD-001, TD-002 and TD-004/005/006.
Replaced with a pointer to the register and the dependency order, keeping the requirement that each
carries its own frozen gate.

---

## 2. Correction NOT applied — deferred to TD-017 with reasons

### `KNOWN_LIMITATIONS.md` §2.2 is stale by one measurement

```text
KNOWN_LIMITATIONS.md line 147   "8 recorded measurements in Debug: 5 pass / 3 timeout"
FINAL_QUALIFICATION.md line 157 "9 recorded measurements: 5 pass / 4 timeout"
TODO.md line 489                "9 Debug measurements: 5 pass / 4 timeout"
```

The qualification report is authoritative and `TODO.md` already agrees with it; the limitations
document was not updated after the final run added the ninth measurement.

**Not corrected here, deliberately.** `KNOWN_LIMITATIONS.md` is P18 qualification evidence, and the
campaign forbids rewriting historical phase evidence. It is also already inside TD-017's declared
scope ("KNOWN_LIMITATIONS L36 wording"), and TD-007 regenerates the document wholesale after the
P18 re-qualification. Editing it now would mean touching evidence twice and from outside its own
frozen gate.

Recorded in the register under DOCUMENTATION ONLY so it cannot be lost.

---

## 3. What this correction did not touch

* no production file;
* no `results/` measurement, log, hash or gate;
* no historical phase narrative — the per-phase P18.x sections of `TODO.md` are unchanged,
  including their debt lists, which the register classifies rather than edits;
* no threshold, no acceptance criterion;
* the pre-existing dirty working tree was not cleaned.

## 4. Verification

Documentation-only change: the floor is a read-back of the edited regions plus confirmation that no
build input changed.

```text
files edited by TD-000   TODO.md, 3 regions:
                           - section 2 header ("NOTHING AUTHORIZED" -> debt campaign)
                           - the "None is authorized" line -> TD pointers
                           - section 4 execution order (P18.8 COMPLETE, P18.9 added)
physics changed          none
results/ evidence        none rewritten
thresholds / criteria    none
```

**Build-input check, performed rather than assumed.** `grep -rln 'TODO\.md'` over `CMakeLists.txt`,
`cmake/`, `.github/workflows/` and `tests/` returns ten files, which looks like TODO.md is consumed
by the build. **It is not.** Every match is a *comment referencing* TODO.md — for example
`CMakeLists.txt:82  # TODO.md "P1 -- Quality Gate" section 7-9: …`. No `install`, `configure_file`,
`file()` or test-fixture rule touches it, so L1 is not required.

This is the "exclusion grep that matches prose about the exclusion" failure mode recorded in the
project's own notes: the better the comments, the more false hits. The `-l` form hid it; filtering
for `install|configure_file|COPY|file(` returned nothing.

**On the `git diff --stat` figure.** `git diff --stat -- TODO.md` reports 628 insertions /
308 deletions, and **that is not this correction's diff.** `TODO.md` already carried uncommitted
P18.0–P18.9 changes before the campaign began (the whole P13–P18 series is uncommitted — see
`KNOWN_LIMITATIONS` §2.5, 303 paths at this audit). TD-000's own contribution is the three regions
listed above; the rest of that diff is pre-existing and was not cleaned, per §50. A `--stat` number
cannot be used as evidence of what this step changed, and is recorded here so it is not read that
way later.
