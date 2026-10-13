#!/usr/bin/env bash
# P13-005 -- non-vacuity controls. Each deliberately breaks one thing and
# requires the named criterion to FAIL. Every source file is restored and the
# restore is verified by sha256 against the pre-control hash.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
B=build/final/apps/cli/cfdapp
BASE=results/p13/diagnostics/data/baseline-output
BK=/tmp/p13_diag_controls; rm -rf $BK; mkdir -p $BK

FILES="src/core/Logger.cpp apps/cli/main.cpp src/app/ProjectRunner.cpp"
for f in $FILES; do cp "$f" "$BK/$(echo $f | tr / _)"; done
sha256sum $FILES > $BK/pre.sha256
echo "pre-control hashes recorded:"; cat $BK/pre.sha256

restore() {
  for f in $FILES; do cp "$BK/$(echo $f | tr / _)" "$f"; done
  if sha256sum -c $BK/pre.sha256 --quiet; then echo "  RESTORED (sha256 verified)";
  else echo "  *** RESTORE FAILED ***"; exit 9; fi
}

rebuild() { cmake --build build/final -j 12 >/dev/null 2>&1 || { echo "  BUILD FAILED"; return 1; }; }

corpus_diff() { # -> "<stdout differing> <stderr differing>"
  local out=/tmp/p13_ctl_out; rm -rf $out
  bash results/p13/diagnostics/tools/cli_output_corpus.sh $out >/dev/null 2>&1
  local sd=0 se=0
  for f in "$BASE"/*.stdout; do diff -q "$f" "$out/$(basename $f)" >/dev/null 2>&1 || sd=$((sd+1)); done
  for f in "$BASE"/*.stderr; do diff -q "$f" "$out/$(basename $f)" >/dev/null 2>&1 || se=$((se+1)); done
  echo "$sd $se"
}

verdict() { # verdict <id> <must-break-count> <label>
  if [ "$2" -gt 0 ]; then echo "  $1: DETECTED ($3) -- the criterion failed, as it must";
  else echo "  $1: *** UNDETECTED *** ($3 = 0) -- the criterion is VACUOUS"; fi
}

echo
echo "########## N1 -- route Info back to stdout; C1 and C2 must break"
python3 - <<'PY'
import io
p='src/core/Logger.cpp'; s=io.open(p,encoding='utf-8',newline='').read()
o='bool targetsStderr(LogLevel) noexcept { return true; }'
assert s.count(o)==1, "N1 anchor missing"
s=s.replace(o,'bool targetsStderr(LogLevel level) noexcept {\n  return level == LogLevel::Warning || level == LogLevel::Error;\n}',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
if rebuild; then
  read SD SE <<<"$(corpus_diff)"
  echo "  corpus at default: stdout differing=$SD stderr differing=$SE"
  # C1 at every level: does a log line reach stdout?
  W=/tmp/p13_ctl_c1; rm -rf $W; cp -r tests/data/cases/valid_cavity $W; rm -rf $W/results
  $B --case $W --log-level debug >$W/o 2>/dev/null
  LEAK=$(grep -cE '^\[(TRACE|DEBUG|INFO)\]' $W/o)
  echo "  log lines on stdout at --log-level debug: $LEAK"
  verdict N1-C1 "$LEAK" "stdout log lines"
  verdict N1-C2 "$SD"  "stdout files differing"
fi
restore

echo
echo "########## N2 -- drop the CLI's explicit default level; V4 must break"
python3 - <<'PY'
import io
p='apps/cli/main.cpp'; s=io.open(p,encoding='utf-8',newline='').read()
o='  cfd::Logger::instance().setLevel(cfd::LogLevel::Warning);\n'
assert s.count(o)==1, "N2 anchor missing"
s=s.replace(o,'',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
if rebuild; then
  read SD SE <<<"$(corpus_diff)"
  echo "  corpus at default: stdout differing=$SD stderr differing=$SE"
  verdict N2-V4 "$SE" "stderr files differing"
fi
restore

echo
echo "########## N3 -- the comparison instrument itself must be able to fail"
rebuild
T=/tmp/p13_ctl_n3; rm -rf $T; cp -r $BASE $T
printf 'x' >> $T/valid_cavity.stdout
n3=0
for f in "$BASE"/*.stdout; do diff -q "$f" "$T/$(basename $f)" >/dev/null 2>&1 || n3=$((n3+1)); done
echo "  one byte appended to one of 7 baseline files -> differing: $n3"
verdict N3 "$n3" "files reported differing"

echo
echo "########## N4 -- delete one L point's emission; V3/L must break"
python3 - <<'PY'
import io
p='src/app/ProjectRunner.cpp'; s=io.open(p,encoding='utf-8',newline='').read()
o='  cfd::Logger::instance().debug(\n'
i=s.find('"solver start: "')
assert i>0, "N4 anchor missing"
j=s.rfind('cfd::Logger::instance().debug(',0,i)
k=s.find(';',i)
s=s[:j]+'(void)0'+s[k:]
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
if rebuild; then
  W=/tmp/p13_ctl_n4; rm -rf $W; cp -r tests/data/cases/valid_cavity $W; rm -rf $W/results
  $B --case $W --log-level debug >/dev/null 2>$W/e
  if grep -q "solver start:" $W/e; then echo "  L3 still present -> 0"; n4=0; else n4=1; fi
  echo "  --log-level debug stderr lines: $(wc -l <$W/e)  (intact run had 3)"
  verdict N4 "$n4" "L3 missing from the debug run"
fi
restore

echo
echo "########## rebuild the restored tree"
rebuild && echo "  restored tree builds"
sha256sum -c $BK/pre.sha256 && git status --porcelain -- $FILES
