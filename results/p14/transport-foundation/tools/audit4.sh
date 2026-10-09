#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "################ IS SCALAR CONVECTION/DIFFUSION SHARED, OR COPIED? ################"
grep -rn "void assemble.*Convection.*Contribution\|void assemble.*Diffusion.*Contribution" include/cfd/ | sed 's/:.*void /  /' | sort

echo
echo "################ SourceContribution -- what is it? ################"
grep -rn "struct SourceContribution\|class SourceContribution" -A 14 include/ | head -22
echo "  used in:"; grep -rl "SourceContribution" include/ src/ | head -10

echo
echo "################ Su/Sp linearised source -- who has it? ################"
grep -rn "Su\b\|Sp\b" include/cfd/turbulence/KEpsilonEquation.hpp | head -6
grep -rn "assemble.*Source" include/cfd/ | sed 's/:.*\[\[nodiscard\]\] //' | head -10

echo
echo "################ SCALAR BOUNDARY CONDITIONS ################"
grep -n "class\|enum class\|struct" include/cfd/boundary/BoundaryCondition.hpp | head -12
echo "  scalar-specific BC types:"
ls include/cfd/boundary/ | grep -i "temp\|heat\|omega\|value\|gradient"

echo
echo "################ GPU DEVICE KERNELS ################"
ls cuda/kernels/
