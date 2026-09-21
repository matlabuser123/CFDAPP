#!/usr/bin/env bash
# GPU-PIPE-001 rho-carry: BEFORE (pre-rho-carry binary, already built) vs AFTER
# (rebuilt against the current library), same session, alternating.
set -eu
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
PROBE=results/gpu-pipe-001/phase2-reductions/tools/paired_probe.cpp
CUDA_LIB=/usr/local/cuda-12.9/lib64

# /tmp/paired_after currently holds the PRE-rho-carry build; preserve it.
if [ ! -x /tmp/paired_before_rho ]; then
  cp /tmp/paired_after /tmp/paired_before_rho
  echo "preserved pre-rho-carry binary -> /tmp/paired_before_rho"
fi

g++ -std=c++20 -O2 -I include "$PROBE" \
  -Wl,--start-group build/cuda/src/libcfdcore.a build/cuda/cuda/libcfdcuda.a -Wl,--end-group \
  -L"$CUDA_LIB" -lcudart -o /tmp/paired_after
echo "rebuilt /tmp/paired_after against the rho-carry library"
echo

show() { # binary label edge outer
  local line
  line=$("$1" "$3" "$4" gpu | grep '^RESULT')
  local k rg rq d s
  k=$(echo "$line"  | grep -oE 'krylov_it=[0-9]+'      | cut -d= -f2)
  rg=$(echo "$line" | grep -oE 'red_groups=[0-9-]+'    | cut -d= -f2)
  rq=$(echo "$line" | grep -oE 'red_quantities=[0-9-]+'| cut -d= -f2)
  d=$(echo "$line"  | grep -oE 'd2h_calls=[0-9]+'      | cut -d= -f2)
  s=$(echo "$line"  | grep -oE 'syncs=[0-9]+'          | cut -d= -f2)
  printf '  %-7s krylov=%-6s red_groups=%-7s per_iter=%-6.2f quantities=%-7s d2h=%-7s syncs=%-7s\n' \
    "$2" "$k" "$rg" "$(awk -v a="$rg" -v b="$k" 'BEGIN{print a/b}')" "$rq" "$d" "$s"
  echo "$line" | grep -oE 'p_res=[^ ]+ cont=[^ ]+ mass=[^ ]+' | sed "s/^/  $2    /"
}

for spec in 160:4 320:3 640:2; do
  edge=${spec%%:*}; outer=${spec##*:}
  echo "=== ${edge}^2, $outer outer iterations ==="
  show /tmp/paired_before_rho BEFORE "$edge" "$outer"
  show /tmp/paired_after      AFTER  "$edge" "$outer"
  echo
done
