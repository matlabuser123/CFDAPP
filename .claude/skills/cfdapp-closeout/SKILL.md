---
name: cfdapp-closeout
description: Standardized CFDApp phase completion - the final report format, the evidence audit, the TODO/ROADMAP update rules, and the commit/push authorization boundary. Use at the end of every CFDApp phase or task, including one that stopped at a failed gate.
---

# CFDApp closeout

Every phase ends here — including a phase that **failed**. A stop is a result, reported in the same
format.

## 1. Evidence audit before reporting

For a task with no phase directory (a documentation fix, a small test change), use the **doc form**
in `cfdapp-verify` instead of the phase checklist below: intended files only, expected diff size,
another session's in-flight work undisturbed, no generated noise, and no altered claim that is not
traceable to `results/`.

For a phase:

* `results/<phase>/` is complete: `summary.md`, `acceptance_gate*.md`, `logs/NN_*.log`, `tools/`,
  `data/`.
* Every number in `summary.md` traces to a log. No estimated, backfilled or reused aggregates.
* Failed and superseded results are still present, marked in place — `INVALID AS AUTHORITATIVE`,
  never deleted (`CLAUDE.md` §4).
* Limits and gaps are disclosed in the summary, not omitted. A missed capture is a disclosed gap.
* `git status --short` shows only intended files. `git diff --check` is clean.
* Generated-file noise restored; no build artifacts; line endings unchanged (the repo is LF and
  `.gitattributes` stores bytes verbatim — a Windows-side edit can silently introduce CRLF).

## 1a. Phase naming

`<phase>` is not yours to invent. Existing IDs follow the authorizing prompt's own name —
`p12-grad-002`, `cuda-qual-001`, `gpu-pcorr-001` — with amendments nested beneath
(`a1/`, `a2/`, `a3/`). Creating `results/<phase>/` is itself an act within an authorized phase:
use the ID the authorization used, and if it named none, **ask** rather than coining one. Sub-studies
of an authorized phase nest inside it rather than claiming a new top-level ID.

## 2. TODO.md / ROADMAP.md

Update **after** verification, with verified state only (`CLAUDE.md` §10).

`TODO.md` carries only `FINISHED · PROCESSING · NEXT · FUTURE · TECHNICAL DEBT` plus short evidence
links — one line per phase. No derivations, hashes, test-count histories or amendment prose; those
live in `results/<phase>/`. `ROADMAP.md` describes capability gained and its limits, not gate logs.

Never mark a debt item resolved that the phase did not actually resolve.

## 3. Final report

This is a **superset of `CLAUDE.md` §13**, not a replacement — §13 remains authoritative and every
one of its fields appears below (`SCOPE`, `GATE` and `RESULT` are carried by `IMPLEMENTATION`,
`ACCEPTANCE GATES` and the per-criterion result line). For a trivial edit, §13's own allowance
applies: fill in only the relevant fields.

```text
SCOPE:                  <authorized scope, and what was deliberately not done>
STATUS:                 <verbatim verdict: "<PHASE> COMPLETE" / "BLOCKED / FAILED GATE at <item>">
ROOT CAUSE / GOAL:      <the mechanism fixed, or the objective>
IMPLEMENTATION:         <the minimal change, and what was deliberately not changed>
NEGATIVE CONTROL:       <result, or "not practical (reason)">

ACCEPTANCE GATES:       <gate file + sha256; per-criterion PASS/FAIL; first failure named>
FOCUSED TESTS:          <exact run/passed/failed/disabled>
FULL REGRESSION:        <exact counts per configuration, or "NOT RUN (reason)">
SANITIZERS:             <exact result, or "NOT RUN (reason)">
NUMERICAL RESULTS:      <orders, residuals, conservation, tolerances — or n/a>
CUDA RESULTS:           <equivalence, determinism, compute-sanitizer, counters — or n/a>
PERFORMANCE RESULTS:    <baseline, median + spread, grid ladder, crossover — or n/a>

FILES CHANGED:          <all>
PRODUCTION FILES:       <list, or "none">
TEST FILES:             <list, or "none">
EVIDENCE:               results/<phase>/...

KNOWN FAILURES:         <each, with reproduced values>
NEW FAILURES:           <each, or "none">
KNOWN REMAINING DEBT:   <what this phase did NOT fix>

WORKING TREE:           <clean / listed changes>
GIT HEAD:               <sha>
origin/main:            <sha>
GIT DIFF --CHECK:       <clean / issues>

TODO UPDATED:           <yes/no>
ROADMAP UPDATED:        <yes/no>

COMMIT STATUS:          <none / sha — only if authorized>
PUSH STATUS:            <none / verified — only if authorized>
NEXT AUTHORIZED ACTION: <explicit question or "awaiting review">
```

Then **STOP**.

## 4. The authorization boundary

Governed by `CLAUDE.md` §11. Restated because it is the most common violation:

* A successful implementation does **not** imply permission to commit.
* A successful commit does **not** imply permission to push.
* Authorization is per phase **and per action**, and does not carry over to the next one.
* After an authorized push, all must hold before claiming it is pushed: `HEAD == origin/main`,
  CI green on that exact SHA, full SHA reported.
* Finishing a phase does not authorize starting the next one — even one listed under NEXT.

Ask. Then stop and wait.
