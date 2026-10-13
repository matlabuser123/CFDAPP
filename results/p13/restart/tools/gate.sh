#!/usr/bin/env bash
# P13-001 -- run the frozen acceptance gate, in its frozen order, on ONE tree.
#
# Every criterion is executed against the same rebuilt binaries, so the whole
# verdict describes one state of the source rather than a mixture of states.
# STOPS at the first failed group, because the milestone's rule is to stop at
# the first failed gate rather than continue to see whether later items pass.
#
#   usage: gate.sh [--skip-heavy]
#            --skip-heavy omits G3 (full CTest), G4 (CPU-only build) and the
#            CLI group, for a fast re-check during development. A gate verdict
#            is only ever recorded from a run WITHOUT it.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/p13/restart
T=$E/tools
L=$E/logs
cd "$ROOT"
mkdir -p "$L"

skipHeavy=0
[ "${1:-}" = "--skip-heavy" ] && skipHeavy=1

step() { printf '\n########## %s ##########\n' "$*"; }
die()  { printf '\nGATE STOPPED at %s\n' "$*"; exit 1; }

# --- rebuild, and prove the binaries are of THIS source --------------------
step "rebuild build/final (Release + CUDA)"
cmake --build build/final -j20 2>&1 | tail -2 || die "build"
# CLAUDE.md section 8 wants "every test binary newer than the newest source".
# A previous version of this check walked `find -type f -executable`, which on
# drvfs matches every file including object files and CMake scripts -- it
# reported 405 "stale binaries" on a perfectly fresh build. The honest check
# is ninja's own: a second build that finds nothing to do means every target
# is up to date with respect to every input it knows about.
freshness=$(cmake --build build/final -j20 2>&1 | tail -1)
echo "freshness: $freshness"
case "$freshness" in
  *"no work to do"*) ;;
  *) die "build freshness (ninja still had work to do after a completed build)" ;;
esac
{ echo "date:     $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "git HEAD: $(git rev-parse HEAD)"
  echo "freshness: $freshness"
  sha256sum build/final/src/libcfdcore.a build/final/cuda/libcfdcuda.a \
            src/pressure_velocity/SIMPLE.cpp src/app/ProjectRunner.cpp \
            build/final/apps/cli/cfdapp
} > "$L/gate-identity.log"
cat "$L/gate-identity.log"

# --- G1: preservation ------------------------------------------------------
step "G1 preservation -- the existing solver's numbers must not move"
bash "$T/run.sh" fingerprint final > /dev/null 2>&1 || die "G1 (instrument failed to run)"
if diff <(tail -n +8 "$L/fingerprint-baseline.log") <(tail -n +8 "$L/fingerprint-final.log") \
     > "$L/G1_diff.log" 2>&1; then
  echo "G1: IDENTICAL -- 10 cases, every field, residual and history, bitwise"
else
  head -20 "$L/G1_diff.log"; die "G1"
fi

# --- A, C, E ---------------------------------------------------------------
step "A (format/identity), C (rejection matrix), E (residency)"
bash "$T/run.sh" negative > /dev/null 2>&1 || { tail -30 "$L/negative.log"; die "A/C/E"; }
grep -E "^checks|GROUPS" "$L/negative.log"

# --- B ---------------------------------------------------------------------
step "B -- continuous vs checkpoint/resume, bitwise"
bash "$T/run.sh" equiv > /dev/null 2>&1 || { grep -m20 "FAIL" "$L/equivalence.log"; die "B"; }
grep -E "^rows|GROUP B" "$L/equivalence.log"

# --- D: the controls, each of which MUST fail -------------------------------
step "D -- non-vacuity controls (each MUST fail)"
for control in drop-flux skip-replay ignore-count; do
  if bash "$T/run.sh" equiv --quick "--control-$control" > /dev/null 2>&1; then
    cp "$L/equivalence.log" "$L/control-$control.log"
    printf '  %-14s DETECTED (the criterion can fail)\n' "$control"
  else
    cp "$L/equivalence.log" "$L/control-$control.log"
    die "D control $control was NOT detected -- the criterion it targets is vacuous"
  fi
done
# Restore the clean group-B evidence the controls just overwrote.
bash "$T/run.sh" equiv > /dev/null 2>&1 || die "B (re-run after the controls)"

if [ $skipHeavy -eq 1 ]; then
  printf '\n--skip-heavy: F, G3, G4 and G6 NOT RUN. This is not a gate verdict.\n'
  exit 0
fi

# --- F: the production CLI --------------------------------------------------
step "F -- the production CLI path"
bash "$T/run.sh" cli lid_driven_cavity 400 150 > /dev/null 2>&1 || { tail -40 "$L/cli.log"; die "F"; }
grep -cE "^  PASS" "$L/cli.log" | sed 's/^/  checks passed: /'
grep -E "GROUP F" "$L/cli.log"

# --- G6: the known failure must still reproduce its recorded value ----------
step "G6 -- the known GPU BiCGSTAB restart asymmetry"
bash "$T/known_debt.sh" || die "G6"

# --- G3: full Release + CUDA regression -------------------------------------
step "G3 -- full Release + CUDA CTest"
{ echo "date:     $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "git HEAD: $(git rev-parse HEAD)"
  echo "freshness: $freshness"
  sha256sum build/final/src/libcfdcore.a build/final/cuda/libcfdcuda.a
  echo ""; } > "$L/G3_regression.log"
ctest --test-dir build/final -j16 --output-on-failure >> "$L/G3_regression.log" 2>&1
rc=$?
echo "CTEST_RC=$rc" >> "$L/G3_regression.log"
grep -E "tests passed|tests failed|Total Test time" "$L/G3_regression.log" | sed 's/^/  /'
[ $rc -eq 0 ] || { grep -E "\(Failed\)" "$L/G3_regression.log" | head; die "G3"; }

# --- G4: the CPU-only blind spot ---------------------------------------------
step "G4 -- CPU-only build (CFDAPP_ENABLE_CUDA=OFF) and its suite"
bash "$T/cpu_only.sh" || die "G4"

printf '\n==================== P13-001 GATE: PASS ====================\n'
