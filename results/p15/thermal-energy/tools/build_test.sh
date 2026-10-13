#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i include/cfd/thermal/Enthalpy.hpp include/cfd/thermal/TemperatureDependentProperty.hpp src/thermal/Enthalpy.cpp src/thermal/TemperatureDependentProperty.cpp tests/unit/thermal/test_enthalpy_properties.cpp
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final --target CFDThermalTests -j 12 2>&1 | grep -E "error:|FAILED" | head -15
cmake --build build/final --target CFDThermalTests -j 12 2>&1 | tail -1
./build/final/tests/unit/thermal/CFDThermalTests --gtest_filter='TemperatureProperty.*:SensibleEnthalpyTest.*' 2>&1 | tail -12
