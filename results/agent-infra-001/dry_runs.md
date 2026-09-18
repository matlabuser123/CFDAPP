# AGENT-INFRA-001 — self-test dry runs

Three hypothetical tasks routed through the skill system. **No file was modified, no build, test or
benchmark was run.** Each was executed by an independent agent given only the repository and the
skills — not the author's reasoning — so that the test did not share the author's blind spot
(`cfdapp-review` item 4). Each was asked to route the task *and* to attack the skills.

The tests were run against the **initial** version of the skills. Every defect below was
subsequently fixed; §4 records the fixes.

## Test A — numerical bug

> "The pressure gradient loses second-order convergence on a skewed mesh."

**Expected:** understand → reproduce → numerical analysis → root-cause investigation; **no immediate
random fix.**

**Result: PASS.** Classified `bug repair` + `numerics` + `mesh` + `physics`; selected nine of the ten
skills and excluded `cfdapp-cuda` **on evidence** (`grep` proved no GPU path shares `Gradient`),
not by assumption. Verification earned: L1–L4, L6–L9, L11, L12; L5 and L10 correctly declined with
justification. It did not open `Gradient.cpp`.

Two behaviours the skills produced that the task did not ask for:

* It stopped at Step 0 on `AUTHORIZED NEW DEVELOPMENT: None` and `No new development while CI is
  running`, and identified four existing `TODO.md` debt items the task might *be* — asking which,
  rather than starting.
* Following `cfdapp-understand`'s "existing evidence is not optional", it found the symptom is
  **already measured and gated**: distorted-mesh pressure L2 observed order **1.678** against a
  frozen gate of `[1.5, 2.4]` (`results/p12-mesh-001/`), with the first-order boundary ring
  disclosed as deliberate in `results/p12-num-003/` §16. Its first hypothesis became an
  *iterative-convergence floor*, not a discretization defect — `cases/poiseuille_distorted/solver.json`
  uses fixed absolute tolerances (`pressure_tolerance: 5e-4`) against an error plateau of ~1.28e-3,
  which `cfdapp-numerics` §3 names as the classic way to manufacture a fake order loss. The
  discriminating test is a tolerance sweep requiring no production change.

## Test B — CUDA performance

> "Reduce GPU SIMPLE runtime at 160×160."

**Expected:** understand → baseline → transfer analysis → architecture → correctness constraints →
optimization. **Must NOT immediately start rewriting kernels.**

**Result: PASS.** It did not touch a kernel, and placed the first kernel edit at step 8 of 16,
behind authorization, evidence reconciliation, a measured baseline and a profile.

It also reached the correct *technical* conclusion from existing evidence: at 160² the profile shows
**82,869 D2H calls** moving 103 MB in 7.92 s (~95 µs/call) — about **7 downloads per Krylov
iteration**, matching BiCGSTAB's ~7 reductions. The path is **latency-bound on synchronization**, not
bandwidth- or kernel-bound (kernel time is 9.0 s of 22.9 s). So the first change is to finish the
reduction on-device, not to optimize a kernel.

It then bounded the achievable result **in advance**: removing all download time gives 22.9 s →
~15.0 s, still **0.77× against the 11.58 s CPU** — because 160² sits *below* the measured crossover.
The honest deliverable is "less slow", not "faster", and it proposed saying so before starting.

It stopped four times: the task **is** GPU-PIPE-001 (unauthorized); CI is mid-flight on HEAD; the
target is ring-fenced debt; and three **contradictory, unmarked** 160² baselines exist across
`results/` (1.79× / 0.46× / 0.51×), which `CLAUDE.md` §12 makes a stop condition.

## Test C — documentation

> "Fix a typo in README.md."

**Expected:** lightweight verification; **must NOT launch the full regression.**

**Result: PASS.** Earned exactly **L11 + L12**, declined all ten other levels with a reason each, and
refused the regression citing three independent places in the skill text. It declined to commit,
noting CI was mid-qualification on `c1355ea`.

