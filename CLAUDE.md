# CFDApp — Claude Engineering Rules

CFDApp is a C++20 finite-volume CFD application: SIMPLE/PISO flow with multi-physics, 2D production
meshes and a 3D foundation, CPU/OpenMP/CUDA, and a CLI plus Qt6/QML GUI on one solver backend. It is
developed as a series of **pre-registered numerical experiments**. This file is the permanent
operating contract. It holds rules, not project status.

**Read first:** this file → [TODO.md](TODO.md) → [ROADMAP.md](ROADMAP.md) → the relevant
`results/<phase>/summary.md`.

## 1. Prime Directive

1. **`[x]` means implemented AND verified with real evidence.** Implementation alone is never
   completion.
2. **Never fabricate evidence.** No invented, estimated or backfilled results, counts, SHAs or
   output. If evidence is missing, write `NOT VERIFIED`.
3. **Work top-to-bottom and stop at the first failed acceptance gate.** Do not continue because
   later items would probably pass.
4. **Never weaken a threshold to make an implementation pass.**
5. **Never erase a failed experiment, rewrite chronology, or turn a historical FAIL into a PASS.**
6. **Work only the authorized scope.** No commit and no push unless explicitly authorized.

Correctness over speed. Evidence over confidence. Root-cause fixes over workarounds. Honest
limitations over exaggerated capability.

## 2. Authorization & Scope

* Authorization is **per phase and per action**. It does not carry over to a later phase,
  amendment, commit or push.
* Appearing in `ROADMAP.md` or `TODO.md` → Future is **not** authorization.
* **Do not fix unrelated defects inside another phase.** Record them under `TODO.md` → Known
  Technical Debt and report them.
* **Investigation-only phases change nothing authoritative** — no production code, tests,
  thresholds, golden outputs or prior evidence — unless explicitly authorized. Probes go in
  `results/<phase>/tools/`. Baselines are built from a copy **outside** the repo.
* State **library/API-only** scope explicitly whenever the production path is not in scope (§9).
* **Trust source and tests over checkboxes.** This repo has had doc drift. Audit before assuming
  work is missing or done.
* **Check for in-flight work before editing.** Look at `git status` and recently modified
  `results/` files. Never edit files that another active phase or session is changing.

## 3. Standard Phase Workflow

1. Read the authorization — its exact scope, stop rules and forbidden actions.
2. Read the existing evidence: `TODO.md`, the phase's `results/` directory, and the predecessors.
3. Confirm the exact scope. Ask if it is ambiguous.
4. Reproduce the baseline on the unchanged tree and record it.
5. Design the acceptance gate (§5).
6. Dry-run **every** criterion against the baseline.
7. Prove non-vacuity: each criterion must be able to both pass and fail.
8. Freeze the gate: record its sha256 plus production, test and library hashes in `logs/00_freeze.log`.
9. Implement the smallest correct change.
10. Run the focused verification, in gate order.
11. **Stop at the first failed criterion.** Preserve the result and report it.
12. Run the broader regression only after the focused gates pass (§8).
13. Write the evidence: `results/<phase>/summary.md`, logs and tools.
14. Update `TODO.md` and `ROADMAP.md` with **verified** state only.
15. Report in the §13 format.
16. Commit or push only if separately authorized (§11).

## 4. Evidence Standard

* Evidence lives in `results/<phase>/`: `summary.md`, `acceptance_gate.md` (plus
  `acceptance_gate_AN.md` per amendment), `logs/NN_*.log` (raw output, exact commands, counts,
  hashes, environment), `tools/` and `data/`.
* Record **exact** test counts from the actual run. Never estimate or reuse an earlier aggregate.
* Label every check as **regression**, **consistency**, **verification** or **validation**.
  Self-consistency (re-deriving the same formula) is not validation.
* **Never adopt production output as a reference value.**
* Evidence is environment-specific. WSL2 CUDA does not establish native Windows CUDA; name the
  environment actually tested.
* Automated GUI tests do not replace a required human GUI acceptance. Never claim to have seen or
  clicked a native window.
* Performance claims need end-to-end measurement: hardware, build type, problem size,
  threads/devices and baseline. Kernel-only microbenchmarks do not count.
* When a result is found to be invalid (for example, from stale binaries), mark it
  **INVALID AS AUTHORITATIVE** in place. Never delete it.

