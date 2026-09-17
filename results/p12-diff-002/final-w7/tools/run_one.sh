#!/usr/bin/env bash
# Run one test binary with a gtest filter and record the full output.
# Usage: run_one.sh <binary-name> <gtest-filter> <log-name>
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/final-w7
mkdir -p "$P/logs"
cd "$R" || exit 1
BIN=$(find build/release -type f -name "$1" 2>/dev/null | head -1)
if [ -z "$BIN" ]; then echo "NOT FOUND: $1"; exit 1; fi
chmod +x "$BIN"
{
  echo "# binary $BIN"
  echo "# sha256 $(sha256sum "$BIN" | cut -d' ' -f1)"
  echo "# libcfdcore.a $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  ./"$BIN" --gtest_filter="$2"
  echo "exit $?"
} > "$P/logs/$3" 2>&1
cat "$P/logs/$3"
