#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
clang-format --style=file -i apps/cli/main.cpp src/app/ProjectRunner.cpp src/core/Logger.cpp
bash results/p13/diagnostics/tools/verify_c2_v4.sh