## 5. Gate Protocol

### 5.1 Design

* **Freeze numerical acceptance gates before any production change.**
* Name the exact quantity each threshold bounds. **Derive** its floor from that quantity's
  dimensional, round-off and discretization behaviour. If the quantity scales with 1/h or a
  coordinate ratio, the threshold must scale too. Label any empirical constant as empirical.
* Check criteria pairwise for contradictions. Check that every mesh in a criterion's list actually
  has the property the list is named for.
* Where it helps, include an exactly representable (dyadic) control that must give exactly 0.

### 5.2 Dry-run (mandatory before freezing)

* Run **every** criterion against the unchanged baseline, including criteria carried over
  unchanged. A clause that was never executed is not "passing".
* A sanity criterion that the baseline fails is a broken criterion.
* **Non-vacuity.** The instrument must exercise both pass and fail behaviour:
  * negative controls must fail — for example the pre-change library, a disabled feature, or a
    corrupted operator (sign flip, ×2 coefficient);
  * the correct implementation must pass.
* **If the baseline cannot exercise the new behaviour being gated, the dry-run is insufficient.**
  For example, "100 % fallback" is vacuously true on a library with no new path. Ask separately
  whether a correct post-change implementation can satisfy the criterion, and add a control that
  can fail.

### 5.3 Execution

* Run the criteria in the frozen order. Stop at the first failure.
* Classifying or explaining a failure **does not waive it**.

**When a frozen threshold is wrong:**

1. Preserve the original failure verbatim.
2. Investigate independently, without changing production.
3. Derive the replacement from first principles, never from the observed output.
4. Request authorization.
5. Freeze the amendment as a separate file, with its own dry-run and non-vacuity check.
6. Rerun fresh. Reuse no earlier result.

**Migrating a "legacy" test** is a hypothesis, not a finding. Audit each test individually. Tell
apart:

* an **obsolete constant** — replace it with an independently derived value;
* a **contaminated instrument** — the property still holds; re-test it where nothing can
  contaminate it;
* a **real defect detector** — do not migrate it; report the defect.

## 6. Numerical Correctness

* A successful build proves only compilation. It says nothing about conservation, convergence,
  stability, accuracy or CPU/GPU equivalence.
* Numerical changes require, where applicable:
  * an analytical or reference derivation, stated before measuring;
  * manufactured solutions;
  * grid convergence — observed order, Richardson extrapolation and GCI;
  * conservation, local and global;
  * dimensional consistency;
  * **independent reference paths**, for example exact-rational Python with no CFDApp code;
  * negative controls and non-vacuity checks.
* **New numerical methods require quantitative convergence verification. New physics requires
  independent physical validation** against analytical or literature results.
* An observed-order claim is valid only if all of these hold:
  * the quantity is iteratively converged (check against a long-run plateau);
  * the mesh family is self-similar and in the asymptotic range;
  * the error does not change sign (order is undefined across a zero crossing);
  * refinement triplets are parity-consistent.
* **Do not silently change existing behaviour** — incompressible, thermal, turbulence, species,
  multiphase, compressible, CLI, GUI, CPU/OpenMP/CUDA or file formats.
  * Prefer a dedicated new path over conditionals bolted onto shared code.
  * When shared code changes, prove existing results are unchanged (bitwise where claimed).
* **Attribution needs two libraries.** Before blaming a change for a failure, build a baseline and
  measure both. The baseline is either an isolated copy with only the change removed, or a
  reconstruction whose hashes match the frozen ones.
* **Numerical correctness before optimization.** Performance work must not alter numerics without
  equivalence evidence. The CPU path is the reference; GPU and OpenMP need verified equivalence.
* Document non-obvious constants: tolerances, relaxation factors, CFL limits and iteration limits.

## 7. Testing Protocol

* **Focused tests first**, for the code actually changed. Broader regression only after the focused
  gates pass.
* Never disable, skip, loosen or special-case a test or benchmark, and never inflate an iteration
  limit to get a pass.
* Build and test run in WSL2 via CMake presets `debug`, `release` and `asan` (`build/<preset>`).
* Sanitizer runs use CI's settings:
  * `ctest --preset asan --timeout 7200`;
  * `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0`;
  * `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`.
