#!/usr/bin/env python3
"""P12-DIFF-002-UC-001 Step 9: DRY RUN of every pre-registered migrated criterion, BEFORE any
authoritative test is edited.

For each criterion the table reports
    PRODUCTION   -- CFDApp's measured value (subject of the comparison), expected PASS
    TWO-POINT    -- the superseded operator's EXACT solution substituted for the measurement
    FAR-CELL x2  -- a deliberately corrupted DIFF-002 coefficient
    SIGN FLIP    -- far-cell coefficient sign flipped
    NO FAR CELL  -- far-cell term dropped
Every wrong control must FAIL, or the criterion is vacuous and must not be frozen.

The reference side of every criterion is a FORMULA OF ny derived in uc001_reference.py /
uc001_parity.py. No CFDApp output is used as a reference anywhere.
"""

import sys
import os
from fractions import Fraction as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uc001_reference as ref  # noqa: E402

H, MU, U = F(1), F(1, 10), F(1)
DPDX_EXACT = F(-6, 5)
APEX = F(3, 2)
NY_STUDY = (8, 12, 18)
CONTROLS = ("two_point", "diff002_far_cell_x2", "diff002_far_cell_sign", "diff002_no_far_cell")
LABEL = {
    "two_point": "TWO-POINT",
    "diff002_far_cell_x2": "FAR-CELL x2",
    "diff002_far_cell_sign": "SIGN FLIP",
    "diff002_no_far_cell": "NO FAR CELL",
}

# ---- the derived reference: formulas of ny only -----------------------------------------------
def ref_dpdx(ny):
    return DPDX_EXACT * F(2 * ny * ny, 2 * ny * ny + 1)


def ref_u(ny, j):
    g = ref_dpdx(ny) / MU
    y = F(2 * j + 1, 2 * ny) * H
    return -g / 2 * y * (H - y)


def ref_centreline(ny):
    k = F(9, 2) if ny % 2 == 0 else F(3, 2)
    return APEX - k / (2 * ny * ny + 1)


def ref_rel_error_vs_continuum(ny):
    return F(1, 2 * ny * ny + 1)


def ref_drop_ratio(ny):
    return F(2 * ny * ny, 2 * ny * ny + 1)


# ---- CFDApp PRODUCTION RESULT (results/.../logs/05_grids.log, 03_uc_tests_before.log) ---------
PROD = {
    8:  {"dpdx": -1.189704987660398, "centreline": 1.465119668846815,
         "profile_linf": 4.068475e-06, "drop": -3.271689, "drop_exact": -3.300000},
    12: {"dpdx": -1.195741448267857, "centreline": 1.484429036247831,
         "profile_linf": 3.594301e-08},
    18: {"dpdx": -1.198150788747067, "centreline": 1.493066251845519,
         "profile_linf": 5.479533e-09},
    27: {"dpdx": -1.199177691187090, "centreline": 1.498971906785702,
         "profile_linf": 8.225044e-09},
}

# ---- the wrong controls, solved exactly ------------------------------------------------------
def control_solution(operator, ny):
    u, g = ref.build_and_solve(operator, ny)
    return u, g


rows = []


def emit(cid, statement, bound, values):
    """values: list of (label, measured_value, expected) with expected in {'PASS','FAIL'}."""
    print(f"\n  {cid}. {statement}")
    print(f"      bound: {bound}")
    ok = True
    for label, v, expected in values:
        verdict = "PASS" if v <= bound_value(bound) else "FAIL"
        flag = "" if verdict == expected else "   <<< UNEXPECTED"
        if verdict != expected:
            ok = False
        margin = (bound_value(bound) / v) if v > 0 else float("inf")
        print(f"      {label:<12} value {v:.6e}   {verdict}  (expected {expected}"
              f", {'margin' if verdict == 'PASS' else 'exceeds by'} {margin if verdict == 'PASS' else 1/margin:.1f}x)"
              f"{flag}")
    rows.append((cid, ok))


_BOUNDS = {}


