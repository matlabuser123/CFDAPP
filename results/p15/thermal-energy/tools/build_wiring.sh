#!/usr/bin/env bash
# P15 -- build after wiring the property models through to the solver.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i \
  include/cfd/thermal/TemperatureDependentProperty.hpp \
  src/thermal/TemperatureDependentProperty.cpp \
  include/cfd/io/SimulationSetup.hpp \
  src/io/CaseBuilder.cpp \
  src/app/ProjectRunner.cpp
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final --target cfdcore -j 16 2>&1 | grep -E "error:|FAILED" | head -30
echo "--- core done ---"
cmake --build build/final --target CFDIoTests CFDThermalTests -j 16 2>&1 | grep -E "error:|FAILED" | head -30
echo "--- tests built ---"
