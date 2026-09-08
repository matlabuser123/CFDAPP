# P1 — Quality Gate: evidence record

Run 2026-09-08, WSL Ubuntu-22.04 (GCC 11.4.0 / Clang 14.0.0, CMake 3.22.1,
Ninja 1.10.1, Python 3.11.0rc1), against a repository initialized fresh for
this pass (see "Git" below). This is the final record, after two follow-up
discretization fixes (Laplacian, then upwind convection) that closed both
`GridRefinementTest` failures this gate started with -- CTest is genuinely
371/371 (100%) in every configuration below.

```text
Debug build (GCC):    PASS -- 0 warnings (fresh, from an empty build/debug)
Release build (GCC):  PASS -- 0 warnings (fresh, from an empty build/release)
Debug build (Clang):  not run as a full preset build this pass; see CI's
                       ubuntu-clang-debug matrix leg for that coverage

CTest (debug):    371/371 PASS, 0 FAILED, 7 DISABLED (documented below)
CTest (release):  371/371 PASS, 0 FAILED, 7 DISABLED (same 7)

ASan+UBSan (debug, GCC): 371/371 PASS, 0 FAILED
                         0 AddressSanitizer reports
                         0 UndefinedBehaviorSanitizer reports

clang-format --dry-run --Werror:  PASS (0 violations, repo-wide)
clang-tidy (bugprone-*/performance-*/modernize-*/+selected
            cppcoreguidelines-*/readability-*, include/ + src/ only): PASS
                                   (0 findings, repo-wide)

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
                     metadata.json sha256-identical across repeated runs

Poiseuille (cases/poiseuille_flow, 64x8, Re=10):
  Converged:        yes (1319 iterations)
  Finite:            yes (NaN/Inf: no)
  Mass imbalance:    1.9628e-07
  Deterministic:     yes -- same 4 files sha256-identical across repeated
                     runs

solution.vtk:     structurally verified (legacy ASCII UNSTRUCTURED_GRID,
                   441 points / 400 cells matching the 20x20 mesh, no NaN/
                   Inf, CELL_DATA in the documented pressure/velocity/
                   velocity_magnitude order). Genuine visual inspection in
                   ParaView is a separate, already-documented gap needing a
                   human with ParaView installed (no GUI/ParaView access in
                   this environment) -- see TODO.md "P1 -- Result Export".

Git:      clean (see "Git" below for what "baseline" means here)
CI:       .github/workflows/ci.yml added (config-only, not pushed/run --
          see "CI" below for expected first-run result)
```

---

## Both GridRefinementTest failures fixed

This gate started with 2 known, pre-existing failures (documented in
TODO.md before this pass began). Both are now fixed, via two independent
follow-up passes:

### 1. Laplacian order-of-accuracy

`GridRefinementTest.LaplacianOfSmoothFieldConvergesAtSecondOrder` --
observed order was capped at ~1.5 instead of >1.7.

**Root cause**: at a boundary-adjacent cell, the boundary face sits half a
cell short of a full interior spacing (h1 = h2/2 on a uniform grid). The
existing 3-point one-sided formula there is second-order accurate for the
*gradient* at the boundary face itself, but combining it with the
(unchanged) 2-point central difference at the opposite interior face to
get the *Laplacian* (a second derivative) is mathematically capped at
first order whenever the two spacings differ, regardless of how accurate
each individual flux is.

**Fix** (`src/discretization/Diffusion.cpp`): reach one cell further (a
second interior neighbor, "N2", found via a new `nextInteriorFaceAwayFrom`
helper) to get 4 points total (boundary, owner, N1, N2), fit a cubic
through them via Newton divided differences, and take *its* second
derivative at the owner cell directly -- exact for the true second
derivative up to O(h²), and exact with zero error whenever phi is itself
cubic or lower (so it also preserves/generalizes the existing quadratic-
exactness tests). That value is then solved back algebraically for what
the boundary face's *own* flux would have to be, given the interior face's
flux is left untouched -- so the interior face's flux (shared with N1's own
sum, sign-flipped) is unaffected, and `DiffusionTest.
ZeroFluxBoundaryConservesGlobally`'s pairwise conservation still holds.

