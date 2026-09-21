#!/usr/bin/env bash
# GPU-PIPE-001 Phase 2 -- required negative control.
#
# The Phase-2 claim is "D2H calls and synchronizations fell". That claim is only
# meaningful if the instrumentation could have shown them NOT falling. So:
# inject one extra reduction round trip per BiCGSTAB iteration, rebuild, and
# require the counters to detect it. Then remove the injection and require the
# counters to return to their measured values.
#
# The injected defect is deliberately the exact thing Phase 2 removed -- an
# extra host round trip -- not an unrelated perturbation.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

SRC=cuda/kernels/GpuLinearSolverCuda.cpp
BACKUP=/tmp/GpuLinearSolverCuda.cpp.orig
PROBE=results/gpu-pipe-001/phase2-reductions/tools/paired_probe.cpp
CUDA_LIB=/usr/local/cuda-12.9/lib64

cleanup() {
  if [ -f "$BACKUP" ]; then
    cp "$BACKUP" "$SRC"
    # Restoring the SOURCE is not enough: build/cuda still holds the mutant
    # library, and the next measurement would silently use it. That actually
    # happened once (a Phase 4 probe read 23,400 D2H calls instead of 19,502 --
    # one extra reduction per Krylov iteration, i.e. the injected defect) and is
    # exactly the stale-binary hazard CLAUDE.md 8 exists for. Rebuild here, and
    # prove the rebuilt library no longer contains the injection.
    cmake --build build/cuda --target cfdcore cfdcuda > /tmp/nc_restore_build.log 2>&1 \
      && echo "restored $SRC and rebuilt" \
      || { echo "RESTORE REBUILD FAILED -- build/cuda may still be the mutant"; tail -3 /tmp/nc_restore_build.log; }
    if grep -q 'injectedExtraRoundTrip' "$SRC"; then
      echo "*** source still contains the injection -- manual check required ***"
    fi
  fi
}
trap cleanup EXIT

measure() { # label -> prints the RESULT line
  cmake --build build/cuda --target cfdcore cfdcuda > /tmp/nc_build.log 2>&1 || {
    echo "BUILD FAILED"; tail -5 /tmp/nc_build.log; exit 1; }
  g++ -std=c++20 -O2 -I include "$PROBE" \
    -Wl,--start-group build/cuda/src/libcfdcore.a build/cuda/cuda/libcfdcuda.a -Wl,--end-group \
    -L"$CUDA_LIB" -lcudart -o /tmp/nc_probe 2>/dev/null
  /tmp/nc_probe 160 4 gpu | grep '^RESULT'
}

cp "$SRC" "$BACKUP"

echo "=== baseline (Phase 2 code as committed to the working tree) ==="
CLEAN=$(measure)
echo "$CLEAN"
clean_d2h=$(echo "$CLEAN"  | grep -oE 'd2h_calls=[0-9]+' | cut -d= -f2)
clean_sync=$(echo "$CLEAN" | grep -oE 'syncs=[0-9]+'     | cut -d= -f2)
clean_res=$(echo "$CLEAN"  | grep -oE 'p_res=[^ ]+'      | cut -d= -f2)
clean_it=$(echo "$CLEAN"   | grep -oE 'krylov_it=[0-9]+' | cut -d= -f2)

echo
echo "=== inject one extra reduction round trip per BiCGSTAB iteration ==="
python3 - "$SRC" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
# Must be unique to GpuBiCGSTAB. "const Real residualNorm = l2Norm(r_);" is NOT:
# it appears in GpuCG too, so the first attempt refused to patch and the control
# correctly reported FAIL because nothing had been injected.
anchor = "      const Real sNorm = l2Norm(s_);"
assert s.count(anchor) == 1, f"anchor found {s.count(anchor)} times -- not unique to BiCGSTAB"
s = s.replace(anchor,
  "      // NEGATIVE CONTROL (injected): a redundant reduction round trip.\n"
  "      volatile Real injectedExtraRoundTrip = dot(r_, r_);\n"
  "      (void)injectedExtraRoundTrip;\n" + anchor)
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("  injected an extra dot(r_, r_) per iteration")
PY

MUTANT=$(measure)
echo "$MUTANT"
mut_d2h=$(echo "$MUTANT"  | grep -oE 'd2h_calls=[0-9]+' | cut -d= -f2)
mut_sync=$(echo "$MUTANT" | grep -oE 'syncs=[0-9]+'     | cut -d= -f2)
mut_res=$(echo "$MUTANT"  | grep -oE 'p_res=[^ ]+'      | cut -d= -f2)
mut_it=$(echo "$MUTANT"   | grep -oE 'krylov_it=[0-9]+' | cut -d= -f2)

echo
echo "=== verdict ==="
printf '  d2h_calls  clean %-8s mutant %-8s delta %+d\n' "$clean_d2h" "$mut_d2h" "$((mut_d2h-clean_d2h))"
printf '  syncs      clean %-8s mutant %-8s delta %+d\n' "$clean_sync" "$mut_sync" "$((mut_sync-clean_sync))"
printf '  krylov_it  clean %-8s mutant %-8s (must match: the injection is redundant)\n' "$clean_it" "$mut_it"
printf '  p_res      clean %s\n             mutant %s\n' "$clean_res" "$mut_res"

fail=0
[ "$mut_d2h" -gt "$clean_d2h" ] || { echo "  FAIL instrumentation did NOT detect the extra D2H"; fail=1; }
[ "$mut_sync" -gt "$clean_sync" ] || { echo "  FAIL instrumentation did NOT detect the extra sync"; fail=1; }
[ "$mut_it" = "$clean_it" ] || { echo "  FAIL the injection changed the solve (it should be inert)"; fail=1; }
[ "$mut_res" = "$clean_res" ] || { echo "  FAIL the injection changed the residual"; fail=1; }
# Expected magnitude: one extra round trip per Krylov iteration.
echo "  expected extra round trips = krylov_it = $clean_it, observed = $((mut_d2h-clean_d2h))"

echo
[ "$fail" -eq 0 ] && echo "NEGATIVE CONTROL: PASS" || echo "NEGATIVE CONTROL: FAIL"
exit "$fail"
