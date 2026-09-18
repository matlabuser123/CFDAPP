#!/usr/bin/env python3
"""Prove a sharded CTest run covered every test exactly once (CI-PERF-001).

Audits from what the shards themselves recorded, so it reflects the build that
actually ran rather than a re-derivation on a different machine.  Per group it
checks four failure modes the acceptance gate names:

    missing test         in the full list, run by no shard
    duplicate assignment run by more than one shard
    unexpected test      run but not in the full list
    empty shard          a shard that selected nothing

plus two that would otherwise hide a defect:

    disagreement         shards disagree about what the full list even is
    silent non-execution a selected, enabled test that neither passed nor failed

Input: a directory of files named
    <group>__full__<shard>.txt      `ctest -N`
    <group>__sel__<shard>.txt       `ctest -N -I ...` (what this shard selected)
    <group>__run__<shard>.txt       test names ctest reported Passed/Failed
Exit status is non-zero if any group fails.
"""

from __future__ import annotations

import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

CTEST_N_LINE = re.compile(r"^\s*Test\s+#(?P<index>\d+):\s+(?P<name>.*\S)\s*$")
CTEST_N_ANY = re.compile(r"^\s*Test\s+#\d+:")
DISABLED_SUFFIX = " (Disabled)"


def names(path: Path) -> list[str]:
    """Names from a `ctest -N` listing, failing closed if the parse loses lines."""
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    parsed = [m.group("name") for m in map(CTEST_N_LINE.match, text) if m]
    any_count = sum(1 for line in text if CTEST_N_ANY.match(line))
    if len(parsed) != any_count:
        raise SystemExit(f"{path}: parse lost {any_count - len(parsed)} test lines")
    return parsed


def run_names(path: Path) -> list[str]:
    """Names a shard actually executed (one bare test name per line)."""
    if not path.exists():
        return []
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def audit_group(group: str, files: dict[str, dict[str, Path]]) -> bool:
    fulls = {s: names(p) for s, p in sorted(files.get("full", {}).items())}
    sels = {s: names(p) for s, p in sorted(files.get("sel", {}).items())}
    runs = {s: run_names(p) for s, p in sorted(files.get("run", {}).items())}

    print(f"\n=== {group} ===")
    if not fulls or not sels:
        print(f"FAIL: {group}: missing full ({len(fulls)}) or selection ({len(sels)}) lists")
        return False
    if set(fulls) != set(sels):
        print(f"FAIL: {group}: shards differ between full {sorted(fulls)} and sel {sorted(sels)}")
        return False

    ok = True
    reference = next(iter(fulls.values()))
    for shard, listing in fulls.items():
        if listing != reference:
            print(f"FAIL: shard {shard} disagrees about the full list ({len(listing)} vs {len(reference)})")
            ok = False

    union = [n for listing in sels.values() for n in listing]
    counts = Counter(union)
    duplicates = sorted(n for n, c in counts.items() if c > 1)
    missing = sorted(set(reference) - set(union))
    unexpected = sorted(set(union) - set(reference))
    empty = sorted(s for s, listing in sels.items() if not listing)

    for label, items in (
        ("run by more than one shard", duplicates),
        ("run by no shard", missing),
        ("run but not in the full list", unexpected),
    ):
        if items:
            ok = False
            print(f"FAIL: {len(items)} tests {label}: {items[:10]}")
    if empty:
        ok = False
        print(f"FAIL: empty shard(s): {empty}")

    enabled = {n for n in reference if not n.endswith(DISABLED_SUFFIX)}
    disabled = len(reference) - len(enabled)
    for shard, listing in sorted(sels.items()):
        print(f"  shard {shard}: selected {len(listing)}, executed {len(runs.get(shard, []))}")

    if runs:
        executed = {n for listing in runs.values() for n in listing}
        # A disabled test is listed but never executed -- expected, not a defect.
        not_executed = sorted(enabled - executed)
        if not_executed:
            ok = False
            print(
                f"FAIL: {len(not_executed)} enabled tests were selected but never reported a "
                f"result: {not_executed[:10]}"
            )
        print(f"  executed {len(executed)} of {len(enabled)} enabled ({disabled} disabled)")

    print(f"  full list {len(reference)}; union {len(union)} ({len(set(union))} distinct)")
    print(f"  {group} shard coverage: " + ("COMPLETE, each test in exactly one shard" if ok else "BROKEN"))
    return ok


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "lists")
    found = sorted(root.rglob("*__*__*.txt"))
    if not found:
        print(f"FAIL: no shard list files under {root}")
        return 1

    groups: dict[str, dict[str, dict[str, Path]]] = defaultdict(lambda: defaultdict(dict))
    for path in found:
        group, kind, shard = path.stem.split("__", 2)
        groups[group][kind][shard] = path

    results = {group: audit_group(group, files) for group, files in sorted(groups.items())}
    print("\n=== summary ===")
    for group, ok in sorted(results.items()):
        print(f"  {group}: {'PASS' if ok else 'FAIL'}")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
