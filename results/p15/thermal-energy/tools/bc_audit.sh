#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "### scalar BC implementations (would need a signature change under Option B) ###"
grep -rln "public ScalarBoundaryCondition" include/cfd/boundary/ | sed 's/^/  /'
echo "  count: $(grep -rln 'public ScalarBoundaryCondition' include/cfd/boundary/ | wc -l)"
echo
echo "### vector BC implementations ###"
echo "  count: $(grep -rln 'public VectorBoundaryCondition' include/cfd/boundary/ | wc -l)"
echo
echo "### call sites of scalar boundaryValue() ###"
grep -rn "boundaryValue(" --include=*.cpp --include=*.hpp src/ include/ apps/ cuda/ | grep -v "Vector2\|unitNormal" | wc -l
echo "  files:"
grep -rln "boundaryValue(" --include=*.cpp src/ cuda/ | sed 's/^/    /'
echo
echo "### CUDA boundary encoding: how does the device represent a BC? ###"
grep -n "enum\|kBoundary\|struct" include/cfd/gpu/BoundaryEncoding.hpp | head -14
echo
echo "### does the device encode a Gamma-dependent BC today? ###"
grep -c "conductivity\|gamma\|Gamma" include/cfd/gpu/BoundaryEncoding.hpp
