#!/usr/bin/env bash
# GPU-PIPE-001 rho-carry -- negative control.
#
# Claim under test: reduction groups per Krylov iteration are now 4.00, and the
# instrumentation would notice if they were not. Inject the redundant reduction
# the rho-carry removed (recompute rho at the top of the loop instead of using
# the carried value) and require the counters to read 5.00 again.
#
# The previous negative control restored the SOURCE but left the mutant LIBRARY
# in build/cuda, and a later probe silently measured the mutant. This procedure
# therefore does, explicitly and in order:
#   inject -> build -> measure -> restore -> REBUILD -> verify restored -> verify freshness
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

SRC=cuda/kernels/GpuLinearSolverCuda.cpp
BACKUP=/tmp/GpuLinearSolverCuda.rho.orig
PROBE=results/gpu-pipe-001/phase2-reductions/tools/paired_probe.cpp
CUDA_LIB=/usr/local/cuda-12.9/lib64
fail=0

build_and_measure() { # -> RESULT line
  cmake --build build/cuda --target cfdcore cfdcuda > /tmp/nc_rho_build.log 2>&1 || {
    echo "BUILD FAILED"; grep -iE 'error' /tmp/nc_rho_build.log | head -5; exit 1; }
  g++ -std=c++20 -O2 -I include "$PROBE" \
    -Wl,--start-group build/cuda/src/libcfdcore.a build/cuda/cuda/libcfdcuda.a -Wl,--end-group \
    -L"$CUDA_LIB" -lcudart -o /tmp/nc_rho_probe 2>/dev/null
  /tmp/nc_rho_probe 160 4 gpu | grep '^RESULT'
}

field() { echo "$1" | grep -oE "$2=[0-9.eE+-]+" | cut -d= -f2; }

cp "$SRC" "$BACKUP"
restore() {
  cp "$BACKUP" "$SRC"
  cmake --build build/cuda --target cfdcore cfdcuda > /tmp/nc_rho_restore.log 2>&1 \
    && echo "  source restored AND library rebuilt" \
    || { echo "  *** RESTORE REBUILD FAILED -- build/cuda may hold the mutant ***"; fail=1; }
}
trap 'restore' EXIT

echo "=== A: rho-carry as implemented (expect 4.00 groups/iteration) ==="
CLEAN=$(build_and_measure); echo "  $CLEAN"
c_rg=$(field "$CLEAN" red_groups); c_k=$(field "$CLEAN" krylov_it)
c_d2h=$(field "$CLEAN" d2h_calls); c_res=$(field "$CLEAN" p_res)
c_per=$(awk -v a="$c_rg" -v b="$c_k" 'BEGIN{printf "%.2f", a/b}')
echo "  groups/iteration = $c_per"

echo
echo "=== B: inject the removed reduction back (expect 5.00) ==="
python3 - "$SRC" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
anchor = "      const Real rho = rhoNext;"
assert s.count(anchor) == 1, f"anchor found {s.count(anchor)} times"
s = s.replace(anchor,
  "      // NEGATIVE CONTROL (injected): recompute rho instead of carrying it.\n"
  "      const Real rho = dot(rHat_, r_);")
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("  injected: rho recomputed at the top of the loop")
PY

MUT=$(build_and_measure); echo "  $MUT"
m_rg=$(field "$MUT" red_groups); m_k=$(field "$MUT" krylov_it)
m_d2h=$(field "$MUT" d2h_calls); m_res=$(field "$MUT" p_res)
m_per=$(awk -v a="$m_rg" -v b="$m_k" 'BEGIN{printf "%.2f", a/b}')
echo "  groups/iteration = $m_per"

echo
echo "=== verdict ==="
printf '  groups/iteration   clean %-6s mutant %-6s\n' "$c_per" "$m_per"
printf '  red_groups         clean %-8s mutant %-8s delta %+d\n' "$c_rg" "$m_rg" "$((m_rg-c_rg))"
printf '  d2h_calls          clean %-8s mutant %-8s delta %+d\n' "$c_d2h" "$m_d2h" "$((m_d2h-c_d2h))"
printf '  krylov_it          clean %-8s mutant %-8s (must match)\n' "$c_k" "$m_k"
printf '  p_res              clean %s\n                     mutant %s\n' "$c_res" "$m_res"

awk -v a="$c_per" 'BEGIN{exit !(a>3.95 && a<4.05)}' || { echo "  FAIL clean is not 4.00"; fail=1; }
awk -v a="$m_per" 'BEGIN{exit !(a>4.95 && a<5.05)}' || { echo "  FAIL mutant is not 5.00"; fail=1; }
[ "$m_rg" -gt "$c_rg" ] || { echo "  FAIL instrumentation did not detect the extra reduction"; fail=1; }
[ "$m_k" = "$c_k" ] || { echo "  FAIL injection changed the solve"; fail=1; }
[ "$m_res" = "$c_res" ] || { echo "  FAIL injection changed the residual"; fail=1; }
echo "  expected extra groups = krylov_it = $c_k, observed = $((m_rg-c_rg))"

echo
echo "=== C: restore and PROVE the production binary is back ==="
restore
trap - EXIT
if grep -q 'NEGATIVE CONTROL (injected)' "$SRC"; then
  echo "  FAIL injection still present in source"; fail=1
else
  echo "  source clean: no injection markers"
fi
if ninja -C build/cuda -n cuda/libcfdcuda.a 2>&1 | grep -q 'no work to do'; then
  echo "  build/cuda up to date with the restored source (ninja)"
else
  echo "  FAIL build/cuda is stale after restore"; fail=1
fi
RESTORED=$(build_and_measure)
r_rg=$(field "$RESTORED" red_groups); r_k=$(field "$RESTORED" krylov_it)
r_per=$(awk -v a="$r_rg" -v b="$r_k" 'BEGIN{printf "%.2f", a/b}')
echo "  restored groups/iteration = $r_per (must equal the clean $c_per)"
[ "$r_per" = "$c_per" ] || { echo "  FAIL restored binary does not match the clean measurement"; fail=1; }

echo
[ "$fail" -eq 0 ] && echo "NEGATIVE CONTROL (rho-carry): PASS" || echo "NEGATIVE CONTROL (rho-carry): FAIL"
exit "$fail"
