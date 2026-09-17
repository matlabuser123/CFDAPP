#!/usr/bin/env bash
# UC-001: build and run the grid sweep, which needs the validation-only Poiseuille utils too.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uc-001
W=$HOME/uc001; mkdir -p "$W" "$P/logs"
INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include -I$R/tests/integration/poiseuille"
LIB=$R/build/release/src/libcfdcore.a
c++ -std=c++20 -O3 -DNDEBUG $INC "$P/tools/uc001_grids.cpp" \
  "$R/tests/integration/poiseuille/PoiseuilleValidationUtils.cpp" "$LIB" -o "$W/uc001_grids" || exit 1
cd "$R" || exit 1
{
  echo "# UC-001 grid sweep (libcfdcore.a sha256 $(sha256sum $LIB | cut -d' ' -f1))"
  echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  "$W/uc001_grids"
  echo "exit $?"
} > "$P/logs/$1" 2>&1
cat "$P/logs/$1"
