#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G10 step 1: freeze the pre-closeout state.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G10=$ROOT/results/p13/final-qualification/2026-09-24/g10-closeout
cd "$ROOT"
mkdir -p "$G10/logs" "$G10/data"

out=$G10/source_fingerprint.md
{
echo "# G10 — pre-closeout state, frozen"
echo
echo "Recorded $(date -u +%FT%TZ) before any G10 build or test ran."
echo
echo '## HEAD and working tree'
echo
echo '```text'
echo "HEAD          : $(git rev-parse HEAD)"
echo "HEAD short    : $(git rev-parse --short=12 HEAD)"
echo "branch        : $(git rev-parse --abbrev-ref HEAD)"
echo "dirty         : $([ -n "$(git status --porcelain)" ] && echo YES || echo NO)"
echo "tracked modified                 : $(git status --porcelain | grep -c '^ *M')"
echo "untracked                        : $(git status --porcelain | grep -c '^??')"
echo "tracked engineering files changed: $(git diff --name-only HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | wc -l)"
echo '```'
echo
echo 'The tree is deliberately dirty: P13, P14, P15 and P16 are all uncommitted.'
echo 'Commit and push are not authorized, so `3bd32f918656-modified` is the honest'
echo 'build identity and V2 is the qualification provenance. They are different'
echo 'things and are not conflated (see G5 §6).'
echo
echo '## The qualified source fingerprint'
echo
echo 'Canonical method: `g4-cli-gui/tools/fingerprint_v2.sh`, adopted at G4 when V1'
echo 'was withdrawn for including `TODO.md`. Excludes exactly `results/**` and'
echo '`TODO.md`; nothing else.'
echo
echo '```text'
bash "$ROOT/results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh" G10_before \
  | sed 's/^/  /'
echo '```'
echo
echo '### The qualified-state value this must match at closeout'
echo
echo '```text'
echo "V2 at G4 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
echo "V2 at G5 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
echo "V2 at G6 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
echo "V2 at G7 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
echo "V2 at G9 close  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c"
echo '```'
echo
echo 'Unmoved across five consecutive gates. That is the qualified state.'
echo
echo '## V1, for the record only'
echo
echo '```text'
echo "V1 (WITHDRAWN at G4)  86346f151e5359e37d23b13fca2cdc7f5549c88900a9f9dc7e77e58973fc8d5f"
echo '```'
echo
echo 'Never compared numerically with a V2 value: they hash different file sets.'
echo 'Withdrawn because it included `TODO.md`, which qualification itself edits at'
echo 'every gate closeout, so it could not survive closing a gate. Full derivation:'
echo '`g4-cli-gui/AMENDMENT_G4_fingerprint.md`.'
echo
echo '## Canonical build configurations for G10'
echo
echo 'The G1 qualification trees, built clean on 2026-09-24:'
echo
echo '```text'
for b in qual-final qual-debug qual-cpuonly; do
  printf '%-16s %s\n' "build/$b" "$(grep -hE 'CMAKE_BUILD_TYPE:STRING|CFDAPP_ENABLE_CUDA:BOOL|CFDAPP_BUILD_GUI:BOOL|CFDAPP_ENABLE_OPENMP:BOOL' "$ROOT/build/$b/CMakeCache.txt" 2>/dev/null | tr '\n' ' ')"
done
echo '```'
echo
echo '`build/final` is NOT used. It is `CFDAPP_ENABLE_CUDA:BOOL=OFF` with no'
echo '`cuda/libcfdcuda.a`, so it cannot be the Release+CUDA configuration. The'
echo 'frozen `regress_both.sh` iterates `build/cuda` and `build/final`; G10 applies'
echo 'its exclusions and invocation policy to the three canonical qual trees'
echo 'instead. Third gate to hit this (G6, G8, G9) -- see the G9 report.'
echo
echo '## Frozen invocation policy'
echo
echo '```text'
echo "ctest -j 8 --timeout 7200 -E <documented exclusions>"
echo '```'
echo
echo 'From regress_both.sh and G3 amendment. G3 first attempt used -j16 with'
echo 'the default 1500 s timeout and produced 250 timeouts and zero assertion'
echo 'failures; that invalid invocation is preserved at'
echo '`2026-09-24/logs/G3_FAILURE_PRESERVED_j16_default_timeout.log`.'
} > "$out"
cat "$out"
