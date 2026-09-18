#!/usr/bin/env python3
"""Derive the ASan job's real parallel efficiency from the baseline run.

The prediction in baseline/summary.md assumed wall = serial / 4 vCPU.  The first
measured job of the optimized run (gcc-release) came in at 2.42x, not 4x, so the
ASan projection has to be re-derived from ASan's OWN measured behaviour rather
than from a nominal core count.

Method: reproduce the OLD stride partition (`ctest -I <shard>,,5`: ctest index i
goes to shard (i mod 5) + 1) over the recorded per-test ASan runtimes, giving
each baseline shard's serial load; divide by that shard's measured test-step
wall time to get its realised speed-up.
"""

import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIST = HERE / "ci_ctest_full_list.txt"
TABLE = HERE.parent.parent.parent / "scripts" / "ci" / "test_runtimes.json"

CTEST_N = re.compile(r"^\s*Test\s+#(\d+):\s+(.*\S)\s*$")
DISABLED = " (Disabled)"

# Measured in run 35352132866: sanitizer shard TEST-step wall times (minutes).
# Job wall minus ~5 min of install/configure/build.
MEASURED_TEST_WALL = {1: 86.6, 2: 95.8, 3: 67.0, 4: 88.0, 5: 60.1}

weights = json.loads(TABLE.read_text())["asan"]
tests = []
for line in LIST.read_text(encoding="utf-8", errors="replace").splitlines():
    m = CTEST_N.match(line)
    if m:
        tests.append((int(m.group(1)), m.group(2)))


def w(name):
    return 0.0 if name.endswith(DISABLED) else weights.get(name, 0.0)


# Old stride partition.
loads = {k: 0.0 for k in range(1, 6)}
for index, name in tests:
    loads[(index % 5) + 1] += w(name)

print("baseline ASan shards, stride partition (`-I <shard>,,5`):")
print(f"{'shard':>6} {'serial min':>11} {'measured wall':>14} {'speed-up':>9}")
effs = []
for k in range(1, 6):
    serial = loads[k] / 60.0
    wall = MEASURED_TEST_WALL[k]
    eff = serial / wall
    effs.append(eff)
    print(f"{k:>6} {serial:>11.1f} {wall:>14.1f} {eff:>8.2f}x")

mean_eff = sum(effs) / len(effs)
worst_eff = min(effs)
total_serial = sum(loads.values()) / 60.0
print(f"\ntotal ASan serial load: {total_serial:.1f} min")
print(f"realised speed-up: mean {mean_eff:.2f}x, worst {worst_eff:.2f}x")

FLOOR = max(w(n) for _, n in tests) / 60.0
print(f"single-test floor: {FLOOR:.1f} min")

print("\nprojection for the balanced 5-shard partition (276.4 min serial each):")
balanced = total_serial / 5
for label, eff in (("mean", mean_eff), ("worst", worst_eff)):
    wall = max(FLOOR, balanced / eff)
    print(f"  at {label} efficiency {eff:.2f}x -> test {wall:.1f} min, job ~{wall + 5:.1f} min")
