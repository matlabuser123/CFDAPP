#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "=== expectations ==="
cmake -DCFDAPP_SOURCE_DIR=$PWD -P tests/cmake/test_package_naming.cmake
echo "  exit status: $?"
echo
echo "=== refusal (must exit nonzero) ==="
cmake -DCFDAPP_SOURCE_DIR=$PWD -P tests/cmake/test_package_naming_refuses.cmake
echo "  exit status: $?"
