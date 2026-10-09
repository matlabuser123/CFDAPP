# GPU closeout — summary

**Result: the lineage is committed and pushed; CI qualification is BLOCKED by a GitHub-side
condition that this lineage did not cause and this session cannot fix.**

```text
commit       3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
             feat(gpu): complete resident CUDA SIMPLE pipeline
             1214 files changed, 508,900 insertions(+), 261 deletions(-)
push         548401a..3bd32f9  main -> main    no force, no history rewrite
local HEAD == origin/main                       YES

local qualification    17 / 17 checks PASS
CI run 35579942600     FAILURE -- all 17 jobs in 1-3 s, three attempts, no runner
```

Nine of the ten closeout phases are complete. Phase 8 is the open gate, and it is open on
infrastructure, not on the code.

---

## 1. Phase 1 — audit

`audit.md`, `pre-clean-status.txt`, `audit-classification.txt`, `audit-artifacts.txt`.

```text
tracked modified   67      A 13    B 1    C 3    E 50    F 0
untracked        1187      A 48    D 1141                F 0
```

**Category F was empty in both.** Nothing in the tree was unexplained.

## 2. Phase 2 — the timing churn, proven then restored, twice

The claim "timing-only, 0 non-timing changes" was **re-proved on the tree as it stood**, using the
project's own structural classifier, before anything was restored:

```text
checked 3028 files;  IDENTICAL 1841,  RUNTIME-ONLY 50,  VALUES 0,  NEW 1137
```

50 files restored; `git diff -- results/validation/` went to 0.

**It had to be done again after Phase 4, and the second time the script refused.** Phase 4's four
test suites rewrote the reports, and the re-classification found `VALUES 5` — five files with
non-timing differences of ~3e-9 in distorted-mesh MMS norms. The script stopped rather than
restoring.

The cause was established by experiment: re-running only the four report-writing tests under
**Release+CUDA** returned the classification to `VALUES 0`. The five files had been written last by
the **ASan** build (Debug + sanitizers, CUDA OFF), whose optimization level and FP contraction
differ. Every one of those tests passes in both configurations. `audit.md` §6 has the detail.

**Practical rule this produced:** the restore must happen after a *Release* run, not merely after
the last run of any configuration.

**No numerical or reference result was discarded.** 50 files restored, each differing from HEAD only
in a timing field.

## 3. Phase 3 — hygiene

`phase3-hygiene.log`. Zero mutation markers, zero stray prints in the lineage's 59 production
sources, zero hardcoded paths in `src`/`include`/`cuda`/`apps`, root build configuration untouched,
no debug or sanitizer flag added to any `CMakeLists`, clang-format clean across 590 files, and
TODO internally consistent with P13 unstarted.

Two of that script's own checks were wrong and are recorded: a comment filter anchored against
grep's `file:line:` prefix, and a `-G` pattern that matched `Green-Gauss`. Both corrected; the
script now ends with a deliberate failing check proving its aggregator propagates.

`clang-tidy` was run as well, replicating the CI job: **exit 0, 4 warnings**, three pre-existing and
one newly introduced that copies the idiom fourteen lines below it verbatim.
`local-qualification/13-clang-tidy.log` also records that CMake writes **absolute** paths into
`compile_commands.json`, so the CI job's `^(include|src)/…` regex matches nothing and that job
analyses nothing — a pre-existing CI weakness, noted, not fixed here.

## 4. Phase 4 — local qualification of the exact committed tree

`local-qualification/`, `phase4-console.log`. All 17 checks PASS.

```text
clean Release+CUDA build      configure rc=0, build rc=0, 0 warnings, 235 targets
build freshness               test binaries newer than newest source; ninja "no work to do"
CTest Release+CUDA            1998 / 1998   0 failed   1404.93 s
15 GPU-DISC differential gates 15 / 15 green
residency transfer guard      PASS
resident-loop comparison      PASS, bitwise, 12 cases, 217,755 values
dispatch probe                PASS
lifecycle                     PASS
CPU/GPU production equivalence 15 cases, 0 failures
production GPU CLI smoke      PASS, exit 0, 4 output files
determinism                   PASS, all four backend combinations bitwise
CUDA diagnostics, re-run      16 / 16 clean, 0 vacuous
CPU-only build + suite        1932 / 1932, 0 nvcc invocations, no libcudart
Debug + GUI                   1984 / 1984   1273.28 s
ASan / UBSan at CI settings   1932 / 1932   3959.69 s   0 sanitizer reports
```

