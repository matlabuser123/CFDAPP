# P1 — Quality Gate: evidence record

Run 2026-09-08, WSL Ubuntu-22.04 (GCC 11.4.0 / Clang 14.0.0, CMake 3.22.1,
Ninja 1.10.1, Python 3.11.0rc1), against a repository initialized fresh for
this pass (see "Git" below). Every case/test run in this record was executed
after the fixes noted under "Fixes applied", not before.

```text
Debug build (GCC):    PASS -- 0 warnings
Release build (GCC):  PASS -- 0 warnings
Debug build (Clang):  not run as a full preset build this pass; see CI's
                       ubuntu-clang-debug matrix leg for that coverage

CTest (debug):    369/371 PASS, 2 FAILED, 7 DISABLED (documented below)
CTest (release):  369/371 PASS, 2 FAILED, 7 DISABLED (same 2/7)

ASan+UBSan (debug, GCC): 369/371 PASS, 2 FAILED (same 2 as above)
                         0 AddressSanitizer reports
                         0 UndefinedBehaviorSanitizer reports

clang-format --dry-run --Werror:  PASS (0 violations; 2 files reformatted
                                   in this pass, see "Fixes applied")
clang-tidy (bugprone-*/performance-*/modernize-*/+selected
            cppcoreguidelines-*/readability-*, include/ + src/ only): PASS
                                   (0 findings)

Python:
  pytest python/tests:          73 passed, 1 skipped
  compileall python/cfdapp:     PASS
  ruff check python:            PASS (0 violations)
  ruff format --check python:   PASS (0 violations)

Cavity (cases/lid_driven_cavity, 20x20, Re=100):
  Converged:        yes (3036 iterations)
  Finite:            yes (NaN/Inf: no)
  Mass imbalance:    0.0
  Deterministic:     yes -- fields.csv/residuals.csv/solution.vtk/
                     metadata.json sha256-identical across 3 independent runs

Poiseuille (cases/poiseuille_flow, 64x8, Re=10):
  Converged:        yes (1319 iterations)
  Finite:            yes (NaN/Inf: no)
  Mass imbalance:    1.9628e-07
  Deterministic:     yes -- same 4 files sha256-identical across 3 independent
                     runs

Git:      clean (see "Git" below for what "baseline" means here)
CI:       .github/workflows/ci.yml added (config-only, not pushed/run --
          see "CI" below for expected first-run result)
```

---

## Known gap: 2 pre-existing GridRefinementTest failures

`GridRefinementTest.LaplacianOfSmoothFieldConvergesAtSecondOrder` and
`GridRefinementTest.UpwindConvectionConvergesAtFirstOrder` fail under every
configuration exercised in this pass (debug, release, ASan+UBSan) — this is
**not new**: both were already failing and already root-caused/documented in
[TODO.md](TODO.md) (P0 — Finite Volume Operators, "Status (2026-09-08)")
before this quality-gate pass began, and neither failure produces a
sanitizer report (confirmed above) — they are `EXPECT_GT` assertions on
observed convergence order, not memory/UB defects.

* **Laplacian**: observed order plateaus at ~1.5 instead of >1.7. Root
  cause per TODO.md: the boundary flux's 3-point one-sided derivative
  estimate is exact for the *gradient* at a boundary face, but a genuinely
  second-order *Laplacian* (a second derivative) at a boundary-adjacent
  cell needs a 4-point stencil when the spacing to the boundary (h1) and to
  the far interior neighbor (h2) differ, as they do here (h1 = h2/2 on a
  uniform grid) — confirmed by Taylor expansion in TODO.md. Fixing this
  means extending `Diffusion.cpp`'s boundary treatment to a 4-point
  stencil, a larger change than this gate pass is scoped for.
* **Upwind convection**: observed order ~0.55-0.63 instead of >0.8, not yet
  root-caused (TODO.md marks it "deferred").

Per this gate's own section 43 ("do not mask failures... fix or explicitly
document the underlying defect"), the tests are left failing rather than
loosened, disabled, or filtered out of CI — `ctest`, `ctest --preset asan`,
and the CI `build-test` job all surface them honestly. **This means the
gate's own "100% tests passed" criterion (section 2) is not met by this
pass**, and P1 should not be marked fully closed until either the
discretization fix lands or the project explicitly accepts this as a known,
documented limitation.

---

## Fixes applied during this pass

* **10 `-Wshadow` warnings** in `src/io/case/JsonUtil.cpp` /
  `JsonUtil.hpp`: every parsing-helper parameter named `json` shadowed the
  file-scope `using nlohmann::json;` alias. Renamed every such parameter to
  `node` (both declarations and definitions) rather than removing the
  `using` alias, since it is still used for its intended purpose in
  `readJsonFile`. Debug and release now build with 0 warnings.
* **2 `clang-format` violations** (`tests/unit/discretization/
  test_divergence.cpp`, `tests/unit/physics/test_continuity_equation.cpp`):
  reformatted in place with `clang-format -i`.
* **CTest labels** (section 39): every test target now carries exactly one
  tier label (`unit`, `numerical`, `solver`, `integration`, or
  `validation`) via `gtest_discover_tests(... PROPERTIES LABELS "<tier>")`;
  see the convention comment in `cmake/Testing.cmake` for why each target
  gets exactly one label rather than a list.
* **`.github/workflows/ci.yml`** added: `build-test` (Ubuntu+GCC+Debug,
  Ubuntu+GCC+Release, Ubuntu+Clang+Debug), `format`, `clang-tidy`,
  `sanitizers`, `python` as separate jobs (sections 30-37). Config-only —
  not pushed to a remote or run this pass (no GitHub remote exists yet for
  this repository).
* Two stray IDE/tool scratch files at the repo root
  (`tidy_output.txt`, `cmake_test_discovery_*.json`) were excluded from the
  baseline commit and added to `.gitignore` rather than committed.

## Git

This repository had no `.git` before this pass. A baseline commit was made
of the existing tree (source, tests, cases, and pre-existing validation
evidence under `results/` and `validation/`) before any of the fixes above,
so "clean working tree" here means clean *relative to that new baseline* —
it is not evidence of a stable history predating this session. `git status
--short` is empty and `git diff --check` reports no whitespace errors as of
the final commit of this pass.

## CI

`.github/workflows/ci.yml` was authored and reviewed but deliberately not
pushed or executed (no GitHub remote exists for this repository yet). Its
expected first real run: every job green **except** `build-test`, which
will fail on the same 2 known `GridRefinementTest` cases documented above,
in every one of its three matrix legs.
