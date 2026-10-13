#!/usr/bin/env bash
# P13-006 audit, part 2 -- what the configuration actually PRODUCES here.
# Reading the file says what it intends; this says what it does.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
R=results/p13/packaging

echo "=== A7: build the packaging-enabled tree ==="
cmake --build build/pkg -j 12 > $R/logs/02_build_pkg.log 2>&1
echo "  build exit status: $?"
tail -2 $R/logs/02_build_pkg.log | sed 's/^/  /'

echo
echo "=== A8: what does `cmake --install` put in the tree? ==="
rm -rf /tmp/p13_pkg_install
cmake --install build/pkg --prefix /tmp/p13_pkg_install > $R/logs/03_install.log 2>&1
echo "  install exit status: $?"
echo "  files installed: $(find /tmp/p13_pkg_install -type f | wc -l)"
echo "  total size     : $(du -sh /tmp/p13_pkg_install 2>/dev/null | cut -f1)"
echo "  top-level      : $(ls /tmp/p13_pkg_install | tr '\n' ' ')"
echo "  --- bin/ ---"
ls /tmp/p13_pkg_install/bin 2>/dev/null | head -30 | sed 's/^/    /'
echo "  bin/ entries: $(ls /tmp/p13_pkg_install/bin 2>/dev/null | wc -l)"
echo "  --- build-only artifacts that leaked in ---"
find /tmp/p13_pkg_install -name 'CMakeFiles' -o -name '*.o' -o -name '*.a' -o -name '*autogen*' \
     -o -name 'CMakeCache.txt' -o -name '*Tests' -o -name 'compile_commands.json' 2>/dev/null \
  | head -20 | sed 's/^/    /'

echo
echo "=== A9: what does cpack produce, and what does it call it? ==="
( cd build/pkg && cpack ) > $R/logs/04_cpack.log 2>&1
echo "  cpack exit status: $?"
grep -a "CPack: \|Error\|error" $R/logs/04_cpack.log | head -12 | sed 's/^/  /'
echo "  --- artifacts ---"
ls -la build/pkg/*.zip build/pkg/*.exe build/pkg/*.tar.gz build/pkg/*.sh 2>/dev/null | sed 's/^/  /'

echo
echo "=== A10: does the installed CLI run a case from the installed examples? ==="
if [ -x /tmp/p13_pkg_install/bin/cfdapp ]; then
  W=/tmp/p13_pkg_case; rm -rf $W
  EX=$(ls -d /tmp/p13_pkg_install/examples/*/ 2>/dev/null | head -1)
  echo "  example used: $EX"
  if [ -n "$EX" ]; then
    cp -r "$EX" $W
    ( cd /tmp && /tmp/p13_pkg_install/bin/cfdapp --case $W > $R/logs/05_installed_run.log 2>&1 )
    echo "  installed cfdapp exit status: $?"
    head -6 $R/logs/05_installed_run.log | sed 's/^/    /'
  else
    echo "  NO examples/ directory installed"
  fi
else
  echo "  /tmp/p13_pkg_install/bin/cfdapp is not there"
fi

echo
echo "=== A11: version agreement ==="
grep -n "project(CFDApp" -A 3 CMakeLists.txt | head -5 | sed 's/^/  /'
/tmp/p13_pkg_install/bin/cfdapp --version 2>/dev/null | sed 's/^/  cfdapp --version: /'
grep -a "CPACK_PACKAGE_FILE_NAME\|CPACK_PACKAGE_VERSION " build/pkg/CPackConfig.cmake | sed 's/^/  /'
