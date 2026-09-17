# P12-DIFF-002 A4-1 — harness freshness self-test

Proves the A4 harness rebuilds rather than inferring freshness from an executable's mere
existence -- the defect that made A1's and A2's W7 counts unreliable.

```text
date                 2026-09-16T13:00:11Z
git HEAD             b66310ca871811c4c7671056beec291a451af55c
target               CFDDiscretizationTests
test source touched  tests/unit/discretization/test_boundary_reconstruction.cpp
production library   58be6b751328c9bb   (must not change at any step)

step 1  baseline          binary bc698f840a6eb6b9  test-source 474530ce10f3842f  lib 58be6b751328c9bb
step 2  marker appended   test-source 474530ce10f3842f -> 28b044915474f25d  (a temporary always-passing TEST, so the
                          binary CONTENT and the test COUNT must both respond)
step 3  rebuild OK        binary f44e2670f6f134cb  tests 169 -> 170  lib 58be6b751328c9bb
step 4  RESPONDS          binary bc698f840a6eb6b9 -> f44e2670f6f134cb and tests 169 -> 170  => it really rebuilds  PASS
step 5  marker reverted   test-source 28b044915474f25d -> 474530ce10f3842f (baseline was 474530ce10f3842f)
step 6  rebuild OK        binary bc698f840a6eb6b9  tests 169 (baseline 169)  lib 58be6b751328c9bb
step 7  RESTORED          test-source and binary both back to baseline  PASS

production library after all steps: 58be6b751328c9bb  (unchanged throughout)
```

The harness also fails closed: `w7_harness.sh` counts any target whose build step returns
non-zero, or whose executable is absent after a successful build, reports it as BUILD FAILED,
never executes it, and exits non-zero.
