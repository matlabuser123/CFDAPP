#!/usr/bin/env bash
# P18.5 -- NON-VACUITY OF THE COMPARISON INSTRUMENT (CLAUDE.md section 5.2).
#
# compare_probe.py reported PASS on the real log. That is worth nothing until the same instrument
# is shown to FAIL on a log it should reject. Five corruptions are injected into a COPY of the real
# log -- the original is never modified -- and each must be caught.
#
# This exists because of a recurring failure mode in this project: a probe that reads a missing
# file, greps for a renamed symbol, or parses nothing at all prints exactly what success looks
# like. Cases 4 and 5 below are that failure mode specifically.
set -u

cd "$(dirname "$0")/../../../.." || exit 1
DIR=results/p18/arrhenius_chemistry
LOG=$DIR/logs/04_probe.log
CMP=$DIR/tools/compare_probe.py
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [[ ! -f "$LOG" ]]; then
  echo "FAIL: $LOG is missing"
  exit 1
fi

status=0

expect() {
  local label="$1"
  local want="$2" # PASS or FAIL
  local file="$3"
  local out
  out=$(python3 "$CMP" "$file" 2>&1)
  local code=$?
  local got="PASS"
  [[ "$code" -ne 0 ]] && got="FAIL"
  if [[ "$got" == "$want" ]]; then
    printf "  [ok]       %-42s expected %s, got %s\n" "$label" "$want" "$got"
  else
    printf "  [NOT OK]   %-42s expected %s, got %s\n" "$label" "$want" "$got"
    echo "$out" | sed 's/^/             /'
    status=1
  fi
  # show the reason a rejection was given, so the instrument is seen to fail for the RIGHT reason
  if [[ "$want" == "FAIL" && "$got" == "FAIL" ]]; then
    echo "$out" | grep -E "^FAIL|eps >|not exactly 0" | head -2 | sed 's/^/             reason: /'
  fi
}

echo "=== comparator non-vacuity ==="
echo "instrument : $CMP"
echo "real log   : $LOG"
echo

# 0 -- the control: the unmodified log must PASS, or the four corruptions below prove nothing
cp "$LOG" "$WORK/clean.log"
expect "0 the unmodified log (control)" PASS "$WORK/clean.log"

# 1 -- perturb one omega by ~100 eps. Small enough to be invisible to the eye, large enough that
#      G6's 32 eps must reject it.
python3 - "$LOG" "$WORK/omega.log" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
out = []
done = False
for line in open(src):
    f = line.split()
    if not done and f[:1] == ["MATRIX"] and len(f) == 12:
        try:
            value = float(f[7])
        except ValueError:
            out.append(line)
            continue
        if value != 0.0:
            f[7] = repr(value * (1.0 + 100 * 2.220446049250313e-16))
            line = " ".join(f) + "\n"
            done = True
    out.append(line)
assert done, "no row was corrupted -- this corruption would prove nothing"
open(dst, "w").write("".join(out))
PY
expect "1 one omega_H2 off by 100 eps" FAIL "$WORK/omega.log"

# 2 -- a zero-concentration row made non-zero. q = 0 there is exact, so ANY non-zero value is wrong.
python3 - "$LOG" "$WORK/zero.log" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
out = []
done = False
for line in open(src):
    f = line.split()
    if not done and f[:1] == ["MATRIX"] and len(f) == 12 and f[1] == "0":
        f[6] = "1e-300"
        line = " ".join(f) + "\n"
        done = True
    out.append(line)
assert done, "no zero row was corrupted"
open(dst, "w").write("".join(out))
PY
expect "2 an exactly-zero q made 1e-300" FAIL "$WORK/zero.log"

# 3 -- the row count disagrees with the rows present
grep -v "^MATRIX 0 0.5 1000000 2200" "$LOG" > "$WORK/count.log"
expect "3 a matrix row deleted, MATRIX_ROWS stale" FAIL "$WORK/count.log"

# 4 -- AN EMPTY LOG. The failure mode this project keeps hitting: an instrument that measures
#      nothing and reports success.
: > "$WORK/empty.log"
expect "4 an empty log (measures nothing)" FAIL "$WORK/empty.log"

# 5 -- every zero-rate row removed, so the exact-zero branch is never exercised
grep -v "^MATRIX 0 0.5" "$LOG" | sed 's/^MATRIX_ROWS 36$/MATRIX_ROWS 27/' > "$WORK/nozero.log"
expect "5 no exactly-zero row present" FAIL "$WORK/nozero.log"

echo
if [[ "$status" -eq 0 ]]; then
  echo "NON-VACUITY: PASS -- the comparator accepts the real log and rejects all five corruptions"
else
  echo "NON-VACUITY: FAIL -- the comparator cannot be trusted"
fi
exit "$status"