* Formatting: `clang-format-18 --dry-run --Werror` over `include/ src/ apps/ tests/`.
* Heavy `DISABLED_` validation runs use `build/release` with `--gtest_also_run_disabled_tests`, run
  from the repo root.
* Take timings from gtest's own `(N ms)` output; WSL's clock jumps. Do not run timing-sensitive
  jobs alongside a Debug `ctest -j32`.
* Every full `ctest` run rewrites tracked `results/validation/**` reports with timing-only changes.
  Classify those changes and restore the noise; do not commit it.

## 8. Regression Integrity

* **A regression result is authoritative only if the test binary was demonstrably rebuilt against
  the exact source and library being tested.**
* The authoritative sequence:
  1. configure;
  2. clean-first build of every target;
  3. verify that every test binary is newer than the newest production source;
  4. record the library and test-binary hashes;
  5. run and record exact counts.

  The harness must **fail closed** if any step fails.
* **Never report stale-binary results as current evidence.** A harness that rebuilds only *missing*
  binaries is invalid.
* A "known" failure stays known only while it reproduces its recorded values exactly. Any new or
  changed failure is a regression.
* The full regression is:
  * Release;
  * Debug + GUI;
  * sanitizers at CI settings;
  * clang-format clean.

  Report each count separately. The only permitted failures are those the gate names.

## 9. Production-Path Requirement

A feature is **not** production capability merely because a library API exists. Production means:

```text
case format → parser → builder → solver dispatch → results/export → CLI → GUI (where required)
```

* The only exception is scope that explicitly says library/API only. Record that limit in
  `TODO.md` and `ROADMAP.md`.
* Preserve backward compatibility. Committed cases must stay byte-identical, or every change must be
  explained. Breaking changes need explicit authorization.

## 10. Documentation Rules

Each kind of information has exactly one home:

| Information | Home |
| --- | --- |
| Current tasks, blockers, next action | `TODO.md` |
| Capability, direction, dependency order | `ROADMAP.md` |
| Agent rules | `CLAUDE.md` |
| Evidence, numbers, hashes, gate chronology | `results/<phase>/` |
| User and developer introduction | `README.md`, `CONTRIBUTING.md`, `docs/` |

* Update `TODO.md` and `ROADMAP.md` **after** verification, never when starting work.
* Keep `TODO.md` lean:
  * status, NEXT, blockers, one line per completed phase, links to evidence;
  * no derivations, hashes, test-count histories or amendment prose.
* `ROADMAP.md` describes capability gained and limits, not gate logs.
* Amendments are recorded as separate files. Superseded results are marked, never deleted.
* Release evidence is immutable. Record corrections going forward, in `ROADMAP.md` and the next
  release notes.
* Change this file only at the user's direction.

## 11. Git Rules

* **No commit or push unless explicitly authorized**, each time.
* Before any authorized commit:
  1. `git status --short` and `git diff` — only the intended files have changed;
  2. `git diff --check` is clean;
  3. focused tests pass;
  4. the required regression passes (§8);
  5. generated-file audit: runtime noise reverted, no build artifacts;
  6. evidence audit: `results/` is complete, and `TODO.md` and `ROADMAP.md` match it;
  7. `TODO.md` lists no push blockers.
* Make one coherent task per commit. Never skip hooks or bypass signing.
* After a push, all of these must hold before claiming it is pushed:
  * `git rev-parse HEAD` equals `git rev-parse origin/main`;
  * CI is green on that exact SHA;
  * the full SHA is reported.

## 12. Stop Conditions

Stop and report — do not work around the problem — when:

* an acceptance gate or a physical-validation criterion fails;
* a regression appears, or a known failure changes value;
* a gate criterion looks mis-derived, contradictory or vacuous after freezing;
* the fix would weaken an existing guarantee, or needs production changes in an
  investigation-only phase;
* the architecture conflicts with the approved plan, or needs a bigger case or file-format change
  than approved;
* a defect outside the scope is found;
* evidence is ambiguous, or a result cannot be reproduced exactly;
* required hardware or human verification is unavailable;
* finishing would silently start another phase.

## 13. Required Final Report

End every numerical or phase task with this report. Trivial edits need only the relevant fields.

