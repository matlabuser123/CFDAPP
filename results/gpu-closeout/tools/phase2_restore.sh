#!/usr/bin/env bash
# GPU closeout Phase 2 -- prove the generated-output churn is timing-only, THEN
# restore it.
#
# The classifier is the project's own (results/p12-mesh-005/tools/). It parses
# .json/.md/.csv structurally and treats ONLY "runtime", "runtime_seconds",
# "seconds" and the Markdown column "runtime (s)" as timing keys -- a bare
# "wall" or "time" is physics (wall_shear_*, time_step) and is always compared.
#
# --restore copies back ONLY the files it classified RUNTIME-ONLY. It never
# touches a file classified VALUES or NEW, so a real numerical change cannot be
# silently reverted by it. That is why this is run in two passes: prove first,
# restore second.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-closeout
BASE=/tmp/gitbase
cd "$ROOT"

echo "##################### 1. snapshot HEAD #####################"
rm -rf "$BASE"; mkdir -p "$BASE"
git archive HEAD results cases tests/data 2>/dev/null | tar -x -C "$BASE" 2>/dev/null
echo "  HEAD = $(git rev-parse HEAD)"
echo "  snapshot files: $(find "$BASE" -type f | wc -l)"

echo ""
echo "##################### 2. PROVE: classify, do not restore #####################"
python3 results/p12-mesh-005/tools/classify_generated_outputs.py "$BASE" \
  > "$E/phase2-classification-before.log" 2>&1
tail -1 "$E/phase2-classification-before.log"
values=$(grep -c '^VALUES ' "$E/phase2-classification-before.log" || true)
echo "  VALUES (non-timing differences): $values"
if [ "$values" -ne 0 ]; then
  echo "  STOP: a non-timing difference exists. Not restoring."
  grep '^VALUES ' "$E/phase2-classification-before.log" | head -20
  exit 1
fi

echo ""
echo "##################### 3. restore the timing-only files #####################"
python3 results/p12-mesh-005/tools/classify_generated_outputs.py "$BASE" --restore \
  > "$E/phase2-restore.log" 2>&1
grep -E "^checked|^restored" "$E/phase2-restore.log" | sed 's/^/  /'

echo ""
echo "##################### 4. verify #####################"
n=$(git diff --name-only -- results/validation/ | wc -l)
echo "  git diff -- results/validation/ : $n files"
if [ "$n" -ne 0 ]; then
  echo "  REMAINING:"; git diff --name-only -- results/validation/ | sed 's/^/    /'
fi
echo "  git diff -- cases/ tests/data/ : $(git diff --name-only -- cases/ tests/data/ | wc -l) files"
echo ""
echo "  tracked modified now : $(git status --porcelain | grep -c '^ M')"
git diff --name-only | sed 's/^/    /'
exit $n
