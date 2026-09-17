#!/usr/bin/env bash
# UF-001.10: build a DELIBERATELY CORRUPTED library in an isolated copy outside the repo, for the
# non-vacuity controls. Usage: build_control.sh <farx2|signflip|nofar>
# The authoritative tree is never written to.
set -e
MODE=$1
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uf-001
B=$HOME/uf001_ctrl_$MODE
mkdir -p "$P/logs" "$P/data"

rm -rf "$B"; mkdir -p "$B"
rsync -a --exclude /build --exclude /results --exclude /.git "$R"/ "$B"/ > /dev/null

python3 - "$B/src/discretization/NonOrthogonalDiffusion.cpp" "$MODE" <<'PY'
import sys
path, mode = sys.argv[1], sys.argv[2]
src = open(path, encoding='utf-8').read()
old = '      const Real cF = h1 / (h2 * (h2 - h1));'
assert old in src, "anchor not found"
if mode == 'farx2':
    new = ('      const Real cF = 2.0 * (h1 / (h2 * (h2 - h1)));'
           '  // UF-001 CONTROL: far-cell coefficient DOUBLED')
elif mode == 'signflip':
    new = ('      const Real cF = -(h1 / (h2 * (h2 - h1)));'
           '  // UF-001 CONTROL: far-cell coefficient SIGN FLIPPED')
elif mode == 'nofar':
    new = ('      const Real cF = 0.0 * (h1 / (h2 * (h2 - h1)));'
           '  // UF-001 CONTROL: far-cell term DROPPED, cP kept')
else:
    raise SystemExit("unknown mode " + mode)
src = src.replace(old, new, 1)
open(path, 'w', encoding='utf-8', newline='').write(src)
print("control %s applied" % mode)
PY

cd "$B"
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=OFF \
      > "$P/logs/10_ctrl_${MODE}_configure.log" 2>&1
cmake --build build/release --target cfdcore -j"$(nproc)" \
      > "$P/logs/10_ctrl_${MODE}_build.log" 2>&1
LIB=$(find "$B/build" -name libcfdcore.a | head -1)
echo "$LIB" > "$P/data/ctrl_${MODE}_lib_path.txt"
echo "control $MODE library: $LIB"
sha256sum "$LIB"
diff -u "$R/src/discretization/NonOrthogonalDiffusion.cpp" \
        "$B/src/discretization/NonOrthogonalDiffusion.cpp" | tail -12 || true
