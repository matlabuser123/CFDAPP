#!/usr/bin/env bash
# P13-006 non-vacuity controls. Each breaks one thing deliberately and requires
# the named criterion to FAIL. Sources restored from a pre-control copy, with
# the restore verified by sha256.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $ROOT
BK=/tmp/p13_pkg_controls; rm -rf $BK; mkdir -p $BK
FILES="cmake/PackageNaming.cmake cmake/Packaging.cmake"
for f in $FILES; do cp "$f" "$BK/$(echo $f | tr / _)"; done
sha256sum $FILES > $BK/pre.sha256
cat $BK/pre.sha256

restore() {
  for f in $FILES; do cp "$BK/$(echo $f | tr / _)" "$f"; done
  sha256sum -c $BK/pre.sha256 --quiet && echo "  RESTORED (sha256 verified)" \
    || { echo "  *** RESTORE FAILED ***"; exit 9; }
}
reconf() { # reconf <builddir> <gui>
  cmake -S . -B "$1" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCFDAPP_BUILD_GUI=$2 -DCFDAPP_ENABLE_PACKAGING=ON -DCFDAPP_ENABLE_CUDA=OFF 2>&1
}
verdict() { [ "$2" = "yes" ] && echo "  $1: DETECTED -- $3" || echo "  $1: *** UNDETECTED *** -- $3"; }

echo
echo "########## NV1 -- hardcode the Windows name again; N1/N4 must break"
python3 - <<'PY'
import io
p='cmake/PackageNaming.cmake'; s=io.open(p,encoding='utf-8',newline='').read()
o='''  elseif(PN_SYSTEM STREQUAL "Linux")
    set(platform "Linux-${arch}")'''
assert s.count(o)==1
s=s.replace(o,'''  elseif(PN_SYSTEM STREQUAL "Linux")
    set(platform "Windows-${arch}")''',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
N=$(reconf build/pkgctl ON | grep -a "CFDApp package:" | head -1)
echo "  $N"
echo "$N" | grep -q "Windows" && verdict NV1 yes "a Linux build is named Windows again" || verdict NV1 no "$N"
echo "  ctest naming test now:"
cmake -DCFDAPP_SOURCE_DIR=$ROOT -P tests/cmake/test_package_naming.cmake >/dev/null 2>&1 \
  && verdict NV1-test no "the naming test still passes" \
  || verdict NV1-test yes "the naming test fails"
restore

echo
echo "########## NV2 -- drop the suffix from the package version; N3 must break"
python3 - <<'PY'
import io
p='cmake/PackageNaming.cmake'; s=io.open(p,encoding='utf-8',newline='').read()
o='''  set(version "${PN_VERSION}")
  if(PN_SUFFIX)
    set(version "${version}-${PN_SUFFIX}")
  endif()'''
assert s.count(o)==1
s=s.replace(o,'  set(version "${PN_VERSION}")',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
N=$(reconf build/pkgctl ON | grep -a "CFDApp package:" | head -1)
echo "  $N"
echo "$N" | grep -q "0.2.0-dev" && verdict NV2 no "$N" || verdict NV2 yes "the package no longer carries the binary's version"
restore

echo
echo "########## NV3 -- force NSIS on Linux; G1 must break"
python3 - <<'PY'
import io
p='cmake/PackageNaming.cmake'; s=io.open(p,encoding='utf-8',newline='').read()
o='    set(generators "TGZ;STGZ")'
assert s.count(o)==1
s=s.replace(o,'    set(generators "TGZ;NSIS")',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
reconf build/pkgctl ON > /dev/null 2>&1
cmake --build build/pkgctl -j 12 >/dev/null 2>&1
( cd build/pkgctl && cpack ) > /tmp/p13_nv3.log 2>&1
rc=$?
echo "  cpack exit status: $rc"
grep -a "NSIS" /tmp/p13_nv3.log | head -2 | sed 's/^/    /'
[ $rc -ne 0 ] && verdict NV3 yes "cpack exits $rc" || verdict NV3 no "cpack still exits 0"
restore

echo
echo "########## NV4 -- restore the GUI-only guard; C1 must break"
python3 - <<'PY'
import io
p='cmake/Packaging.cmake'; s=io.open(p,encoding='utf-8',newline='').read()
o='set(CFDAPP_PACKAGE_INCLUDES_GUI ${CFDAPP_BUILD_GUI})'
assert s.count(o)==1
s=s.replace(o,'''if(NOT CFDAPP_BUILD_GUI)
  message(WARNING "packaging is only meaningful for the GUI build. Skipping.")
  return()
endif()
set(CFDAPP_PACKAGE_INCLUDES_GUI ${CFDAPP_BUILD_GUI})''',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
rm -rf build/pkgctl2
reconf build/pkgctl2 OFF > /dev/null 2>&1
if [ -f build/pkgctl2/CPackConfig.cmake ]; then verdict NV4 no "CPackConfig.cmake still produced"
else verdict NV4 yes "no CPackConfig.cmake for a CLI-only build"; fi
restore

echo
echo "########## NV5 -- an unknown system must refuse"
cmake -DCFDAPP_SOURCE_DIR=$ROOT -P tests/cmake/test_package_naming_refuses.cmake >/dev/null 2>&1
rc=$?
[ $rc -ne 0 ] && verdict NV5 yes "refused with exit $rc" || verdict NV5 no "invented a name"

echo
echo "########## NV6 -- the cleanliness check must be able to fail"
J=$(find build/pkg \( -name 'CMakeFiles' -o -name '*.o' -o -name 'CMakeCache.txt' \) | wc -l)
echo "  the same check pointed at a BUILD tree finds: $J"
[ "$J" -gt 0 ] && verdict NV6 yes "$J build-only artifacts found" || verdict NV6 no "found none"

echo
echo "########## restored tree reconfigures"
rm -rf build/pkgctl build/pkgctl2
reconf build/pkg ON | grep -a "CFDApp package:" | sed 's/^/  /'
sha256sum -c $BK/pre.sha256 && git status --porcelain -- $FILES
