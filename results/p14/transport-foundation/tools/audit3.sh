#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "################ THE SHARED CONTRIBUTION ASSEMBLERS ################"
grep -n "assembleConvectionContribution\|assembleDiffusionContribution" -A 12 include/cfd/physics/MomentumEquation.hpp | head -60

echo
echo "################ SOURCE TERM API? ################"
echo "  'SourceContribution' :   $(grep -rl 'SourceContribution' include/ src/ 2>/dev/null | wc -l) files"
echo "  'implicitCoefficient':   $(grep -rl 'implicitCoefficient' include/ src/ 2>/dev/null | wc -l) files"
echo "  Sp / Su linearisation:   $(grep -rn 'Sp\b.*Su\b\|linearis\|lineariz' include/cfd/ 2>/dev/null | head -4)"
echo "  how turbulence adds production/destruction:"
grep -n "production\|destruction\|source" src/turbulence/KEpsilonEquation.cpp | head -8

echo
echo "################ TimeDerivative: density scalar or field? ################"
sed -n '40,60p' include/cfd/discretization/TimeDerivative.hpp

echo
echo "################ GPU: what scalar transport exists on the device? ################"
ls cuda/kernels/ | head -25
echo "  device scalar-transport kernels: $(ls cuda/kernels/ | grep -ci 'scalar\|transport\|species\|thermal\|energy')"
