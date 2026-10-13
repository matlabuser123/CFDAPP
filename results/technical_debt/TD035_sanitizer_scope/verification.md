# TD-035 — the sanitizer fixture is IMPLEMENTED and CLEAN. §11.4 is still blocked, for a different reason.

```text
STATUS:      PARTIALLY RESOLVED. The premixed-flame code now has real sanitizer coverage where it
             previously had NONE, and the known infeasibility (>= 3 tests exceeding the per-test
             cap) is removed by an explicit, recorded scope limit. Whether the FULL asan preset
             completes is UNMEASURED, so §11.4 is not unblocked -- see §5.
SCOPE:       a new test target, one CMakeLists addition, one asan testPreset filter. No production
             source changed; `libcfdcore.a` is untouched by the fixture itself.
FILES:       tests/unit/species/test_premixed_flame_sanitizer.cpp   NEW
             tests/unit/species/CMakeLists.txt                      +33 lines (new target)
             CMakePresets.json                                      +7 lines (asan filter)
EVIDENCE:    logs/03_fixture_under_asan.log · logs/01_cost_ladder.log · logs/02_asan_measured.log
COMMIT:      none — not authorized.
```

---

## 1. What was implemented

```text
the fixture    tests/unit/species/test_premixed_flame_sanitizer.cpp
               PremixedFlameSanitizerTest.TheReactingPathRunsCleanAtReducedResolution
               150 cells, relaxT 10, production dt -- the configuration logs/01 and /02 measured.
the target     CFDSpeciesSanitizerTests, its OWN executable. See §2.
the filter     CMakePresets.json's asan testPreset gains
                   "filter": { "exclude": { "name": "G8PremixedFlameTest" } }
```

## 2. The fixture is in its own target BECAUSE C7 forbids the obvious placement

C7 of TD-004's frozen gate bounds *"`CFDSpeciesTests` excluding `G8PremixedFlameTest`"* at **exactly
343 tests, unchanged**. Adding a test to that binary would make it 344 and **break a frozen
criterion**. So the fixture gets its own executable, and the constraint is verified rather than
assumed:

```text
non-G8 tests in CFDSpeciesTests:  343      C7's frozen baseline: 343
```

## 3. The measurement

```text
build        Release and asan trees both configure and build the new target
Release      PASSED,  21.67 s
ASan+UBSan   PASSED, 348.75 s   against ctest --timeout 7200  ->  20.6x headroom
exit code    0, captured BEFORE any pipe
```

