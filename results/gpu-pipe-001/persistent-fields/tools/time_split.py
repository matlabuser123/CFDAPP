#!/usr/bin/env python3
"""Where does GPU-path wall time actually go, after Phases 2-3 and rho-carry?

gpu_solve_seconds is the time inside GpuCG/GpuBiCGSTAB::solve(). Everything else
in simple_solve_seconds is CPU work: momentum assembly, Rhie-Chow, response
coefficients, flux prediction, pressure assembly, velocity correction and flux
correction. That split decides whether field residency can help at all.
"""

import re
import statistics as st
from collections import defaultdict
from pathlib import Path

RAW = Path(__file__).resolve().parent.parent.parent / "phase5-rho-carry" / "paired_raw.txt"
rows = []
for line in RAW.read_text().splitlines():
    if "side=after" not in line or "RESULT" not in line:
        continue
    rows.append(dict(re.findall(r"(\w+)=(\S+)", line)))

by = defaultdict(list)
for r in rows:
    by[int(r["edge"])].append(r)

print(f"{'grid':>7} {'end-to-end':>11} {'in GPU solves':>14} {'CPU stages':>11} {'CPU share':>10}")
for g in sorted(by):
    rs = by[g]
    tot = st.median(float(r["solve_s"]) for r in rs)
    gpu = st.median(float(r["gpu_solve_s"]) for r in rs)
    cpu = tot - gpu
    print(f"{g:>5}^2 {tot:>11.3f} {gpu:>14.3f} {cpu:>11.3f} {cpu/tot*100:>9.1f}%")

print()
print("The CPU share is assembly + correction: operations with no device")
print("implementation. A field cannot usefully stay resident when the next")
print("thing that reads it is host code.")
