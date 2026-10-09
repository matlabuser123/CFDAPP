#!/usr/bin/env bash
# P13-003 -- the gate dry-run, on the UNCHANGED tree.
#
# Every baseline is measured here and recorded, including the ones that come
# out zero. P13-002's amendment A3 exists because a baseline was inferred
# rather than measured, and a criterion that was already satisfied got reported
# as a pass. This runs first, and its numbers are what the criteria are judged
# against.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/cli-gui/logs
cd "$ROOT"
mkdir -p "$L"

log=$L/00_dryrun.log
{ echo "P13-003 gate dry-run -- UNCHANGED tree"
  echo "date:     $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "git HEAD: $(git rev-parse HEAD)"
  echo ""; } > "$log"
say() { echo "$*" | tee -a "$log"; }

# --- A: one execution path --------------------------------------------------
say "=== A: one execution path (PRESERVATION) ==="
solvers=$(grep -rn 'SIMPLE(\|PISO(\|\.solve(' apps/cli apps/gui --include=*.cpp 2>/dev/null \
          | grep -v '//' | grep -v '/tests/' | wc -l)
say "  A1 solver constructions in apps/cli + apps/gui (excluding tests): $solvers   (must be 0)"
entries=$(grep -rn 'ProjectRunner::run\|session_.run' apps/cli apps/gui --include=*.cpp 2>/dev/null \
          | grep -v '/tests/' | wc -l)
say "  A2 solver entry points from the clients: $entries"
grep -rn 'ProjectRunner::run\|session_.run' apps/cli apps/gui --include=*.cpp 2>/dev/null \
  | grep -v '/tests/' | sed 's/^/       /' | tee -a "$log"
fwd=$(grep -c 'options.cancellationCheck = ' src/app/CaseSession.cpp)
say "  A3 options fields CaseSession rewrites: $fwd   (documented: cancellationCheck only)"

# --- P: capability parity ---------------------------------------------------
say ""
say "=== P: capability parity (DETECTION -- baseline expected to be absent) ==="
guick=$(grep -rn 'checkpoint\|resume' apps/gui --include=*.cpp --include=*.hpp --include=*.qml 2>/dev/null \
        | grep -v '/tests/' | wc -l)
say "  P1 GUI references to checkpoint/resume: $guick   (0 = not reachable)"
cliflags=$(grep -c 'checkpoint\|resume' apps/cli/main.cpp)
say "     CLI references to checkpoint/resume: $cliflags"

# --- V: validation and error parity ----------------------------------------
say ""
say "=== V: validation and error parity ==="
v2core=$(grep -c 'CaseReader\|CaseBuilder' apps/gui/SimulationControllerEditing.cpp)
say "  V2 validateDraft references to CaseReader/CaseBuilder: $v2core   (>0 = shared core)"
v2private=$(grep -rn 'parseGeometry\|parseMesh\|parsePhysics\|parseSolver\|parseBoundary' \
            apps/gui --include=*.cpp 2>/dev/null | grep -v '/tests/' | wc -l)
say "     GUI calls into the private parsers directly: $v2private   (must be 0)"
v3=$(grep -c 'CFDAppCliCheckpoint' tests/CMakeLists.txt 2>/dev/null || echo 0)
say "  V3 ctest entries covering the CLI checkpoint flags: $v3   (baseline, expected 0)"

# --- E: numerical equivalence ------------------------------------------------
say ""
say "=== E: numerical equivalence (DETECTION) ==="
say "  E1..E8  no harness exists on this tree, so the baseline is NOT MEASURABLE."
say "          Recorded as such. It is not a pass, and it is not a failure."
weak=$(grep -c 'cliEquivalentRun.status' apps/gui/tests/test_case_editing.cpp)
say "  the existing 'CLI/GUI Round-Trip Gate' assertions: $weak   (status only -- audit section 3)"

say ""
say "dry-run complete. Every number above is measured, not inferred."
