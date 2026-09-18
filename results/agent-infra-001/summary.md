# AGENT-INFRA-001 — repository-local engineering-agent skills

**Status: implemented, self-tested, uncommitted. Awaiting review.**

Scope: **agent and documentation infrastructure only.** No production CFD code, numerics, CUDA
implementation, CMake production behaviour or CI workflow was modified. Baseline
`c1355eaa77a1b98e37926d20f262b6dae76e45c5`, whose exact-SHA CI qualification (run 35352132866) was
in progress throughout and was not touched.

## 1. Goal

Encode CFDApp's existing engineering process — pre-registered experiments, frozen gates, non-vacuity
controls, attribution with two libraries, proportionate verification, the authorization boundary —
as reusable repository-local Claude skills, so the same judgment applies to every task without
being restated per prompt.

**Division of labour, deliberately:** `CLAUDE.md` keeps the *rules* (governance, authorization,
evidence standard) and remains authoritative. The skills carry *procedure* (what to do, in what
order, with which commands, against which real paths). This is why the skills are not a restatement
of `CLAUDE.md`: a skill that duplicated it would drift from it.

## 2. What was created

```text
.claude/skills/cfdapp-task/SKILL.md          80 lines   router: classify → route → proportionality
.claude/skills/cfdapp-understand/SKILL.md   111 lines   verified repo map + production path
.claude/skills/cfdapp-blast-radius/SKILL.md  81 lines   reach analysis + high-reach table
.claude/skills/cfdapp-architect/SKILL.md     70 lines   design before implementation
.claude/skills/cfdapp-debug/SKILL.md        106 lines   reproduce → root cause → negative control
.claude/skills/cfdapp-numerics/SKILL.md      96 lines   formulation first, derived tolerances
.claude/skills/cfdapp-cuda/SKILL.md         126 lines   CPU-reference rule, equivalence ladder
.claude/skills/cfdapp-verify/SKILL.md       129 lines   L1–L12 ladder, exact commands
.claude/skills/cfdapp-review/SKILL.md        57 lines   adversarial self-review, 15 attacks
.claude/skills/cfdapp-closeout/SKILL.md      84 lines   final report + authorization boundary
docs/AGENT_WORKFLOW.md                                  the human-facing explanation
CLAUDE.md                                               one new section, 18 lines
```

`CLAUDE.md` was changed only at the user's direction (`CLAUDE.md` §10) and only by **adding** the
`## CFDApp Agent Skills` section before the hardware policy. No existing rule was edited.

## 3. Design decisions worth recording

* **Proportionality is explicit and bidirectional.** The router must stop a README typo from
  triggering the 4.5-hour regression *and* stop a three-line change to a shared operator from
  escaping it. Both directions are failures; the routing table names both.
* **The skills encode the project's own scars.** The CUDA skill prohibits zero-work speed-up
  comparisons by name because `results/cuda-qual-001/` produced two (18.8×, 22.9×, both rejected).
  The numerics skill warns against absolute thresholds on scale-varying quantities because
  `results/gpu-pcorr-001/` is exactly that defect. `cfdapp-debug` cites that phase as its worked
  model.
* **`cfdapp-review` and `cfdapp-closeout` are unskippable** by any route, because the two most
  common process failures are declaring success without attacking the result, and treating a
  successful implementation as permission to commit.
* **Verification integrity is restated in `cfdapp-verify`**, not merely referenced: stale-binary
  results are the failure mode most likely to look like success.

## 4. Verification performed

Proportionate to the change — this phase edits Markdown only, so per its own routing table it earns
L11 (whitespace) and L12 (evidence audit), not L1–L10.

