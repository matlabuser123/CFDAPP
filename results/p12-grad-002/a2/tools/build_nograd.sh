#!/usr/bin/env bash
# P12-GRAD-002 A2 -- the attribution control "current tree minus GRAD-002" (nograd): a copy of the
# repository sources with ONLY src/discretization/Gradient.cpp replaced by the pre-GRAD-002
# (pre-GRAD-001, pre-MESH-007) version from /root/m7ref/base (library eaadaa63...). Every other
# production file -- DIFF-002, MESH-007, formatting -- is the current tree's, so a difference between
# this library and build/release is GRAD-002's alone. Release, GUI off, all tests built.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
T=$HOME/g2/nograd
LOG=$P/logs/00_build_nograd.log
mkdir -p "$T" "$P/logs"
{
  echo "# nograd build $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(cd $R && git rev-parse HEAD)"
  rsync -a --delete --exclude build --exclude results --exclude .git --exclude '__pycache__' \
    --exclude '.venv' "$R/" "$T/src_tree/" || { echo "rsync failed"; exit 1; }
  cp /root/m7ref/base/src/discretization/Gradient.cpp "$T/src_tree/src/discretization/Gradient.cpp"
  echo "Gradient.cpp (pre-GRAD-002, from base): $(sha256sum "$T/src_tree/src/discretization/Gradient.cpp" | cut -d' ' -f1)"
  echo "Gradient.cpp (current repo):            $(sha256sum "$R/src/discretization/Gradient.cpp" | cut -d' ' -f1)"
  echo "files differing from the repo under src/ include/:"
  (cd "$T/src_tree" && for f in $(find src include -type f); do cmp -s "$f" "$R/$f" || echo "  $f"; done)
  D=$R/build/release/_deps
  cmake -S "$T/src_tree" -B "$T/build" -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$(ls -d $D/googletest-src 2>/dev/null || ls -d $D/*googletest* | head -1)" \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$(ls -d $D/nlohmann_json-src 2>/dev/null || ls -d $D/*json* | head -1)" \
    > "$T/configure.txt" 2>&1 || { tail -20 "$T/configure.txt"; echo "configure failed"; exit 1; }
  nice -n 15 cmake --build "$T/build" -j8 > "$T/build.txt" 2>&1 || { tail -30 "$T/build.txt"; echo "build failed"; exit 1; }
  echo "build OK, warnings $(grep -c 'warning:' "$T/build.txt")"
  echo "nograd libcfdcore.a $(sha256sum "$T/build/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$LOG" 2>&1
cat "$LOG"
