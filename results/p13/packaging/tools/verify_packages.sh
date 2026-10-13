#!/usr/bin/env bash
# P13-006 -- groups N, G, C and P, measured end to end.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $ROOT
R=$ROOT/results/p13/packaging
FAIL=0
ok()  { echo "  $1: PASS  $2"; }
bad() { echo "  $1: FAIL  $2"; FAIL=$((FAIL+1)); }

echo "=== configure + build: GUI package ==="
rm -rf build/pkg
cmake -S . -B build/pkg -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCFDAPP_BUILD_GUI=ON -DCFDAPP_ENABLE_PACKAGING=ON -DCFDAPP_ENABLE_CUDA=OFF \
  > $R/logs/10_configure_gui.log 2>&1 || { echo "  CONFIGURE FAILED"; tail -5 $R/logs/10_configure_gui.log; exit 1; }
grep -a "CFDApp package:" $R/logs/10_configure_gui.log | sed 's/^/  /'
cmake --build build/pkg -j 12 > $R/logs/11_build_gui.log 2>&1 || { echo "  BUILD FAILED"; tail -5 $R/logs/11_build_gui.log; exit 1; }

echo
echo "=== configure + build: CLI-only package (criterion C1) ==="
rm -rf build/pkgcli
cmake -S . -B build/pkgcli -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCFDAPP_BUILD_GUI=OFF -DCFDAPP_ENABLE_PACKAGING=ON -DCFDAPP_ENABLE_CUDA=OFF \
  > $R/logs/12_configure_cli.log 2>&1 || { echo "  CONFIGURE FAILED"; tail -5 $R/logs/12_configure_cli.log; exit 1; }
grep -a "CFDApp package:" $R/logs/12_configure_cli.log | sed 's/^/  /'
if [ -f build/pkgcli/CPackConfig.cmake ]; then ok C1a "CPackConfig.cmake produced"; else bad C1a "no CPackConfig.cmake"; fi
cmake --build build/pkgcli -j 12 > $R/logs/13_build_cli.log 2>&1 || { echo "  BUILD FAILED"; tail -5 $R/logs/13_build_cli.log; exit 1; }

echo
echo "=== G1: cpack exit status ==="
( cd build/pkg && cpack ) > $R/logs/14_cpack_gui.log 2>&1
rc=$?;  [ $rc -eq 0 ] && ok G1a "GUI cpack exit 0" || bad G1a "GUI cpack exit $rc"
( cd build/pkgcli && cpack ) > $R/logs/15_cpack_cli.log 2>&1
rc=$?;  [ $rc -eq 0 ] && ok C1b "CLI cpack exit 0" || bad C1b "CLI cpack exit $rc"
grep -a "NSIS" $R/logs/14_cpack_gui.log | head -3 | sed 's/^/  (G2) /'
[ -z "$(grep -a NSIS $R/logs/14_cpack_gui.log)" ] && ok G2 "NSIS never mentioned on Linux" || bad G2 "NSIS referenced"

echo
echo "=== N1/N3: what the artifacts are called ==="
ls build/pkg/CFDApp-* build/pkgcli/CFDApp-* 2>/dev/null | sed 's/^/  /'
GUIPKG=$(ls build/pkg/CFDApp-*.tar.gz 2>/dev/null | head -1)
case "$(basename "${GUIPKG:-none}")" in
  *Windows*) bad N1 "a Linux build is still named Windows: $(basename "$GUIPKG")" ;;
  *Linux-x64*) ok N1 "$(basename "$GUIPKG")" ;;
  *) bad N1 "unexpected name: $(basename "${GUIPKG:-none}")" ;;
esac
BINVER=$(build/pkg/apps/cli/cfdapp --version | awk '{print $2}')
case "$(basename "${GUIPKG:-none}")" in
  *"$BINVER"*) ok N3 "package name carries the binary's version $BINVER" ;;
  *) bad N3 "binary says $BINVER, package is $(basename "${GUIPKG:-none}")" ;;
esac

echo
echo "=== N4: the packaged binary matches the platform in the name ==="
rm -rf /tmp/p13_pkg_x; mkdir -p /tmp/p13_pkg_x
tar xzf "$GUIPKG" -C /tmp/p13_pkg_x
PB=$(find /tmp/p13_pkg_x -name cfdapp -type f | head -1)
TYPE=$(file -b "$PB")
echo "  $TYPE" | cut -c1-80 | sed 's/^/  /'
case "$TYPE" in
  *"GNU/Linux"*) ok N4 "ELF/Linux inside a Linux-named package" ;;
  *) bad N4 "$TYPE" ;;
esac

echo
echo "=== C2/C3: what is in each package ==="
rm -rf /tmp/p13_cli_x; mkdir -p /tmp/p13_cli_x
CLIPKG=$(ls build/pkgcli/CFDApp-*.tar.gz 2>/dev/null | head -1)
tar xzf "$CLIPKG" -C /tmp/p13_cli_x
echo "  GUI package bin/: $(ls /tmp/p13_pkg_x/*/bin | tr '\n' ' ')"
echo "  CLI package bin/: $(ls /tmp/p13_cli_x/*/bin | tr '\n' ' ')"
ls /tmp/p13_pkg_x/*/bin | grep -q cfdapp_gui && ok C3 "GUI package has both executables" || bad C3 "cfdapp_gui missing"
if ls /tmp/p13_cli_x/*/bin | grep -q cfdapp_gui; then bad C2a "CLI package contains cfdapp_gui"; else ok C2a "CLI package has no cfdapp_gui"; fi
for d in examples docs licenses; do
  [ -d "$(ls -d /tmp/p13_cli_x/*/ | head -1)$d" ] && ok "C2-$d" "present in the CLI package" || bad "C2-$d" "missing"
done

echo
echo "=== P1: install tree cleanliness ==="
for b in build/pkg build/pkgcli; do
  P=/tmp/p13_inst_$(basename $b); rm -rf $P
  cmake --install $b --prefix $P > /dev/null 2>&1
  JUNK=$(find $P \( -name 'CMakeFiles' -o -name '*.o' -o -name '*.a' -o -name '*autogen*' \
        -o -name 'CMakeCache.txt' -o -name '*Tests' -o -name 'compile_commands.json' \) | wc -l)
  echo "  $b -> $(find $P -type f | wc -l) files, bin/: $(ls $P/bin | tr '\n' ' ')"
  [ "$JUNK" -eq 0 ] && ok "P1-$(basename $b)" "no build-only artifacts" || bad "P1-$(basename $b)" "$JUNK leaked"
done

echo
echo "=== P2: the PACKAGED cli runs a PACKAGED example ==="
W=/tmp/p13_pkg_run; rm -rf $W
EX=$(ls -d /tmp/p13_cli_x/*/examples/*/ | head -1)
cp -r "$EX" $W
CLIB=$(find /tmp/p13_cli_x -name cfdapp -type f | head -1)
chmod +x "$CLIB"
( cd /tmp && "$CLIB" --case $W > $R/logs/16_packaged_run.log 2>&1 )
rc=$?; [ $rc -eq 0 ] && ok P2 "packaged cfdapp ran $(basename $EX), exit 0" || bad P2 "exit $rc"
grep -a "Converged\|Case:" $R/logs/16_packaged_run.log | head -2 | sed 's/^/    /'

echo
echo "FAILURES: $FAIL"
