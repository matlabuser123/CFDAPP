# TD-001 — REGRESSION

```text
log         logs/24_regression.log
build       Release (CMake preset `release`), -j20, WSL2 Ubuntu-22.04 on Windows 11
policy      CLAUDE.md section 8 -- every binary demonstrably rebuilt against the exact source
            being tested, harness fails closed, exact counts from the actual run
```

---

## 1. The harness failed closed once, and that was correct

The first attempt stopped at the staleness check:

```text
FAIL: stale build/release/tests/unit/io/CFDIoTests
src/app/ProjectRunner.cpp
```

The authoritative rerun's last step restores `src/app/ProjectRunner.cpp` from the saved production
copy, which rewrites the file and moves its mtime even though its content is **byte-identical** to
the freeze record (`f14aa151d743190777d242e101cef6b19fb7ed77005bb9c65ead090292d8bc9a`, verified
before and after). §8 asks for a demonstrable rebuild, not an argument about timestamps, so every
target was relinked before anything was measured. Recorded here rather than quietly worked around.

## 2. Binaries measured

```text
c58b32eeea5a64fc6409bafc5dec50496e4a6a09237ca674377bbc09fbf984d3  apps/cli/cfdapp
9448c1293c86c9dbf3d5e2ab25d7270f7be98e55ca314927d8b6bacae5a88833  tests/unit/io/CFDIoTests
50f0ce5e4c10f243c0ee37c810d82b51b169d7fabc21edca5cec585e325f93ac  tests/unit/app/CFDAppLayerTests
faaf7f5e564ce07fc7d9dd2bd1a445b2fd4f8d000412a0eaf1ef2231b9a7e175  tests/unit/species/CFDSpeciesTests
```

## 3. Counts

| suite | gate baseline | ran | passed | failed |
| --- | --- | --- | --- | --- |
| `CFDIoTests` | 325 | **328** | **328** | 0 |
| `CFDAppLayerTests` | 32 | **32** | **32** | 0 |
| `CFDSpeciesTests` (baseline conditions) | 339 | **339** | **339** | 0 |

## 4. `CFDIoTests` is 328, not 325 — the delta is enumerated, not absorbed

TD-001 made two things mandatory for a reacting case, and a new required constraint with no test is
an untested constraint. Three tests were added:

```text
G8ReactingCaseTest.AReactingBlockWithoutATimeStepIsRefused
G8ReactingCaseTest.ANonPositiveTimeStepIsRefused               (0.0 and -0.125)
G8ReactingCaseTest.AReactingBlockWithoutAThermalBlockIsRefused
```

`325 + 3 = 328`, and **all 325 baseline tests pass**. The gate's §4 expectation reads
`all pass; 325 baseline`; the count is stated both ways here rather than reported as 325 or left to
look like a drift.

### 4.1 Making `time_step` required exposed a contaminated instrument

`tests/unit/io/test_reacting_case.cpp` builds nine cases from one helper. The moment `time_step`
became mandatory the suite went **324/325** — one positive test failed. The other eight passed, and
that was the problem: six are **refusal** tests, and they were being refused for the missing key
rather than for the defect each one names. They passed while measuring nothing — CLAUDE.md §5.3's
contaminated instrument, visible only because one test in the same file failed loudly.

Repairing the helper then created a second one: a mandatory `thermal` block gave the fixture a
second `"initial_temperature": 298.15`, and `AnUnknownKeyInTheReactingBlockIsRefused` locates its
injection point with a plain `find()`, so it would have injected into `thermal` and tested the
thermal parser. Fixed by searching from the `"reacting": {` offset, with the reason at the call site.

Neither is a TD-001 regression — both are defects TD-001 *revealed* in tests written earlier — but
both are recorded because the suite was green while two instruments were blind.

## 5. `CFDSpeciesTests` — the baseline's own conditions, and why the filter is not a dodge

The 339 baseline was recorded **excluding `G8PremixedFlameTest`**, in
`results/p18/g8_closure/flame_solver_findings.md`:

```text
CFDSpeciesTests  (excluding G8PremixedFlameTest)   339 ran   339 passed   0 failed
```

This run uses the same exclusion, `--gtest_filter=-G8PremixedFlameTest.*`, and gets **339 / 339**.
Comparing against the baseline requires the baseline's conditions; using them is what makes the
comparison valid rather than what weakens it.

An earlier unfiltered attempt was stopped after **40 minutes** without completing. With the
exclusion the suite finishes in **6.0 s**, which locates the entire cost in the flame tests' march
loops.

`G8PremixedFlameTest` is in-flight TD-004 territory, is explicitly **not credited against any TD
item** (`DEBT_REGISTER.md` §6), and its own record says "13 tests written; most currently FAIL". It
now declares **17** tests — it grew during that in-flight work. That discrepancy is reported here
and belongs to TD-004; it is not measured, claimed or relied on by TD-001.

## 6. The P18 library is untouched, two ways

* `git status --short -- src/species include/cfd/species` shows no modification attributable to
  TD-001; no file under either directory was edited by this item.
* `tests/unit/species/CFDSpeciesTests` hashes **`faaf7f5e…`**, and that hash is **identical across
  every build in this session** — before the dispatch existed, after it, and after each of the four
  source variants. `ProjectRunner.o` is not pulled into that executable, so the 339 count is a
  measurement of a library this item did not change.

## 7. Backward compatibility (§9) — bitwise

Gate C10, measured in the authoritative rerun on three chemistry-off cases:

```text
species_diffusion          fields.csv / solution.vtk / residuals.csv
heated_species_diffusion   fields.csv / solution.vtk / residuals.csv
lid_driven_cavity_40x40    fields.csv / solution.vtk / residuals.csv

all nine hashes IDENTICAL to baseline/c10_prefix_hashes.txt (the pre-change build)
```

Anything short of bitwise here would mean `ProjectRunner` behaviour moved for every existing user.
It did not.

## 8. Runtime noise

The C10 runs regenerate `cases/*/results`. The three tracked files those runs removed —
`cases/lid_driven_cavity_40x40/results/analysis/{cavity_u_centerline.csv,cavity_v_centerline.csv,validation.json}`
— were restored with `git checkout` and verified gone from `git status`.

Four `cases/*/results/metadata.json` files remain modified. They are **not** this item's noise: they
add `build_identity`, `convection_scheme`, `gradient_scheme`, `linear_solver_backend`,
`non_orthogonal_corrections` and `gpu_discretization_used`, fields from the uncommitted P12–P18 body
of work. `cases/heated_cavity` carries the same change and TD-001 never ran it. Left exactly as
found; C10's comparison is against frozen hashes, which matched bitwise.

## 9. Not run, and why

| configuration | state |
| --- | --- |
| Debug + GUI | **NOT RUN.** TD-001 changes `src/io`, `src/app` and one test file; no GUI source. CLAUDE.md §8's full ladder belongs to a commit, and no commit is authorized. |
| sanitizers at CI settings | **NOT RUN.** Same reason. `ctest --preset asan --timeout 7200` is hours, and TD-001 adds no new memory ownership — four stack-local solves and `std::vector` fields. |
| `clang-format-18 --dry-run --Werror` | **NOT RUN** as a suite. |
| full `ctest` | **NOT RUN.** It rewrites tracked `results/validation/**` with timing-only churn (§7), and TD-001 is not a commit. |

These are stated as gaps, not waived: whenever a commit is authorized, §8's full ladder is a
precondition and these four are the outstanding items.
