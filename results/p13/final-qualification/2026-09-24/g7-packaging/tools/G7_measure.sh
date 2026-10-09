#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G7, the measurement half.
#
# The frozen instruments are run UNMODIFIED:
#   results/p13/packaging/tools/run_naming_tests.sh   the pure-CMake naming contract
#   results/p13/packaging/tools/verify_packages.sh    N, G, C, P measured end to end
#
# verify_packages.sh writes into results/p13/packaging/logs/ -- the COMMITTED
# P13-006 evidence (logs 10..16). Those are copied into this gate's tree FIRST,
# so the 2026-09-21 originals survive.
#
# results/p13/packaging/tools/final.sh is deliberately NOT run whole: beyond the
# CFDAppPackageNaming ctests it is the three-configuration full regression, which
# is G10's gate. The naming ctests are run here on their own.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G7=$ROOT/results/p13/final-qualification/2026-09-24/g7-packaging
L=$G7/logs
P13=$ROOT/results/p13/packaging/logs
cd "$ROOT"
mkdir -p "$L"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

log=$L/G7_measure.log
{
echo "P13 G7 -- packaging, measurement half"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### preserving the committed P13-006 logs before the frozen script rewrites them ###"
for f in 10_configure_gui 11_build_gui 12_configure_cli 13_build_cli \
         14_cpack_gui 15_cpack_cli 16_packaged_run; do
  [ -f "$P13/$f.log" ] && cp "$P13/$f.log" "$L/PRESERVED_2026-09-21_$f.log"
done
ls -1 "$L" | grep -c PRESERVED | sed 's/^/  preserved files: /'
echo ""

echo "### Step 11 (part 1) -- what package artifacts existed BEFORE G7 ###"
echo "  so a stale archive cannot later be mistaken for the one G7 qualified:"
find build -maxdepth 2 -name 'CFDApp-*' -printf '    %TY-%Tm-%Td_%TH:%TM  %10s  %p\n' 2>/dev/null | sort || true
echo "  (count: $(find build -maxdepth 2 -name 'CFDApp-*' 2>/dev/null | wc -l))"
echo "  removing them, so every artifact below is provably produced by this run:"
find build -maxdepth 2 -name 'CFDApp-*' -delete 2>/dev/null || true
echo "  remaining: $(find build -maxdepth 2 -name 'CFDApp-*' 2>/dev/null | wc -l)"
echo ""

echo "### Step 2 -- platform naming, the pure-CMake contract (incl. Windows derivation) ###"
bash results/p13/packaging/tools/run_naming_tests.sh 2>&1 | sed 's/^/  /'
echo ""

echo "### Step 2 -- the naming contract as registered ctests ###"
ctest --test-dir build/final -R 'CFDAppPackageNaming' --output-on-failure 2>&1 | tail -12 | sed 's/^/  /'
echo ""

echo "### Steps 3-7 -- the frozen end-to-end verifier ###"
bash results/p13/packaging/tools/verify_packages.sh 2>&1 | sed 's/^/  /'
echo ""

echo "### Step 5 -- exactly which artifacts THIS run produced ###"
for p in $(find build/pkg build/pkgcli -maxdepth 1 -name 'CFDApp-*' 2>/dev/null | sort); do
  echo "  $p"
  echo "      size      : $(stat -c%s "$p") bytes"
  echo "      mtime     : $(stat -c%y "$p")"
  echo "      sha256    : $(sha256sum "$p" | cut -d' ' -f1)"
done
echo "  generator / version / platform as CPack resolved them:"
grep -aE 'CPACK_GENERATOR|CPACK_PACKAGE_VERSION|CPACK_PACKAGE_FILE_NAME|CPACK_SYSTEM_NAME' \
  build/pkg/CPackConfig.cmake 2>/dev/null | head -8 | sed 's/^/    /'
echo "  arch of the host that produced them: $(uname -m)   kernel: $(uname -s)"
echo ""

echo "### Step 3 -- one version source drives filename AND executable ###"
echo "  project() version in CMakeLists.txt:"
grep -aE 'project\(|VERSION' CMakeLists.txt | head -4 | sed 's/^/    /'
echo "  CFDAPP_VERSION_SUFFIX:"
grep -a 'CFDAPP_VERSION_SUFFIX' CMakeLists.txt | head -3 | sed 's/^/    /'
echo "  build/pkg/apps/cli/cfdapp --version : $(build/pkg/apps/cli/cfdapp --version 2>&1)"
echo "  CPACK_PACKAGE_VERSION               : $(grep -a 'CPACK_PACKAGE_VERSION ' build/pkg/CPackConfig.cmake 2>/dev/null | head -1)"
echo ""

echo "### Step 7 -- the packaged binary's real type, not its filename ###"
GUIPKG=$(find build/pkg -maxdepth 1 -name 'CFDApp-*.tar.gz' | head -1)
X=/tmp/g7_type; rm -rf $X; mkdir -p $X
tar xzf "$GUIPKG" -C $X
for b in $(find $X -type f -name 'cfdapp*'); do
  printf '    %-28s %s\n' "$(basename "$b")" "$(file -b "$b" | cut -c1-84)"
done
echo "  package name says : $(basename "$GUIPKG")"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
