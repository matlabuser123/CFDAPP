#!/usr/bin/env python3
"""Runtime-balanced CTest sharding for CI (CI-PERF-001).

The previous scheme was ``ctest -I <shard>,,<N>`` -- a stride over the test
index, which balances *count*, not *time*.  CFDApp's suite is extremely skewed
(mean 8.4 s, slowest single test 890 s), so stride sharding produced a 65..101
minute spread across five nominally equal sanitizer shards.

This assigns tests to shards by measured runtime (longest-processing-time-first
greedy, the classic 4/3-approximation for makespan), then emits an explicit
index list for ``ctest -I 0,0,0,<indices>``.

Coverage is structural, not statistical:

* every test in the live ``ctest -N`` list is assigned to exactly one shard;
* a test with no recorded runtime is still assigned (default weight), so a newly
  added test can never be silently dropped -- only mis-balanced;
* ``--check`` re-derives all shards and asserts they partition the list exactly;
* CI additionally proves it from what actually ran, in the coverage-audit job.

Usage:
  shard_tests.py --list-file ctest_full.txt --config clang_debug --shards 4 --shard 2
  shard_tests.py --list-file ctest_full.txt --config asan --shards 5 --check
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# "  Test #123: Suite.Case", and for a gtest DISABLED_ case,
# "  Test #123: Suite.Case (Disabled)" -- the name must therefore be captured as
# the whole trailing string.  An earlier `(?P<name>\S+)\s*$` silently dropped
# every disabled test (45 of 1977 here) *and* reported its own partition as
# complete, because the checker used the same regex as the assigner.
CTEST_N_LINE = re.compile(r"^\s*Test\s+#(?P<index>\d+):\s+(?P<name>.*\S)\s*$")

# An independent, deliberately looser counter: if these two disagree, the parse
# is losing tests and we fail closed rather than shard an incomplete list.
CTEST_N_ANY = re.compile(r"^\s*Test\s+#\d+:")

DISABLED_SUFFIX = " (Disabled)"

DEFAULT_TABLE = Path(__file__).resolve().parent / "test_runtimes.json"


def parse_ctest_list(path: Path) -> list[tuple[int, str]]:
    """Return [(ctest_index, test_name)] in listing order, failing closed."""
    tests: list[tuple[int, str]] = []
    seen_any = 0
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if CTEST_N_ANY.match(line):
            seen_any += 1
        m = CTEST_N_LINE.match(line)
        if m:
            tests.append((int(m.group("index")), m.group("name")))
    if not tests:
        raise SystemExit(f"no 'Test #N: name' lines found in {path}")
    if len(tests) != seen_any:
        raise SystemExit(
            f"parse lost {seen_any - len(tests)} of {seen_any} test lines in {path} -- "
            "refusing to shard an incomplete list"
        )
    return tests


def weight_of(name: str, weights: dict[str, float], default: float) -> float:
    """Runtime for a ctest listing name.

    A disabled test is listed but never executed, so it costs nothing and must
    not be given the default weight -- that would distort every shard.
    """
    if name.endswith(DISABLED_SUFFIX):
        return 0.0
    return weights.get(name, default)


def assign(
    tests: list[tuple[int, str]], weights: dict[str, float], shards: int
) -> list[list[tuple[int, str]]]:
    """Longest-processing-time-first greedy assignment into `shards` buckets."""
    if shards < 1:
        raise SystemExit("--shards must be >= 1")
    if shards > len(tests):
        raise SystemExit(f"--shards {shards} exceeds test count {len(tests)}")

    # Unknown tests get the median of known weights -- not zero, which would pile
    # every new test onto one shard, and not the max, which would starve it.
    known = sorted(weights.values())
    default = known[len(known) // 2] if known else 1.0

    # Sort by weight desc; ties broken by index so the assignment is deterministic
    # across machines and Python versions.
    ordered = sorted(tests, key=lambda t: (-weight_of(t[1], weights, default), t[0]))

    buckets: list[list[tuple[int, str]]] = [[] for _ in range(shards)]
    loads = [0.0] * shards
    for index, name in ordered:
        # Lightest bucket wins; ties broken by test count, then by index.
        # Without the count term, a zero-weight test (every disabled test) never
        # raises its bucket's load, so that bucket stays the minimum and absorbs
        # every remaining zero-weight test -- time-balanced but absurdly
        # count-skewed, and fragile if a runtime table goes stale and many tests
        # fall back to the same weight.
        target = min(range(shards), key=lambda b: (loads[b], len(buckets[b]), b))
        buckets[target].append((index, name))
        loads[target] += weight_of(name, weights, default)

    for bucket in buckets:
        bucket.sort(key=lambda t: t[0])
    return buckets


def check_partition(tests: list[tuple[int, str]], buckets: list[list[tuple[int, str]]]) -> int:
    """Assert the buckets partition `tests` exactly.  Returns a process exit code."""
    expected = {i for i, _ in tests}
    seen: dict[int, int] = {}
    problems: list[str] = []

    for shard_number, bucket in enumerate(buckets, start=1):
        if not bucket:
            problems.append(f"shard {shard_number} is EMPTY")
        for index, name in bucket:
            if index in seen:
                problems.append(
                    f"DUPLICATE: test #{index} ({name}) in shards {seen[index]} and {shard_number}"
                )
            seen[index] = shard_number

    for index in sorted(expected - set(seen)):
        problems.append(f"MISSING: test #{index} is in no shard")
    for index in sorted(set(seen) - expected):
        problems.append(f"UNEXPECTED: test #{index} is not in the full list")

    total = sum(len(b) for b in buckets)
    print(f"full list {len(expected)} tests; assigned {total} ({len(seen)} distinct)")
    for shard_number, bucket in enumerate(buckets, start=1):
        print(f"  shard {shard_number}/{len(buckets)}: {len(bucket)} tests")

    if problems:
        print("shard coverage: BROKEN")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("shard coverage: COMPLETE, each test in exactly one shard")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--list-file", required=True, type=Path, help="output of `ctest -N`")
    ap.add_argument("--config", required=True, help="runtime-table key, e.g. clang_debug")
    ap.add_argument("--shards", required=True, type=int)
    ap.add_argument("--shard", type=int, help="1-based shard to emit indices for")
    ap.add_argument("--check", action="store_true", help="verify the partition and exit")
    ap.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    args = ap.parse_args()

    tests = parse_ctest_list(args.list_file)

    table = json.loads(args.table.read_text(encoding="utf-8")) if args.table.exists() else {}
    weights: dict[str, float] = table.get(args.config, {})
    if not weights:
        print(
            f"warning: no runtimes for config '{args.config}'; falling back to equal weights "
            "(coverage is unaffected, balance is not)",
            file=sys.stderr,
        )

    buckets = assign(tests, weights, args.shards)

    if args.check:
        known = sorted(weights.values())
        default = known[len(known) // 2] if known else 1.0
        loads = [sum(weight_of(n, weights, default) for _, n in b) for b in buckets]
        slowest_test = max((weight_of(n, weights, default) for _, n in tests), default=0.0)
        disabled = sum(1 for _, n in tests if n.endswith(DISABLED_SUFFIX))
        print(f"{len(tests)} listed = {len(tests) - disabled} executed + {disabled} disabled")
        print(
            f"predicted serial load per shard: min {min(loads) / 60:.1f} min, "
            f"max {max(loads) / 60:.1f} min, spread {max(loads) / max(min(loads), 1e-9):.2f}x"
        )
        print(f"single-test floor: {slowest_test / 60:.1f} min")
        return check_partition(tests, buckets)

    if args.shard is None:
        raise SystemExit("--shard is required unless --check is given")
    if not 1 <= args.shard <= args.shards:
        raise SystemExit(f"--shard must be in 1..{args.shards}")

    bucket = buckets[args.shard - 1]
    if not bucket:
        raise SystemExit(f"shard {args.shard}/{args.shards} is empty -- refusing to emit")
    print(",".join(str(i) for i, _ in bucket))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
