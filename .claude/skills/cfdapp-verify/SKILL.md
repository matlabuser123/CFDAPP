---
name: cfdapp-verify
description: The CFDApp verification ladder. Selects which levels L1-L12 a change must pass based on the files it touched, gives the exact build/test commands, and defines how counts and build identity are recorded. Use whenever verifying any CFDApp change.
---

# CFDApp verification ladder

The central verification skill. `CLAUDE.md` §7 and §8 are authoritative for the rules; this is the
executable form.

## The ladder

| Level | What | Cost |
| --- | --- | --- |
| L1 | configure + clean build | minutes |
| L2 | focused tests for the changed code | seconds–minutes |
| L3 | subsystem tests | minutes |
| L4 | frozen numerical acceptance gates | varies |
| L5 | CPU/GPU equivalence | minutes |
| L6 | full Release regression | ~15 min local |
| L7 | Debug + GUI regression | ~15 min local |
| L8 | Clang regression | ~15 min local |
| L9 | sanitizer regression at CI settings | hours |
| L10 | CUDA diagnostics (compute-sanitizer, counters) | minutes |
| L11 | clang-format + whitespace | seconds |
| L12 | evidence audit | minutes |

## Selecting levels from the diff

**Which selector, when.** The gate must be frozen *before* any production change
(`CLAUDE.md` §5.1), so at freeze time there is no diff to read. `cfdapp-blast-radius`'s
`VERIFICATION EARNED` field selects the ladder **up front**, from the predicted reach. The table
below is then the **post-hoc check**: after implementing, confirm the diff touched nothing outside
that prediction. If it did, the blast radius was wrong — re-derive it and raise the ladder; never
lower the frozen one to match.

Run `git status --short` and `git diff --stat`, then take the **union** of every row that matches:

| Changed path | Required levels |
| --- | --- |
| `*.md`, `docs/` only | L11 (doc form), L12 (doc form) |
| `.claude/`, agent infrastructure | L11 (doc form), L12 (doc form) |
| **`results/**`** | **STOP — this is the evidence corpus, not documentation.** See below. |
| `tests/**` | L1, L2, L3, L11 |
| `python/**` | pytest + ruff + compileall, L11 |
| `.github/workflows/**` | L11 + proof on a real CI run |
| `include/**`, `src/**` | L1, L2, L3, L6, L7, L8, L9, L11, L12 |
| discretization, gradients, fluxes, boundary treatment, solver criteria | **+ L4** |
| `cuda/**` only (no `src/`, no `include/`) | L1, L2, L3, L5, L10, L11, L12 — CPU translation units are unaffected, so L6–L9 are not earned; **prove it** by rebuilding the CPU trees as no-ops |
| `include/cfd/gpu/**` | the `include/**` row **+ L5, L10** — a header reaches both `cfdcuda` and the CPU stubs in `src/gpu/` |
| anything with a performance claim | **+ baseline before, measurement after** |
| case format, parser, schema, export | + backward-compatibility check (`CLAUDE.md` §9) |

Do not run the expensive tail (L6–L9) for a Markdown-only change. Do not skip it for a
three-line change to a shared numerical operator.

### `results/` is not documentation

It looks like Markdown; it is the evidence corpus. `CLAUDE.md` §4 and §10: superseded results are
marked `INVALID AS AUTHORITATIVE`, **never deleted**; amendments are separate files; release
evidence is immutable. Editing a frozen `acceptance_gate*.md`, a log or a recorded number is
rewriting history, not fixing a document.

**Appending** new evidence for the phase you are authorized to work on is normal. **Modifying or
deleting** existing evidence needs explicit authorization for that act — stop and ask. A recorded
number that looks wrong is a finding to report, not a typo to correct.

### Editing prose that contains evidence

`README.md`, `ROADMAP.md` and the release notes cite measured results — speed-ups, tolerances,
grid sizes, SHAs, phase IDs, `results/` references. Before changing any such string, trace it to
the log that produced it. **If it disagrees with `results/`, that is a stop condition, not a typo.**
Never "correct" a number toward what you expect it to be.

## Commands

Builds and tests run in **WSL2** (Ubuntu 22.04) from the repo root. This session may be on Windows;
if so, enter WSL rather than building natively — native Windows is a *different environment* whose
evidence does not substitute (`CLAUDE.md` §4):

```bash
wsl.exe -d Ubuntu-22.04 -- bash -lc 'cd /mnt/c/Users/<user>/Desktop/CFDAPP/CFDApp && <command>'
```

Compute evidence hashes **inside WSL** — Git Bash's `sha256sum`/`sort` have produced different
digests for the same tree. Take timings from gtest's own `(N ms)`; the WSL clock jumps.

