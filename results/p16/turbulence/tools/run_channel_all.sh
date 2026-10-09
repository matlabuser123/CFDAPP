#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
B=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests
"$B" 2>&1 | grep -v "^\[INFO\]" | tail -40
