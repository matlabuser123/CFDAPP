#!/usr/bin/env python3
"""P12-MESH-006 Amendment A3: freeze the independent expected values BEFORE the fresh acceptance run.

Writes a3/data/frozen_expected.json from CFDApp-free calculations only:
  - the analytical fully developed square-duct solution (g5-investigation/tools/duct_reference.py:
    single series, 20 001 terms; verified against the double series, the literature constants, the
    PDE, the walls and the flow rate);
  - the independent exact solution of the scheme's discrete equations
    (g5-investigation/tools/duct_discrete.py: 1D eigen-decomposition, cross-checked by a sparse solve).
Both investigation tools are imported read-only; their sha256 is recorded here.

Contents, per gated grid n = 8, 16, 24 (canonical cross-section [c1][c2], cell centres (i + 1/2) h):
  u_discrete, G_discrete, u_axis_discrete;  u_exact_point (the analytical value at the cell centres);
  predicted scheme errors of the pointwise metric (L-inf, RMS, L1, dp/dx, u_max);
  the G5.1-D envelopes 1.25 x predicted L-inf / RMS at n = 24.
"""
import datetime
import hashlib
import json
import pathlib
import sys

import mpmath
import numpy as np
import scipy

A3 = pathlib.Path(__file__).resolve().parents[1]
INV_TOOLS = A3.parent / "g5-investigation" / "tools"
sys.path.insert(0, str(INV_TOOLS))
import duct_discrete as disc  # noqa: E402
import duct_reference as ref  # noqa: E402

GRIDS = (8, 16, 24)
ENVELOPE_FACTOR = 1.25  # the three-grid GCI safety factor F_s (Roache; Celik et al. 2008)


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def main():
    out = {
        "purpose": "P12-MESH-006 Amendment A3 frozen independent expected values (no CFDApp code or output used)",
        "created_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "python": sys.version.split()[0], "numpy": np.__version__, "scipy": scipy.__version__,
        "mpmath": mpmath.__version__,
        "tool_sha256": {
            "g5-investigation/tools/duct_reference.py": sha256(INV_TOOLS / "duct_reference.py"),
            "g5-investigation/tools/duct_discrete.py": sha256(INV_TOOLS / "duct_discrete.py"),
            "a3/tools/freeze_expected.py": sha256(__file__),
        },
        "problem": {"a": ref.A, "U": ref.U, "mu": ref.MU, "rho": ref.RHO, "Re": ref.RHO * ref.U * ref.A / ref.MU,
                    "measurement_plane": "cell plane x = 4a - h/2 (index 4n - 1 along the flow axis; A2.2)",
                    "dpdx_definition": "-(least-squares slope of the plane-mean pressure over the cell planes with "
                                       "centres in [2.5a, 4.5a]) (G5)",
                    "u_max_definition": "mean of the four axis-adjacent cells on the measurement plane vs the exact "
                                        "axis value (A2.3)"},
        "analytical": {"K": ref.k_double(1999), "G": ref.G_EXACT, "u_max": ref.UMAX_S},
        "grids": {},
    }
    for n in GRIDS:
        h = ref.A / n
        c = (np.arange(n) + 0.5) * h
        u, g = disc.discrete(n)
        point = ref.u_single(c, c)
        m = n // 2
        axis = 0.25 * (u[m - 1, m - 1] + u[m, m - 1] + u[m - 1, m] + u[m, m])
        e = u - point
        out["grids"][str(n)] = {
            "n": n,
            "G_discrete": g,
            "u_axis_discrete": float(axis),
            "u_discrete": u.tolist(),
            "u_exact_point": point.tolist(),
            "predicted_pointwise_errors": {
                "linf": float(np.abs(e).max()),
                "rms": float(np.sqrt((e * e).mean())),
                "l1": float(np.abs(e).mean()),
                "dpdx_rel": abs(g - ref.G_EXACT) / ref.G_EXACT,
                "u_max_rel": abs(axis - ref.UMAX_S) / ref.UMAX_S,
            },
        }
    p24 = out["grids"]["24"]["predicted_pointwise_errors"]
    out["envelope_n24"] = {"factor": ENVELOPE_FACTOR,
                           "linf_predicted": p24["linf"], "linf_allowed": ENVELOPE_FACTOR * p24["linf"],
                           "rms_predicted": p24["rms"], "rms_allowed": ENVELOPE_FACTOR * p24["rms"]}
    target = A3 / "data" / "frozen_expected.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(out, indent=1) + "\n")
    digest = sha256(target)
    (A3 / "data" / "frozen_expected.json.sha256").write_text(f"{digest}  frozen_expected.json\n")
    print(f"frozen: {target} sha256 {digest}")
    for n in GRIDS:
        p = out["grids"][str(n)]["predicted_pointwise_errors"]
        print(f"n={n:2d}: G_disc {out['grids'][str(n)]['G_discrete']:.12f}  predicted Linf {p['linf']:.10e} RMS {p['rms']:.10e} "
              f"L1 {p['l1']:.10e} dp/dx {p['dpdx_rel']:.10e} u_max {p['u_max_rel']:.10e}")
    print(f"envelope n=24: Linf <= {out['envelope_n24']['linf_allowed']:.10e}, RMS <= {out['envelope_n24']['rms_allowed']:.10e}")


if __name__ == "__main__":
    main()
