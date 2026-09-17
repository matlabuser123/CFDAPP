#!/usr/bin/env bash
# UF-001: build and run one investigation program. Usage: run.sh <stem> <log-name> [args...]
# Optional LIB env var selects a different libcfdcore.a (the pre-DIFF-002 baseline).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uf-001
W=$HOME/uf001; mkdir -p "$W" "$P/logs" "$P/data"
LIB=${LIB:-$R/build/release/src/libcfdcore.a}
SRCROOT=${SRCROOT:-$R}
INC="-I$SRCROOT/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include -I$R/tests/integration/thermal"
# Name the binary after the LOG, not the source, so concurrent runs of the same probe against
# different libraries cannot overwrite each other's running executable.
BIN=$W/$1.${2%.log}
c++ -std=c++20 -O3 -DNDEBUG $INC "$P/tools/$1.cpp" \
  "$R/tests/integration/thermal/NaturalConvectionValidationUtils.cpp" "$LIB" -o "$BIN" || exit 1
cd "$R" || exit 1
{
  echo "# UF-001 $1"
  echo "# libcfdcore.a $(sha256sum "$LIB" | cut -d' ' -f1)"
  echo "# headers from $SRCROOT"
  echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  "$BIN" "${@:3}"
  echo "exit $?"
} > "$P/logs/$2" 2>&1
tail -70 "$P/logs/$2"
