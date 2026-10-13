#!/usr/bin/env bash
# SOURCE_FINGERPRINT_V2 -- the amended qualification source fingerprint.
#
# Authorized 2026-09-25. See AMENDMENT_G4_fingerprint.md for why V1 was
# withdrawn. V1 is NOT recomputed here and a V2 value must NEVER be compared
# numerically with a V1 value: they hash different file sets.
#
# EXACT FILE-SELECTION ALGORITHM
# ------------------------------
#   repo root = /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
#   EXCLUDE   = results/**   and   TODO.md        (exactly these two, no others)
#
#   1. tracked component
#        git diff --binary HEAD -- . :(exclude)results/ :(exclude)TODO.md
#        sha256 of that byte stream
#   2. untracked component
#        git ls-files --others --exclude-standard -- . :(exclude)results/ :(exclude)TODO.md
#        sorted; sha256sum of each file; sha256 of that digest list
#   3. SOURCE_FINGERPRINT_V2 = sha256("<tracked> <untracked>")
#
# The untracked component is INCLUDED deliberately. The authorization says
# "the tracked engineering tree"; taken literally that would drop ~120 untracked
# P14/P15/P16 source files (src/turbulence/WallFunctions.cpp,
# src/thermal/Enthalpy.cpp, the whole cuda/ turbulence set, ...) out of the
# hash and leave the instrument blind to most of the new code. Including them is
# strictly stronger and matches V1's own composition. Flagged in the amendment
# rather than decided silently.
#
# ATTRIBUTION BREAKDOWN, not an exclusion. Two untracked directories are solver
# OUTPUT, not engineering source, and are rewritten whenever a suite runs:
#   cli_log_level_valid_cavity_cli_smoke/results/
#   cases/turbulent_heated_channel/results/
# They stay INSIDE V2 -- "do not exclude arbitrary additional files merely to
# obtain a match". They are additionally hashed on their own so that if V2 moves,
# the cause is attributable in one step instead of being argued about.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
LABEL="${1:?usage: fingerprint_v2.sh <label>}"
OUT=results/p13/final-qualification/2026-09-24/g4-cli-gui/v2_${LABEL}.txt

TRACKED=$(git diff --binary HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' \
          | sha256sum | cut -d' ' -f1)
UNTRACKED_LIST=$(git ls-files --others --exclude-standard -- . ':(exclude)results/' ':(exclude)TODO.md' | sort)
UNTRACKED=$(printf '%s\n' "$UNTRACKED_LIST" | xargs -r sha256sum | sha256sum | cut -d' ' -f1)
V2=$(printf '%s %s' "$TRACKED" "$UNTRACKED" | sha256sum | cut -d' ' -f1)

# breakdown only -- never subtracted from V2
OUTPUTS=$(printf '%s\n' "$UNTRACKED_LIST" | grep -E '(^cli_log_level_|^cases/).*/results/' || true)
ENGINEERING=$(printf '%s\n' "$UNTRACKED_LIST" | grep -vE '(^cli_log_level_|^cases/).*/results/' || true)
OUT_HASH=$( [ -n "$OUTPUTS" ] && printf '%s\n' "$OUTPUTS" | xargs -r sha256sum | sha256sum | cut -d' ' -f1 || echo none )
ENG_HASH=$( [ -n "$ENGINEERING" ] && printf '%s\n' "$ENGINEERING" | xargs -r sha256sum | sha256sum | cut -d' ' -f1 || echo none )

NTRACKED=$(git diff --name-only HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | wc -l)
NUNTRACKED=$(printf '%s\n' "$UNTRACKED_LIST" | grep -c . || true)
NOUT=$(printf '%s\n' "$OUTPUTS" | grep -c . || true)

{
  echo "label     : $LABEL"
  echo "date      : $(date -u +%FT%TZ)"
  echo "HEAD      : $(git rev-parse HEAD)"
  echo "definition: SOURCE_FINGERPRINT_V2 (excludes results/** and TODO.md only)"
  echo ""
  echo "file counts"
  echo "  tracked engineering files changed : $NTRACKED"
  echo "  untracked engineering+output files: $NUNTRACKED"
  echo "    of which solver-output files    : $NOUT"
  echo ""
  echo "SOURCE_FINGERPRINT_V2 = $V2"
  echo ""
  echo "components"
  echo "  tracked diff sha256      : $TRACKED"
  echo "  untracked contents sha256: $UNTRACKED"
  echo ""
  echo "attribution breakdown (diagnostic only -- NOT excluded from V2)"
  echo "  untracked ENGINEERING source : $ENG_HASH"
  echo "  untracked SOLVER OUTPUT      : $OUT_HASH"
} | tee "$OUT"
