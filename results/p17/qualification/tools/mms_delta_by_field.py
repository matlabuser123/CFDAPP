#!/usr/bin/env python3
"""Split the Release-vs-Debug MMS difference by FIELD, because a single worst-case number mixes raw
measurements with Richardson-extrapolated ones that amplify any difference in their inputs."""
import json, collections

def leaves(node, path=""):
    if isinstance(node, dict):
        for k, v in node.items():
            yield from leaves(v, f"{path}.{k}" if path else k)
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from leaves(v, f"{path}[{i}]")
    else:
        yield path, node

a = dict(leaves(json.load(open("results/p17/qualification/logs/85_mms_release_cuda_written.json"))))
b = dict(leaves(json.load(open("results/p17/qualification/logs/85_mms_debug_gui_written.json"))))

worst = collections.defaultdict(float)
count = collections.Counter()
for k, x in a.items():
    y = b.get(k)
    if not isinstance(x, (int, float)) or not isinstance(y, (int, float)):
        continue
    if isinstance(x, bool) or isinstance(y, bool) or x == y:
        continue
    field = k.split(".")[-1].split("[")[0]
    count[field] += 1
    worst[field] = max(worst[field], abs(x - y) / max(abs(x), abs(y), 1e-300))

print(f"  {'field':<24} {'n':>4}  {'worst relative diff':>20}")
for f in sorted(worst, key=lambda z: -worst[z]):
    print(f"  {f:<24} {count[f]:>4}  {worst[f]:>20.3e}")
