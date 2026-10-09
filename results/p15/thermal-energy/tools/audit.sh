#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "################ 1. THERMAL SOURCES ################"
wc -l src/thermal/*.cpp include/cfd/thermal/*.hpp | tail -20

echo
echo "################ 2. EnergyEquation public surface ################"
grep -n "^\[\[nodiscard\]\]\|^void \|^struct \|^class \|^Real \|^cfd::" include/cfd/thermal/EnergyEquation.hpp | head -25

echo
echo "################ 3. WHAT IS TRANSPORTED: T or h? ################"
grep -n "specificHeat\|enthalpy\|Enthalpy" include/cfd/thermal/EnergyEquation.hpp | head -10

echo
echo "################ 4. THERMAL PROPERTIES ################"
sed -n '1,40p' include/cfd/thermal/ThermalProperties.hpp

echo
echo "################ 5. temperature-dependent properties? ################"
echo "  k(T)/cp(T)/mu(T) polynomial or function:"
grep -rn "polynomial\|Polynomial\|temperatureDependent\|k(T)\|cpOfT" include/cfd/thermal/ include/cfd/io/case/ 2>/dev/null | head -8
echo "  (empty above = constant properties only)"
