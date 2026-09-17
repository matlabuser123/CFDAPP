#!/usr/bin/env python3
"""P12-DIFF-002 W9 comparison.

For every committed case it reads the three runs A (pre-DIFF-002 library, original cases),
C (current library, original cases) and B (current library, current cases), and reports:

  * status, iterations, face flux, and every conservation entry of metadata.json;
  * for every exported field column: max |C - A|, ||C - A|| / ||A|| (DIFF-002), and
    max |B - C|, ||B - C|| / ||C|| (the DRIFT-001 case fix; non-zero only for the two edited cases);
  * the W9 mass check as frozen in results/p12-diff-002/acceptance_gate.md:
    "mass imbalance not worse than the recorded value". The recorded value is logs/05 for the 8
    cases it lists, and run A (the same pre-DIFF-002 library, reproduced now) for every other case.
    The check is applied literally, B <= recorded.
"""
import csv
import json
import math
import os
import sys

RECORDED_05 = {  # results/p12-diff-002/logs/05_cases_before.log (pre-DIFF-002, 2026-09-16)
    "poiseuille_flow": 1.977e-07,
    "poiseuille_distorted": 7.499e-13,
    "lid_driven_cavity": 0.0,
    "curved_channel_multiblock": 3.270e-11,
    "heated_cavity": 0.0,
    "species_diffusion": 0.0,
    "duct_3d": 2.918e-12,
    "channel_transpiration_graded": 3.480e-12,
}
SKIP_COLUMNS = {"cell_id", "x", "y", "z"}


def load(root, cfg, case):
    d = os.path.join(root, f"run_{cfg}", case)
    out = {}
    try:
        with open(d + ".out") as f:
            for line in f:
                parts = line.split(None, 1)
                if len(parts) == 2:
                    out[parts[0]] = parts[1].strip()
    except OSError:
        pass
    meta = None
    try:
        with open(os.path.join(d, "results", "metadata.json")) as f:
            meta = json.load(f)
    except OSError:
        pass
    fields = None
    try:
        with open(os.path.join(d, "results", "fields.csv")) as f:
            rows = list(csv.reader(f))
        head, body = rows[0], rows[1:]
        fields = {h: [float(r[i]) for r in body] for i, h in enumerate(head)}
    except (OSError, IndexError, ValueError):
        pass
    return out, meta, fields


def conservation(meta):
    return (meta or {}).get("conservation", {})


def l2(a):
    return math.sqrt(sum(x * x for x in a))


def diff(a, b):
    if a is None or b is None:
        return None
    if len(a) != len(b):
        return ("size", len(a), len(b))
    d = [y - x for x, y in zip(a, b)]
    mx = max((abs(v) for v in d), default=0.0)
    na = l2(a)
    return (mx, (l2(d) / na) if na > 0 else (0.0 if l2(d) == 0 else math.inf))


