---
name: cfdapp-task
description: Top-level router for any non-trivial CFDApp engineering task. Classifies the task, selects the required skills and the proportionate verification ladder, and enforces the stop points. Use this first when asked to implement, fix, investigate, optimize or extend anything in CFDApp.
---

# CFDApp task router

`CLAUDE.md` is authoritative for governance, authorization and evidence. This skill is the
**procedure**: what to do, in what order, and how much verification the task actually earns.

## Lifecycle

```text
CLASSIFY → UNDERSTAND → BLAST RADIUS → ARCHITECT → FREEZE GATES → IMPLEMENT
→ FOCUSED VERIFICATION → NUMERICAL/CUDA VERIFICATION → NEGATIVE CONTROL
→ FULL REGRESSION → ADVERSARIAL REVIEW → EVIDENCE → STOP
```

Steps are **skippable only by the routing table below**, never because a step looks likely to pass.

## Step 0 — before anything

1. Read the authorization message. Name its exact scope, its stop rules and its forbidden actions.
2. `git status --short` — is another phase or session mid-flight? Is CI running on this SHA?
   Never edit files another active phase is changing (`CLAUDE.md` §2). Distinguish an **active
   phase's working tree** (uncommitted `src/`, `include/`, `cuda/`, `tests/` or a live
   `results/<phase>/`) from **infrastructure churn** (`.claude/`, `docs/`, agent files): the first
   blocks your work, the second only means do not edit those files.
3. Read `TODO.md` → what is ACTIVE, what is NOT AUTHORIZED, what is listed debt.
4. If the task is not authorized, or is ambiguous, **ask before working** — do not infer
   authorization from `ROADMAP.md` or `TODO.md` → Future.

## Step 1 — classify

Tag the task with every category that applies:

```text
documentation · test-only · CI · numerics · physics · mesh · CUDA
performance · GUI · I/O · production integration · bug repair
```

Classification is by **what the change touches**, not by how it was phrased. A "small doc fix"
that edits `src/` is a production change. A "quick perf tweak" that changes a discretization is a
numerics change.

**The trap worth naming: a performance change that alters floating-point association is a
numerics change**, even though it touches no discretization. Reordering or fusing a reduction,
moving a final sum on-device, switching to atomics, enabling FMA contraction or mixed precision all
change computed values — and on this codebase a changed dot product feeds `cancelledToRoundingLevel`,
the scale-relative breakdown criterion, so it can flip a Krylov trajectory. Route it through
`cfdapp-numerics` and earn **L4**. A "bitwise identical" claim is not available for such a change;
gate the **solution** difference against the CPU reference with a threshold derived from accumulated
round-off, never one chosen to fit the observed output.

## Step 2 — route

| Category | Required skills | Verification floor (see `cfdapp-verify`) |
| --- | --- | --- |
| documentation | `cfdapp-review`, `cfdapp-closeout` | L11 + L12 in **doc form**; add L1 only if the file is a real build input (below). Editing `results/` is **not** documentation — see `cfdapp-verify`. |
| test-only | `cfdapp-understand` | L2, L3, L11 |
| CI | `cfdapp-understand`, `cfdapp-blast-radius` | L1, L11, plus the CI change proved on a real run |
| numerics / physics / mesh | `cfdapp-understand`, `cfdapp-blast-radius`, `cfdapp-architect`, **`cfdapp-numerics`** | L1–L4, L6–L9, L11, L12 (+ L5 and L10 if a GPU path shares the code) |
| CUDA | `cfdapp-understand`, `cfdapp-blast-radius`, `cfdapp-architect`, **`cfdapp-cuda`** | L1, L2, L3, L5, L10, L11, L12 (+ L6–L9 if a shared header or `src/` also changed) |
| performance | `cfdapp-understand`, `cfdapp-blast-radius`, `cfdapp-architect` (+ `cfdapp-cuda` if GPU, **+ `cfdapp-numerics` if it alters floating-point association**) | measured baseline **first**, then L1–L3, L5/L10 as applicable, **L4 if FP association changes**, L12 |
| GUI / I/O / production integration | `cfdapp-understand`, `cfdapp-blast-radius` | L1–L3, L6, L7, L11, L12 |
| bug repair | **`cfdapp-debug`** + the category of the code being repaired | the repaired code's floor, plus a negative control |

These floors are **minima**. Take the union with `cfdapp-verify`'s own path table; where the two
differ, the higher requirement wins.

Every route ends with `cfdapp-review` then `cfdapp-closeout`. Those two are never skipped — though
for a documentation- or test-only diff, `cfdapp-review` may answer its inapplicable items
`n/a (documentation-only)` once for the block rather than line by line.

**"Build input"** means the file is consumed by the build, the tests or CI — not merely that it is
tracked. Test it, do not assume it:

```bash
grep -rn '<filename>' CMakeLists.txt '**/CMakeLists.txt' cmake/ .github/workflows/ tests/ python/
```

A hit in `configure_file`, an install rule, a test fixture or a CI step makes it a build input
(add L1). The root `README.md` has no such consumer.

**Mandatory project gates that no routing may waive:** never weaken a threshold; never delete a
failed result; stop at the first failed gate; no commit or push without explicit authorization for
that action.

## Step 3 — proportionality

Verification cost must match risk, in both directions. Over-verifying is a failure of the same
standing as under-verifying: `CLAUDE.md`'s hardware policy gives authoritative runs the machine, so
a needless regression can degrade a run that actually matters.

* A typo in `README.md` earns L11 + L12 in doc form — not the local regression ladder (L6–L8 are
  ~15 min each; L9 is hours).
* A change to a discretization, a breakdown criterion, a boundary treatment or a shared operator
  earns the full ladder even if it is three lines.
* If unsure which side a change falls on, run `cfdapp-blast-radius` and let its output decide.

**Local cost is not the whole cost.** `ci.yml` has **no path filters**: every push to `main` runs
the entire matrix — build-test ×3, format, clang-tidy, sanitizers ×5, coverage, python — currently
about 4.5 hours end to end. A one-character documentation commit costs that much CI. So a trivial
doc fix is usually better held and carried along with the next substantive commit than pushed
alone. Say so when recommending the next action.

## Stop conditions

Stop and report — do not work around — when any of `CLAUDE.md` §12 fires, and additionally when:

* **the task is, in substance, a phase listed under `TODO.md` → NEXT or FUTURE and no authorization
  was given for it** — the most likely stop, and the easiest to walk past. Match on what the work
  *is*, not on whether the phase name was used;
* `TODO.md` says `AUTHORIZED NEW DEVELOPMENT: None`, or a gate is mid-flight on HEAD;
* prior evidence for the task's baseline is ambiguous or contradictory across `results/` phases;
* the task as scoped would require changing production behaviour in an infrastructure-only phase;
* finishing the task would silently begin another phase;
* an acceptance gate looks vacuous, contradictory or mis-derived **after** freezing;
* the authorization does not cover an action the task turns out to need.

Report the stop in the `cfdapp-closeout` format. Do not repair unrelated findings on the way past.
