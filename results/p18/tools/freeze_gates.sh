#!/usr/bin/env bash
# P18.0 section 28 -- freeze the gate definitions and the audit they were derived from.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
echo "=== P18 gates frozen at $(date -u +%FT%TZ) ==="
sha256sum results/p18/gates/P18_GATES.md
echo ""
echo "=== the audit the gates were derived from ==="
sha256sum results/p18/audit/*.md
echo ""
echo "=== P18 start fingerprint, re-verified ==="
DOCEXC=(. ':(exclude)results/' ':(exclude)TODO.md' ':(exclude)ROADMAP.md' ':(exclude)CLAUDE.md'
        ':(exclude)README.md' ':(exclude)CONTRIBUTING.md' ':(exclude)QUALITY_GATE.md'
        ':(exclude)PROJECT_STRUCTURE.md' ':(exclude)RELEASE_NOTES_*.md' ':(exclude)docs/')
T=$(git diff --binary HEAD -- "${DOCEXC[@]}" | sha256sum | awk '{print $1}')
U=$(git ls-files --others --exclude-standard -- "${DOCEXC[@]}" \
    | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
echo "P18_START_SOURCE_FINGERPRINT = $(printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}')"
echo "expected (frozen at P18.0 start) = 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318"
echo ""
echo "=== inherited P17 gates, must be unmoved ==="
sha256sum results/p17/gates/P17_GATES.md results/p17/density_based_solver/acceptance_gate.md \
          results/p17/shock_validation/acceptance_gate.md results/p17/cpu_gpu_equivalence/acceptance_gate.md
