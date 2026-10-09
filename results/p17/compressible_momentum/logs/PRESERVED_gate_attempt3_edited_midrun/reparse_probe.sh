#!/usr/bin/env bash
# Does bash re-read a script from a byte offset mid-run, or pre-parse a { ... } | tee body?
# Decides whether gate attempt 3's post-edit output is trustworthy. Measured, not assumed.
set -uo pipefail
D=/mnt/c/Users/Hasib/AppData/Local/Temp/claude/c--Users-Hasib-Desktop-CFDAPP/42677f38-263b-4b01-b061-66669864a781/scratchpad
mkdir -p "$D"

# ---- case A: body wrapped in { ... } | tee, exactly like run_gate.sh ----------
cat > "$D/caseA.sh" <<'INNER'
#!/usr/bin/env bash
{
echo "A-start"
sleep 6
echo "A-ORIGINAL-TAIL"
} 2>&1 | tee /dev/null
INNER

# ---- case B: plain sequential body, no brace group ---------------------------
cat > "$D/caseB.sh" <<'INNER'
#!/usr/bin/env bash
echo "B-start"
sleep 6
echo "B-ORIGINAL-TAIL"
INNER

for c in A B; do
  bash "$D/case$c.sh" > "$D/case$c.out" 2>&1 &
  pid=$!
  sleep 2
  # insert many lines BEFORE the tail, shifting every later byte offset
  python3 - "$D/case$c.sh" "$c" <<'PY'
import sys
p, c = sys.argv[1], sys.argv[2]
s = open(p).read()
pad = "".join("echo \"%s-INSERTED-%d\"\n" % (c, i) for i in range(40))
s = s.replace('echo "%s-start"\n' % c, 'echo "%s-start"\n' % c + pad)
open(p, "w").write(s)
PY
  wait $pid
  echo "=== case $c ==="
  echo "  tail line seen: $(tail -1 "$D/case$c.out")"
  echo "  INSERTED lines executed: $(grep -c INSERTED "$D/case$c.out")"
done

echo ""
echo "Interpretation:"
echo "  ORIGINAL-TAIL + 0 inserted  -> the body was pre-parsed; a mid-run edit cannot affect it"
echo "  anything else               -> bash re-read from a shifted offset; output is corrupt"
