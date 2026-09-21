#!/usr/bin/env python3
"""GPU-PIPE-001 Phase 1: attribute D2H *bytes* to purpose, and test the model.

Counting calls says reductions are ~100% of D2H traffic.  Bytes tell a different
story, because a reduction downloads one double per thread block while the
solution download moves the whole vector.  Both matter for a residency design:
call count drives latency, bytes drive bandwidth.

Model, from the source rather than from a fitted constant:
  reduction bytes = krylov_iters * 7 * ceil(n/256) * 8      (kThreadsPerBlock=256,
                                                             DeviceVectorOpsKernel.cu)
  solution bytes  = solves * n * 8                          (result.solution =
                                                             x_.downloadToVector())
The model is falsifiable: predicted total is compared against measured d2h_bytes.
"""

import csv
import math
import statistics as st
from pathlib import Path

HERE = Path(__file__).resolve().parent
rows = [r for r in csv.DictReader(open(HERE / "data" / "runs.csv")) if r["backend"] == "GPU"]
GRIDS = ["20x20", "40x40", "80x80", "160x160", "320x320", "640x640"]
THREADS = 256
DBL = 8


def med(g, key):
    return st.median(float(r[key]) for r in rows if r["grid"] == g)


print("D2H BYTES by purpose -- model vs measurement")
print(
    f"\n{'grid':>9} {'measured MB':>12} {'reduction MB':>13} {'solution MB':>12} "
    f"{'model MB':>10} {'error':>8} {'reduc %':>8} {'soln %':>7}"
)
for g in GRIDS:
    n = int(med(g, "cells"))
    k = med(g, "gpu_linear_solver_iterations")
    s = med(g, "gpu_linear_solves")
    measured = med(g, "d2h_bytes")

    blocks = math.ceil(n / THREADS)
    reduction = k * 7 * blocks * DBL
    solution = s * n * DBL
    model = reduction + solution
    err = (model - measured) / measured * 100

    print(
        f"{g:>9} {measured/1e6:>12.2f} {reduction/1e6:>13.2f} {solution/1e6:>12.2f} "
        f"{model/1e6:>10.2f} {err:>+7.2f}% {reduction/model*100:>7.1f}% {solution/model*100:>6.1f}%"
    )

print("\nCALLS by purpose (the latency view)")
print(f"\n{'grid':>9} {'measured':>10} {'reduction':>10} {'solution':>9} {'reduc %':>8}")
for g in GRIDS:
    k, s = med(g, "gpu_linear_solver_iterations"), med(g, "gpu_linear_solves")
    measured = med(g, "d2h_calls")
    print(f"{g:>9} {measured:>10.0f} {k*7:>10.0f} {s:>9.0f} {k*7/measured*100:>7.2f}%")

print("\nPer-call latency (download seconds / download calls)")
print(f"\n{'grid':>9} {'dl seconds':>11} {'D2H calls':>10} {'us/call':>9} {'MB/s':>9}")
for g in GRIDS:
    dl, dc, db = med(g, "download_seconds"), med(g, "d2h_calls"), med(g, "d2h_bytes")
    print(f"{g:>9} {dl:>11.3f} {dc:>10.0f} {dl/dc*1e6:>9.1f} {db/dl/1e6:>9.1f}")
