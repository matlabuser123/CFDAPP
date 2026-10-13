#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
clang-format-18 --style=file -i include/cfd/gpu/DeviceScalarTransport.hpp cuda/kernels/DeviceScalarTransportKernel.cu
for f in include/cfd/gpu/DeviceScalarTransport.hpp; do
  clang-format-18 --style=file "$f" | diff -q - "$f" >/dev/null && echo "OK $f" || echo "STILL BAD $f"
done
