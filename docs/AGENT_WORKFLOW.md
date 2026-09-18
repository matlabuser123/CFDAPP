# CFDApp agent workflow

> How Claude Code is expected to work on CFDApp.
> Rules: [CLAUDE.md](../CLAUDE.md) · Tasks: [TODO.md](../TODO.md) · Evidence: [results/](../results/)

## Why this exists

CFDApp is developed as a series of pre-registered numerical experiments. That process — freeze the
gate, dry-run every criterion, prove non-vacuity, execute fresh, stop at the first failure, preserve
the failure — is what makes the results trustworthy. It was previously carried in prose and in one
engineer's head, and it had to be restated for every task.

The skills under [`.claude/skills/`](../.claude/skills/) encode it as reusable procedure, so the
same engineering judgment applies whether the task is a typo or a solver repair.

**The division of labour:** `CLAUDE.md` holds the *rules* — governance, authorization, the evidence
standard — and stays authoritative. The skills hold the *procedure* — what to do, in what order,
with which commands, against which real file paths. The skills never override `CLAUDE.md`.

## The skills

| Skill | Responsibility |
| --- | --- |
| `cfdapp-task` | Router. Classifies the task, picks the skills and the verification floor, enforces stop points. |
| `cfdapp-understand` | Understand before editing. Carries the verified repository map and the production path. |
| `cfdapp-blast-radius` | What a change can reach: callers, tests, formats, backends, compatibility, evidence at risk. |
| `cfdapp-architect` | Design before implementation: interfaces, ownership, failure behaviour, gates, controls. |
| `cfdapp-debug` | Bug repair: reproduce → freeze → narrow → hypothesize → discriminate → root cause → minimal fix → negative control. |
| `cfdapp-numerics` | Numerical changes: formulation first, derived tolerances, MMS, grid convergence, conservation, independent paths. |
| `cfdapp-cuda` | GPU work: CPU is the reference; execution vs fallback; the equivalence ladder; valid performance claims. |
| `cfdapp-verify` | The L1–L12 verification ladder, the exact commands, regression integrity, exact counts. |
| `cfdapp-review` | Adversarial self-review: vacuity, shared mistakes, hidden fallbacks, stale binaries, weakened thresholds. |
| `cfdapp-closeout` | The final report, the evidence audit, and the commit/push authorization boundary. |

## Normal workflow

```text
TASK → UNDERSTAND → BLAST RADIUS → ARCHITECT → FREEZE GATES → IMPLEMENT
→ FOCUSED VERIFICATION → NUMERICAL / CUDA VERIFICATION → NEGATIVE CONTROL
→ FULL REGRESSION → ADVERSARIAL REVIEW → EVIDENCE → STOP
```

Steps are skipped only by `cfdapp-task`'s routing table, never because a step looks likely to pass.
`cfdapp-review` and `cfdapp-closeout` are never skipped.

## Examples

**Bug repair**

```text
cfdapp-task → cfdapp-understand → cfdapp-debug → cfdapp-verify → cfdapp-review → cfdapp-closeout
```

**New numerical feature**

```text
cfdapp-task → cfdapp-understand → cfdapp-blast-radius → cfdapp-architect → cfdapp-numerics
→ implementation → cfdapp-verify → cfdapp-review → cfdapp-closeout
```

**CUDA optimization**

```text
cfdapp-task → cfdapp-understand → cfdapp-blast-radius → cfdapp-architect → baseline
→ cfdapp-cuda → implementation → cfdapp-verify → benchmark → cfdapp-review → cfdapp-closeout
```

**Documentation fix**

```text
cfdapp-task → edit → L11 doc form (git diff --check + line endings)
            → L12 doc form (diff containment; every claim traceable to results/)
            → cfdapp-review (n/a for most items) → cfdapp-closeout → done
```

Note two traps the router covers: `results/` is **evidence, not documentation** — modifying it needs
its own authorization; and prose files like `README.md` cite measured numbers, so a "typo" may be a
recorded result. Trace it before changing it.

## Verification levels

`cfdapp-verify` selects from the union of every level the diff implies:

```text
L1  build                       L7  Debug + GUI regression
L2  focused tests               L8  Clang regression
L3  subsystem tests             L9  sanitizer regression (CI settings)
L4  numerical acceptance gates  L10 CUDA diagnostics
L5  CPU/GPU equivalence         L11 formatting / whitespace
L6  full Release regression     L12 evidence audit
```

Cost is proportional to risk in **both** directions: a `README.md` typo does not earn a 4.5-hour
regression, and a three-line change to a shared discretization does earn the full ladder.

## Evidence

Reports go in `results/<phase>/`, never in `TODO.md`:

```text
results/<phase>/
├── summary.md            the narrative, every number traceable to a log
├── acceptance_gate.md    frozen before any production change (+ _AN.md per amendment)
├── logs/NN_*.log         raw output, exact commands, counts, hashes, environment
├── tools/                probes, reproducers, independent reference implementations
└── data/                 measurements
```

`TODO.md` carries only `FINISHED · PROCESSING · NEXT · FUTURE · TECHNICAL DEBT` plus short links.

Failed and superseded results are marked in place (`INVALID AS AUTHORITATIVE`) and never deleted.

## The authorization boundary

This is the line the skills exist to hold:

* Authorization is **per phase and per action**. It never carries over.
* A successful implementation does **not** imply permission to commit.
* A successful commit does **not** imply permission to push.
* Appearing in `TODO.md` → NEXT or `ROADMAP.md` is **not** authorization.
* Finishing a phase does not authorize starting the next one.

The skills can plan, verify, review and report. Committing, pushing, and starting a new phase remain
the user's decision, every time.