The counts match the previously qualified levels exactly; no test was added or removed by this
lineage.

The sanitizers were **re-run** rather than inherited: the earlier CUDA diagnostics predate the
clang-format remediation, which touched ten CUDA headers. The change is whitespace plus one
reordered `#include` and is proven null (`libcfdcore.a` byte-identical), but the source identity
differed and memory/synchronization headers are exactly what the brief names.

## 5. Phase 5 — diff review

`phase5-diff-review.log`. Run against the **index**, so what was reviewed is what was committed.

```text
staged 1213   A 61   B 1   C 3   D 1148   E 0   F 0
+508,879 / -261 lines, 59.30 MB
non-empty binary files staged        0
build artifacts / caches staged      0
leftover unstaged or untracked       0
```

The first run of this review **caught the re-appeared timing churn** (50 category-E files staged)
and is the reason §2's second restore happened before the commit rather than after it.

Excluded deliberately: two Nsight Systems binaries totalling ~10 MB (`.nsys-rep` + `.sqlite`),
whose derived text is committed and which turned out to contain no usable data at all; and six
zero-byte `.log.build` side-effect files. `.gitignore` gained a rule for profiler artefacts, with a
comment pointing at the derived text.

`git diff --cached --check` reports 204,067 trailing-whitespace diagnostics across **175 files, all
under `results/`** and **zero source files**. They are column-aligned machine-generated logs;
stripping the whitespace would alter verbatim program output, and already-committed evidence from
three earlier phases has the same property. `commit/diff-check-analysis.txt`.

## 6. Phases 6 and 7 — commit and push

```text
SHA          3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
worktree     clean apart from this closeout's own evidence
pushed       548401a..3bd32f9   main -> main
force        NO
local HEAD == origin/main       YES
```

**Two commits were pushed, not one.** `d7d74f5 docs(ci-perf-001): record the exact-SHA CI
qualification` was already unpushed before this closeout began; it rides along.

## 7. Phase 8 — CI: BLOCKED

`ci/ci-FAILED-all-attempts.txt`, `ci/run-35579942600.json`.

```text
run 35579942600   head_sha 3bd32f91   event push   conclusion FAILURE
all 17 jobs       failed in 1-3 seconds
whole run         6 seconds, created to updated
```

Every job, on **three separate attempts**:

```text
runner_name    ''      no runner was ever assigned
steps          0       not one step ran, not even actions/checkout
job logs       HTTP 404 BlobNotFound -- no log was ever produced
check-run      title None, summary empty, no annotations
```

**This cannot be a defect in the pushed tree.** A source, test, formatting or platform-specific
failure requires a runner to start, check out the code and run a step. Nothing ran.

What was ruled out:

| hypothesis | finding |
| --- | --- |
| exhausted Actions minutes | the repository is **public** — GitHub-hosted minutes are free and unmetered |
| Actions disabled | repo permissions: `enabled=true`, `allowed_actions=all` |
| GitHub platform incident | githubstatus.com: All Systems Operational, Actions component operational |
| bad workflow YAML | `.github/workflows/ci.yml` is unchanged by this lineage, and the run built the correct 17-job graph, so it parsed |
| transient | three attempts, including one after a deliberate three-minute pause, all identical |

The billing/usage API needs a `user` OAuth scope this session does not have and did not add.
Resolving this needs the account's own Actions usage view or GitHub support.

## 8. What was NOT done, and why

* **`KNOWN-GOOD` was not updated.** It still records `548401a` / CI 35385979133, the last revision
  whose CI actually passed. Pointing it at `3bd32f9` would assert a qualification that does not
  exist.
* **No Phase 10 metadata commit was made.** Its whole content is the CI result, and there is none.
* **The closeout is not declared complete.** The final acceptance gate requires "CI has zero
  unexpected failures".
* **No test, threshold or CI configuration was weakened** to get past this, and no workflow was
  edited.
* **P13 was not started.**

`TODO.md` records the true state: the lineage is FINISHED and pushed, `KNOWN-GOOD` is the last
CI-green revision, and a `CI BLOCKER` block names run 35579942600.
