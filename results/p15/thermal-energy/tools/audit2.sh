#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "################ TRANSIENT? Does the energy equation have a time term? ################"
grep -c "TimeDerivative\|dt\b\|transient" src/thermal/EnergyEquation.cpp src/thermal/ThermalSolver.cpp

echo
echo "################ FIELD-BASED (variable) property overloads ################"
grep -n "const cfd::fields::ScalarField& conductivity\|const cfd::fields::ScalarField& specificHeat" include/cfd/thermal/EnergyEquation.hpp | head -6

echo
echo "################ THERMAL BCs ################"
ls include/cfd/boundary/ | grep -i "temp\|heat\|adiab"
echo "  Robin/convective BC present?"
grep -rln "Robin\|convectiveBoundary\|heatTransferCoefficient" include/ src/ | head -5
echo "  (empty = absent)"

echo
echo "################ BOUSSINESQ ################"
grep -n "beta\|referenceTemperature\|buoyan" include/cfd/physics/BoussinesqBuoyancy.hpp | head -8

echo
echo "################ CASE FORMAT: thermal keys ################"
grep -rn "conductivity\|specific_heat\|temperature" src/io/case/PhysicsConfigParser.cpp 2>/dev/null | head -8
ls tests/data/cases/ | grep -i "thermal\|heat\|conduct" | head

echo
echo "################ GPU THERMAL? ################"
echo "  device thermal kernels: $(ls cuda/kernels/ | grep -ci thermal)"
grep -rln "thermal" cuda/ | head -3
echo "  (empty = no GPU thermal path)"

echo
echo "################ EXISTING THERMAL TESTS ################"
ls tests/integration/thermal/
echo "  unit: $(ls tests/unit/thermal/ 2>/dev/null | tr '\n' ' ')"
