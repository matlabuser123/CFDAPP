#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
./build/final/tests/unit/turbulence/CFDTurbulenceTests 2>&1 | tail -8
