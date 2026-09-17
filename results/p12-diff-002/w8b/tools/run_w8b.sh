#!/usr/bin/env bash
# P12-DIFF-002 W8B -- the authoritative run (acceptance_gate.md section 8).
#   integrity checks (fail closed) -> configure -> CLEAN-FIRST rebuild of everything -> proof the
#   binaries correspond to the source -> both production-case suites.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
P=$R/results/p12-diff-002/w8b
LOG=$P/logs/01_integrity_build.log
RUN=$P/logs/02_W8B.log
cd "$R" || exit 1
fail() { echo "FAIL CLOSED: $*"; exit 1; }
{
  echo "# P12-DIFF-002 W8B authoritative run; $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
  echo
  echo "## (a) repository tests == frozen candidate"
  for f in test_structured_quad_production_case.cpp test_multiblock_production_case.cpp; do
    a=$(sha256sum "tests/integration/case/$f" | cut -d' ' -f1); b=$(sha256sum "$P/data/candidate/$f" | cut -d' ' -f1)
    echo "  $f $a"; [ "$a" = "$b" ] || fail "$f differs from the candidate"
  done
  echo "## (b) case edits are exactly DRIFT-001 F1"
  python3 - <<'EOF' || exit 1
import json, subprocess, sys
ok = True
for case, before_hash in (("poiseuille_distorted", None), ("curved_channel_multiblock", None)):
    now = json.load(open(f"cases/{case}/solver.json"))
    if now.get("face_flux") != "rhie_chow":
        print("  face_flux missing in", case); ok = False
    print(f"  {case}/solver.json keys: {sorted(now)}")
sys.exit(0 if ok else 1)
EOF
  # Reverse-apply proof: removing exactly the added line / sentence restores the frozen hashes.
  S1=$(grep -v '^  "face_flux": "rhie_chow",$' cases/poiseuille_distorted/solver.json | sha256sum | cut -d' ' -f1)
  S2=$(grep -v '^  "face_flux": "rhie_chow",$' cases/curved_channel_multiblock/solver.json | sha256sum | cut -d' ' -f1)
  C1=$(sed 's| The Rhie-Chow face flux (face_flux=rhie_chow) is selected because the 2D linear flux leaves an undamped odd-even pressure mode on this open domain, so its solutions keep moving long after the outer tolerance is met -- see results/p12-grad-002/drift-001/summary.md.",$|",|' cases/poiseuille_distorted/case.json | sha256sum | cut -d' ' -f1)
  C2=$(sed 's| The Rhie-Chow face flux (face_flux=rhie_chow) is selected because the 2D linear flux leaves an undamped odd-even pressure mode on this open domain, which contaminates the pressure gradient long after the outer tolerance is met -- see results/p12-grad-002/drift-001/summary.md.",$|",|' cases/curved_channel_multiblock/case.json | sha256sum | cut -d' ' -f1)
  echo "  reverse-applied: solver $S1 / $S2, case $C1 / $C2"
  [ "$S1" = b1c19195209e1f85b1a1b1d044c8f155e519fb90cdd789d5b60b9f7398c5f5d4 ] || fail "poiseuille_distorted/solver.json edit is not exactly F1"
  [ "$S2" = 7a8fb438295cb414599ae261653e78a4a4299feaac7e085222354238d26d0fd4 ] || fail "curved_channel_multiblock/solver.json edit is not exactly F1"
  [ "$C1" = 60516117a3773587844f8e7285100f20d5e0148514b0529052b5b330c232e235 ] || fail "poiseuille_distorted/case.json edit is not exactly F1"
  [ "$C2" = 65e66e1af656e501d2ce1e0cf9f22d5f71bba0ee253e99e2c985624157dcd757 ] || fail "curved_channel_multiblock/case.json edit is not exactly F1"
  REST=$(find cases -type f ! -path 'cases/poiseuille_distorted/solver.json' ! -path 'cases/curved_channel_multiblock/solver.json' ! -path 'cases/poiseuille_distorted/case.json' ! -path 'cases/curved_channel_multiblock/case.json' ! -path '*/results/*' -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
  echo "  rest of cases/: $REST"; [ "$REST" = 49f77e957b00481697d56c879d32d555520a4b6b74b5949abfd783a731ec4cc1 ] || fail "other case files changed"
  echo "## (c) production unchanged"
  TREE=$(find src include -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
  echo "  src/ + include/ tree $TREE"; [ "$TREE" = 0bc5c6c3dc2aaf411e1858019b701616611642aadf3b4692258a30f572068846 ] || fail "production sources changed"
  echo "## (d) frozen evidence byte-identical"
  sed -n '/## evidence that must stay byte-identical/,/^$/p' "$P/logs/00_freeze.log" | grep -E '^[0-9a-f]{64}' | sha256sum -c - || fail "frozen evidence changed"
  sed -n '/## gates (frozen now)/,/^$/p' "$P/logs/00_freeze.log" | grep -E '^[0-9a-f]{64}' | sha256sum -c - || fail "a gate changed after freezing"
  echo
  echo "## configure + clean-first rebuild of everything"
  cmake -S "$R" -B "$B" > "$P/logs/01_configure.txt" 2>&1 || fail "configure"
  START=$(date +%s)
  cmake --build "$B" -j"$(nproc)" --clean-first > "$P/logs/01_build.txt" 2>&1 || { tail -30 "$P/logs/01_build.txt"; fail "build"; }
  echo "  BUILD OK ($(wc -l < "$P/logs/01_build.txt") lines)"
  echo "  warnings in the two test sources: $(grep -cE 'test_(structured_quad|multiblock)_production_case\.cpp:[0-9]+:[0-9]+: warning' "$P/logs/01_build.txt")"
  NEWEST=$(find "$R/src" "$R/include" "$R/tests" -type f \( -name '*.cpp' -o -name '*.hpp' \) -printf '%T@ %p\n' | sort -rn | head -1)
  NT=${NEWEST%% *}
  echo "  newest source: ${NEWEST#* }"
  STALE=0; TOTAL=0
  while read -r t bin; do TOTAL=$((TOTAL+1)); awk -v a="$t" -v b="$NT" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "  STALE $bin"; }; done \
    < <(find "$B" -type f -name 'CFD*Tests' -printf '%T@ %p\n')
  echo "  test binaries $TOTAL, stale $STALE"; [ "$STALE" = 0 ] || fail "stale binaries"
  find "$B" -type f -name 'CFD*Tests' -exec chmod +x {} + 2>/dev/null
  echo "  libcfdcore.a $(sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1)"
  BIN=$(find "$B" -type f -name CFDCaseIntegrationTests | head -1)
  echo "  CFDCaseIntegrationTests $(sha256sum "$BIN" | cut -d' ' -f1)"
} > "$LOG" 2>&1
cat "$LOG"
grep -q 'FAIL CLOSED' "$LOG" && exit 1
BIN=$(find "$B" -type f -name CFDCaseIntegrationTests | head -1)
{
  echo "# P12-DIFF-002 W8B -- production-case suites; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore.a $(sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "# binary       $(sha256sum "$BIN" | cut -d' ' -f1)"
  "$BIN" --gtest_filter='StructuredQuadProductionCase.*:MultiBlockProductionCase.*'
  echo "exit $?"
} > "$RUN" 2>&1
grep -E '^\[  (PASSED|FAILED) |^\[       OK|Failure|exit ' "$RUN"
