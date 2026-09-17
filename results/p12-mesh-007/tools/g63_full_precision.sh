#!/usr/bin/env bash
# P12-MESH-007: full-precision readout of G6.3 (NOT a gate run; runs only AFTER gate_rerun.sh).
# The frozen test prints the G6.3 maxima with %.3e, and gtest prints the full value only when the
# assertion fails. This diagnostic compiles a COPY of tests/solver/piso/test_ale_piso.cpp in which
# the only change is %.3e -> %.17e in the one printf of the G6.3 summary line (checked below), with
# the exact Release compile command of CFDPisoTests, and links it against the same build/release
# libcfdcore.a and gtest. Its %.17e values, rounded to %.3e, must reproduce log 22's printed line.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
L=$R/results/p12-mesh-007/logs
W=$HOME/m7fp
LOG=$L/23_g63_full_precision_NOT_gate.log
[ -e "$LOG" ] && { echo "REFUSED: $LOG exists"; exit 1; }
[ -e "$L/22_rerun_stage3_G1.2_G5_G6_G7_G8.log" ] || { echo "REFUSED: run gate_rerun.sh first"; exit 1; }
rm -rf "$W"; mkdir -p "$W"
cd "$R" || exit 1
{
  echo "# G6.3 full-precision readout (NOT a gate criterion); $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
  src=tests/solver/piso/test_ale_piso.cpp
  echo "# source $src sha256 $(sha256sum $src | cut -d' ' -f1)"
  python3 - "$src" "$W/test_ale_piso_fp.cpp" <<'EOF'
import sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()
key = '"G6.3 translating cavity (ALE): max |u_B - b - u_A| %.3e (<= 1e-8), gauge-aligned |p| %.3e "\n      "(<= 1e-8), |F_rel,B - F_A| %.3e (<= 6.25e-10)\\n"'
assert text.count(key) == 1, "G6.3 summary printf not found exactly once"
new = key.replace("%.3e", "%.17e")
open(dst, "w", newline="\n").write(text.replace(key, new))
print("# copy written: the G6.3 summary printf's three %.3e -> %.17e")
EOF
  echo "## diff (source -> diagnostic copy)"
  diff "$src" "$W/test_ale_piso_fp.cpp"
  echo "## diff lines: $(diff "$src" "$W/test_ale_piso_fp.cpp" | grep -c '^[<>]') (expected 4: two changed lines each side)"
  CMD=$(python3 -c "
import json
for e in json.load(open('$B/compile_commands.json')):
    if e['file'].endswith('tests/solver/piso/test_ale_piso.cpp'):
        print(e['command'])
")
  CMD=${CMD/-o tests\/solver\/piso\/CMakeFiles\/CFDPisoTests.dir\/test_ale_piso.cpp.o/-o $W/test_ale_piso_fp.o}
  CMD=${CMD/-c $R\/tests\/solver\/piso\/test_ale_piso.cpp/-c $W/test_ale_piso_fp.cpp}
  echo "## compile: $CMD"
  case "$CMD" in *"-o $W/test_ale_piso_fp.o -c $W/test_ale_piso_fp.cpp"*) ;; *) echo "compile command substitution FAILED"; exit 1 ;; esac
  ( cd "$B" && eval "$CMD" ) || { echo "compile FAILED"; exit 1; }
  ( cd "$B" && /usr/bin/c++ -O3 -DNDEBUG "$W/test_ale_piso_fp.o" -o "$W/AleFp" src/libcfdcore.a lib/libgtest_main.a lib/libgtest.a ) || { echo "link FAILED"; exit 1; }
  echo "# libcfdcore.a $(sha256sum $B/src/libcfdcore.a | cut -d' ' -f1)"
  echo "## run"
  "$W/AleFp" --gtest_filter='AlePisoGalilean.TranslatingCavityIsTheFixedCavityPlusTheTranslation' 2>&1
  echo "exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "## consistency with log 22"
} > "$LOG.partial" 2>&1
python3 - "$LOG.partial" "$L/22_rerun_stage3_G1.2_G5_G6_G7_G8.log" >> "$LOG.partial" 2>&1 <<'EOF'
import re, sys
fp = [l for l in open(sys.argv[1]) if l.startswith("G6.3 translating cavity (ALE)")]
g = [l for l in open(sys.argv[2]) if l.startswith("G6.3 translating cavity (ALE)")]
num = re.compile(r"[-+]?\d+\.\d+e[-+]\d+")
if len(fp) != 1 or len(g) != 1:
    print(f"CONSISTENCY: cannot compare ({len(fp)} diagnostic lines, {len(g)} log-22 lines)")
    sys.exit(0)
# the three measured values precede each "(<= ...)" threshold; the threshold 6.25e-10 is not one
# of them (fixed after the first run, whose check counted it -- see log 23b)
a = num.findall(re.sub(r"\([^)]*\)", "", fp[0]))
b = num.findall(re.sub(r"\([^)]*\)", "", g[0]))
names = ["max |u_B - b - u_A|", "gauge-aligned |p|", "|F_rel,B - F_A|"]
ok = len(a) == 3 and len(b) == 3 and all(f"{float(x):.3e}" == y for x, y in zip(a, b))
for n, x, y in zip(names, a, b):
    print(f"  {n}: full precision {x}  -> %.3e {float(x):.3e}; log 22 prints {y}")
print("CONSISTENCY with log 22:", "REPRODUCED" if ok else "NOT REPRODUCED")
EOF
mv "$LOG.partial" "$LOG"
cat "$LOG"