def bound_value(b):
    return _BOUNDS[b]


print(__doc__)
print("=" * 104)
print("PRE-REGISTERED CRITERIA -- DRY RUN (no authoritative test has been modified)")
print("=" * 104)

# ================================================================================================
_BOUNDS["1e-4"] = 1e-4
_BOUNDS["1e-3"] = 1e-3

# --- C1: profile vs the derived discrete reference, every cell, per grid -----------------------
for ny in NY_STUDY:
    vals = [("PRODUCTION", PROD[ny]["profile_linf"], "PASS")]
    for op in CONTROLS:
        u, _ = control_solution(op, ny)
        linf = max(abs(float(u[j] - ref_u(ny, j))) for j in range(ny))
        vals.append((LABEL[op], linf, "FAIL"))
    emit(f"C1(ny={ny})",
         "max_j |u_j - u_j^ref(ny)|, u_j^ref = -(G_ref/2) y_j (H - y_j), "
         "G_ref = dp/dx_exact*2ny^2/(2ny^2+1)/mu",
         "1e-4", vals)

# --- C2: dp/dx vs the derived discrete reference, relative ------------------------------------
for ny in NY_STUDY:
    r = float(ref_dpdx(ny))
    vals = [("PRODUCTION", abs(PROD[ny]["dpdx"] - r) / abs(r), "PASS")]
    for op in CONTROLS:
        _, g = control_solution(op, ny)
        d = float(g * MU)
        vals.append((LABEL[op], abs(d - r) / abs(r), "FAIL"))
    emit(f"C2(ny={ny})", "|dp/dx - dp/dx_ref(ny)| / |dp/dx_ref(ny)|, "
         "dp/dx_ref = dp/dx_exact * 2ny^2/(2ny^2+1)", "1e-3", vals)

# --- C3: centreline vs the derived discrete reference (parity-aware) --------------------------
ny = 8
r = float(ref_centreline(ny))
vals = [("PRODUCTION", abs(PROD[ny]["centreline"] - r), "PASS")]
for op in CONTROLS:
    u, _ = control_solution(op, ny)
    vals.append((LABEL[op], abs(float(ref.centreline(u, ny)) - r), "FAIL"))
emit("C3(ny=8)", "|u_c - u_c^ref(ny)|, u_c^ref = 1.5U - k/(2ny^2+1), k = 4.5 (ny even) / "
     "1.5 (ny odd)", "1e-4", vals)

# --- C4: the centreline discretisation-error identity -----------------------------------------
target = float(APEX - ref_centreline(ny))
vals = [("PRODUCTION", abs((1.5 - PROD[ny]["centreline"]) - target), "PASS")]
for op in CONTROLS:
    u, _ = control_solution(op, ny)
    vals.append((LABEL[op], abs((1.5 - float(ref.centreline(u, ny))) - target), "FAIL"))
emit("C4(ny=8)", "|(1.5U - u_c) - 4.5/(2ny^2+1)|  (the identity, not a stored number)",
     "1e-4", vals)

# --- C5: dp/dx relative error against the CONTINUUM equals 1/(2ny^2+1) ------------------------
target = float(ref_rel_error_vs_continuum(ny))
meas = abs(PROD[ny]["dpdx"] - float(DPDX_EXACT)) / abs(float(DPDX_EXACT))
vals = [("PRODUCTION", abs(meas - target), "PASS")]
for op in CONTROLS:
    _, g = control_solution(op, ny)
    d = float(g * MU)
    vals.append((LABEL[op], abs(abs(d - float(DPDX_EXACT)) / abs(float(DPDX_EXACT)) - target),
                 "FAIL"))
emit("C5(ny=8)", "| |dp/dx - dp/dx_exact|/|dp/dx_exact| - 1/(2ny^2+1) |", "1e-3", vals)

