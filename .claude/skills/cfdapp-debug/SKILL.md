---
name: cfdapp-debug
description: The CFDApp bug-repair process - reproduce, freeze the reproducer, narrow, hypothesize, discriminate, root-cause, minimal fix, negative control. Use for any failure investigation, defect repair or unexplained numerical result.
---

# CFDApp bug repair

```text
REPRODUCE → FREEZE REPRODUCER → NARROW → HYPOTHESES → DISCRIMINATING TESTS
→ ROOT CAUSE → MINIMAL FIX → NEGATIVE CONTROL → FOCUSED REGRESSION → FULL REGRESSION
```

`results/gpu-pcorr-001/` is the worked model of the **investigation**: read its ruled-out table, its
canonical reproducer, the mechanism shown in isolation, and the negative control that reproduced the
failure at the same iteration count.

It is **not** the model for the evidence *layout* — it has no `logs/00_freeze.log` and no
`acceptance_gate.md`, because it was a repair of a failure already frozen by CUDA-QUAL-001. For the
directory layout follow a gated phase such as `results/p12-grad-002/a2/`, whose `logs/00_freeze.log`
and `acceptance_gate_A2.md` are what `cfdapp-closeout` §1 audits against.

## Rules

* **Never start by changing code.** A fix written before the cause is known is a guess.
* **Correlation is not cause.** "It went away" is not a root cause.
* **Symptom ≠ cause.** `PressureCorrectionFailure` was the symptom; an absolute breakdown threshold
  in the GPU BiCGSTAB was the cause.
* **Preserve the original failure verbatim** — logs, exact counts, exact iteration numbers.
  Never delete or overwrite it (`CLAUDE.md` §1.5).
* **Do not repair unrelated debt** found on the way. Record it in `TODO.md` → Technical Debt and
  report it (`CLAUDE.md` §2).
* Stop and report if the fix would weaken an existing guarantee.

## 1. Reproduce

Reproduce before theorising. Record the exact command, environment, build type, preset, grid,
settings and output. If it will not reproduce, that is the finding — report it; do not proceed on
a failure you cannot observe.

## 2. Freeze the reproducer

Write a standalone probe under `results/<phase>/tools/` and record its sha256 in
`logs/00_freeze.log` alongside the production, test and library hashes. The reproducer must not
change for the rest of the investigation — otherwise "fixed" is unfalsifiable.

Prefer the smallest reproducer that still fails: a probe over a full case, one solve over a full
SIMPLE run, one grid over a ladder.

## 3. Narrow

Bisect the failure along whatever axis is cheapest and most discriminating:

```text
grid size · backend (CPU/GPU) · build type · compiler · preset · scheme
scale of the RHS · iteration count · single operator vs whole solver
```

The goal is a boundary: the largest configuration that passes and the smallest that fails.

## 4. Hypotheses, then discriminating tests

List **every** plausible cause before testing any. For each, design a test whose outcome
*separates* it from the others — a test that every hypothesis predicts the same answer to is
worthless.

Record the result as a table: hypothesis · evidence · verdict (ruled out / not ruled out). Keep
the ruled-out rows in the summary; they are why the conclusion is credible.

**Attribution needs two libraries** (`CLAUDE.md` §6): before blaming a change, build the baseline
without it and measure both. This is the most error-prone step in the process, so do it explicitly:

```bash
# an isolated copy OUTSIDE the repo (CLAUDE.md §2) — never a second build dir inside it,
# which would let a stray rebuild cross-contaminate the two libraries
git worktree list                                  # check for an existing one first
cp -r <repo> /tmp/cfdapp-baseline-<phase>          # or: git clone <repo> <dir> && git checkout <sha>
# then revert ONLY the change under test, and prove it:
sha256sum build/release/libcfdcore.a               # must match the frozen hash if reconstructing
```

The baseline is valid only when it is either (a) a checkout of the pre-change SHA, or (b) a
reconstruction whose library sha256 **matches the frozen one recorded in `logs/00_freeze.log`**.
A baseline you cannot hash-match is not evidence — say so rather than reporting attribution from it.

## 5. Root cause

State the cause as a mechanism, in one sentence, with the evidence that demonstrates it. Then
classify it — the class determines the fix's blast radius:

```text
assembly · discretization · boundary treatment · solver criterion · solver integration
fallback/recovery · transfer/residency · backend state · toolchain/architecture
test defect · harness/evidence defect
```

If you cannot name the mechanism, you do not have the root cause yet.

## 6. Minimal fix

Fix the demonstrated cause and nothing else. Prefer restoring an existing invariant over inventing
a new one — where a CPU reference path already encodes the correct criterion, mirror it exactly
rather than devising a third behaviour. State explicitly what you deliberately did **not** change
and why.

## 7. Negative control (required where practical)

Build a mutant tree with the fix reverted — or the defect reintroduced — and show it reproduces
the failure **with the same values**: same status, same iteration count, same message. That is
what separates "I fixed the cause" from "I perturbed the system until it passed".

Record the control in its own log. If a negative control is impractical, say so explicitly and
explain why.

## 8. Regression

Focused tests first, then the ladder `cfdapp-verify` selects for the repaired code. A repair to a
numerical criterion is a numerical change — it earns `cfdapp-numerics`. A repair in `cuda/` earns
`cfdapp-cuda`.

## Evidence

`results/<phase>/summary.md` should carry, in this order: root cause · what was ruled out before
any change · the canonical reproducer · the mechanism shown directly · the minimal fix · acceptance
· the negative control · known remaining asymmetries not fixed · regression and diagnostics ·
files changed.
