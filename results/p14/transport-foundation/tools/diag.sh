#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
./build/final/tests/integration/transport/CFDTransportVerificationTests \
  --gtest_filter=TransportVerification.Case1SteadyDiffusionIsLinear 2>&1 | head -18