```bash
# L1 — configure + build.  16-24 jobs, never all 32 (CLAUDE.md hardware policy).
cmake --preset debug   && cmake --build --preset debug   -- -j20
cmake --preset release && cmake --build --preset release -- -j20
cmake --preset asan    && cmake --build --preset asan    -- -j20

# L2/L3 — focused, by test name, target or tier label
ctest --preset debug -R '<TestTargetRegex>' --output-on-failure
ctest --preset debug -L unit          # tiers: unit|numerical|solver|integration|validation
./build/debug/tests/<tier>/<area>/CFD<Area>Tests --gtest_filter='<Suite>.*'   # quote the filter

# L6/L7/L8 — full regression, start at -j16
ctest --preset release -j16 --output-on-failure
ctest --preset debug   -j16 --output-on-failure
CC=clang CXX=clang++ cmake --preset debug && ctest --preset debug -j16 --output-on-failure

# L9 — sanitizers at CI settings (CLAUDE.md §7)
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
  ctest --preset asan -j8 --timeout 7200 --output-on-failure

# heavy DISABLED_ validation cases (excluded from a normal ctest run; run from the repo root)
./build/release/tests/integration/<area>/<Target> --gtest_also_run_disabled_tests \
  --gtest_filter='<name>'
ctest --preset release -L validation          # select a tier on demand, not the DISABLED_ split

# Performance baseline / measurement — the mandatory step for any performance claim.
# Release, idle machine, warm-up then repeats; output lands in results/performance/.
# CFDAPP_BUILD_BENCHMARKS is ON by default; CFDAPP_ENABLE_CUDA is OFF, so the GPU
# benchmarks exist only in a CUDA-enabled tree (see cfdapp-cuda).
cmake --build --preset release -- -j20
./build/release/benchmarks/cpu/cfd_benchmark_cpu
./build/cuda/benchmarks/gpu/cfd_benchmark_cuda_end_to_end     # the GPU size ladder
./build/cuda/benchmarks/gpu/cfd_benchmark_large_grid_stress

# L11 — formatting.  This is CI's format step verbatim (.github/workflows/ci.yml); note it
# includes *.h, which a '*.[ch]pp' glob would silently miss.
find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 |
  xargs -0 clang-format-18 --dry-run --Werror
git diff --check
```

`cuda/` is **outside** CI's clang-format scope and carries known pre-existing violations. Report
its count as unchanged; do not reformat it inside another phase.

### L11 and L12 in "doc form"

For a diff that touches no C++, the clang-format command above matches **zero changed files**. It
will pass, and that pass proves nothing — it is vacuous, and recording it as "L11 PASS" is the
exact failure `cfdapp-review` attacks. For a documentation-only diff these two levels mean:

* **L11 (doc form)** — `git diff --check`, plus a **line-ending check**: this repo is LF and
  `.gitattributes` is `* -text`, so bytes are stored verbatim and a Windows-side edit can silently
  rewrite a whole file to CRLF, changing its hash and producing a whole-file diff for a
  one-character fix. Verify with `git ls-files --eol <file>` after editing.
* **L12 (doc form)** — the `results/<phase>/` completeness audit in `cfdapp-closeout` §1 does not
  apply when there is no phase. Instead: `git status --short` shows only the intended files and has
  not disturbed another session's in-flight work; `git diff --stat` is the expected size (a
  one-character fix is not a 400-line diff); no generated noise or build artifacts; and **no claim
  was introduced or altered that is not traceable to `results/`**.

## Regression integrity (non-negotiable)

A regression result is authoritative **only** if the test binary was demonstrably rebuilt against
the exact source and library under test (`CLAUDE.md` §8). The sequence is:

1. configure;
2. clean-first build of **every** target;
3. verify every test binary is newer than the newest production source;
4. record library and test-binary sha256;
5. run and record exact counts.

Fail closed at any step. A harness that rebuilds only *missing* binaries is invalid. Never report
stale-binary results as current evidence.

## What to record

Never write "all tests passed" when exact numbers exist. Record, per configuration:

```text
configuration:      <preset, compiler, build type>
build identity:     <library sha256, test-binary sha256 or mtime proof>
tests run:          N
passed:             N
failed:             N   (name each)
disabled:           N
skipped:            N
runtime:            <from gtest's own (N ms), not the wall clock — WSL's clock jumps>
diagnostics:        <sanitizer / compute-sanitizer findings, or 0 errors per tool>
```

Distinguish **executed** from **disabled** in every count. "1920 identical" is wrong when 45 of
them were disabled; write "1875 executed + 45 disabled".

## Generated-file noise

Every full `ctest` run rewrites tracked `results/validation/**` reports. **Classify before
restoring** — these files are both regeneration noise and tracked evidence, and the two cases have
opposite correct actions:

* **runtime-only differences** (timings, durations) → noise. Restore; never commit.
* **value-differing** on a change that was *not* meant to alter numbers → a regression. **Stop
  and report**; do not restore it away.
* **value-differing** on an authorized numerics change that *is* expected to alter them → the
  updated report is the new evidence. Keep it, and account for every changed value in `summary.md`.

**Capture the filenames before restoring anything** — losing them has been a disclosed evidence gap
twice. Record the count and the class of each (`CLAUDE.md` §7).

## Known failures

A known failure stays "known" only while it reproduces its recorded values **exactly**. Any new or
changed failure is a regression — stop and report it.
