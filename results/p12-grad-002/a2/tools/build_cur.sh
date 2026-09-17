#!/usr/bin/env bash
# P12-GRAD-002 A2 -- "cur": the same isolated copy as build_nograd.sh but with the repository's own
# Gradient.cpp, built identically (Release, GUI off, same flags, same dependency sources). cur and
# nograd differ ONLY in src/discretization/Gradient.cpp, so every difference between their test
# outputs and generated files is GRAD-002's.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
T=$HOME/g2/cur
LOG=$P/logs/00_build_cur.log
mkdir -p "$T" "$P/logs"
{
  echo "# cur build $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(cd $R && git rev-parse HEAD)"
  rsync -a --delete --exclude build --exclude results --exclude .git --exclude '__pycache__' \
    --exclude '.venv' "$R/" "$T/src_tree/" || { echo "rsync failed"; exit 1; }
  echo "files differing from the repo under src/ include/:"
  (cd "$T/src_tree" && for f in $(find src include -type f); do cmp -s "$f" "$R/$f" || echo "  $f"; done)
  D=$R/build/release/_deps
  cmake -S "$T/src_tree" -B "$T/build" -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$D/googletest-src" \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$D/nlohmann_json-src" \
    > "$T/configure.txt" 2>&1 || { tail -20 "$T/configure.txt"; echo "configure failed"; exit 1; }
  nice -n 15 cmake --build "$T/build" -j8 > "$T/build.txt" 2>&1 || { tail -30 "$T/build.txt"; echo "build failed"; exit 1; }
  echo "build OK, warnings $(grep -c 'warning:' "$T/build.txt")"
  echo "cur libcfdcore.a $(sha256sum "$T/build/src/libcfdcore.a" | cut -d' ' -f1)  (build/release: $(sha256sum $R/build/release/src/libcfdcore.a | cut -d' ' -f1))"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$LOG" 2>&1
cat "$LOG"
