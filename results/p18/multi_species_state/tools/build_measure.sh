#!/usr/bin/env bash
# Builds the P18.1 measurement probe against the SAME cfdcore the tests use.
# Compiled out-of-tree on purpose: the probe is instrumentation and must not
# enter any CMake target or the shipped library.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1

rm -f build/final/p18_1_measure
g++ -std=c++20 -O2 -I include \
  results/p18/multi_species_state/tools/p18_1_measure.cpp \
  -o build/final/p18_1_measure \
  -L build/final/src -lcfdcore 2>&1 | tail -20

if [ ! -x build/final/p18_1_measure ]; then
  echo "PROBE BUILD FAILED"
  exit 1
fi
echo "probe built: $(sha256sum build/final/p18_1_measure | awk '{print $1}')"
