#!/usr/bin/env bash
# P13 Final Qualification -- derive the case-validation gate (your G3).
#
# Built from the lessons of the restart gate, where four separate attempts each
# fixed the symptom in front of me instead of auditing what the scripts depend
# on. The audit was done FIRST this time.
#
# WHAT THE AUDIT FOUND, and what is derived for each:
#
#   1. Every script writes into results/p13/case-validation/logs -- the
#      PRESERVED evidence directory. Same violation that destroyed three files
#      in the restart gate. All log targets are redirected here.
#
#   2. Build trees used: build/final (CLI sweep + N4), build/debug (GUI build,
#      for the N1/N1b/rename controls), build/p13v2cpuonly (created fresh by
#      cpu_only.sh). Verified present and correctly configured:
#         build/final        CUDA=OFF OPENMP=OFF GUI=OFF
#         build/debug        CUDA=OFF OPENMP=OFF GUI=ON
#      Both are OPENMP=OFF, so the oversubscription that wrecked the restart
#      gate's G3 cannot apply. Their ctest calls are left as the originals had
#      them, because changing them would fix a problem that is not present.
#
#   3. THE CONTROLS MUTATE PRODUCTION SOURCE. N1, N1b and
#      regression_marker_rename patch a source file, rebuild, verify the
#      criterion DETECTS the breakage, then restore. That is the non-vacuity
#      mechanism and must not be interfered with -- but it means the source
#      fingerprint moves mid-gate and must return. The caller re-checks the
#      SOURCE fingerprint afterwards; a mismatch means a control failed to
#      restore and is a hard failure, not a rounding detail.
#
# No criterion, threshold or mutation count is altered. The preserved originals
# under results/p13/case-validation/tools/ are never written to.
set -euo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT"
SRC=results/p13/case-validation/tools
Q=results/p13/final-qualification/2026-09-24
DST=$Q/tools/case-validation

mkdir -p "$DST" "$Q/case_validation_logs"
cp -p "$SRC"/* "$DST"/
echo "copied $(ls "$SRC" | wc -l) files (including mutation_matrix.py and patch_field_marker.py)"

# Seed the log set so anything the harness reads as a baseline is present,
# while every write lands in this copy.
cp -pn results/p13/case-validation/logs/* "$Q/case_validation_logs"/ 2>/dev/null || true

QABS='$ROOT/results/p13/final-qualification/2026-09-24'
for f in "$DST"/*.sh; do
  sed -i "s#^L=\$ROOT/results/p13/case-validation/logs\$#L=${QABS}/case_validation_logs#" "$f"
  sed -i "s#^L=\$E/logs\$#L=${QABS}/case_validation_logs#" "$f"
  sed -i "s#^E=\$ROOT/results/p13/case-validation\$#E=${QABS}#" "$f"
  sed -i "s#^T=\$E/tools\$#T=${QABS}/tools/case-validation#" "$f"
  sed -i "s#^T=\$ROOT/results/p13/case-validation/tools\$#T=${QABS}/tools/case-validation#" "$f"
done

echo ""
echo "=== verification ==="
echo "python helpers present:"
for p in mutation_matrix.py patch_field_marker.py; do
  printf '  %-26s %s\n' "$p" "$(test -f "$DST/$p" && echo YES || echo MISSING)"
done
# The check must separate WRITES from READS. An earlier version flagged
# controls_n2_n3.sh:91, which passes results/p13/case-validation/data/baseline_case
# to mutation_matrix.py -- a READ of the frozen baseline. That script does
# shutil.copytree(args.case, work) and mutates the COPY (mutation_matrix.py:213),
# so the preserved data is never written. Reading the frozen baseline is correct
# behaviour and must not be "fixed".
echo "writes into the preserved LOGS dir (must be 0):"
w=$( { grep -n "results/p13/case-validation/logs" "$DST"/*.sh || true; } | { grep -vE ":[[:space:]]*#" || true; } | wc -l)
echo "  $w"
if [ "$w" -ne 0 ]; then
  grep -n "results/p13/case-validation/logs" "$DST"/*.sh | grep -vE ":[[:space:]]*#" || true
  echo "FATAL: a derived script still writes into the preserved evidence"
  exit 1
fi
echo "reads of the preserved DATA dir (expected, read-only input):"
r=$( { grep -ho "results/p13/case-validation/data" "$DST"/*.sh || true; } | wc -l)
echo "  $r  (mutation_matrix.py copies the case and mutates the copy)"
for f in "$DST"/*.sh; do bash -n "$f" || { echo "SYNTAX FAIL $f"; exit 1; }; done
echo "syntax: all OK"

echo ""
echo "=== trees this gate needs ==="
for d in final debug; do
  printf '  build/%-8s CUDA=%-4s OPENMP=%-4s GUI=%-4s cfdapp=%s\n' "$d" \
    "$(grep -m1 CFDAPP_ENABLE_CUDA build/$d/CMakeCache.txt | cut -d= -f2)" \
    "$(grep -m1 CFDAPP_ENABLE_OPENMP build/$d/CMakeCache.txt | cut -d= -f2)" \
    "$(grep -m1 CFDAPP_BUILD_GUI build/$d/CMakeCache.txt | cut -d= -f2)" \
    "$(test -x build/$d/apps/cli/cfdapp && echo yes || echo NO)"
done
