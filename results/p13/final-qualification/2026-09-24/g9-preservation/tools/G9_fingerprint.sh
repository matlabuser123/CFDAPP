#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G9: the P13-001 SIMPLE fingerprint, bitwise.
#
# WHY A SECOND SCRIPT, recorded rather than quietly folded into the first:
# G9_preservation.sh's fingerprint step FAILED TO LINK:
#
#   build/qual-final/src/libcfdcore.a(SparseMatrix.cpp.o):
#     undefined reference to `omp_get_num_threads'
#     undefined reference to `GOMP_parallel'
#
# The frozen results/p14/transport-foundation/tools/fingerprint.sh compile line
# has no -fopenmp, because build/final had OpenMP OFF when it was written.
# build/qual-final has OpenMP ON, so linking its libcfdcore.a needs the flag.
# Classification: invalid gate invocation in MY adaptation -- not a regression,
# and no production code involved. The failed link is preserved in
# logs/G9_fp_build.log.
#
# This is the SECOND consecutive gate in which the OpenMP difference between
# build/final and build/qual-final caused a problem: in G8 it produced three
# false "REGRESSION" verdicts because the comparator could not see the build
# configuration (G8-F1). Same underlying fact, two different symptoms.
#
# Nothing else changes: same solve_fingerprint.cpp, same frozen baseline, same
# bitwise comparison.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G9=$ROOT/results/p13/final-qualification/2026-09-24/g9-preservation
L=$G9/logs
B=build/qual-final
BASE=results/p13/restart/logs/fingerprint-after-histories.log
cd "$ROOT"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

log=$L/G9_fingerprint_rerun.log
{
echo "P13 G9 -- the P13-001 SIMPLE fingerprint, re-run with -fopenmp"
echo "date: $(date -u +%FT%TZ)"
echo "tree: $B   (Release + CUDA ON + OpenMP ON)"
echo ""

echo "### what this instrument is for ###"
echo "  P14 added a transport layer and edited TimeDerivative; P15 and P16 added"
echo "  solvers that run after SIMPLE. A passing CTest proves those additions"
echo "  work. It does NOT prove SIMPLE still produces the same BITS. This does."
echo ""

g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" \
    -I /usr/local/cuda-12.9/include results/p13/restart/tools/solve_fingerprint.cpp \
    -o /tmp/g9_fingerprint -Wl,--start-group "$B/src/libcfdcore.a" \
    "$B/cuda/libcfdcuda.a" -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart \
    2> "$L/G9_fp_build_openmp.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED ($rc)"; tail -10 "$L/G9_fp_build_openmp.log" | sed 's/^/    /'; exit 1
fi
echo "  compiled with -fopenmp: ok"
echo ""

/tmp/g9_fingerprint > "$L/G9_fingerprint.log" 2>&1
echo "### result ###"
echo "  cases fingerprinted: $(grep -c FINGERPRINT "$L/G9_fingerprint.log")"
echo "  frozen baseline    : $BASE"
echo "  baseline cases     : $(grep -c FINGERPRINT "$BASE")"
echo ""

# Strip the baseline's "#" header/footer lines and blanks from BOTH sides, as the
# frozen script does: it was written by a wrapper that prepends 6 header lines,
# and an earlier version of it used a fixed `tail -n +8` on both sides and
# reported a spurious 7-line difference.
strip() { grep -v '^#' "$1" | grep -v '^$'; }
if diff <(strip "$BASE") <(strip "$L/G9_fingerprint.log") > "$L/G9_fingerprint_diff.txt" 2>&1; then
  echo "  BITWISE IDENTICAL to the P13-001 baseline."
  echo "  P14, P15 and P16 did not move a single bit of the SIMPLE solution."
  echo "  lines compared: $(strip "$BASE" | wc -l)"
else
  echo "  *** DIFFERS from the P13-001 baseline ***"
  echo "  differing lines: $(grep -cE '^[<>]' "$L/G9_fingerprint_diff.txt")"
  head -24 "$L/G9_fingerprint_diff.txt" | sed 's/^/    /'
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
