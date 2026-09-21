#!/usr/bin/env bash
# GPU-DISC-001R Phase C -- a genuinely fresh production build.
#
# A NEW directory, configured from scratch. `build/cuda` is left alone rather
# than deleted: isolating the previous build is what the brief allows, and it
# keeps a fallback if the fresh configure turns out to need a flag nobody wrote
# down. Everything from Phase D onward tests THIS build.
#
# The CUDA compiler is pinned explicitly. /usr/bin/nvcc is the apt CUDA 11.5,
# which cannot compile this project with gcc 11 (std_function parameter-pack
# error) -- found the hard way in GPU-DISC-001Q. The production build/cuda pins
# 12.9 in its cache; a from-scratch configure has to pin it on the command line.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/clean-build
BUILD=$ROOT/build/final
cd "$ROOT"
mkdir -p "$EVID"

echo "=== toolchain ==="
/usr/bin/c++ --version | head -1
/usr/local/cuda-12.9/bin/nvcc --version | tail -2
cmake --version | head -1
ninja --version

echo ""
echo "=== remove any previous fresh build (isolation, not reuse) ==="
rm -rf "$BUILD"
echo "  removed $BUILD"

echo ""
echo "=== configure command ==="
CONFIGURE=(cmake -S "$ROOT" -B "$BUILD" -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCFDAPP_ENABLE_CUDA=ON
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.9/bin/nvcc
  -DBUILD_TESTING=ON)
printf '  %s\n' "${CONFIGURE[*]}"
"${CONFIGURE[@]}" > "$EVID/configure.log" 2>&1
rc=$?
echo "  configure rc=$rc"
grep -iE "architectures|cfdcuda:|CUDA [0-9]|Found CUDA|error" "$EVID/configure.log" | head -10 | sed 's/^/  /'
[ $rc -ne 0 ] && { tail -30 "$EVID/configure.log"; exit 1; }

echo ""
echo "=== build ==="
echo "  ninja -C $BUILD"
/usr/bin/time -f "  wall %e s, max RSS %M KB" ninja -C "$BUILD" > "$EVID/build.log" 2>&1
rc=$?
tail -1 "$EVID/build.log"
echo "  build rc=$rc"
if [ $rc -ne 0 ]; then
  grep -vE '^\[[0-9]+/[0-9]+\]' "$EVID/build.log" | tail -40
  exit 1
fi

echo ""
echo "=== warnings ==="
grep -iE "warning:" "$EVID/build.log" > "$EVID/warnings.log" 2>&1
echo "  warning lines: $(wc -l < "$EVID/warnings.log")"
sort "$EVID/warnings.log" | sed 's/.*warning: //' | sort | uniq -c | sort -rn | head -10 | sed 's/^/  /'

echo ""
echo "=== errors ==="
grep -iE "^FAILED|error:" "$EVID/build.log" | head -10 | sed 's/^/  /' || echo "  none"

echo ""
echo "=== targets ==="
ninja -C "$BUILD" -t targets all > "$EVID/targets.txt" 2>&1
echo "  build edges: $(wc -l < "$EVID/targets.txt")"
ninja -C "$BUILD" -t targets 2>/dev/null | grep -cE "phony" | xargs -I{} echo "  phony targets: {}"
ls "$BUILD"/src/*.a "$BUILD"/cuda/*.a 2>/dev/null | sed 's/^/  /'

echo ""
echo "=== CUDA architectures actually compiled ==="
grep -oE "\-\-generate-code=arch=compute_[0-9]+,code=\[?[a-z_0-9,]+\]?" "$BUILD/build.ninja" \
  | sort -u | sed 's/^/  /'

echo ""
echo "=== freshness: an immediate rebuild must do no work ==="
ninja -C "$BUILD" 2>&1 | tail -1 | sed 's/^/  /'

echo ""
echo "=== build identity ==="
sha256sum "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" | sed 's/^/  /'

echo ""
echo "=== test discovery ==="
ctest --test-dir "$BUILD" -N 2>/dev/null | tail -2 | sed 's/^/  /'

echo ""
echo "CLEAN BUILD: PASS"
