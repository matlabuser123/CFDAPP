#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i include/cfd/turbulence/KEpsilonModel.hpp \
  include/cfd/turbulence/KEpsilonEquation.hpp include/cfd/turbulence/TurbulenceModel.hpp \
  src/turbulence/KEpsilonModel.cpp src/turbulence/KEpsilonEquation.cpp \
  include/cfd/physics/MomentumEquation.hpp src/physics/MomentumEquation.cpp \
  include/cfd/pressure_velocity/RelaxedMomentum.hpp src/pressure_velocity/RelaxedMomentum.cpp \
  src/pressure_velocity/SIMPLE.cpp
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final --target cfdcore -j 16 2>&1 | grep -E "error:|FAILED" | head -25
echo "--- core done ---"
