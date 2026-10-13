#!/usr/bin/env python3
"""How far apart are the Release- and Debug-written MMS norms? A count of differing lines says
nothing about whether this is a rounding difference or a real divergence."""
import json, sys

def leaves(node, path=""):
    if isinstance(node, dict):
        for k, v in node.items():
            yield from leaves(v, f"{path}.{k}" if path else k)
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from leaves(v, f"{path}[{i}]")
    else:
        yield path, node

a = json.load(open("results/p17/qualification/logs/85_mms_release_cuda_written.json"))
b = json.load(open("results/p17/qualification/logs/85_mms_debug_gui_written.json"))
da, db = dict(leaves(a)), dict(leaves(b))

worst, worst_key, n, timing = 0.0, None, 0, 0
for k in da:
    if k not in db: continue
    x, y = da[k], db[k]
    if not (isinstance(x, (int, float)) and isinstance(y, (int, float))): continue
    if isinstance(x, bool) or isinstance(y, bool): continue
    if x == y: continue
    if k.split(".")[-1].split("[")[0] in ("seconds", "runtime", "runtime_seconds"):
        timing += 1
        continue
    n += 1
    denom = max(abs(x), abs(y), 1e-300)
    rel = abs(x - y) / denom
    if rel > worst:
        worst, worst_key = rel, k

print(f"  non-timing numeric leaves that differ : {n}")
print(f"  timing leaves that differ             : {timing}")
print(f"  WORST relative difference             : {worst:.3e}")
print(f"  at                                    : {worst_key}")
print(f"  double precision eps                  : 2.220e-16")
print(f"  ratio to eps                          : {worst/2.220446049250313e-16:.3e}")
