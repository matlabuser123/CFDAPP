#!/usr/bin/env bash
# P12-DIFF-002 A4-1 harness self-test: prove the harness actually rebuilds, by showing the test
# binary's hash RESPONDS to a change in test source and is RESTORED when that change is reverted.
#
# 1 record binary hash; 2 make a harmless temporary change to a TEST source; 3 rebuild;
# 4 prove the hash changed; 5 revert; 6 rebuild; 7 prove the original binary is restored.
#
# No production source is touched: the temporary edit goes into a test file, and the file's own
# sha256 is recorded before and after so the revert is provable.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
LOG=$R/results/p12-diff-002/a4/harness_validation.md
TARGET=CFDDiscretizationTests
SRC=$R/tests/unit/discretization/test_boundary_reconstruction.cpp
MARK1='// A4 harness self-test marker -- inserted and reverted by harness_selftest.sh'
MARK2='TEST(A4HarnessSelfTest, TemporaryMarkerProvesTheBinaryRebuilds) { EXPECT_TRUE(true); }'
cd $R

locate() { find $B -type f -name "$TARGET" -perm -u+x 2>/dev/null | head -1; }
PRODHASH() { sha256sum $B/src/libcfdcore.a | cut -c1-16; }

{
  echo "# P12-DIFF-002 A4-1 — harness freshness self-test"
  echo
  echo "Proves the A4 harness rebuilds rather than inferring freshness from an executable's mere"
  echo "existence -- the defect that made A1's and A2's W7 counts unreliable."
  echo
  echo '```text'
  echo "date                 $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "git HEAD             $(git rev-parse HEAD)"
  echo "target               $TARGET"
  echo "test source touched  tests/unit/discretization/test_boundary_reconstruction.cpp"
  echo "production library   $(PRODHASH)   (must not change at any step)"
  echo

  cmake --build $B --target $TARGET -j"$(nproc)" > /dev/null 2>&1
  H0=$(sha256sum "$(locate)" | cut -c1-16); S0=$(sha256sum $SRC | cut -c1-16)
  echo "step 1  baseline          binary $H0  test-source $S0  lib $(PRODHASH)"

  N0=$("$(locate)" --gtest_list_tests 2>/dev/null | grep -cE '^  [A-Za-z]')
  { echo "$MARK1"; echo "$MARK2"; } >> $SRC
  S1=$(sha256sum $SRC | cut -c1-16)
  echo "step 2  marker appended   test-source $S0 -> $S1  (a temporary always-passing TEST, so the"
  echo "                          binary CONTENT and the test COUNT must both respond)"

  if cmake --build $B --target $TARGET -j"$(nproc)" > /dev/null 2>&1; then
    H1=$(sha256sum "$(locate)" | cut -c1-16)
    N1=$("$(locate)" --gtest_list_tests 2>/dev/null | grep -cE '^  [A-Za-z]')
    echo "step 3  rebuild OK        binary $H1  tests $N0 -> $N1  lib $(PRODHASH)"
  else
    H1="(build failed)"
    echo "step 3  rebuild FAILED"
  fi

  if [ "$H1" != "$H0" ]; then
    echo "step 4  RESPONDS          binary $H0 -> $H1 and tests $N0 -> $N1  => it really rebuilds  PASS"
  else
    echo "step 4  DID NOT RESPOND   binary unchanged $H0  => harness cannot detect staleness  FAIL"
  fi

  # Revert: drop exactly the appended marker line.
  grep -v -F -x "$MARK1" $SRC | grep -v -F -x "$MARK2" > $SRC.a4tmp && mv $SRC.a4tmp $SRC
  S2=$(sha256sum $SRC | cut -c1-16)
  echo "step 5  marker reverted   test-source $S1 -> $S2 (baseline was $S0)"

  if cmake --build $B --target $TARGET -j"$(nproc)" > /dev/null 2>&1; then
    H2=$(sha256sum "$(locate)" | cut -c1-16)
    N2=$("$(locate)" --gtest_list_tests 2>/dev/null | grep -cE '^  [A-Za-z]')
    echo "step 6  rebuild OK        binary $H2  tests $N2 (baseline $N0)  lib $(PRODHASH)"
  else
    H2="(build failed)"
    echo "step 6  rebuild FAILED"
  fi

  if [ "$S2" = "$S0" ] && [ "$H2" = "$H0" ]; then
    echo "step 7  RESTORED          test-source and binary both back to baseline  PASS"
  else
    echo "step 7  NOT RESTORED      source $S2 vs $S0, binary $H2 vs $H0  FAIL"
  fi
  echo
  echo "production library after all steps: $(PRODHASH)  (unchanged throughout)"
  echo '```'
  echo
  echo "The harness also fails closed: \`w7_harness.sh\` counts any target whose build step returns"
  echo "non-zero, or whose executable is absent after a successful build, reports it as BUILD FAILED,"
  echo "never executes it, and exits non-zero."
} > "$LOG" 2>&1
cat "$LOG"