def main():
    root = sys.argv[1]
    cases = sys.argv[2:]
    mass_failures = []
    global runs_cache
    runs_cache = {}
    for case in cases:
        runs = {c: load(root, c, case) for c in "ACB"}
        runs_cache[case] = runs
        print(f"\n=== {case}")
        for c in "ACB":
            out, meta, _ = runs[c]
            cons = conservation(meta)
            th = (meta or {}).get("thermal", {})
            print(f"  {c}: status {out.get('status', '?'):>2} simple {out.get('simple_status', '-'):>2}"
                  f" it {out.get('simple_iterations', '-'):>6} flux {out.get('face_flux', '-'):<9}"
                  f" mass {cons.get('global_mass_imbalance', '-')!s:<24}"
                  f" rel {cons.get('relative_imbalance', '-')!s:<24}"
                  f" maxcell {cons.get('max_cell_imbalance', '-')!s:<24}"
                  + (f" thermal it {th.get('iterations')} res {th.get('final_residual')}" if th.get("enabled") else "")
                  + (f" wall {out.get('wall_seconds', '?')}s"))
        fa, fc, fb = runs["A"][2], runs["C"][2], runs["B"][2]
        if fa and fc and fb:
            print(f"  {'field':<24} {'max|C-A|':>12} {'|C-A|/|A|':>12} {'max|B-C|':>12} {'|B-C|/|C|':>12}")
            for col in fa:
                if col in SKIP_COLUMNS:
                    continue
                d1 = diff(fa.get(col), fc.get(col))
                d2 = diff(fc.get(col), fb.get(col))
                fmt = lambda d: ("  n/a  n/a" if d is None else
                                 (f"{'size':>12} {'':>12}" if d[0] == "size" else f"{d[0]:12.4e} {d[1]:12.4e}"))
                print(f"  {col:<24} {fmt(d1)} {fmt(d2)}")
        else:
            print("  (fields.csv missing in at least one run)")
        # W9 mass check, literal.
        mb = conservation(runs["B"][1]).get("global_mass_imbalance")
        if case in RECORDED_05:
            rec, src = RECORDED_05[case], "logs/05"
        else:
            rec, src = conservation(runs["A"][1]).get("global_mass_imbalance"), "run A"
        if mb is None or rec is None:
            verdict = "n/a (no SIMPLE conservation entry)"
        else:
            verdict = "OK" if mb <= rec else "WORSE"
            if verdict == "WORSE":
                mass_failures.append((case, mb, rec, src))
        print(f"  W9 mass: B {mb} vs recorded {rec} ({src}) -> {verdict}")
    print("\n=== W9 mass summary (literal, as frozen in acceptance_gate.md)")
    if not mass_failures:
        print("  no case has a larger global mass imbalance than its recorded value")
    for case, mb, rec, src in mass_failures:
        print(f"  WORSE: {case}: {mb} > {rec} ({src})")

    # W9A (w9/acceptance_gate_W9A.md): the pressure-correction resolution floor of each run,
    #   floor = sqrt(N) * max(absTol_p, relTol_p * ||b_p||_final),
    # N the cell count, (absTol_p, relTol_p) the case's pressure linear solver tolerances and
    # ||b_p||_final the last outer iteration's pressure-correction right-hand-side norm
    # (metadata residuals.p). The global imbalance is the sum of that solve's residual components,
    # |sum r_i| <= sqrt(N) ||r||_2, and the solver stops once ||r||_2 <= max(absTol, relTol ||b||).
    print("\n=== W9A: model check (every run must satisfy imbalance <= floor) and verdict")
    w9a_failures = []
    model_failures = []
    for case in cases:
        rows = []
        for c in "ACB":
            meta = runs_cache[case][c][1]
            if meta is None or "conservation" not in meta:
                rows.append((c, None, None))
                continue
            with open(os.path.join(root, f"run_{c}", case, "solver.json")) as f:
                solver = json.load(f)
            ps = solver.get("pressure_linear_solver", {})
            abs_tol = ps.get("absolute_tolerance", 1e-12)
            rel_tol = ps.get("relative_tolerance", 1e-10)
            n = meta["mesh"]["cells"]
            bnorm = meta.get("residuals", {}).get("p", 0.0)
            floor = math.sqrt(n) * max(abs_tol, rel_tol * bnorm)
            imb = meta["conservation"]["global_mass_imbalance"]
            rows.append((c, imb, floor))
            if imb > floor:
                model_failures.append((case, c, imb, floor))
        a = rows[0]; b = rows[2]
        if b[1] is None:
            verdict = "n/a"
        else:
            rec = RECORDED_05.get(case, a[1])
            allowed = max(rec, b[2])
            verdict = "PASS" if b[1] <= allowed else "FAIL"
            if verdict == "FAIL":
                w9a_failures.append((case, b[1], allowed))
        print(f"  {case:<40} " + " | ".join(
            f"{c}: imb {i:.3e} floor {fl:.3e}" if i is not None else f"{c}: n/a" for c, i, fl in rows)
            + f" | W9A {verdict}")
    # W9A-1: every run completed with ProjectRunStatus::Converged (0).
    for case in cases:
        for c in "ACB":
            status = runs_cache[case][c][0].get("status")
            if status != "0":
                w9a_failures.append((case, c, "status", status))
    # W9A-4: outside the two DRIFT-001 cases, C and B are bit-identical in every field.
    drift_cases = {"poiseuille_distorted", "curved_channel_multiblock"}
    for case in cases:
        if case in drift_cases:
            continue
        fc, fb = runs_cache[case]["C"][2], runs_cache[case]["B"][2]
        if fc is None or fb is None or fc != fb:
            w9a_failures.append((case, "C != B outside the DRIFT-001 scope"))
    for m in model_failures:
        print(f"  MODEL VIOLATION: {m}")
    for f in w9a_failures:
        print(f"  W9A FAIL: {f}")
    print(f"  W9A: {'PASS' if not w9a_failures and not model_failures else 'FAIL'}")
    return 1 if (w9a_failures or model_failures) else 0


if __name__ == "__main__":
    sys.exit(main())
