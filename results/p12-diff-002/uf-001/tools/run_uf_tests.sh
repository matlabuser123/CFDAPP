#!/usr/bin/env bash
# UF-001.13: build and run the natural-convection validation suite.
# Usage: run_uf_tests.sh <log-name> [gtest-filter]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uf-001
mkdir -p "$P/logs"
cd "$R" || exit 1
TARGET=CFDNaturalConvectionValidationTests
BIN=$(find build/release -type f -name "$TARGET" 2>/dev/null | head -1)
if [ -z "$BIN" ]; then
  # Discover the target that owns the natural-convection test.
  TARGET=$(grep -rl "test_natural_convection_validation.cpp" "$R"/tests --include=CMakeLists.txt \
           | head -1 | xargs -r dirname)
  TARGET=$(grep -oP 'add_executable\(\K[A-Za-z0-9_]+' "$TARGET/CMakeLists.txt" | head -1)
fi
cmake --build build/release --target "$TARGET" -j"$(nproc)" > "$P/logs/$1.build" 2>&1 || {
  echo "BUILD FAILED"; tail -40 "$P/logs/$1.build"; exit 1; }
BIN=$(find build/release -type f -name "$TARGET" 2>/dev/null | head -1)
chmod +x "$BIN"
FILTER=${2:-NaturalConvectionValidation.*}
{
  echo "# UF-001.13 $TARGET; filter $FILTER"
  echo "# libcfdcore.a $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# test binary  $(sha256sum "$BIN" | cut -d' ' -f1)"
  echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  ./"$BIN" --gtest_filter="$FILTER"
  echo "exit $?"
} > "$P/logs/$1" 2>&1
grep -E "^\[ RUN|^\[       OK|^\[  FAILED|^\[==========|^\[  PASSED|de Vahl Davis Ra|Failure|exit " "$P/logs/$1" | head -60
