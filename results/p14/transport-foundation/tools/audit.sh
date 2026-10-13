#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "################ 1. MESH ################"
ls include/cfd/mesh/ | head -20
echo "--- cell/face accessors ---"
grep -n "numberOfCells\|numberOfFaces\|cellVolume\|faceArea\|owner\|neighbour" include/cfd/mesh/Mesh.hpp 2>/dev/null | head -18

echo
echo "################ 2. FIELDS ################"
ls include/cfd/fields/
grep -n "class ScalarField\|class VectorField\|class SurfaceField" -A 6 include/cfd/fields/*.hpp | head -30

echo
echo "################ 3. FACE FLUX ################"
grep -rn "class SurfaceField\|SurfaceField;" include/cfd/fields/*.hpp | head -5
grep -rn "calculateMassFlux\|rhieChowMassFlux" include/cfd/physics/*.hpp | head -5

echo
echo "################ 4/5. FV ASSEMBLY + DISCRETIZATION ################"
ls include/cfd/discretization/
echo "--- existing scalar-transport-like equations ---"
ls include/cfd/thermal/ include/cfd/species/ include/cfd/turbulence/ 2>/dev/null

echo
echo "################ 6. BOUNDARY CONDITIONS ################"
ls include/cfd/boundary/ 2>/dev/null | head -20

echo
echo "################ 7. LINEAR SYSTEM ################"
ls include/cfd/algebra/ 2>/dev/null | head -20
grep -n "struct LinearSystem\|class LinearSystem" -A 20 include/cfd/algebra/LinearSystem.hpp 2>/dev/null | head -28
