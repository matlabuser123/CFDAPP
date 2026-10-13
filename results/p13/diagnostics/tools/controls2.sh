#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
BK=/tmp/p13_diag_controls2; rm -rf $BK; mkdir -p $BK
F=src/core/Logger.cpp
cp $F $BK/Logger.cpp; sha256sum $F > $BK/pre.sha256

echo "########## C2b on the intact tree (must be 0)"
cmake --build build/final -j 12 >/dev/null 2>&1
bash results/p13/diagnostics/tools/verify_c2b.sh

echo
echo "########## N1b -- route Info/Debug/Trace back to stdout; C2b must break"
python3 - <<'PY'
import io
p='src/core/Logger.cpp'; s=io.open(p,encoding='utf-8',newline='').read()
o='bool targetsStderr(LogLevel) noexcept { return true; }'
assert s.count(o)==1, "anchor missing"
s=s.replace(o,'bool targetsStderr(LogLevel level) noexcept {\n  return level == LogLevel::Warning || level == LogLevel::Error;\n}',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
cmake --build build/final -j 12 >/dev/null 2>&1 || echo "BUILD FAILED"
bash results/p13/diagnostics/tools/verify_c2b.sh

cp $BK/Logger.cpp $F
sha256sum -c $BK/pre.sha256 --quiet && echo "RESTORED (sha256 verified)" || { echo "*** RESTORE FAILED ***"; exit 9; }
cmake --build build/final -j 12 >/dev/null 2>&1 && echo "restored tree builds"
bash results/p13/diagnostics/tools/verify_c2b.sh
