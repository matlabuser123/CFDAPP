#!/usr/bin/env bash
# CI-PERF-001: prove the sharding MECHANISM end to end against a real ctest,
# not just the assignment arithmetic.  Checks that ctest accepts the explicit
# index list, that what it selects is exactly what the assigner intended, and
# that the shards partition the live list.
set -u
cd "$(dirname "$0")/../.."
PRESET=${1:-debug}
CONFIG=${2:-gcc_debug}
SHARDS=${3:-3}
fail=0

FULL=/tmp/shardtest_full.txt
ctest --preset "$PRESET" -N > "$FULL" 2>/dev/null
total=$(grep -cE '^ *Test +#' "$FULL")
echo "live ctest -N list: $total tests (preset $PRESET)"

: > /tmp/shardtest_union.txt
for k in $(seq 1 "$SHARDS"); do
  idx=$(python3 scripts/ci/shard_tests.py --list-file "$FULL" --config "$CONFIG" \
          --shards "$SHARDS" --shard "$k") || { echo "  assigner FAILED for shard $k"; fail=1; continue; }
  want=$(echo "$idx" | tr ',' '\n' | grep -c .)
  chars=${#idx}

  # The real question: does ctest accept a ~3 kB index list, and select exactly
  # the tests the assigner named?
  ctest --preset "$PRESET" -N -I "0,0,0,$idx" > /tmp/shardtest_sel.txt 2>/dev/null
  got=$(grep -cE '^ *Test +#' /tmp/shardtest_sel.txt)

  # Indices ctest actually selected, vs the ones we asked for.
  sed -n 's/^ *Test *#\([0-9]*\):.*/\1/p' /tmp/shardtest_sel.txt | sort -n > /tmp/shardtest_got.txt
  echo "$idx" | tr ',' '\n' | grep . | sort -n > /tmp/shardtest_want.txt
  if ! diff -q /tmp/shardtest_want.txt /tmp/shardtest_got.txt >/dev/null; then
    echo "  shard $k: MISMATCH between requested and selected indices"
    fail=1
  fi
  [ "$want" = "$got" ] || { echo "  shard $k: count mismatch want=$want got=$got"; fail=1; }

  printf '  shard %d/%d: %4d tests, %5d-char index list -> ctest selected %4d  %s\n' \
    "$k" "$SHARDS" "$want" "$chars" "$got" "$([ "$want" = "$got" ] && echo OK || echo MISMATCH)"
  sed -n 's/^ *Test *#[0-9]*: *\(.*[^ ]\) *$/\1/p' /tmp/shardtest_sel.txt >> /tmp/shardtest_union.txt
done

# The union must be the live list exactly once.
sed -n 's/^ *Test *#[0-9]*: *\(.*[^ ]\) *$/\1/p' "$FULL" | sort > /tmp/shardtest_all.txt
sort /tmp/shardtest_union.txt > /tmp/shardtest_u.txt
dups=$(uniq -d /tmp/shardtest_u.txt | wc -l)
missing=$(comm -23 /tmp/shardtest_all.txt /tmp/shardtest_u.txt | wc -l)
extra=$(comm -13 /tmp/shardtest_all.txt /tmp/shardtest_u.txt | wc -l)
echo "union: $(wc -l < /tmp/shardtest_u.txt) selected, duplicates=$dups missing=$missing unexpected=$extra"
[ "$dups" -eq 0 ] && [ "$missing" -eq 0 ] && [ "$extra" -eq 0 ] || fail=1

echo
if [ "$fail" -eq 0 ]; then echo "sharding mechanism self-test: PASS"; else echo "sharding mechanism self-test: FAIL"; fi
exit "$fail"
