#!/usr/bin/env python3
"""GPU-PIPE-001 Phase 1: reduce the baseline runs.csv to the reported table.

Reports medians across repeats.  Anything the harness does not measure is
printed as `unavailable` rather than estimated (the phase requires this).
"""

import csv
import statistics as st
from pathlib import Path

HERE = Path(__file__).resolve().parent
rows = list(csv.DictReader(open(HERE / "data" / "runs.csv")))
GRIDS = ["20x20", "40x40", "80x80", "160x160", "320x320", "640x640"]


def med(rs, key):
    vals = [float(r[key]) for r in rs if r.get(key) not in (None, "", "n/a")]
    return st.median(vals) if vals else None


def spread(rs, key):
    vals = [float(r[key]) for r in rs if r.get(key) not in (None, "", "n/a")]
    return (max(vals) - min(vals)) if len(vals) > 1 else 0.0


print("available columns:", ", ".join(rows[0].keys()))
print()

# ---- runtime and speed-up -------------------------------------------------
print("=== end-to-end (median of repeats; simple_solve_seconds) ===")
hdr = f"{'grid':>9} {'cells':>8} {'outer':>6} {'CPU s':>9} {'GPU s':>9} {'speedup':>8} {'CPU spread':>11} {'GPU spread':>11}"
print(hdr)
for g in GRIDS:
    c = [r for r in rows if r["grid"] == g and r["backend"] == "CPU"]
    p = [r for r in rows if r["grid"] == g and r["backend"] == "GPU"]
    if not c or not p:
        continue
    cs, ps = med(c, "simple_solve_seconds"), med(p, "simple_solve_seconds")
    print(
        f"{g:>9} {int(float(p[0]['cells'])):>8} {int(med(p,'outer_iterations')):>6} "
        f"{cs:>9.3f} {ps:>9.3f} {cs/ps:>7.3f}x {spread(c,'simple_solve_seconds'):>11.3f} "
        f"{spread(p,'simple_solve_seconds'):>11.3f}"
    )

# ---- transfers ------------------------------------------------------------
print("\n=== transfers (GPU backend, median) ===")
print(
    f"{'grid':>9} {'H2D cnt':>9} {'H2D MB':>9} {'D2H cnt':>9} {'D2H MB':>9} "
    f"{'total cnt':>10} {'total MB':>9} {'B/D2H call':>11}"
)
for g in GRIDS:
    p = [r for r in rows if r["grid"] == g and r["backend"] == "GPU"]
    if not p:
        continue
    hc, hb = med(p, "h2d_calls"), med(p, "h2d_bytes")
    dc, db = med(p, "d2h_calls"), med(p, "d2h_bytes")
    print(
        f"{g:>9} {hc:>9.0f} {hb/1e6:>9.2f} {dc:>9.0f} {db/1e6:>9.2f} "
        f"{hc+dc:>10.0f} {(hb+db)/1e6:>9.2f} {db/max(dc,1):>11.0f}"
    )

# ---- per-iteration normalisation -----------------------------------------
print("\n=== per SIMPLE (outer) iteration, and per Krylov iteration ===")
print(
    f"{'grid':>9} {'outer':>6} {'krylov':>8} {'xfer/outer':>11} {'D2H/outer':>10} "
    f"{'H2D/outer':>10} {'kB/outer':>10} {'D2H/krylov':>11}"
)
for g in GRIDS:
    p = [r for r in rows if r["grid"] == g and r["backend"] == "GPU"]
    if not p:
        continue
    o, k = med(p, "outer_iterations"), med(p, "gpu_linear_solver_iterations")
    hc, dc = med(p, "h2d_calls"), med(p, "d2h_calls")
    tb = med(p, "h2d_bytes") + med(p, "d2h_bytes")
    print(
        f"{g:>9} {o:>6.0f} {k:>8.0f} {(hc+dc)/o:>11.1f} {dc/o:>10.1f} {hc/o:>10.2f} "
        f"{tb/o/1e3:>10.1f} {dc/max(k,1):>11.2f}"
    )

# ---- timing breakdown -----------------------------------------------------
print("\n=== GPU timing breakdown (median seconds) ===")
print(
    f"{'grid':>9} {'solve':>9} {'gpu_solve':>10} {'kernel':>9} {'upload':>9} "
    f"{'download':>9} {'dl %':>7} {'xfer %':>7}"
)
for g in GRIDS:
    p = [r for r in rows if r["grid"] == g and r["backend"] == "GPU"]
    if not p:
        continue
    s = med(p, "simple_solve_seconds")
    gs = med(p, "gpu_solve_seconds")
    kn, ul, dl = med(p, "kernel_seconds"), med(p, "upload_seconds"), med(p, "download_seconds")
    print(
        f"{g:>9} {s:>9.3f} {gs:>10.3f} {kn:>9.3f} {ul:>9.3f} {dl:>9.3f} "
        f"{dl/s*100:>6.1f}% {(ul+dl)/s*100:>6.1f}%"
    )

# ---- what the harness does NOT record ------------------------------------
present = set(rows[0].keys())
wanted = {
    "SpMV time": "spmv_seconds",
    "pressure-correction time": "pressure_correction_seconds",
    "SIMPLE-loop time": "simple_loop_seconds",
    "allocations": "allocations",
    "deallocations": "frees",
    "synchronizations": "synchronizations",
    "transfer purpose breakdown": "h2d_bytes_field",
}
print("\n=== metrics NOT recorded by this harness (reported unavailable, not estimated) ===")
for label, col in sorted(wanted.items()):
    if col not in present:
        print(f"  {label:<28} unavailable (no '{col}' column)")
