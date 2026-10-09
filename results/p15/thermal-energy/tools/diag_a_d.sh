#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
./build/final/tests/integration/thermal/CFDThermalValidationTests \
  --gtest_filter='ThermalValidation.CaseASteadyConductionIsLinear:ThermalValidation.CaseDConvection*' 2>&1 \
  | grep -vE "^\[ *OK|^\[----------\]|^\[=" | head -30
