#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "### scalar transport assemblers that already exist (the duplication) ###"
grep -rn "void assemble" include/cfd/thermal/*.hpp include/cfd/species/*.hpp \
     include/cfd/turbulence/KEpsilonEquation.hpp include/cfd/multiphase/VolumeFractionEquation.hpp \
  | grep -i "convect\|diffus\|source" | sed 's/.*void /  /' | sed 's/(.*//' | sort -u
echo
echo "  scalar CONVECTION assemblers: $(grep -rho 'assemble[A-Za-z]*Convection[A-Za-z]*' include/cfd/thermal include/cfd/species include/cfd/multiphase include/cfd/turbulence 2>/dev/null | sort -u | wc -l)"
echo "  scalar DIFFUSION  assemblers: $(grep -rho 'assemble[A-Za-z]*Diffusion[A-Za-z]*' include/cfd/thermal include/cfd/species include/cfd/multiphase include/cfd/turbulence 2>/dev/null | sort -u | wc -l)"
echo
echo "### does a generic transport layer already exist? ###"
echo "  src/transport, include/cfd/transport : $(ls -d src/transport include/cfd/transport 2>/dev/null | wc -l) (0 = absent)"
echo "  'ScalarTransport' anywhere           : $(grep -rl 'ScalarTransport' include/ src/ 2>/dev/null | wc -l)"
echo
echo "### GPU: is any SCALAR transport on the device? ###"
echo "  scalar/species/thermal/energy kernels: $(ls cuda/kernels/ | grep -ci 'scalar\|species\|thermal\|energy\|transport')"
echo "  momentum/pressure kernels            : $(ls cuda/kernels/ | grep -ci 'momentum\|pressure\|faceflux\|gradient\|diffusion\|convection')"
echo
echo "### the existing Su/Sp source assembler (the one precedent) ###"
grep -n "assembleScalarSourceContribution\|assembleSourceContribution" include/cfd/turbulence/KEpsilonEquation.hpp | head -3
echo
echo "### clean-build warnings: pre-existing or new? ###"
grep -o "warning: .*\[-W[a-z-]*\]" results/p13/final-qualification/logs/01_release_cuda_build.log 2>/dev/null | sed 's/.*\[-W/  -W/' | sort | uniq -c | sort -rn | head -8
