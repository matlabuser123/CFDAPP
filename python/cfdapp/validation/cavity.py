"""Lid-driven-cavity validation against Ghia, Ghia & Shin (1982) (TODO.md
"P1 -- Python Tooling" sections 17-22): load -> extract numerical
centerlines -> interpolate to Ghia coordinates -> errors -> metrics ->
plots -> summary.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd

from cfdapp.interpolation import extract_profile, linear_interpolate
from cfdapp.metrics import ErrorMetrics, compute_error_metrics
from cfdapp.results import ResultData, load_case_results

#: Default location of the published reference data (section 18).
DEFAULT_GHIA_DIR = Path(__file__).resolve().parents[3] / "validation" / "ghia"


@dataclass(frozen=True)
class GhiaReference:
    u: pd.DataFrame  # columns: y, u -- vertical centerline (x=0.5).
    v: pd.DataFrame  # columns: x, v -- horizontal centerline (y=0.5).


def load_ghia_reference(ghia_dir: str | Path = DEFAULT_GHIA_DIR) -> GhiaReference:
    """Loads validation/ghia/{ghia_re100_u,ghia_re100_v}.csv (section 18)."""
    ghia_dir = Path(ghia_dir)
    u_path = ghia_dir / "ghia_re100_u.csv"
    v_path = ghia_dir / "ghia_re100_v.csv"
    if not u_path.is_file() or not v_path.is_file():
        raise FileNotFoundError(f"Ghia reference CSVs not found under {ghia_dir}")

    u = pd.read_csv(u_path).sort_values("y").reset_index(drop=True)
    v = pd.read_csv(v_path).sort_values("x").reset_index(drop=True)
    for frame, columns, path in ((u, ("y", "u"), u_path), (v, ("x", "v"), v_path)):
        missing = [c for c in columns if c not in frame.columns]
        if missing:
            raise ValueError(f"{path}: missing column(s) {missing}")
    return GhiaReference(u=u, v=v)


@dataclass(frozen=True)
class CavityValidationSummary:
    case: str
    nx: int
    ny: int
    reynolds_number: float | None
    converged: bool
    global_mass_imbalance: float
    u_l2: float
    u_linf: float
    u_mae: float
    v_l2: float
    v_linf: float
    v_mae: float


def _extract_anchored_u_centerline(fields: pd.DataFrame, lid_velocity: float):
    """u(x=0.5, y), with exact no-slip/lid anchors at y=0/y=1 (matching
    CavityValidationUtils.cpp's `extractVerticalProfileU` convention --
    the boundary condition is exact, unlike an extrapolated cell value).
    """
    y, u = extract_profile(
        fields["x"], fields["y"], fields["velocity_x"], fixed_axis="x", fixed_value=0.5
    )
    y_full = np.concatenate(([0.0], y, [1.0]))
    u_full = np.concatenate(([0.0], u, [lid_velocity]))
    order = np.argsort(y_full)
    return y_full[order], u_full[order]


def _extract_anchored_v_centerline(fields: pd.DataFrame):
    """v(x, y=0.5), with exact no-slip anchors at x=0/x=1."""
    x, v = extract_profile(
        fields["x"], fields["y"], fields["velocity_y"], fixed_axis="y", fixed_value=0.5
    )
    x_full = np.concatenate(([0.0], x, [1.0]))
    v_full = np.concatenate(([0.0], v, [0.0]))
    order = np.argsort(x_full)
    return x_full[order], v_full[order]


def validate_cavity_case(
    result: ResultData, ghia: GhiaReference, *, lid_velocity: float = 1.0
) -> tuple[CavityValidationSummary, pd.DataFrame, pd.DataFrame]:
    """Returns (summary, u_centerline, v_centerline) -- the two
    DataFrames have columns (y|x, numerical, reference, error) and are
    exactly what section 13 asks to write as
    analysis/cavity_{u,v}_centerline.csv.
    """
    if result.fields is None:
        raise ValueError("validate_cavity_case: result has no fields.csv (non-finite solve)")
    fields = result.fields

    y_num, u_num = _extract_anchored_u_centerline(fields, lid_velocity)
    x_num, v_num = _extract_anchored_v_centerline(fields)

    # Interpolate the *numerical* solution to Ghia's coordinates (never
    # the reverse -- section 12).
    u_at_ghia = linear_interpolate(y_num, u_num, ghia.u["y"])
    v_at_ghia = linear_interpolate(x_num, v_num, ghia.v["x"])

    u_metrics: ErrorMetrics = compute_error_metrics(u_at_ghia, ghia.u["u"])
    v_metrics: ErrorMetrics = compute_error_metrics(v_at_ghia, ghia.v["v"])

    u_centerline = pd.DataFrame(
        {
            "y": ghia.u["y"],
            "u_numerical": u_at_ghia,
            "u_reference": ghia.u["u"],
            "error": u_at_ghia - ghia.u["u"],
        }
    )
    v_centerline = pd.DataFrame(
        {
            "x": ghia.v["x"],
            "v_numerical": v_at_ghia,
            "v_reference": ghia.v["v"],
            "error": v_at_ghia - ghia.v["v"],
        }
    )

    reynolds = result.metadata.get("reynolds_number")
    summary = CavityValidationSummary(
        case=result.case_name,
        nx=result.nx,
        ny=result.ny,
        reynolds_number=float(reynolds) if reynolds is not None else None,
        converged=result.converged,
        global_mass_imbalance=float(result.metadata["conservation"]["global_mass_imbalance"]),
        u_l2=u_metrics.l2,
        u_linf=u_metrics.linf,
        u_mae=u_metrics.mae,
        v_l2=v_metrics.l2,
        v_linf=v_metrics.linf,
        v_mae=v_metrics.mae,
    )
    return summary, u_centerline, v_centerline


def cavity_grid_refinement(
    summaries_by_grid: dict[tuple[int, int], CavityValidationSummary],
) -> tuple[pd.DataFrame, bool]:
    """Table of U/V L2/Linf across grids (coarsest to finest by nx),
    plus whether the primary trend (error decreases with refinement,
    section 21) holds. `refinement_pass` requires *both* u_l2 and v_l2
    to be non-increasing at every step -- the section's own criterion,
    checked rather than assumed.
    """
    rows = [
        {"nx": nx, "ny": ny, "u_l2": s.u_l2, "u_linf": s.u_linf, "v_l2": s.v_l2, "v_linf": s.v_linf}
        for (nx, ny), s in summaries_by_grid.items()
    ]
    table = pd.DataFrame(rows).sort_values("nx").reset_index(drop=True)

    refinement_pass = bool(
        (table["u_l2"].diff().dropna() <= 0).all() and (table["v_l2"].diff().dropna() <= 0).all()
    )
    return table, refinement_pass


def write_cavity_validation_json(
    summaries_by_grid: dict[tuple[int, int], CavityValidationSummary], path: str | Path
) -> Path:
    """Writes cavity_validation.json in exactly the section 22 shape,
    from freshly computed values (never hard-coded past results).
    """
    any_summary = next(iter(summaries_by_grid.values()))
    doc = {
        "case": any_summary.case,
        "reynolds_number": any_summary.reynolds_number,
        "grids": {
            f"{nx}x{ny}": {"u_l2": s.u_l2, "v_l2": s.v_l2}
            for (nx, ny), s in summaries_by_grid.items()
        },
    }
    if len(summaries_by_grid) > 1:
        _table, refinement_pass = cavity_grid_refinement(summaries_by_grid)
        doc["refinement_pass"] = refinement_pass

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2)
        handle.write("\n")
    return path


def _main(argv: list[str] | None = None) -> int:
    """`python -m cfdapp.validation.cavity --results <dir>` (section 30):
    validates one grid's case/results/ directory. Exit 0 = passed.
    """
    parser = argparse.ArgumentParser(description="Validate a CFDApp lid-driven-cavity result.")
    parser.add_argument("--results", required=True, help="path to a case's results/ directory")
    parser.add_argument("--ghia-dir", default=str(DEFAULT_GHIA_DIR))
    parser.add_argument("--lid-velocity", type=float, default=1.0)
    parser.add_argument("--output", help="write validation.json here (default: <results>/analysis)")
    parser.add_argument("--u-l2-max", type=float, default=0.05)
    parser.add_argument("--v-l2-max", type=float, default=0.05)
    args = parser.parse_args(argv)

    try:
        result = load_case_results(args.results)
        ghia = load_ghia_reference(args.ghia_dir)
        summary, u_centerline, v_centerline = validate_cavity_case(
            result, ghia, lid_velocity=args.lid_velocity
        )
    except Exception as exc:  # noqa: BLE001 -- CLI boundary, report and fail clearly.
        print(f"Cavity validation error: {exc}", file=sys.stderr)
        return 2

    output_dir = Path(args.output) if args.output else Path(args.results) / "analysis"
    output_dir.mkdir(parents=True, exist_ok=True)
    u_centerline.to_csv(output_dir / "cavity_u_centerline.csv", index=False)
    v_centerline.to_csv(output_dir / "cavity_v_centerline.csv", index=False)
    write_cavity_validation_json(
        {(summary.nx, summary.ny): summary}, output_dir / "validation.json"
    )

    print(f"case: {summary.case}")
    print(f"converged: {summary.converged}")
    print(f"u L2: {summary.u_l2:.6g}  v L2: {summary.v_l2:.6g}")

    passed = summary.converged and summary.u_l2 < args.u_l2_max and summary.v_l2 < args.v_l2_max
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(_main())
