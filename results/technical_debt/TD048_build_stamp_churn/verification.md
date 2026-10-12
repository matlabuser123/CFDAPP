# TD-048 — RESOLVED. The build stamps are pinned: all seven criteria pass, with no production change.

```text
STATUS:      **RESOLVED.** The churn is fixed at its two sources, not reverted after the fact.
               C1 cases/heated_cavity byte-identical after the run   PASS  (git status EMPTY)
               C2 it stays TRACKED, 4 files                          PASS
               C3 the 3 fixture results/ untracked AND ignored       PASS  (check-ignore: IGNORED)
               C4 visualization suite / app-layer suite              PASS  11/11, 36/36
               C5 the CMake CLI smoke tests                          PASS  27/27
               C6 the working tree is clean of all 4 stamp changes   PASS
               C7 NO production file changed                         PASS  JSONWriter.cpp hash
                                                                           unchanged
GATE:        GATE.md, frozen `16fffa38ece3891eb22c74cd9fa4a7d3c7ededc602b9424d91ee0d6ef7a87042`
             BEFORE any edit (logs/00_freeze.log).
SCOPE:       one test call site, one .gitignore rule, 12 files dropped from the index. No
             production change: `build_identity` is still written, because it is deliberate
             provenance (P13-004) and a real user's run needs it.
EVIDENCE:    logs/00_freeze.log · logs/01_verification.log
COMMIT:      carried as evidence.
```

---

## 1. C1 IS A DEMONSTRATION, NOT AN ARGUMENT

The decisive check is not "the code looks right" — it is running the test that used to cause the
churn and watching the tracked case stay put:

```text
build/release/tests/unit/app/CFDAppLayerTests --gtest_filter='*Snapshot*'
  11 tests from 3 test suites ran. 11 PASSED.

git status --porcelain cases/heated_cavity
  (EMPTY)
```

Before the fix that same run rewrote `cases/heated_cavity/results/metadata.json` and moved
`build_identity` from `458457542a2e-modified` to `b4b206f96b8d-modified`.

## 2. THE TWO SOURCES, FIXED DIFFERENTLY BECAUSE THEY ARE DIFFERENT

### 2.1 `cases/heated_cavity` — a deliverable, so the TEST moved, not the file

`tests/unit/app/test_visualization_snapshot.cpp` ran `ProjectRunner::run("cases/heated_cavity")`
against the tracked path and then read its `results/` back. The file **already used
`CaseFixtureCopy` 11 times**, including at `:120` for another `cases/` production case, and
`apps/gui/tests/test_cli_gui_equivalence.cpp:345` already wrapped this very case. So the site was
an inconsistency inside code that knew the idiom.

```cpp
const CaseFixtureCopy fixture("cases/heated_cavity");
const auto run = ProjectRunner::run(fixture.path());
...
const auto reloaded = loadSnapshotFromResults(fixture.path() / "results");
```

`CaseFixtureCopy.hpp`'s own header states the rule this restores: *"The fix is not 'figure out
exactly which access pattern is safe to leave shared' -- it is 'make every test's own copy of the
fixture private', full stop."*

`MeshQualityCampaign.cpp:180` only READS the case through `CaseReader` and writes nothing, so it
needed no change — checked rather than assumed.

### 2.2 The three fixtures — byproducts, so the INDEX moved, not the test

`tests/CMakeLists.txt:21-28` records that a raw ctest `add_test` runs a real `cfdapp` process and
therefore *"cannot construct a tests/support/CaseFixtureCopy the way every gtest-based consumer
does"*; its answer was a permanent private fixture directory, which closed a real parallel-ctest
race. That directory is git-tracked, so the CLI writing `results/` into it still churned.

```text
measured: ZERO files in tests/, apps/ or src/ reference these three results/ directories
history:  first committed in 3d26930 -- the commit that CREATED the fixture dirs -- so they were
          swept in, not curated; 9dc2b50 is literally "chore: regenerated ... committed-case
          outputs"
```

So they were untracked with `git rm -r --cached` (12 files) plus a `.gitignore` rule. **`--cached`
matters: the files stay ON DISK, so the CLI tests keep running unchanged** — verified by C5, 27/27.

**The alternative was rejected deliberately**, and is recorded in GATE.md §2.2: wrapping ~10
`add_test` calls in copy-then-run scripts would also redirect the writes, but that is build-system
surgery against a design the repository chose on purpose, to stop churn in files nothing reads.

## 3. WHAT WAS NOT DONE, AND WHY IT WOULD HAVE BEEN WRONG

```text
NOT removed     build_identity from JSONWriter.cpp:150. It is deliberate provenance (P13-004);
                a real user's run needs to know which build produced a result. Deleting it to
                quiet a test would be fixing the wrong thing, and C7 asserts the file is
                untouched by hash.
NOT untracked   cases/heated_cavity/results/. It is a production-case deliverable and §9 wants
                committed cases byte-identical. C2 asserts it is still tracked with 4 files, so
                this gate cannot have quietly deleted the thing it was meant to stabilise.
NOT reverted    merely restored. A `git checkout --` on the four files would have been undone by
                the next regression; both sources are fixed so the churn cannot recur.
```

## 4. VERIFICATION LADDER

```text
§7 clang-format   tests/unit/app/test_visualization_snapshot.cpp  OK
build             CFDAppLayerTests + cfdapp, rc=0
focused           *Snapshot*            11/11
target regression CFDAppLayerTests      36/36
CLI smoke         ctest -R CFDAppCli    27/27, 11.88 s
ignore check      git check-ignore      all three results/ IGNORED
production        git status src/ include/ apps/ cuda/  EMPTY
```

## 5. WHAT IS NOT ESTABLISHED

```text
NOT established  that no OTHER tracked file can churn. These four are the ones that DID churn,
                 measured over three full regressions (Release, Debug+GUI, ASan). A systematic
                 search for other candidates was not done.
NOT established  that the untracked outputs were worthless. They are unread TODAY (measured);
                 if some future check wants a golden CLI output, it must add one deliberately
                 rather than rely on a byproduct that happened to be committed.
NOT re-run       the full §8 ladder after this change. It is test-infrastructure only, verified
                 on the two suites that touch these paths plus all 27 CLI smoke tests, but
                 §11.4 is not re-discharged by this item.
NOT addressed    TD-044 (a CLI test leaking its working directory into the repo root). It is the
                 same family -- tests writing where they should not -- and is still open.
```
