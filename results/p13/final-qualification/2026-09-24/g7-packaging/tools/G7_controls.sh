#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G7, the control half.
#
# results/p13/packaging/tools/controls.sh is run UNMODIFIED. It implements all six
# frozen NV controls, patching cmake/PackageNaming.cmake and cmake/Packaging.cmake
# and verifying its own restore by sha256.
#
# Anchors verified present and unique BEFORE anything ran:
#   cmake/PackageNaming.cmake  elseif(PN_SYSTEM STREQUAL "Linux")      1   NV1
#   cmake/PackageNaming.cmake  set(version "${PN_VERSION}")            1   NV2
#   cmake/PackageNaming.cmake  set(generators "TGZ;STGZ")              1   NV3
#   cmake/Packaging.cmake      CFDAPP_PACKAGE_INCLUDES_GUI             1   NV4
#
# This wrapper keeps its OWN pre-control hashes and re-checks them after the
# script finishes -- a script that restores and verifies with the same broken
# assumption would report success either way.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G7=$ROOT/results/p13/final-qualification/2026-09-24/g7-packaging
L=$G7/logs
cd "$ROOT"
mkdir -p "$L"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

WATCHED="cmake/PackageNaming.cmake cmake/Packaging.cmake tests/cmake/test_package_naming.cmake tests/cmake/test_package_naming_refuses.cmake"
PRE=/tmp/g7_wrapper_pre.sha256
sha256sum $WATCHED > "$PRE"

log=$L/G7_controls.log
{
echo "P13 G7 -- control half"
echo "date: $(date -u +%FT%TZ)"
echo ""
echo "### this wrapper's independent pre-control hashes ###"
sed 's/^/  /' "$PRE"
echo ""
echo "### the fresh G7 artifacts, hashed BEFORE the controls run ###"
echo "  so a control that leaves an artifact behind cannot be mistaken for them:"
find build/pkg build/pkgcli -maxdepth 1 -name 'CFDApp-*' -printf '  %p\n' 2>/dev/null | sort
sha256sum $(find build/pkg build/pkgcli -maxdepth 1 -name 'CFDApp-*' 2>/dev/null | sort) > /tmp/g7_artifacts.sha256 2>/dev/null || true
echo ""

echo "############ controls.sh -- NV1..NV6 ############"
bash results/p13/packaging/tools/controls.sh 2>&1 | sed 's/^/  /'
echo "  controls.sh rc=${PIPESTATUS[0]}"
echo ""

echo "### FINAL wrapper restore check -- the watched cmake files ###"
sha256sum -c "$PRE" 2>&1 | sed 's/^/  /'
echo ""
echo "### control markers left behind? ###"
grep -rn 'NV[0-9] CONTROL' cmake/ tests/ 2>/dev/null | sed 's/^/  /' || echo "  none -- clean"
echo ""
echo "### Step 11 -- filesystem hygiene after the controls ###"
echo "  repo-root package strays:"
find . -maxdepth 1 -name 'CFDApp-*' -printf '    STRAY: %p\n' 2>/dev/null || true
[ -z "$(find . -maxdepth 1 -name 'CFDApp-*' 2>/dev/null)" ] && echo "    none"
echo "  control build trees left behind (expected: they are scratch, under build/):"
ls -d build/pkgctl* 2>/dev/null | sed 's/^/    /' || echo "    none"
echo "  the four G7 artifacts, re-hashed -- must be UNCHANGED by the controls:"
sha256sum -c /tmp/g7_artifacts.sha256 2>&1 | sed 's/^/    /' || echo "    (no artifact list)"
echo "  every CFDApp-* archive now under build/, with mtime:"
find build -maxdepth 2 -name 'CFDApp-*' -printf '    %TY-%Tm-%Td_%TH:%TM  %10s  %p\n' 2>/dev/null | sort
echo ""
echo "### the tree is back: re-measure the naming contract at baseline ###"
bash results/p13/packaging/tools/run_naming_tests.sh 2>&1 | grep -E 'all expectations met|exit status' | sed 's/^/  /'
ctest --test-dir build/final -R 'CFDAppPackageNaming' 2>&1 | grep -E 'tests passed|tests failed' | sed 's/^/  /'
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