# --- C6: pressure-drop ratio identity ---------------------------------------------------------
target = float(ref_drop_ratio(ny))
meas = PROD[ny]["drop"] / PROD[ny]["drop_exact"]
vals = [("PRODUCTION", abs(meas - target), "PASS")]
for op in CONTROLS:
    _, g = control_solution(op, ny)
    # The drop over a fully-developed segment scales exactly like dp/dx.
    vals.append((LABEL[op], abs(float(g * MU) / float(DPDX_EXACT) - target), "FAIL"))
emit("C6(ny=8)", "| drop/drop_exact - 2ny^2/(2ny^2+1) |", "1e-3", vals)

print()
print("=" * 104)
print("ORDER GATES -- pre-registered separately (they constrain the ORDER, not the operator)")
print("=" * 104)


def order_stat(coarse, medium, fine, r=1.5, formal=2.0):
    import math
    phi1, phi2, phi3 = float(fine), float(medium), float(coarse)
    e21, e32 = phi2 - phi1, phi3 - phi2
    ratio = e32 / e21
    if ratio <= 1.0:
        return None, None
    p = math.log(ratio) / math.log(r)
    rf = r ** formal
    u21, u32 = abs(e21) / (rf - 1.0), abs(e32) / (rf - 1.0)
    return p, u32 / (rf * u21)


print("\n  C7. dp/dx asymptotic ratio in [0.9, 1.1] on the triplet ny = 12/18/27")
for name, trip in (("PRODUCTION", [PROD[n]["dpdx"] for n in (12, 18, 27)]),
                   ("REFERENCE  ", [float(ref_dpdx(n)) for n in (12, 18, 27)])):
    p, a = order_stat(*trip)
    print(f"      {name}  p = {p:7.4f}  asymptotic ratio = {a:7.4f}  "
          f"{'PASS' if abs(a - 1) <= 0.1 else 'FAIL'}")
print("      for comparison, the CURRENT triplet ny = 8/12/18:")
for name, trip in (("PRODUCTION", [PROD[n]["dpdx"] for n in (8, 12, 18)]),
                   ("REFERENCE  ", [float(ref_dpdx(n)) for n in (8, 12, 18)])):
    p, a = order_stat(*trip)
    print(f"      {name}  p = {p:7.4f}  asymptotic ratio = {a:7.4f}  "
          f"{'PASS' if abs(a - 1) <= 0.1 else 'FAIL'}")

print("\n  C8. centreline asymptotic ratio in [0.9, 1.1] on a PARITY-CONSISTENT triplet "
      "ny = 8/12/18 (all even)")
for name, trip in (("PRODUCTION", [PROD[n]["centreline"] for n in (8, 12, 18)]),
                   ("REFERENCE  ", [float(ref_centreline(n)) for n in (8, 12, 18)])):
    p, a = order_stat(*trip)
    print(f"      {name}  p = {p:7.4f}  asymptotic ratio = {a:7.4f}  "
          f"{'PASS' if abs(a - 1) <= 0.1 else 'FAIL'}")
print("      the MIXED-parity triplet 12/18/27 must NOT be used -- even the exact reference "
      "fails it:")
for name, trip in (("PRODUCTION", [PROD[n]["centreline"] for n in (12, 18, 27)]),
                   ("REFERENCE  ", [float(ref_centreline(n)) for n in (12, 18, 27)])):
    p, a = order_stat(*trip)
    print(f"      {name}  p = {p:7.4f}  asymptotic ratio = {a:7.4f}  "
          f"{'PASS' if abs(a - 1) <= 0.1 else 'FAIL'}")

print()
print("=" * 104)
print("DRY-RUN SUMMARY")
print("=" * 104)
bad = [cid for cid, ok in rows if not ok]
print(f"\n  criteria dry-run: {len(rows)}   all controls behaved as pre-registered: "
      f"{'YES' if not bad else 'NO -- ' + ', '.join(bad)}")
print("\n  C1-C6 are operator-discriminating: every wrong control fails them.")
print("  C7/C8 constrain the observed ORDER and are deliberately NOT operator-discriminating")
print("  (the two-point operator is also second order); C1-C6 carry the operator claim.")
sys.exit(0 if not bad else 1)
