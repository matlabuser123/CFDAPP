#!/usr/bin/env bash
# P18.0 section 1 -- freeze the P18 STARTING state, before any source file is modified.
#
# The fingerprint methodology is P13/P17.0's, reused UNCHANGED: hash the tracked diff against HEAD
# plus every untracked file, both restricted to code (results/ and the top-level control docs are
# excluded by construction, because evidence changes during qualification and must not move the
# fingerprint of the code being qualified).
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
Q=results/p18/baseline

DOCEXC=(. ':(exclude)results/' ':(exclude)TODO.md' ':(exclude)ROADMAP.md' ':(exclude)CLAUDE.md'
        ':(exclude)README.md' ':(exclude)CONTRIBUTING.md' ':(exclude)QUALITY_GATE.md'
        ':(exclude)PROJECT_STRUCTURE.md' ':(exclude)RELEASE_NOTES_*.md' ':(exclude)docs/')

echo "=== P18 STARTING STATE, frozen $(date -u +%FT%TZ) ==="
echo ""
echo "--- repository ---"
echo "HEAD           : $(git rev-parse HEAD)"
echo "branch         : $(git rev-parse --abbrev-ref HEAD)"
echo "upstream ahead : $(git rev-list --count @{u}..HEAD 2>/dev/null || echo 'no upstream')"
echo "submodules     : $(git submodule status 2>/dev/null | wc -l) entries"
DIRTY=$(git status --porcelain | wc -l)
echo "working tree   : $([ "$DIRTY" -eq 0 ] && echo CLEAN || echo "DIRTY -- $DIRTY entries")"
echo ""
echo "--- tracked modifications (code only, evidence excluded) ---"
git status --porcelain -- "${DOCEXC[@]}" | grep -v '^??' | wc -l | sed 's/^/  count: /'
echo "--- untracked files (code only) ---"
git ls-files --others --exclude-standard -- "${DOCEXC[@]}" | wc -l | sed 's/^/  count: /'
echo ""
echo "--- the FULL working-tree state, all paths, by status letter ---"
git status --porcelain | awk '{print substr($0,1,2)}' | sort | uniq -c | sed 's/^/  /'
echo ""

T=$(git diff --binary HEAD -- "${DOCEXC[@]}" | sha256sum | awk '{print $1}')
U=$(git ls-files --others --exclude-standard -- "${DOCEXC[@]}" \
    | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
FP=$(printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}')
echo "--- fingerprint components ---"
echo "  tracked-diff sha256   : $T"
echo "  untracked-set sha256  : $U"
echo ""
echo "P18_START_SOURCE_FINGERPRINT = $FP"
echo ""
echo "--- toolchain ---"
echo "  gcc        : $(gcc --version | head -1)"
echo "  g++        : $(g++ --version | head -1)"
echo "  clang      : $(clang --version 2>/dev/null | head -1 || echo 'n/a')"
echo "  cmake      : $(cmake --version | head -1)"
echo "  ninja      : $(ninja --version 2>/dev/null || echo 'n/a')"
echo "  nvcc       : $(nvcc --version 2>/dev/null | tail -2 | head -1 || echo 'n/a')"
echo "  sanitizer  : $(compute-sanitizer --version 2>/dev/null | head -3 | tail -1 || echo 'n/a')"
echo "  python3    : $(python3 --version)"
echo "  kernel     : $(uname -srm)"
echo "  distro     : $(. /etc/os-release && echo "$PRETTY_NAME")"
echo ""
echo "--- GPU ---"
nvidia-smi --query-gpu=name,driver_version,memory.total,compute_cap --format=csv,noheader 2>/dev/null | sed 's/^/  /' || echo "  nvidia-smi unavailable"
echo ""
echo "--- build configurations to be qualified ---"
for d in build/qual-final build/qual-cpuonly build/qual-debug; do
  if [ -d "$d" ]; then
    printf '  %-20s ' "$(basename "$d")"
    grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_BUILD_GUI|CFDAPP_ENABLE_CUDA|CFDAPP_ENABLE_OPENMP):' \
      "$d/CMakeCache.txt" 2>/dev/null | sed 's/^CFDAPP_//;s/^CMAKE_//;s/:BOOL=/=/;s/:STRING=/=/' | tr '\n' ' '
    echo ""
  else
    echo "  $(basename "$d")  ABSENT"
  fi
done
