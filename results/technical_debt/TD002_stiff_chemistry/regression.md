# TD-002 — REGRESSION

```text
log      logs/05_c8_two_library_and_regression.log
build    Release, -j20, WSL2 Ubuntu-22.04
policy   CLAUDE.md section 8 -- every binary demonstrably rebuilt, harness fails closed, exact counts
```

---

## 1. Binaries measured

```text
bc3384cdbe7f418b492071fafe9f073aaeb813e4f5ae5708544e0d5dbce74b8e  apps/cli/cfdapp
646a71a12886502f843514fac91cb36770972883d3836c016a4884e7796ad0b9  tests/unit/io/CFDIoTests
f195a2f9b4472cb4580bd3224e736879019f6e397995f458955cfc03ff14125e  tests/unit/app/CFDAppLayerTests
de924805951652c28237ca5ab8ddffb4ad54076dd8c1e0ac4d6271f1f5b6f348  tests/unit/species/CFDSpeciesTests
```

Staleness checked per binary against every `src/`, `include/` and `apps/` source; the harness exits
rather than measuring a stale binary.

## 2. Counts

| suite | gate baseline | ran | passed | failed |
| --- | --- | --- | --- | --- |
| `CFDIoTests` | 328 (TD-001's) | **331** | **331** | 0 |
| `CFDAppLayerTests` | 32 | **32** | **32** | 0 |
| `CFDSpeciesTests` (baseline conditions) | 339 | **343** | **343** | 0 |

Both deltas are **additive and enumerated**, not absorbed.

### 2.1 `CFDIoTests` 328 -> 331

`reacting.steps` became a required key, and a required constraint with no test is an untested
constraint. Three ways for the key to be wrong, three tests:

```text
G8ReactingCaseTest.AReactingBlockWithoutSTEPSIsRefused
G8ReactingCaseTest.ANonPositiveStepCountIsRefused        (0 and -3)
G8ReactingCaseTest.AFractionalStepCountIsRefused         (2.5)
```

All 328 of TD-001's tests still pass. `G8ReactingCaseTest` is 15/15.

### 2.2 `CFDSpeciesTests` 339 -> 343

```text
TD002_ALongMarchKeepsSumToOneInsideP18_1sInvariant       85 472 sub-steps, 0.00 eps
TD002_N1_WithoutTheCarryTheSameMarchIsREFUSED            the control, one settings flip
TD002_TheAccuracyControlCanBeTightenedWithoutFailing     4 rungs, monotone sub-step rise
TD002_TheRawFieldsPathCARRIESATransportClosureRatherThanErasingIt   ratio 1.000000
```

All 339 baseline tests still pass, **including every P18.8 CPU/GPU equivalence test** -- which is
half of C8's evidence; the other half is the two-library sweep in `verification.md` §1.1.

The suite's wall time rose from `5.6 s` to `14.4 s`, almost all of it the 85 472-step march and the
control ladder. That is the cost of a test that actually exercises the budget.

## 3. `CFDSpeciesTests` runs under the baseline's own conditions

The 339 baseline was recorded **excluding `G8PremixedFlameTest`**
(`results/p18/g8_closure/flame_solver_findings.md`). This run uses the same
`--gtest_filter=-G8PremixedFlameTest.*`. Comparing against a baseline requires the baseline's
conditions; using them is what makes the comparison valid. `G8PremixedFlameTest` remains in-flight
TD-004 territory, uncredited, with most of its tests failing by its own record.

## 4. Not run, and why

| configuration | state |
| --- | --- |
| Debug + GUI | **NOT RUN.** No commit is authorized, and §8's full ladder is a commit precondition. |
| sanitizers at CI settings | **NOT RUN.** Same reason. TD-002 adds one `std::vector<std::vector<Real>>` owned by the integrator and passed by pointer; no new raw ownership. |
| `clang-format-18 --dry-run --Werror` | **NOT RUN** as a suite. |
| full `ctest` | **NOT RUN.** It rewrites tracked `results/validation/**` with timing-only churn. |

Stated as gaps, not waived: they are the outstanding items whenever a commit is authorized.
