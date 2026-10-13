#!/usr/bin/env bash
# P17.2 group N -- non-vacuity controls. Each MUST make the named criterion fail.
#
#   N1  return rho*T (drop R)          -> R4, and R2 for any R != 1
#   N2  return a constant             -> R1/R2/R3 at every non-default state
#   N3  remove the input validation    -> V1/V2
#   N4  perturb R by 1e-6 relative     -> R4 at the 1e-12 bound; proves the BOUND
#                                         detects, not the plumbing
#
# N4 is the sharp one. 1e-6 is six orders above the criterion's 1e-12 bound and far
# below anything a casual reader would notice, so it demonstrates the bound's
# tightness is what does the work. A control that only proves "something broke" would
# pass with a 10x error.
#
# Safety: sha256 before, patch target asserted UNIQUE, restore VERIFIED here rather
# than trusted to the trap, trap kept as a backstop that exits 99 if it ever acts.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible/controls
B=build/qual-final
F=src/compressible/IdealGasEOS.cpp
cd "$ROOT"
mkdir -p "$P17/logs"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

BK=$(mktemp -d /tmp/p17_2_ctl.XXXXXX)
cp "$F" "$BK/$(basename "$F")"
SHA=$(sha256sum "$F" | cut -d' ' -f1)
restore_and_verify() {
  cp "$BK/$(basename "$F")" "$F"
  local now; now=$(sha256sum "$F" | cut -d' ' -f1)
  [ "$now" = "$SHA" ]
}
trap 'echo "TRAP FIRED"; restore_and_verify || exit 99' EXIT

log=$P17/logs/P17_2_controls.log
{
echo "P17.2 -- group N non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "target: $F"
echo "sha256 before: $SHA"
echo ""
fail=0

patch_once() {
  python3 - "$F" "$1" "$2" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path, encoding="utf-8").read()
n = text.count(old)
if n != 1:
    sys.stderr.write("PATCH TARGET NOT UNIQUE (%d) \n" % n)
    sys.exit(1)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
PY
}

build() { cmake --build "$B" -j 16 > /tmp/p17_2_build.log 2>&1; }

run_eos() { OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'IdealGasEOSTest' > "$1" 2>&1; }

report() {  # report <id> <criterion> <detection> <extra>
  echo "  PLANTED DEFECT     yes"
  echo "  CRITERION          $2"
  echo "  DETECTION          $3"
  if restore_and_verify; then echo "  RESTORE            VERIFIED by SHA-256"
  else echo "  RESTORE            FAILED"; fail=1; fi
  [ -n "${4:-}" ] && echo "  $4"
  echo ""
}

# ------------------------------------------------------------------- N1 -------
echo "=== N1 -- drop R: return density * temperature ==="
patch_once '  const Real p = density * gasConstant_ * temperature;' \
           '  const Real p = density * temperature;  // N1 CONTROL -- R dropped' \
  || { echo "  PATCH FAILED"; exit 1; }
if ! build; then echo "  BUILD FAILED"; fail=1; restore_and_verify; echo ""
else
  run_eos "$P17/logs/N1-after.log"; rc=$?
  n=$(grep -acE '\(Failed\)' "$P17/logs/N1-after.log" || true)
  if [ $rc -ne 0 ]; then
    report N1 "FAILED as expected ($n tests)" "DETECTED" \
      "which tests: $(grep -aoE 'IdealGasEOSTest\.[A-Za-z]+ \.+ +Failed' "$P17/logs/N1-after.log" | sed -E 's/ \.+ +Failed//' | tr '\n' ' ')"
  else
    echo "  CRITERION          PASSED -- R4 does not check R"; echo "  DETECTION          UNDETECTED"
    restore_and_verify && echo "  RESTORE            VERIFIED by SHA-256"; fail=1; echo ""
  fi
fi

# ------------------------------------------------------------------- N2 -------
echo "=== N2 -- return a constant ==="
patch_once '  const Real p = density * gasConstant_ * temperature;' \
           '  const Real p = 101325.0;  // N2 CONTROL -- constant' \
  || { echo "  PATCH FAILED"; exit 1; }
