#!/usr/bin/env bash
for t in /root/uf001_baseline /root/uf001_ctrl_farx2 /root/uf001_ctrl_signflip; do
  echo "== $t"
  ls "$t" | head -20
  ls "$t/build/release" 2>/dev/null | head -20
  ls "$t/build/release/tests" 2>/dev/null | head
  [ -f "$t/build/release/CMakeCache.txt" ] && grep -E "^CMAKE_BUILD_TYPE|^CFD_BUILD_TESTS|^BUILD_TESTING" "$t/build/release/CMakeCache.txt"
  sha256sum "$t/build/release/src/libcfdcore.a" 2>/dev/null
  ls "$t/tests/integration/case" 2>/dev/null | head
done
