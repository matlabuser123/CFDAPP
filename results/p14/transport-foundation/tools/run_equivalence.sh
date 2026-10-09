#!/usr/bin/env bash
# NOTE: the binary is DELETED before compiling. An earlier version of this
# script tested [ -x ] on a path that still held the previous build, so a failed
# compile silently re-ran stale results -- the same "the check passed but did
# not run what I thought" failure as P13-007's missing smoke test.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
rm -f /tmp/transport_equivalence
g++ -std=c++20 -O2 -DNDEBUG -I include -I build/final/generated/include \
    -I /usr/local/cuda-12.9/include results/p14/transport-foundation/tools/transport_equivalence.cpp \
    -o /tmp/transport_equivalence -Wl,--start-group build/final/src/libcfdcore.a \
    build/final/cuda/libcfdcuda.a -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart
rc=$?
if [ $rc -ne 0 ] || [ ! -x /tmp/transport_equivalence ]; then
  echo "COMPILE FAILED (exit $rc)"; exit 1
fi
/tmp/transport_equivalence
