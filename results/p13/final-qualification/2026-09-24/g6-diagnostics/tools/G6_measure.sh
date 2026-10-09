#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G6, the measurement half.
#
# The frozen instruments are used unmodified:
#   results/p13/diagnostics/tools/cli_output_corpus.sh   C2, V4
#   results/p13/diagnostics/tools/verify_c2b.sh          C2b (35 comparisons)
#   results/p13/diagnostics/tools/verify_V_L.sh          V1, V2, V3, C1, L1..L8
# Only the OUTPUT destination differs: they are invoked from here so their
# results land under the qualification tree instead of overwriting the
# committed P13-005 logs.
#
# results/p13/diagnostics/tools/final.sh is deliberately NOT run whole: it
# bundles the three-configuration full regression, which is G10's gate and takes
# hours. Its P3 (hot-loop) and P2 (earlier gates) sections are reproduced here.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G6=$ROOT/results/p13/final-qualification/2026-09-24/g6-diagnostics
L=$G6/logs
B=build/final/apps/cli/cfdapp
BASE=results/p13/diagnostics/data/baseline-output
cd "$ROOT"
mkdir -p "$L"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

log=$L/G6_measure.log
{
echo "P13 G6 -- diagnostics / logging, measurement half"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### build/final (Release+CUDA) -- the binary every frozen instrument uses ###"
echo "  NOTE: its previous binary dated 2026-09-25 05:26, which is inside the window"
echo "  when another session's controls had src/io/CaseReader.cpp patched. Rebuilt"
echo "  here from the restored tree rather than trusted."
cmake --build build/final -j20 > /tmp/g6_build.log 2>&1
echo "  build rc=$?"
tail -2 /tmp/g6_build.log | sed 's/^/    /'
ls -l --time-style=+%F_%H:%M "$B" | sed 's/^/    /'
echo ""

echo "### C2 / V4 -- the corpus at the DEFAULT level, against the frozen baseline ###"
O=/tmp/g6_corpus_default; rm -rf $O
bash results/p13/diagnostics/tools/cli_output_corpus.sh $O > /dev/null 2>&1
ds=0; de=0
for f in "$BASE"/*.stdout; do
  b=$(basename "$f")
  if diff -q "$f" "$O/$b" >/dev/null 2>&1; then :; else ds=$((ds+1)); echo "    STDOUT DIFFERS: $b"; fi
done
for f in "$BASE"/*.stderr; do
  b=$(basename "$f")
  if diff -q "$f" "$O/$b" >/dev/null 2>&1; then :; else de=$((de+1)); echo "    STDERR DIFFERS: $b"; fi
done
echo "  C2  stdout files differing from baseline: $ds / 7   (0 required)"
echo "  V4  stderr files differing from baseline: $de / 7   (0 required)"
echo ""

echo "### C2b -- stdout byte-identical at EVERY level (35 comparisons) ###"
bash results/p13/diagnostics/tools/verify_c2b.sh 2>&1 | sed 's/^/  /'
echo ""

echo "### V1 / V2 / V3 / C1 / L1..L8 -- the frozen verifier, unmodified ###"
bash results/p13/diagnostics/tools/verify_V_L.sh 2>&1 | sed 's/^/  /'
echo ""

echo "### P3 -- no logging in a hot loop ###"
echo "  -- static: Logger call sites inside the SIMPLE outer-iteration loop --"
python3 - <<'PY'
import io, re
s = io.open('src/pressure_velocity/SIMPLE.cpp', encoding='utf-8').read()
i = s.find('for (Index iteration')
sites = [m.start() for m in re.finditer(r'Logger::instance\(\)', s)]
print('    Logger call sites in SIMPLE.cpp :', len(sites))
print('    inside the outer-iteration loop :', sum(1 for p in sites if p > i))
PY
echo "  -- dynamic: stderr lines must NOT grow with the iteration count --"
for it in 5 50; do
  W=/tmp/g6_p3_$it; rm -rf $W; cp -r tests/data/cases/valid_cavity $W; rm -rf $W/results
  python3 - $W/solver.json $it <<'PY'
import json, sys
d = json.load(open(sys.argv[1])); d["max_iterations"] = int(sys.argv[2])
json.dump(d, open(sys.argv[1], "w"), indent=2)
PY
  n=$("$B" --case $W --log-level trace 2>&1 >/dev/null | wc -l)
  echo "    max_iterations=$it -> stderr lines at --log-level trace: $n"
done
echo "  (the frozen criterion: the two counts must be equal -- diagnostics are"
echo "   emitted around lifecycle transitions, never per outer iteration)"
echo ""

echo "### Step 7 -- the log must agree with the production result ###"
W=/tmp/g6_consistency; rm -rf $W; cp -r tests/data/cases/valid_cavity $W; rm -rf $W/results
python3 - $W/solver.json <<'PY'
import json, sys
d = json.load(open(sys.argv[1])); d["max_iterations"] = 9
json.dump(d, open(sys.argv[1], "w"), indent=2)
PY
"$B" --case $W --log-level debug > $W/out.txt 2> $W/err.txt
echo "  stdout (the report the client prints):"
grep -iE 'converged|iterations|backend|residual' $W/out.txt | sed 's/^/    /'
echo "  stderr (the diagnostic trail):"
grep -iE 'solve finished|solver start|backend' $W/err.txt | sed 's/^/    /'
echo "  metadata.json (the exported state):"
python3 - "$W/results/metadata.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
s = m["solver"]
print("    iterations=%s status=%s backend=%s gpu_disc=%s"
      % (s["iterations"], s["status"], s["linear_solver_backend"], s["gpu_discretization_used"]))
PY
echo ""

echo "### P2 -- the earlier gates still pass ###"
ctest --test-dir build/final -R "Restart|Checkpoint|ResultExport|ExportWriteFailure|CaseDiagnostic|CFDAppCli" \
      > $L/G6_P2_earlier_gates.log 2>&1
echo "  ctest rc=$?"
grep -E 'tests passed|tests failed out of' $L/G6_P2_earlier_gates.log | sed 's/^/  /'
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
