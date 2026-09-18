#!/usr/bin/env python3
"""Build the CI shard runtime table from recorded ctest output.

CI-PERF-001.  Input is the per-test timing lines scraped from a real CI run
(``Test #N: <name> ... Passed  X sec``), one file per configuration.  Output is
``scripts/ci/test_runtimes.json``: {config: {test_name: seconds}}.

The table is an *input to load balancing only*.  It never decides whether a test
runs -- ``shard_tests.py`` assigns every test in the live ``ctest -N`` list,
whether or not it appears here, and the CI coverage audit proves the partition
is complete.  A stale table costs balance, never coverage.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

# "Test #1827: Suite.Case .......... Passed  890.30 sec"
LINE = re.compile(
    r"Test\s+#(?P<index>\d+):\s+(?P<name>\S+)\s+\.+\s+(?:Passed|Failed)\s+(?P<sec>[\d.]+)\s+sec"
)


def parse(path: Path) -> dict[str, float]:
    times: dict[str, float] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE.search(line)
        if m:
            # Keep the maximum when a name repeats: the slower observation is the
            # safer input to a balancer.
            name, sec = m.group("name"), float(m.group("sec"))
            times[name] = max(times.get(name, 0.0), sec)
    return times


def main() -> int:
    here = Path(__file__).resolve().parent
    baseline = here.parent.parent / "results" / "ci-perf-001" / "baseline"
    sources = {
        "clang_debug": baseline / "testtimes_clang_debug.txt",
        "gcc_debug": baseline / "testtimes_gcc_debug.txt",
        "asan": baseline / "testtimes_asan_all.txt",
    }

    table: dict[str, dict[str, float]] = {}
    for config, path in sources.items():
        if not path.exists():
            print(f"missing {path}", file=sys.stderr)
            return 1
        times = parse(path)
        if not times:
            print(f"no timing lines parsed from {path}", file=sys.stderr)
            return 1
        table[config] = dict(sorted(times.items()))
        total = sum(times.values())
        print(f"{config:12s} {len(times):5d} tests  {total:9.0f} s ({total / 60:6.1f} min)")

    out = here / "test_runtimes.json"
    out.write_text(json.dumps(table, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
