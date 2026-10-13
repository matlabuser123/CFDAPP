#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
echo "### nvidia-smi ###"
nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>&1 | head -2
echo "### CUDA runtime the build links ###"
nvcc --version 2>/dev/null | tail -2
echo "### does the P14 CUDA probe still run? (it ran earlier this session) ###"
if [ -x /tmp/transport_equivalence ]; then
  /tmp/transport_equivalence --quick 2>&1 | tail -5
else
  echo "  probe binary gone; rebuilding"
  bash results/p15/thermal-energy/../../p14/transport-foundation/tools/run_equivalence.sh 2>&1 | tail -6
fi
