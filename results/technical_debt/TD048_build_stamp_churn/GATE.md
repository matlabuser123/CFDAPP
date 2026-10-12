# TD-048 — pin the build stamps. Frozen gate.

```text
PROBLEM      every full regression rewrites `build_identity` in four tracked case outputs
             ("458457542a2e-modified" -> "b4b206f96b8d-modified"), because the SHA is embedded at
             configure time via Version.hpp and written by JSONWriter.cpp:150. Section 7's licence
             covers TIMING-only noise, so this churn is outside it, and section 9 requires committed
             case outputs to stay byte-identical or be explained. It has been handled by
             "regeneration chores" (9dc2b50) rather than fixed.
SCOPE        tests and .gitignore ONLY. **No production change.** JSONWriter keeps writing
             build_identity -- it is deliberate provenance (P13-004) and real runs need it.
FROZEN       before any edit.
```

---

## 1. THE FOUR SPLIT INTO TWO KINDS, and they need different fixes

```text
cases/heated_cavity/results/                      a PRODUCTION case deliverable under cases/.
                                                  Section 9 wants it tracked and byte-identical.
tests/data/cases/does_not_converge/results/       TEST FIXTURE byproducts. First committed in
tests/data/cases/mesh_quality_warning_cli/...     3d26930 -- the commit that CREATED the fixture
tests/data/cases/valid_cavity_cli_smoke/...       directories -- so they were swept in, not curated.
```

**Measured: the three fixture `results/` directories are referenced by ZERO files** in `tests/`,
`apps/`, `src/` (any of .cpp/.hpp/.txt/.py). Nothing reads them as golden data; they are pure output.

## 2. THE TWO FIXES

### 2.1 `cases/heated_cavity` — one test writes the tracked path; use the repo's own idiom

```text
tests/unit/app/test_visualization_snapshot.cpp:95
    const auto run = ProjectRunner::run("cases/heated_cavity");          <- TRACKED path
tests/unit/app/test_visualization_snapshot.cpp:99
    loadSnapshotFromResults(std::filesystem::path("cases/heated_cavity") / "results");
```

That file **already uses `CaseFixtureCopy` 11 times**, and
`apps/gui/tests/test_cli_gui_equivalence.cpp:345` already does
`CaseFixtureCopy fixture("cases/heated_cavity")`. So this single call site is an inconsistency
within code that knows the idiom, not a missing capability. `MeshQualityCampaign.cpp:180` only
READS via `CaseReader` and writes nothing.

`CaseFixtureCopy`'s own header states the rule this restores: *"The fix is not 'figure out exactly
which access pattern is safe to leave shared' -- it is 'make every test's own copy of the fixture
private', full stop."*

### 2.2 The three fixtures — untrack the byproducts

A raw ctest `add_test` runs a real `cfdapp` process, and `tests/CMakeLists.txt:21-28` records that
it *"cannot construct a tests/support/CaseFixtureCopy the way every gtest-based consumer does"*.
That design gave each CLI test a **permanent private fixture directory**, which closed the race --
but the directory is git-tracked, so the CLI writing `results/` into it still churns.

Untracking the generated `results/` subtrees pins them permanently, needs no CMake surgery, and
cannot break a reader because there are none (section 1).

**The alternative was rejected deliberately:** wrapping ~10 `add_test` calls in copy-then-run
scripts would redirect the writes, but that is build-system surgery against a design the
repository chose on purpose, to fix churn in files nothing reads.

## 3. CRITERIA — frozen, stop at the first failure

```text
C1  after the fix, running the affected tests leaves `cases/heated_cavity/results/` byte-identical
    -- `git status --porcelain cases/heated_cavity` is EMPTY.
C2  `cases/heated_cavity/results/` stays TRACKED: 4 files, unchanged count. Section 9 keeps the
    deliverable; this gate must not quietly delete it.
C3  the three fixture `results/` subtrees are untracked (12 files removed from the index) and
    ignored, so a later run cannot re-dirty them.
C4  the tests that touch these paths still pass: the visualization-snapshot suite and the
    app-layer suite.
C5  the CMake CLI smoke tests still pass -- they now write into ignored directories, which must
    not change their verdicts.
C6  the working tree ends CLEAN of all four build-stamp modifications.
C7  NO production file changes. JSONWriter.cpp:150 keeps writing build_identity.
```

## 4. WHAT THIS DOES NOT DO

```text
NOT removing   build_identity from the JSON. It is deliberate provenance and a real user's run
               needs it; removing it to quiet a test would be fixing the wrong thing.
NOT untracking cases/heated_cavity/results/. That is a deliverable under section 9.
NOT touching   the CLI tests' fixture design, which closed a real parallel-ctest race.
NOT claiming   that no other tracked file can churn. This pins the four that DID, measured over
               three full regressions; others may exist and are not searched for here.
```
