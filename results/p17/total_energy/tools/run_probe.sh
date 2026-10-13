#!/usr/bin/env bash
# Build and run a one-off P17.5 probe from tools/.  Usage: run_probe.sh <name-without-.cpp>
# A FILE, not an inline bash -c: composing this inline expanded $PATH and $B in the outer Windows
# shell and produced "syntax error near unexpected token '('" -- the sixth occurrence of that
# hazard in this project.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/total_energy
B=build/qual-final
NAME=${1:?usage: run_probe.sh <name-without-.cpp>}
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/${NAME}.cpp" \
    -o "/tmp/p17_5_${NAME}" -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"/tmp/p17_5_${NAME}_build.txt"
rc=$?
if [ $rc -ne 0 ]; then
  echo "COMPILE FAILED (rc=$rc)"
  grep -E 'error:' "/tmp/p17_5_${NAME}_build.txt" | head -20
  exit 1
fi
OMP_NUM_THREADS=1 "/tmp/p17_5_${NAME}"
