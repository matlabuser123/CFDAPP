#!/usr/bin/env bash
# CI-PERF-001 negative control: the coverage audit must FAIL on each defect it
# claims to detect, and PASS on a correct partition.  An audit that cannot fail
# proves nothing (CLAUDE.md 5.2).
set -u
cd "$(dirname "$0")/../.."
LIST=results/ci-perf-001/baseline/ci_ctest_full_list.txt
WORK=$(mktemp -d)
SHARDS=3
CONFIG=clang_debug
pass=0
fail=0

build_good() {
  local dir="$1"
  mkdir -p "$dir"
  for k in $(seq 1 $SHARDS); do
    cp "$LIST" "$dir/grp__full__$k.txt"
    idx=$(python3 scripts/ci/shard_tests.py --list-file "$LIST" --config $CONFIG \
            --shards $SHARDS --shard "$k")
    ctest_sel_from_indices "$LIST" "$idx" > "$dir/grp__sel__$k.txt"
    # executed = selected, minus disabled (which never run)
    grep -vF ' (Disabled)' "$dir/grp__sel__$k.txt" \
      | sed -n 's/^ *Test *#[0-9]*: *\(.*[^ ]\) *$/\1/p' > "$dir/grp__run__$k.txt"
  done
}

# Emit the `ctest -N`-shaped subset named by a comma-separated index list.
ctest_sel_from_indices() {
  python3 - "$1" "$2" <<'PY'
import re, sys
list_path, indices = sys.argv[1], {int(i) for i in sys.argv[2].split(',') if i}
rx = re.compile(r"^\s*Test\s+#(\d+):\s+(.*\S)\s*$")
for line in open(list_path, encoding="utf-8", errors="replace"):
    m = rx.match(line)
    if m and int(m.group(1)) in indices:
        sys.stdout.write(line)
PY
}

check() { # name expected_exit dir
  local name="$1" want="$2" dir="$3"
  python3 scripts/ci/audit_shards.py "$dir" >"$dir/out.txt" 2>&1
  local got=$?
  if [ "$got" -eq "$want" ]; then
    echo "  PASS  $name (exit $got)"; pass=$((pass+1))
  else
    echo "  FAIL  $name (exit $got, wanted $want)"; sed 's/^/        /' "$dir/out.txt" | tail -5
    fail=$((fail+1))
  fi
}

echo "=== control: a correct partition must PASS ==="
build_good "$WORK/ok"
check "correct partition" 0 "$WORK/ok"

echo "=== mutants: each must FAIL ==="

# M1 missing test: delete one line from a shard's selection
cp -r "$WORK/ok" "$WORK/m1"; sed -i '2d' "$WORK/m1/grp__sel__1.txt"
check "M1 missing test" 1 "$WORK/m1"

# M2 duplicate: copy a line from shard 2 into shard 1
cp -r "$WORK/ok" "$WORK/m2"
head -2 "$WORK/m2/grp__sel__2.txt" | tail -1 >> "$WORK/m2/grp__sel__1.txt"
check "M2 duplicate assignment" 1 "$WORK/m2"

# M3 unexpected: a test not in the full list
cp -r "$WORK/ok" "$WORK/m3"
echo "  Test #99999: Ghost.NotInFullList" >> "$WORK/m3/grp__sel__1.txt"
check "M3 unexpected test" 1 "$WORK/m3"

# M4 empty shard
cp -r "$WORK/ok" "$WORK/m4"; : > "$WORK/m4/grp__sel__3.txt"
check "M4 empty shard" 1 "$WORK/m4"

# M5 shards disagree about the full list
cp -r "$WORK/ok" "$WORK/m5"; sed -i '3d' "$WORK/m5/grp__full__2.txt"
check "M5 full-list disagreement" 1 "$WORK/m5"

# M6 a selected, enabled test silently never executed
cp -r "$WORK/ok" "$WORK/m6"; sed -i '1d' "$WORK/m6/grp__run__1.txt"
check "M6 silent non-execution" 1 "$WORK/m6"

echo
echo "audit self-test: $pass passed, $fail failed"
rm -rf "$WORK"
[ "$fail" -eq 0 ]