if ! build; then echo "  BUILD FAILED"; fail=1; restore_and_verify; echo ""
else
  run_eos "$P17/logs/N2-after.log"; rc=$?
  n=$(grep -acE '\(Failed\)' "$P17/logs/N2-after.log" || true)
  if [ $rc -ne 0 ]; then
    report N2 "FAILED as expected ($n tests)" "DETECTED" \
      "a constant cannot satisfy the round trips at non-default states"
  else
    echo "  CRITERION          PASSED"; echo "  DETECTION          UNDETECTED"
    restore_and_verify && echo "  RESTORE            VERIFIED by SHA-256"; fail=1; echo ""
  fi
fi

# ------------------------------------------------------------------- N3 -------
echo "=== N3 -- remove the input validation ==="
patch_once '  requirePositiveFinite(density, "density");
  requirePositiveFinite(temperature, "temperature");
  const Real p = density * gasConstant_ * temperature;' \
           '  // N3 CONTROL -- validation removed
  const Real p = density * gasConstant_ * temperature;' \
  || { echo "  PATCH FAILED"; exit 1; }
if ! build; then echo "  BUILD FAILED"; fail=1; restore_and_verify; echo ""
else
  run_eos "$P17/logs/N3-after.log"; rc=$?
  n=$(grep -acE '\(Failed\)' "$P17/logs/N3-after.log" || true)
  if [ $rc -ne 0 ]; then
    report N3 "FAILED as expected ($n tests)" "DETECTED" \
      "which tests: $(grep -aoE 'IdealGasEOSTest\.[A-Za-z]+ \.+ +Failed' "$P17/logs/N3-after.log" | sed -E 's/ \.+ +Failed//' | tr '\n' ' ')"
  else
    echo "  CRITERION          PASSED -- V1/V2 do not check rejection"; echo "  DETECTION          UNDETECTED"
    restore_and_verify && echo "  RESTORE            VERIFIED by SHA-256"; fail=1; echo ""
  fi
fi

# ------------------------------------------------------------------- N4 -------
echo "=== N4 -- perturb R by 1e-6 relative; the 1e-12 BOUND must detect it ==="
patch_once '  const Real p = density * gasConstant_ * temperature;' \
           '  const Real p = density * (gasConstant_ * 1.000001) * temperature;  // N4 CONTROL' \
  || { echo "  PATCH FAILED"; exit 1; }
if ! build; then echo "  BUILD FAILED"; fail=1; restore_and_verify; echo ""
else
  run_eos "$P17/logs/N4-after.log"; rc=$?
  n=$(grep -acE '\(Failed\)' "$P17/logs/N4-after.log" || true)
  if [ $rc -ne 0 ]; then
    report N4 "FAILED as expected ($n tests)" "DETECTED -- the 1e-12 bound caught 1e-6" \
      "this is what proves the BOUND detects, not merely the plumbing"
  else
    echo "  CRITERION          PASSED at a 1e-6 error -- the bound is too loose to matter"
    echo "  DETECTION          UNDETECTED"
    restore_and_verify && echo "  RESTORE            VERIFIED by SHA-256"; fail=1; echo ""
  fi
fi

# --------------------------------------------------------------- closeout -----
trap - EXIT
echo "=== restored, and proved back at baseline ==="
restore_and_verify || { echo "  RESTORE FAILED"; exit 99; }
echo "  $(sha256sum "$F" | cut -d' ' -f1)  $F  IDENTICAL"
echo "  markers left: $(grep -c 'CONTROL' "$F" || true)  (0 required)"
build || { echo "  BASELINE REBUILD FAILED"; exit 1; }
run_eos "$P17/logs/after_restore.log"
echo "  EOS tests at baseline: $(grep -aE 'tests passed|tests failed out of' "$P17/logs/after_restore.log" | tail -1)"
rm -rf "$BK"
echo ""
[ $fail -eq 0 ] && echo "P17.2 GROUP N: ALL DETECTED, ALL RESTORED" || echo "P17.2 GROUP N: SEE ABOVE"
echo "done: $(date -u +%FT%TZ)"
exit $fail
} 2>&1 | tee "$log"