```text
STATUS:            <verbatim verdict, e.g. "P12-X COMPLETE" / "BLOCKED / FAILED GATE at <item>">
SCOPE:             <authorized scope, and what was deliberately not done>
GATE:              <gate file + sha256>
RESULT:            <per-criterion PASS/FAIL, first failure named>

FILES CHANGED:     <all>
PRODUCTION FILES:  <list, or "none">
TEST FILES:        <list, or "none">
EVIDENCE:          results/<phase>/...

FOCUSED TESTS:     <exact run/passed/failed>
REGRESSION:        <exact counts per configuration, or "NOT RUN (reason)">
SANITIZERS:        <exact result, or "NOT RUN (reason)">

KNOWN FAILURES:    <each, reproduced values>
NEW FAILURES:      <each, or "none">

GIT HEAD:          <sha>
GIT STATUS:        <summary>
GIT DIFF --CHECK:  <clean / issues>

TODO UPDATED:      <yes/no>
ROADMAP UPDATED:   <yes/no>

NEXT REQUIRED USER DECISION: <explicit question, options>
COMMIT:            <none / sha — only if authorized>
PUSH:              <none / verified — only if authorized>
```

## CFDApp Agent Skills

For non-trivial engineering tasks use the repository-local skills under `.claude/skills/`. Start
with **`cfdapp-task`**, which classifies the task and selects the rest. Default lifecycle:

```text
understand → blast radius → architecture → implementation → verification → review → closeout
```

* Numerical work must use `cfdapp-numerics`.
* CUDA work must use `cfdapp-cuda`.
* Bug investigations must use `cfdapp-debug`.
* All verification goes through `cfdapp-verify`, which sizes the ladder to the change.
* `cfdapp-review` and `cfdapp-closeout` are never skipped.

The skills carry procedure — commands, paths, decision tables. **This file remains authoritative
for governance, authorization and evidence rules**, and a skill never overrides it. See
[docs/AGENT_WORKFLOW.md](docs/AGENT_WORKFLOW.md).

## Local Hardware / Resource Policy

Primary workstation: Intel i9-14900HX (24 cores / 32 threads, hybrid), 64 GB RAM, NVIDIA RTX 5000
Ada Laptop GPU (compute capability 8.9, ~15 GB VRAM), Windows 11 Pro. The canonical Linux
environment is Ubuntu 22.04 on WSL2. This policy governs how authorized work uses the machine. It
authorizes no new phase, optimization or benchmark.

* **Record the environment actually used** for hardware-sensitive work: `nproc`, `free -h`, the
  compiler, CMake, Ninja, `nvcc`, `nvidia-smi` and the relevant CMake cache entries.
  * Four different facts: the driver version, the driver-supported CUDA version, the installed
    toolkit (`nvcc`) and the GPU architecture.
  * WSL2 sees only part of the host RAM; check `free -h`.
* **Builds:** 16–24 jobs. Do not default to all 32 logical processors.
* **CTest:** start at `-j16`, and go lower for sanitizer, memory-heavy or long suites. More
  concurrency never justifies worse stability, memory, reproducibility or sanitizer reliability.
* **Authoritative runs get the machine.** While an evidence-producing build, regression, sanitizer
  run or performance run is active, launch no other heavy CPU or GPU work.
* **Performance evidence:**
  * Release, no sanitizers, no competing load.
  * A warm-up, then repeated runs. Report the median and spread, never the best run.
  * End-to-end timing, including transfers.
  * Record the hardware, compiler, CUDA toolkit, build configuration, threads, grid, iterations
    and timing method.
  * Record telemetry where practical, sanity-check it, and disclose possible thermal throttling.
* **Measure parallel scaling; do not assume it.** The CPU is hybrid, so 32 threads is not 32×.
  Measure speed-up and efficiency over a thread ladder, and check the numerical result at every
  thread count.
* **CUDA:**
  * Keep persistent data resident on the device, and avoid host↔device copies inside iterative
    loops.
  * Estimate VRAM for large cases and keep headroom. An out-of-memory run is not a result.
  * Claim a GPU speed-up only from end-to-end comparisons (CPU serial, CPU OpenMP, GPU) over a size
    ladder, and record the crossover point.
* **Memory:** estimate RAM before large grids. Parallel test instances must not exhaust it.
* **Profile before optimizing.** Every optimization keeps all correctness gates. Never loosen a
  tolerance because parallel or GPU execution fails.
* **Keep environments separate.** Every authoritative result names its environment. WSL2 + CUDA
  evidence does not verify native Windows + CUDA.