A real bug was found and fixed along the way: the first attempt at this
produced huge, refinement-*growing* errors (order ≈ −0.5). Cause: the new
derivation naturally works in a local "into the domain" coordinate, but
this function's established contract is to return `dphi/dn` in the
*outward-normal* sense (confirmed by hand against the pre-existing 3-point
formula for phi=x). The second-derivative term is unaffected (sign-
invariant under reflection), but the final first-derivative-like term
needed an explicit negation, which was missing on the first attempt.

**Result**: observed order now climbs 1.76 → 1.90 → 1.95 with refinement.

### 2. Upwind convection order-of-accuracy

`GridRefinementTest.UpwindConvectionConvergesAtFirstOrder` -- observed
order was drifting toward ~0.5 instead of settling near 1.0.

**Root-caused by instrumentation**, not guesswork: a temporary diagnostic
(added to, then removed from, `test_grid_refinement.cpp`) split the L2
error by which boundary (if any) each cell touches. Interior, outflow-
boundary, and tangential-boundary cells were all already converging at
order ≈1 -- only **inflow-boundary-adjacent** cells had an error that did
not shrink under refinement at all (a genuine O(1) bias), and their
growing relative weight in the global L2 norm as the grid refined is
exactly what pulled the *overall* observed order down toward 0.5 (worked
out quantitatively via a population-weighted-norm argument, then confirmed
by the instrumented breakdown).

**Root cause**: every other upwind face (interior, or outflow-boundary)
feeds the scheme an upstream value one full owner-to-neighbor spacing
away. An inflow face's boundary condition, though, is known exactly *at
the face* -- zero offset, not the matching offset every other face has.
Differenced against the owner value and divided by the full cell width (as
the flux-sum/volume formula does uniformly for every face), that asymmetry
converges to *half* the true gradient at inflow-adjacent cells, not the
true value -- confirmed by hand via Taylor expansion, and the discrepancy
measured was exactly the predicted factor of 2.

**Fix** (`src/discretization/Convection.cpp`): mirror the owner value
through the exactly-known boundary value to produce a "ghost" value the
same distance past the boundary as the owner cell is on this side
(`boundaryValue = (owner + ghost) / 2`, so `ghost = 2*boundaryValue -
owner`) -- the standard technique for this scenario, restoring the same
full-spacing offset structure every other upwind face already has. This
changes `upwindBoundaryFaceValue`'s public contract (it no longer returns
the raw boundary value for inflow), so its doc comment and the one
existing unit test asserting the old value were updated
(`ConvectionTest.BoundaryInflowUsesGhostReflectedValue`, was
`BoundaryInflowUsesBoundaryValue`).

**Result**: observed order is now 0.995 → 0.999 → 1.000 with refinement
(was drifting toward ~0.5).

Both fixes were verified clean under debug, release, and ASan+UBSan
(0 sanitizer reports throughout), with repo-wide `clang-format`/
`clang-tidy` clean afterward, and fresh cavity/Poiseuille runs unaffected
(neither fix touches `MomentumEquation.cpp`'s own, separate boundary
treatment -- see TODO.md "P0 -- Incompressible Physics" for why the SIMPLE
solver's matrix assembly doesn't call these discretization-layer functions
directly).

---

## Fixes applied during this pass

* **10 `-Wshadow` warnings** in `src/io/case/JsonUtil.cpp` /
  `JsonUtil.hpp`: every parsing-helper parameter named `json` shadowed the
  file-scope `using nlohmann::json;` alias. Renamed every such parameter to
  `node`. Debug and release now build with 0 warnings.
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
  `sanitizers`, `python` as separate jobs. Config-only -- not pushed to a
  remote or run this pass (no GitHub remote exists yet for this
  repository).
* Two stray IDE/tool scratch files at the repo root
  (`tidy_output.txt`, `cmake_test_discovery_*.json`) were excluded from the
  baseline commit and added to `.gitignore` rather than committed.
* **Laplacian and upwind-convection order-of-accuracy fixes** -- see above.

## Git

This repository had no `.git` before this pass. A baseline commit was made
of the existing tree (source, tests, cases, and pre-existing validation
evidence under `results/` and `validation/`) before any of the fixes above,
so "clean working tree" here means clean *relative to that new baseline* --
it is not evidence of a stable history predating this session. `git status
--short` is empty and `git diff --check` reports no whitespace errors as of
the final commit of this pass.

## CI

`.github/workflows/ci.yml` was authored and reviewed but deliberately not
pushed or executed (no GitHub remote exists for this repository yet). Its
expected first real run: every job green, `build-test` included, since
CTest is genuinely 100% now.
