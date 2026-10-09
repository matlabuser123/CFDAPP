#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/build/final
./tests/unit/transport/CFDTransportTests --gtest_filter=TransportSource.ZeroSourceChangesNothing 2>&1 | head -20
echo "=== does boundaryFaceDiffusionTerms zero the coefficient for FixedGradient? ==="
grep -n "prescribedValue" -A 18 /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/src/discretization/NonOrthogonalDiffusion.cpp | head -30
