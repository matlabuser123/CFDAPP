#!/usr/bin/env bash
set -eu
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
clang-format --style=file -i apps/cli/main.cpp src/app/ProjectRunner.cpp src/core/Logger.cpp
for f in apps/cli/main.cpp src/app/ProjectRunner.cpp src/core/Logger.cpp; do
  clang-format --style=file "$f" | diff -q - "$f" >/dev/null && echo "OK $f" || echo "STILL BAD $f"
done
