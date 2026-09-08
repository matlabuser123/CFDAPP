# P1 — Quality Gate: evidence record

Run 2026-09-08, WSL Ubuntu-22.04 (GCC 11.4.0 / Clang 14.0.0, CMake 3.22.1,
Ninja 1.10.1, Python 3.11.0rc1), against a repository initialized fresh for
this pass (see "Git" below). Every case/test run in this record was executed
after the fixes noted under "Fixes applied", not before -- including a
second pass that fixed the Laplacian order-of-accuracy failure originally
left open (see "Follow-up: Laplacian order-of-accuracy fix" below).

```text
Debug build (GCC):    PASS -- 0 warnings
Release build (GCC):  PASS -- 0 warnings
Debug build (Clang):  not run as a full preset build this pass; see CI's
                       ubuntu-clang-debug matrix leg for that coverage

CTest (debug):    370/371 PASS, 1 FAILED, 7 DISABLED (documented below)
CTest (release):  370/371 PASS, 1 FAILED, 7 DISABLED (same 1/7)

ASan+UBSan (debug, GCC): 370/371 PASS, 1 FAILED (same 1 as above)
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

## Remaining gap: 1 pre-existing GridRefinementTest failure

`GridRefinementTest.UpwindConvectionConvergesAtFirstOrder` fails under every
configuration exercised in this pass (debug, release, ASan+UBSan) — this is
**not new**: it was already failing and already documented in
[TODO.md](TODO.md) (P0 — Finite Volume Operators) before this quality-gate
pass began, and it does not produce a sanitizer report (confirmed above) —
it is an `EXPECT_GT` assertion on observed convergence order, not a
memory/UB defect.

* **Upwind convection**: observed order ~0.55–0.63 instead of >0.8, not yet
  root-caused (TODO.md marks it "deferred"). Fixing this was explicitly left
  out of scope for this pass (see "Follow-up" below for what *was* fixed).

Per this gate's own section 43 ("do not mask failures... fix or explicitly
document the underlying defect"), the test is left failing rather than
loosened, disabled, or filtered out of CI — `ctest`, `ctest --preset asan`,
and the CI `build-test` job all surface it honestly. **This means the
gate's own "100% tests passed" criterion (section 2) is not met by this
pass**, and P1 should not be marked fully closed until either the
discretization fix lands or the project explicitly accepts this as a known,
documented limitation.

---

## Follow-up: Laplacian order-of-accuracy fix

After the first pass of this gate (which left both grid-refinement failures
open, documented below the line for historical reference), a second pass
fixed `GridRefinementTest.LaplacianOfSmoothFieldConvergesAtSecondOrder`.

**Root cause** (already identified in TODO.md before this fix): at a
boundary-adjacent cell, the boundary face sits half a cell short of a full
interior spacing (h1 = h2/2 on a uniform grid). The existing 3-point
one-sided formula there is second-order accurate for the *gradient* at the
boundary face itself, but combining it with the (unchanged) 2-point central
difference at the opposite interior face to get the *Laplacian* (a second
derivative) is mathematically capped at first order whenever the two
spacings differ, regardless of how accurate each individual flux is.

**Fix** (`src/discretization/Diffusion.cpp`): reach one cell further (a
second interior neighbor, "N2", found via a new `nextInteriorFaceAwayFrom`
helper) to get 4 points total (boundary, owner, N1, N2), fit a cubic
through them via Newton divided differences, and take *its* second
derivative at the owner cell directly — exact for the true second
derivative up to O(h²), and exact with zero error whenever phi is itself
cubic or lower (so it also preserves/generalizes the existing quadratic-
exactness tests). That value is then solved back algebraically for what
the boundary face's *own* flux would have to be, given the interior face's
flux is left untouched — so the interior face's flux (shared with N1's own
sum, sign-flipped) is unaffected, and `DiffusionTest.
ZeroFluxBoundaryConservesGlobally`'s pairwise conservation still holds.

**A real bug found and fixed along the way**: the first attempt at this fix
produced huge, refinement-*growing* errors (order ≈ −0.5) rather than an
improvement. Cause: the new derivation naturally works in a local "into the
domain" coordinate (increasing away from the boundary), but this function's
established contract — confirmed by hand-checking the pre-existing 3-point
formula against `phi=x` at a west boundary, which returns `-1` (not `+1`) —
is to return `dphi/dn` in the *outward-normal* sense. The second derivative
term is unaffected (sign-invariant under reflection), but the final
first-derivative-like term needed an explicit negation, which was missing
on the first attempt. Added, with a hand-derivation comment in the source
so the reasoning doesn't need re-deriving again.

**Result**: observed order now climbs 1.76 → 1.90 → 1.95 with refinement
(grids 8×8 → 16×16 → 32×32 → 64×64), comfortably clearing the test's `>1.7`
threshold at every step, versus the previous plateau at ~1.5. All 4
`DiffusionTest`/3 `LaplacianTest` analytical-exactness cases still pass
(including the quadratic-exactness ones, floating-point exact). Full debug
and release CTest, and ASan+UBSan, all rerun clean afterward (370/371,
same lone convection failure). `clang-format`/`clang-tidy` both still clean
on the changed file.

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
* **Laplacian boundary order-of-accuracy fix** — see "Follow-up" above.

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
will fail on the one remaining known `GridRefinementTest` case documented
above, in every one of its three matrix legs.