It also produced the finding this test was really probing, unprompted: **`README.md` is dense with
evidence-derived claims** — `2.0x`, `80×80`, `1.9e-13`, phase IDs, `results/` citations — so a
character that looks like a typo may be a recorded number, and "correcting" it would be editing
evidence.

## 4. Defects found, and the fixes applied

24 findings across the three reports. All were addressed; every factual claim was re-verified
against the repository before acting on it.

### Wrong statements (would have cost time)

| # | Defect | Verified how | Fix |
| --- | --- | --- | --- |
| 1 | `cfdapp-understand` said tests are "registered by `cfdapp_add_test()`". **It has zero callers** and does not link `GTest::gtest_main`; a test written that way would not compile as a gtest. | `grep -rn cfdapp_add_test --include=CMakeLists.txt` → 0 outside `cmake/` | Corrected to hand-written `add_executable` + `gtest_discover_tests`; the helper is now flagged unused |
| 2 | `cfdapp-cuda` gave the GPU test binary as `build/release/tests/...`. **`build/release` has `CFDAPP_ENABLE_CUDA=OFF`** — that binary does not exist. (The self-test's own suggested path was wrong the same way.) | `CMakeCache.txt` per build dir; `find build -name CFDGpuTests` → only `build/cuda`, `build/perf` | Documented that no preset enables CUDA, gave the explicit `build/cuda` configure line, and added "check `CMakeCache.txt` before trusting a GPU pass" |
| 3 | `cfdapp-verify` labelled its clang-format command "CI's exact scope". CI also matches `*.h`, which a `*.[ch]pp` glob misses. | read `.github/workflows/ci.yml` format step | Replaced with CI's `find` command verbatim |
| 4 | `cfdapp-cuda` quoted only the favourable end of the crossover ladder (1.33×, 3.20×), omitting **0.506× at 160²** — the single most decision-relevant number for test B's task. | `results/gpu-pcorr-001/` §9 | Full ladder now quoted, with the crossover located between 160² and 320² |

### Routing holes

| # | Defect | Fix |
| --- | --- | --- |
| 5 | **A performance change that alters floating-point association is a numerics change** — reordering a reduction, fusion, atomics, FMA contraction. The router caught only "changes a discretization", so an on-device reduction would have shipped **without L4**, perturbing the Krylov trajectory through `cancelledToRoundingLevel` — the criterion GPU-PCORR-001 exists to fix. | Named explicitly in Step 1; `cfdapp-numerics` + L4 added to the performance row |
| 6 | `results/` was routed as documentation (L11+L12). It is the **evidence corpus** — that row would license rewriting a frozen gate. | Split out; modifying existing evidence is now a stop-and-ask, appending is normal |
| 7 | A `cuda/**`-only change matched no base row, inheriting **no build or test level**. | Own self-contained row |
| 8 | `performance` row omitted `cfdapp-blast-radius`; `numerics` row omitted L11 (CI's format job would fail). | Both added; floors declared as minima that union with `cfdapp-verify` |
| 9 | Stop-conditions list omitted the most likely stop: *the task is a `TODO.md` → NEXT phase with no authorization.* | Added, with "match on what the work **is**, not whether the phase name was used" |

### Underspecified

| # | Defect | Fix |
| --- | --- | --- |
| 10 | L11/L12 were **vacuous** for a doc-only diff — clang-format matches zero changed files, yet would be recorded as "L11 PASS". The set attacked vacuity in review while manufacturing it in verify. | "Doc form" defined for both: `git diff --check` + line-ending check; diff containment + claim traceability |
| 11 | Nothing warned that prose under edit may **be evidence**. | Added to `cfdapp-verify` and as `cfdapp-review` item 5 |
| 12 | The skills demanded MMS, Richardson, GCI and distorted meshes without mentioning **the repo already ships all of them**. | New `cfdapp-numerics` §2a naming `include/cfd/validation/`, `DistortedMesh.hpp`, `ManufacturedFields.hpp`, `test_mms_simple.cpp` |
| 13 | `cfdapp-understand` omitted `benchmarks/` — the instrument that produced every GPU performance number — on routes whose defining requirement is "measured baseline first". `cfdapp-verify` gave eight ctest commands and zero benchmark commands. | Benchmarks section added; benchmark invocations added |
| 14 | Two selectors for one decision: blast radius vs the diff table, with no precedence — and the gate must be frozen *before* a diff exists. | Blast radius selects up front; the diff table is the post-hoc check. Never lower a frozen ladder to match a smaller diff |
| 15 | `cfdapp-debug` called `results/gpu-pcorr-001/` "the worked model", but it has **no `logs/00_freeze.log` and no `acceptance_gate.md`** — the artifacts `cfdapp-closeout` audits for. | Scoped to the *investigation*; layout now points at `results/p12-grad-002/a2/` |
| 16 | Competing report formats (`CLAUDE.md` §13 vs closeout §3). | Closeout declared an explicit superset; §13 remains authoritative |
| 17 | "Baseline outside the repo" had no procedure, though it is the most error-prone step. | Commands added, plus the rule that a baseline is valid only at a matching frozen sha256 |
| 18 | `results/validation/**` is both regeneration noise and tracked evidence; "restore the noise" was dangerously close to "restore the evidence" on a numerics phase. | Three-way classification: runtime-only → restore; unexpected value change → **stop**; authorized numerics change → keep and account for |
| 19 | Step 0.2 could not tell an active phase's tree from agent-infrastructure churn. | Distinction added |
| 20 | No WSL entry point, though both skills say builds run in WSL2 and this session is Windows. | `wsl.exe -d Ubuntu-22.04` form added, with the hash and clock caveats |
| 21 | `cfdapp-review` had no proportionality clause while `cfdapp-verify` did, so a typo earned a page of "n/a". | Scaling clause added |
| 22 | "Build input" was undefined; routing-table `Required skills: —` contradicted "never skipped"; no phase-ID convention; CTest label `numerical` vs target name. | All four defined |
| 23 | Local cost was treated as the whole cost. **`ci.yml` has no path filters** — every push runs the full ~4.5 h matrix. | Added, with the recommendation to carry trivial doc fixes on the next substantive commit |
| 24 | The "4.5-hour regression" figure was a **CI** number used to justify skipping a **local** ladder (L6–L8 ≈ 15 min each, L9 hours). | Local and CI costs now stated separately and honestly |

## 5. Reported, not fixed — outside this phase's scope

**`TODO.md`'s GPU-PIPE-001 motivation block cites stale numbers.** Verified:

```text
TODO.md:173   ~71,031 downloads     ← the PRE-fix count
              results/cuda-qual-001/data/cuda_end_to_end/runs.csv  → 71031
              results/gpu-pcorr-001/data/cuda_end_to_end_after_fix/runs.csv → 82869
```

GPU-PCORR-001's `absDot` added a reduction per iteration, so the current authoritative figure is
**82,869**, 17 % higher. `TODO.md:177`'s "resident SpMV ~51× CPU" is the **800² / 640k-unknown**
kernel-only figure from `results/cuda-qual-001/performance.md`, quoted under a heading reading
"160²"; at 160² the same table sits between 10.4× and 21.4×, and `CLAUDE.md` §4 says kernel-only
microbenchmarks do not count as performance claims.

Left unchanged deliberately: `TODO.md` is the user's control file, these are evidence-derived
numbers, and this phase's own rule is that a recorded number which looks wrong is **a finding to
report, not a typo to correct**.

**Three unmarked contradictory 160² baselines** exist across `results/performance/cuda_end_to_end/`
(1.79×, CUDA 11.5), `results/performance/large_grid_stress/` (0.46×) and
`results/gpu-pcorr-001/` (0.506×, current). `CLAUDE.md` §4 requires superseded results to be marked
`INVALID AS AUTHORITATIVE` in place; none is. Marking them is an edit to existing evidence and needs
its own authorization.

## 6. What the self-tests do and do not establish

They establish that the skills **route correctly** on three tasks spanning the cost spectrum, and
that they read clearly enough for an agent with no other context to follow and to argue with.

They do **not** establish that an implementation following the skills would succeed — no code was
written, built or run. The first real test is whichever phase the user authorizes next.
