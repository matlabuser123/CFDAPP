#!/usr/bin/env bash
# GPU-PIPE-001 Phase 4: is solver storage already persistent across solves?
#
# The claim to test: matrix structure, matrix values, the Krylov workspace and
# the reduction buffer are allocated once and reused, so device allocations do
# NOT grow with the number of linear solves. Measured, not assumed: run the same
# grid with increasing outer-iteration counts (3 linear solves per outer) and
# watch allocations / reallocations / frees.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

printf '%-6s %-7s %-8s %-9s %-11s %-8s %-11s %-11s\n' \
  edge outer solves allocs reallocs frees h2d_calls h2d_bytes
for outer in 1 2 4 8; do
  line=$(/tmp/paired_after 160 "$outer" gpu | grep '^RESULT')
  get() { echo "$line" | grep -oE "$1=[0-9]+" | cut -d= -f2; }
  printf '%-6s %-7s %-8s %-9s %-11s %-8s %-11s %-11s\n' \
    160 "$(get outer)" "$(get solves)" "$(get allocs)" "$(get reallocs)" \
    "$(get frees)" "$(get h2d_calls)" "$(get h2d_bytes)"
done

echo
echo "Then the same at a different grid, to separate per-solve growth from per-size setup:"
printf '%-6s %-7s %-8s %-9s %-11s %-8s %-11s %-11s\n' \
  edge outer solves allocs reallocs frees h2d_calls h2d_bytes
for edge in 160 320 640; do
  line=$(/tmp/paired_after "$edge" 2 gpu | grep '^RESULT')
  get() { echo "$line" | grep -oE "$1=[0-9]+" | cut -d= -f2; }
  printf '%-6s %-7s %-8s %-9s %-11s %-8s %-11s %-11s\n' \
    "$edge" "$(get outer)" "$(get solves)" "$(get allocs)" "$(get reallocs)" \
    "$(get frees)" "$(get h2d_calls)" "$(get h2d_bytes)"
done
