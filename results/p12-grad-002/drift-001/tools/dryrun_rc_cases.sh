#!/usr/bin/env bash
# DRIFT-001: DRY-RUN of the candidate case-level fix, OUTSIDE the repository.
# A scratch root gets a copy of cases/ in which ONLY the two W8 cases select "face_flux":
# "rhie_chow"; the UNCHANGED production-case test suites then run from the current release
# binary with that scratch root as working directory (the tests resolve "cases/..." relative to
# it, exactly as ctest's WORKING_DIRECTORY does for the repo). Nothing in the repo is modified.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/drift-001
S=$HOME/drift001_dryrun
rm -rf "$S"; mkdir -p "$S/results/validation/production"
cp -r "$R/cases" "$S/cases"
ln -s "$R/tests" "$S/tests"
python3 - "$S" <<'EOF'
import json, sys, pathlib
root = pathlib.Path(sys.argv[1])
for case in ("poiseuille_distorted", "curved_channel_multiblock"):
    p = root / "cases" / case / "solver.json"
    d = json.loads(p.read_text())
    d["face_flux"] = "rhie_chow"
    p.write_text(json.dumps(d, indent=2) + "\n")
    print("modified", p)
EOF
BIN=$R/build/release/tests/integration/case/CFDCaseIntegrationTests
[ -x "$BIN" ] || BIN=$(find "$R/build/release" -type f -name CFDCaseIntegrationTests | head -1)
LOG=$P/logs/d1_dryrun_rc_cases.log
cd "$S" || exit 1
{
  echo "# DRIFT-001 dry-run: two W8 cases with face_flux rhie_chow; tests UNCHANGED"
  echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)  binary $BIN $(sha256sum "$BIN" | cut -c1-16)"
  echo "# libcfdcore.a $(sha256sum "$R/build/release/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "# test sources $(sha256sum "$R/tests/integration/case/test_structured_quad_production_case.cpp" "$R/tests/integration/case/test_multiblock_production_case.cpp" | cut -c1-16 | tr '\n' ' ')"
  "$BIN" --gtest_filter='StructuredQuadProductionCase.*:MultiBlockProductionCase.*'
  echo "exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$LOG" 2>&1
grep -E '^\[  (FAILED|PASSED) |^\[       OK|Failure|exit ' "$LOG"
