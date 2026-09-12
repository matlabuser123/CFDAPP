# CLAUDE.md — CFDApp Agent Rules

CFDApp is a C++20 finite-volume CFD application (SIMPLE/PISO, thermal,
turbulence, species, multiphase, compressible foundations, CPU/OpenMP/CUDA,
CLI + Qt6/QML GUI). It is developed under an **evidence-first** workflow:
nothing is "done" until it is implemented *and* verified with real evidence.

This file is permanent working rules for Claude Code on this repo. It does
not track project status — see `TODO.md` (active work) and `ROADMAP.md`
(phase history/direction) for that, and `results/` for detailed evidence.

## Workflow

1. Read `TODO.md` and `ROADMAP.md`.
2. Inspect current git state (`git status --short`, recent log).
3. Before a major feature: audit the existing implementation, tests,
   production dispatch, config/parser support, GUI exposure, docs, and
   existing evidence. Compare reality against `TODO.md`/`ROADMAP.md` — an
   unchecked item is not proof the work is missing (this repo has had
   roadmap/doc drift before); trust source and tests over checkboxes.
4. Work only the approved/active task. Don't silently start later phases,
   unrelated roadmap items, speculative refactors, or scope expansion.
5. Define acceptance gates before implementing.
6. Implement the smallest coherent change.
7. Build, then run targeted tests for what changed.
8. Diagnose and fix root causes of failures; rerun.
9. Run the required broader regression (and physical/numerical validation
   when the change is numerical).
10. Review `git diff` for incidental/unrelated changes (including
    regenerated result files — revert noise, see below).
11. Record evidence, update `TODO.md`/`ROADMAP.md`.
12. Commit and push only after gates pass.
13. Stop at the approved task boundary and report.

## Completion Rule

`[x]` means **implemented and verified with real evidence**. It never means
code was written, it compiled, a test was added, or verification is merely
planned. If verification is incomplete, leave `[ ]`. When uncertain, leave
`[ ]` until the evidence resolves it.

## Never Fabricate Evidence

Never invent, infer, estimate, or backfill test/benchmark/CI/GUI/hardware/
release results, residuals, commit SHAs, or command output. If evidence
doesn't exist, say `NOT VERIFIED` and record the blocker. Never convert an
assumption into a PASS.

## CFD / Numerical Rules

- A successful build proves only that the code compiled — not conservation,
  convergence, stability, accuracy, or CPU/GPU equivalence. Numerical
  changes need numerical evidence.
- Self-consistency is not physical validation (e.g. re-deriving the same
  formula in a test proves implementation consistency, not correctness).
  For important physics/numerics, prefer analytical solutions, manufactured
  solutions, or literature benchmarks, and say plainly which kind of check
  each test is (regression / consistency / verification / validation).
- Never weaken an acceptance criterion, loosen a tolerance, disable a test,
  inflate an iteration limit, or special-case a benchmark just to make a
  failing gate pass. A failed gate is a valid, reportable result — fix the
  root cause, or report the failure honestly.
- New physics/solver work must not silently change existing behavior
  (incompressible, thermal, turbulence, species, multiphase, compressible,
  CLI, GUI, CPU/OpenMP/CUDA, file formats). Prefer a dedicated new path
  (e.g. a new solver class) over conditional logic bolted onto a
  regression-sensitive existing one; if refactoring shared code, show the
  existing results are unchanged within explicit tolerances.
- Document non-obvious numerical constants (tolerances, relaxation factors,
  CFL limits, iteration limits) — label empirical values as empirical.

## Manual / Hardware Verification

- Automated GUI/controller tests do not substitute for a human-executed GUI
  acceptance test when one is explicitly required. Never claim to have
  visually inspected or clicked a native GUI window yourself.
- Evidence from one hardware/OS environment does not generalize to another
  (e.g. WSL2 CUDA does not establish native Windows CUDA). Identify the
  actual environment tested.
- Performance/CPU-GPU-equivalence claims need measured, end-to-end evidence
  (hardware, build type, problem size, thread/device count, comparison
  baseline) — not kernel-only microbenchmarks presented as solver speedup.

## Git Discipline

Before committing: `git status --short` and `git diff` — confirm only
intended files changed, and revert incidental noise (e.g. regenerated
`results/validation/**/validation.json` timestamps/timings from running
tests). Commit one coherent task per commit. After pushing:
`git rev-parse HEAD` and `git rev-parse origin/main` must match before
claiming the change is pushed. Report the full commit SHA for phase
completions. Never skip hooks or bypass signing unless explicitly asked.
Once a release is published, its evidence is immutable — record any
correction going forward (this repo, the next release notes), never rewrite
historical release evidence in place.

## Documentation Map

- `TODO.md` — what's active, blocked, and next.
- `ROADMAP.md` — phase history and direction.
- `README.md` — user/developer introduction.
- `CLAUDE.md` — this file: permanent agent rules.
- `results/<task>/` — detailed evidence (commands, counts, failures, fixes).

Update `TODO.md`/`ROADMAP.md` only after verification, not when starting
work. Keep detailed logs and transcripts in `results/`, not in these docs.

## Stop Conditions

Stop and report instead of working around it when:

- the architecture conflicts with the approved plan,
- a feature needs a bigger case/file-format change than approved,
- a numerical or physical-validation gate fails,
- a regression appears,
- required hardware or human verification is unavailable,
- finishing the task would mean silently starting another roadmap phase,
- evidence is ambiguous,
- the proposed fix would weaken an existing guarantee.

Report the blocker instead of inventing evidence.

## Principle

Prefer correctness over speed, evidence over confidence, root-cause fixes
over workarounds, small verified stages over large speculative changes, and
honest limitations over exaggerated capability.
