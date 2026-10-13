#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "################ THE FOUR EXISTING SCALAR TRANSPORT EQUATIONS ################"
for f in include/cfd/thermal/EnergyEquation.hpp include/cfd/species/SpeciesEquation.hpp \
         include/cfd/turbulence/KEpsilonEquation.hpp include/cfd/multiphase/VolumeFractionEquation.hpp; do
  echo "--- $f"
  grep -n "assemble\|LinearSystem\|struct .*Config\|SurfaceField\|ScalarField" "$f" 2>/dev/null | head -8
done

echo
echo "################ DO THEY SHARE AN ASSEMBLER? ################"
for f in src/thermal/EnergyEquation.cpp src/species/SpeciesEquation.cpp \
         src/turbulence/KEpsilonEquation.cpp src/multiphase/VolumeFractionEquation.cpp; do
  printf "%-46s lines=%-5s convection=%-3s diffusion=%-3s laplacian=%-3s timederiv=%s\n" \
    "$f" "$(wc -l < $f 2>/dev/null)" \
    "$(grep -c 'Convection\|upwind\|Upwind' $f 2>/dev/null)" \
    "$(grep -c 'Diffusion' $f 2>/dev/null)" \
    "$(grep -c 'Laplacian' $f 2>/dev/null)" \
    "$(grep -c 'TimeDerivative\|deltaT\|timeStep' $f 2>/dev/null)"
done

echo
echo "################ TimeDerivative -- does transient exist? ################"
sed -n '1,40p' include/cfd/discretization/TimeDerivative.hpp

echo
echo "################ Convection / Diffusion signatures ################"
grep -n "\[\[nodiscard\]\]\|^[A-Za-z].*(" include/cfd/discretization/Convection.hpp | head -12
echo "---"
grep -n "\[\[nodiscard\]\]\|^[A-Za-z].*(" include/cfd/discretization/Diffusion.hpp | head -12