At CI settings (`CLAUDE.md` §7: `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0`,
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`):

```text
AddressSanitizer errors   0
UBSan runtime errors      0
LeakSanitizer reports     0
```

**The premixed-flame reacting path is clean under ASan and UBSan.** The fixture's assertions confirm
the instrument reached the code rather than passing vacuously: `status == Converged`,
`recentrings > 0` (so `recentre()` and `shiftField()` actually fired), and
`finalMarchSteps > 1000`.

### 3.1 The filter does what it claims, verified by listing

```text
ctest --preset asan -N :
  G8PremixedFlameTest entries         0      excluded
  PremixedFlameSanitizerTest entries  1      Test #1871, present
  Total Tests                      2860      2859 before the fixture, so +1
```

### 3.2 The sanitizer factor at this fixture, measured again

```text
Release 21.67 s  ->  ASan+UBSan 348.75 s     factor 16.1x
```

against `12.6x` and `14.7x` from `logs/02` at 100 and 150 cells. Consistently in the 12-17x band
and **nowhere near the `38x` TD-035 was opened on** — that figure remains unexplained (audit §7.1)
and both are on the record.

## 4. THE SCOPE LIMIT, recorded as §9 requires

```text
The heavy G8PremixedFlameTest cases (500-1500 cells) are EXCLUDED from `ctest --preset asan`.
They are therefore exercised under NO sanitizer. The fixture covers the same FUNCTIONS at 150
cells; it does NOT cover the same PROBLEM SIZES, so a defect that only manifests above some
allocation size would be missed.
```

This is a real gap and is stated as one. It is recorded here, in `TECHNICAL_DEBT.md` and in
`TODO.md`, and in `ROADMAP.md` as a standing capability limit.

**Why the exclusion is unavoidable rather than convenient:** `TD004…/logs/A6_02_thousand_cell.log`
tested the 1000-cell level against the real 7200 s cap and it was **killed at exit 124**. The heavy
cases cannot fit, and `CLAUDE.md` §7 forbids inflating a limit to obtain a pass. TD-009 separately
records the Debug+GUI timeout contract as already unstable, so raising `--timeout` is not available
either.

## 5. WHY §11.4 IS STILL BLOCKED

```text
BEFORE   `ctest --preset asan` could not pass: at least three G8 tests exceed the per-test cap, so
         the §8 sanitizer regression was impossible AS SPECIFIED.
NOW      that specific impossibility is removed, and the premixed-flame code has coverage it never
         had. But the preset lists 2860 tests and THE FULL RUN'S COST HAS NOT BEEN MEASURED.
```

**So §11.4 is not unblocked — the reason changed from "known infeasible" to "unmeasured".** Claiming
otherwise would be exactly the kind of unearned progress this campaign exists to prevent. What would
settle it is one full `ctest --preset asan` run, which at CI's own settings is the §8 requirement
itself and is a separate authorization.

## 5a. THE CHANGE IS COMPLETE AND VERIFIED, BUT ONLY PARTLY COMMITTABLE

Discovered while checking §11.1 ("only the intended files have changed"):

```text
CMakePresets.json                 MY CHANGE ONLY, +7 lines (the asan filter). Committable alone.
tests/unit/species/CMakeLists.txt ENTANGLED. It was ALREADY modified before this work -- a
                                  pre-existing hunk wires 11 P18 test sources into
                                  CFDSpeciesTests. My addition is a separate hunk.
test_premixed_flame_sanitizer.cpp NEW and untracked -- as are 11 of the 16 sources that
                                  CMakeLists references, test_premixed_flame.cpp among them.
```

So committing the wiring would carry pre-existing P18 work outside this item's scope, and would
leave a tracked `CMakeLists.txt` referencing 11 untracked sources — **a tree a fresh clone could
not build**. That is the same structural blocker the ~300 uncommitted production files have, now
seen to reach the test tree as well.

**What this does NOT undermine:** the fixture is built, run and clean in this tree, and the preset
filter is verified by listing. The work is done and measured. What is blocked is its DELIVERY,
which is step 9's problem (committing the P18 subsystem) rather than a defect in this change.

**What it does mean:** TD-035's partial resolution is real in the WORKING TREE and not yet real in
the REPOSITORY. The register entry says PARTIALLY RESOLVED on the strength of a measurement, not of
a commit, and the distinction is recorded here so it is not read as shipped.

## 5b. clang-format — a violation I introduced and fixed

The first draft of the fixture had two comment lines over the column limit, so
`clang-format-18 --dry-run --Werror` failed on it. Formatted and re-verified at CI settings:

```text
files considered 742   exit code 0   offending files 0
```

742 rather than 741 because the fixture is the new file. The reformatted file still compiles and
passes (21.07 s Release). Recorded because §7 requires clang-format clean and the violation was
mine, not pre-existing.

## 6. What is NOT established

```text
NOT established  that the full asan preset completes. 2860 tests, cost unmeasured. §5.
NOT established  that 150 cells reaches every branch the 500-1500 cell cases do. It converges and
                 recentres, so the main paths run; branches gated on large allocations or on step
                 counts above ~1400 are not shown to execute.
NOT established  that ASan+UBSan are the only sanitizers at issue. CI runs FIVE configurations;
                 only this one was measured. TSan and the others are unknown for this suite.
NOT explained    the 38x in TD-035's founding measurement against the 12-17x measured three times
                 since. audit §7.1 records the discrepancy and the candidates; it is not resolved.
NOT measured     whether the fixture's assertions would CATCH a memory defect. They are not
                 designed to -- ASan and UBSan are the detectors, and the assertions only prove the
                 instrument arrived. A deliberate fault injection would test the pairing, and is
                 not performed.
NOT changed      any production source. The fixture is test-only; the CMakeLists and preset edits
                 are build/CI wiring.
```

**§6's last measured item is the honest limit on this coverage.** A clean sanitizer run proves the
path executed without ASan or UBSan firing. It does not prove that this fixture would surface a
defect introduced tomorrow — only that the detectors are now pointed at code they previously never
saw.
