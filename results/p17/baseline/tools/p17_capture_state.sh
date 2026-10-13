#!/usr/bin/env bash
# P17.0 step 1 -- freeze the exact starting state. READ-ONLY on the repo.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1
OUT=$ROOT/results/p17/baseline
mkdir -p "$OUT/logs" "$OUT/tools"

{
echo "=== date (UTC) ==="; date -u +%FT%TZ
echo; echo "=== HEAD ==="; git rev-parse HEAD; git rev-parse --short=12 HEAD
echo; echo "=== branch ==="; git branch --show-current
echo; echo "=== HEAD subject ==="; git log -1 --pretty='%H%n%ad%n%s' --date=iso
echo; echo "=== counts ==="
echo "tracked modified                  : $(git status --porcelain | grep -c '^ M\|^M ')"
echo "tracked all changed (any status)   : $(git status --porcelain | grep -vc '^??')"
echo "untracked                          : $(git status --porcelain | grep -c '^??')"
echo "tracked engineering files changed  : $(git diff --name-only HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | wc -l)"
echo "untracked engineering+output files : $(git ls-files --others --exclude-standard -- . ':(exclude)results/' ':(exclude)TODO.md' | wc -l)"
echo; echo "=== submodules ==="
if [ -f .gitmodules ]; then git submodule status; else echo "NONE -- no .gitmodules in the repository"; fi
echo; echo "=== stash ==="; git stash list | head -5; echo "(entries: $(git stash list | wc -l))"
echo; echo "=== git status --short (full) ==="; git status --short
echo; echo "=== git status (long, first 40 lines) ==="; git status | head -40
} > "$OUT/logs/01_git_state.log" 2>&1

# ---- fingerprint V2, method verbatim from P13 G4 fingerprint_v2.sh ----
{
echo "SOURCE_FINGERPRINT_V2 -- P17.0 freeze"
echo "date      : $(date -u +%FT%TZ)"
echo "HEAD      : $(git rev-parse HEAD)"
echo "method    : results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh"
echo "definition: sha256(tracked-diff-sha256 SP untracked-contents-sha256)"
echo "            EXCLUDE = results/**  and  TODO.md   (exactly these two)"
echo ""
T=$(git diff --binary HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | sha256sum | awk '{print $1}')
U=$(git ls-files --others --exclude-standard -- . ':(exclude)results/' ':(exclude)TODO.md' \
    | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
V2=$(printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}')
echo "file counts"
echo "  tracked engineering files changed : $(git diff --name-only HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | wc -l)"
echo "  untracked engineering+output files: $(git ls-files --others --exclude-standard -- . ':(exclude)results/' ':(exclude)TODO.md' | wc -l)"
echo ""
echo "components"
echo "  tracked diff sha256      : $T"
echo "  untracked contents sha256: $U"
echo ""
echo "SOURCE_FINGERPRINT_V2 = $V2"
echo ""
echo "reference values"
echo "  P13 G4..G10 qualified    : 8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
echo "  P13 final / P17 baseline : 721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955"
echo "  post-P17.2 (recorded)    : fb69dc2a9e045f939df71243a01cea931cb7ce29a64e35b2171bc95c9cd284af"
} > "$OUT/source_fingerprint.txt" 2>&1

# ---- the P17.2 delta, attributed file by file ----
{
echo "=== files changed vs HEAD, restricted to the four P17.2 files ==="
for f in include/cfd/compressible/EquationOfState.hpp include/cfd/compressible/IdealGasEOS.hpp \
         src/compressible/IdealGasEOS.cpp tests/unit/compressible/test_ideal_gas_eos.cpp; do
  printf '%-52s ' "$f"
  git diff --numstat HEAD -- "$f" | awk '{printf "+%s -%s\n", $1, $2}'
done
echo
echo "=== sha256 of the four files as they stand now ==="
sha256sum include/cfd/compressible/EquationOfState.hpp include/cfd/compressible/IdealGasEOS.hpp \
          src/compressible/IdealGasEOS.cpp tests/unit/compressible/test_ideal_gas_eos.cpp
echo
echo "=== total numstat over those four ==="
git diff --numstat HEAD -- include/cfd/compressible/EquationOfState.hpp \
  include/cfd/compressible/IdealGasEOS.hpp src/compressible/IdealGasEOS.cpp \
  tests/unit/compressible/test_ideal_gas_eos.cpp | awk '{a+=$1; d+=$2} END{print "insertions:", a, " deletions:", d}'
} > "$OUT/logs/02_p17_2_delta.log" 2>&1

# ---- toolchain ----
{
echo "=== date ==="; date -u +%FT%TZ
echo; echo "=== host ==="; uname -a; echo "WSL distro: $(. /etc/os-release; echo "$PRETTY_NAME")"
echo "cores (nproc): $(nproc)"; echo; free -h | head -2
echo; echo "=== compilers ==="; c++ --version | head -2; echo; gcc --version | head -1
which clang-format-18 >/dev/null 2>&1 && clang-format-18 --version || echo "clang-format-18: not found"
echo; echo "=== cmake / ctest / ninja ==="; cmake --version | head -1; ctest --version | head -1
which ninja >/dev/null 2>&1 && ninja --version || echo "ninja: not on PATH"
echo; echo "=== CUDA ==="
for c in /usr/local/cuda-12.9/bin/nvcc /usr/local/cuda/bin/nvcc /usr/bin/nvcc; do
  [ -x "$c" ] && { echo "-- $c"; "$c" --version | tail -4; }
done
echo; echo "=== GPU ==="; nvidia-smi --query-gpu=name,compute_cap,driver_version,memory.total --format=csv 2>&1
echo; echo "=== nvidia-smi header ==="; nvidia-smi 2>&1 | head -4
echo; echo "=== build trees present ==="
for d in build/qual-final build/qual-debug build/qual-cpuonly; do
  if [ -f "$d/CMakeCache.txt" ]; then
    echo "-- $d   (CMakeCache mtime $(date -u -r "$d/CMakeCache.txt" +%FT%TZ))"
    grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_ENABLE_CUDA|CFDAPP_BUILD_GUI|CFDAPP_ENABLE_OPENMP|CMAKE_CUDA_ARCHITECTURES|CMAKE_CXX_COMPILER):' "$d/CMakeCache.txt" | sed 's/^/     /'
  else echo "-- $d   ABSENT"; fi
done
echo; echo "=== Qt (Debug+GUI configuration) ==="
grep -E '^Qt6_DIR|^QT_DIR' build/qual-debug/CMakeCache.txt 2>/dev/null | sed 's/^/  /' || echo "  not recorded"
} > "$OUT/logs/03_toolchain.log" 2>&1

echo "CAPTURE DONE"
grep -m1 'SOURCE_FINGERPRINT_V2 =' "$OUT/source_fingerprint.txt"
tail -4 "$OUT/logs/02_p17_2_delta.log"
