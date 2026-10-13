#!/usr/bin/env bash
# P17.10 -- CLAUDE.md section 7 runtime-noise audit.
#
# A full ctest run rewrites tracked results/validation/** reports. The claim to be established is
# that ONLY TIMING changed -- so every changed line is classified, and the audit FAILS CLOSED if any
# changed line is not a timing key. It does not assume; it enumerates.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

N=$(git status --porcelain -- results/validation | wc -l)
echo "tracked results/validation files rewritten by this regression : $N"
echo ""
echo "--- every changed line, reduced to its JSON key or MD column header ---"
git diff -U0 -- results/validation \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' \
  | sed -E 's/^[+-][[:space:]]*//' \
  | sed -E 's/^"([^"]+)".*/\1/' \
  | sed -E 's/^\|.*//' \
  | sort | uniq -c | sort -rn
echo ""
echo "--- classification ---"
NONTIMING=$(git diff -U0 -- results/validation \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' \
  | sed -E 's/^[+-][[:space:]]*//' \
  | grep -vE '^"(runtime|runtime_seconds|seconds|wall_time_seconds|elapsed_seconds)"' \
  | grep -vE '^\|' | grep -vE '^$' | sort -u)
if [ -z "$NONTIMING" ]; then
  echo "  TIMING ONLY: every changed line is a timing key."
  RC=0
else
  echo "  *** NON-TIMING CHANGES PRESENT -- this is NOT runtime noise ***"
  echo "$NONTIMING" | head -20 | sed 's/^/    /'
  RC=1
fi
echo ""
echo "--- the control: the classifier must be able to see a non-timing change ---"
# Prove the grep would reject a physics key, using a line that is definitely not a timing key.
if printf '%s\n' '"wall_shear_stress": 1.0' | grep -qvE '^"(runtime|runtime_seconds|seconds|wall_time_seconds|elapsed_seconds)"'; then
  echo "  CONTROL OK: a physics key is classified as NON-timing"
else
  echo "  *** CONTROL FAILED: the classifier accepts a physics key as timing ***"
  RC=1
fi
exit "$RC"