| Check | Result |
| --- | --- |
| Skill frontmatter valid; `name:` matches directory name | **10/10 OK** |
| Line endings LF on all 12 new/modified files | **12/12 LF** (repo is LF; `.gitattributes` stores bytes verbatim) |
| `git diff --check` | **clean** |
| `.claude/` visible to git (not caught by `.gitignore`) | **not ignored** |
| Every repository path cited in the skills exists on disk | **39/39 verified** (19 by pattern scan, 20 by explicit check, plus 9 directories) |
| Sanitizer options in `cfdapp-verify` vs `CLAUDE.md` §7 | **identical** |
| clang-format scope in `cfdapp-verify` vs `CLAUDE.md` §7 | **identical** (`include/ src/ apps/ tests/`) |
| Production files changed | **none** |
| Production behaviour changed | **none** |
| CI workflow changed | **none** |

### Two defects found in the skills, by self-review, and fixed

1. **`cfdapp-verify` routing gap.** A change touching **only** `cuda/**` matched no base row in the
   file-to-level table — it matched only the "+ L5, L10" addition row, which would have left it with
   no build or focused-test level at all. Fixed: `cuda/**`-only is now its own self-contained row
   (L1, L2, L3, L5, L10, L11, L12), with the reason L6–L9 are *not* earned stated explicitly
   (no CPU translation unit is affected — and the rule requires proving it by rebuilding the CPU
   trees as no-ops, as `results/gpu-pcorr-001/` §10 actually did).
2. **`cfdapp-task` / `cfdapp-verify` disagreement.** The router listed the CUDA floor as
   `L1, L2, L5, L10, L12`, omitting L3 and L11 that the ladder required. The two tables now agree.

Both were found by checking the skills against each other rather than by reading each alone — the
same "could the implementation and the test share the same mistake?" attack `cfdapp-review` asks for.

## 5. Self-tests

Full record in [`dry_runs.md`](dry_runs.md). Three hypothetical tasks were routed by **independent
agents** given only the repository and the skills — not the author's reasoning — so the test did not
share the author's blind spot (`cfdapp-review` item 4). Each was asked to route the task *and* to
attack the skills.

| Test | Task | Expected | Result |
| --- | --- | --- | --- |
| **A** | pressure gradient loses 2nd-order convergence on a skewed mesh | understand → reproduce → analyse → root cause; **no random fix** | **PASS** — stopped at authorization; found the symptom is already measured at order **1.678** against a frozen `[1.5, 2.4]` gate and disclosed as deliberate; first hypothesis became a tolerance artefact, testable with no production change |
| **B** | reduce GPU SIMPLE runtime at 160² | understand → baseline → transfers → architecture → correctness → optimize; **must not rewrite kernels first** | **PASS** — no kernel touched; identified the path as **latency-bound** (82,869 D2H calls ≈ 7 per Krylov iteration), bounded the ceiling at 22.9 s → ~15 s (still 0.77× vs CPU, below crossover), and stopped on four grounds including "this task **is** GPU-PIPE-001" |
| **C** | fix a typo in `README.md` | lightweight; **must not run the 4.5 h regression** | **PASS** — earned exactly L11 + L12, declined ten levels with a reason each, declined to commit; independently raised that `README.md` cites measured numbers, so a "typo" may be recorded evidence |

All three routed correctly. Between them they found **24 defects in the skills**, including four
factually wrong statements — most seriously that tests are registered by `cfdapp_add_test()`, which
**has zero callers**, and that the GPU test binary lives under `build/release`, which has CUDA
**OFF**. Every finding was verified against the repository and then fixed; `dry_runs.md` §4 is the
itemized record. Two findings concern `TODO.md` and prior evidence rather than the skills and are
**reported, not fixed** (§5 there).

## 6. Limits and disclosures

* These skills change how an agent *works*; they do not change what the code *does*. Nothing here
  is numerical evidence and nothing here revalidates any prior phase.
* Evidence for this phase is written as `.md` files rather than `logs/NN_*.log`, because
  `.gitignore` excludes `*.log` except for explicitly negated phases. Adding a negation would have
  meant editing `.gitignore`, which is outside this phase's authorized scope. No log content is
  lost — the dry-run records are complete in `dry_runs.md`.
* The self-tests are **routing dry-runs on hypothetical tasks**, not executions. No build, test,
  benchmark or production change was run for them. They verify that the skills route correctly and
  read clearly; they do not verify that a real implementation following them would succeed.
