"""Poiseuille validation workflow (TODO.md "P1 -- Python Tooling"
sections 24-28): load -> evaluate analytical solution -> extract
numerical profile -> velocity errors -> flow-rate check -> pressure-
gradient fit -> conservation check -> summary.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np
import pandas as pd

from cfdapp.interpolation import extract_profile
from cfdapp.metrics import ErrorMetrics, compute_error_metrics, observed_order
from cfdapp.results import ResultData, load_case_results
from cfdapp.validation import analytical


@dataclass(frozen=True)
class PoiseuilleValidationSummary:
    case: str
    channel_height: float
    channel_length: float
    dynamic_viscosity: float
    mean_velocity: float
    profile_x: float
    velocity_l2: float
    velocity_linf: float
    velocity_mae: float
    u_max_numerical: float
    u_max_analytical: float
    u_max_relative_error: float
    pressure_gradient_numerical: float
    pressure_gradient_analytical: float
    pressure_gradient_relative_error: float
    flow_rate_numerical: float
    flow_rate_analytical: float
    flow_rate_relative_error: float
    global_mass_imbalance: float
    mass_conservation_pass: bool
    converged: bool


def fit_pressure_gradient(fields: pd.DataFrame, x_min: float, x_max: float) -> float:
    """Fits p(x) = a + b*x over cells with x in [x_min, x_max] and
    returns b (section 26). Uses column-averaged pressure (mean over all
    cells sharing an x-coordinate) rather than a single row, since
    pressure is only exactly uniform in y for a fully-developed profile
    -- averaging is the robust choice mentioned in section 26's "use
    several cells/sample locations instead of a single pair."
    """
    window = fields[(fields["x"] >= x_min) & (fields["x"] <= x_max)]
    if window.empty:
        raise ValueError(f"fit_pressure_gradient: no cells with x in [{x_min}, {x_max}]")
    column_means = window.groupby("x")["pressure"].mean()
    if len(column_means) < 2:
        raise ValueError(
            "fit_pressure_gradient: need at least two distinct x-columns to fit a slope"
        )
    slope, _intercept = np.polyfit(column_means.index.to_numpy(), column_means.to_numpy(), deg=1)
    return float(slope)


def validate_poiseuille_case(
    result: ResultData,
    *,
    mean_velocity: float,
    dynamic_viscosity: float,
    channel_height: float,
    profile_x: float,
    pressure_fit_x_range: tuple[float, float],
    mass_imbalance_tolerance: float = 1e-6,
) -> PoiseuilleValidationSummary:
    if result.fields is None:
        raise ValueError(
            "validate_poiseuille_case: result has no fields.csv (non-finite solve) -- "
            "cannot validate"
        )
    fields = result.fields

    y_interior, u_interior = extract_profile(
        fields["x"], fields["y"], fields["velocity_x"], fixed_axis="x", fixed_value=profile_x
    )
    # Exact no-slip anchors at both walls (y=0, y=H), same convention as
    # cavity.py's centerline extraction and
    # PoiseuilleValidationUtils.cpp's `extractVerticalProfileU`: the wall
    # boundary condition is exact, not an extrapolated interior value, so
    # comparing against it is legitimate additional evidence rather than
    # a free trivially-zero point inflating the metric.
    y = np.concatenate(([0.0], y_interior, [channel_height]))
    u_numerical = np.concatenate(([0.0], u_interior, [0.0]))
    order = np.argsort(y)
    y, u_numerical = y[order], u_numerical[order]

    u_analytical = analytical.velocity_from_mean_velocity(y, channel_height, mean_velocity)
    velocity_metrics: ErrorMetrics = compute_error_metrics(u_numerical, u_analytical)

    u_max_numerical = float(np.max(u_numerical))
    u_max_analytical = float(np.max(u_analytical))
    u_max_relative_error = abs(u_max_numerical - u_max_analytical) / abs(u_max_analytical)

    pressure_gradient_numerical = fit_pressure_gradient(fields, *pressure_fit_x_range)
    pressure_gradient_analytical = analytical.pressure_gradient_from_mean_velocity(
        channel_height, dynamic_viscosity, mean_velocity
    )
    pressure_gradient_relative_error = abs(
        pressure_gradient_numerical - pressure_gradient_analytical
    ) / abs(pressure_gradient_analytical)

    # Secondary/supplementary check only (section 27): the authoritative
    # mass-conservation metric is `conservation.global_mass_imbalance`
    # from the C++ solver itself (result.metadata), not this integral.
    flow_rate_numerical = float(np.trapezoid(u_numerical, y))
    flow_rate_analytical = analytical.flow_rate(mean_velocity, channel_height)
    flow_rate_relative_error = abs(flow_rate_numerical - flow_rate_analytical) / abs(
        flow_rate_analytical
    )

    global_mass_imbalance = float(result.metadata["conservation"]["global_mass_imbalance"])

    return PoiseuilleValidationSummary(
        case=result.case_name,
        channel_height=channel_height,
        channel_length=float(pressure_fit_x_range[1]),  # informational only.
        dynamic_viscosity=dynamic_viscosity,
        mean_velocity=mean_velocity,
        profile_x=profile_x,
        velocity_l2=velocity_metrics.l2,
        velocity_linf=velocity_metrics.linf,
        velocity_mae=velocity_metrics.mae,
        u_max_numerical=u_max_numerical,
        u_max_analytical=u_max_analytical,
        u_max_relative_error=u_max_relative_error,
        pressure_gradient_numerical=pressure_gradient_numerical,
        pressure_gradient_analytical=pressure_gradient_analytical,
        pressure_gradient_relative_error=pressure_gradient_relative_error,
        flow_rate_numerical=flow_rate_numerical,
        flow_rate_analytical=flow_rate_analytical,
        flow_rate_relative_error=flow_rate_relative_error,
        global_mass_imbalance=global_mass_imbalance,
        mass_conservation_pass=abs(global_mass_imbalance) < mass_imbalance_tolerance,
        converged=result.converged,
    )


def poiseuille_grid_refinement(
    summaries_by_grid: dict[tuple[int, int], PoiseuilleValidationSummary],
) -> pd.DataFrame:
    """Builds the grid-refinement table (section 28): one row per grid,
    sorted coarsest-to-finest by ny, with the observed order between
    each grid and the next-finer one. `h` is the cross-stream cell size
    channel_height/ny -- the resolution that most directly governs the
    profile-shape error this validates.
    """
    rows = []
    for (nx, ny), summary in summaries_by_grid.items():
        rows.append(
            {
                "nx": nx,
                "ny": ny,
                "h": summary.channel_height / ny,
                "l2": summary.velocity_l2,
                "linf": summary.velocity_linf,
                "q_error": summary.flow_rate_relative_error,
                "pressure_gradient_error": summary.pressure_gradient_relative_error,
            }
        )
    table = pd.DataFrame(rows).sort_values("ny").reset_index(drop=True)

    observed = [np.nan]
    for i in range(1, len(table)):
        ratio = table["h"].iloc[i - 1] / table["h"].iloc[i]
        try:
            observed.append(observed_order(table["l2"].iloc[i - 1], table["l2"].iloc[i], ratio))
        except ValueError:
            observed.append(np.nan)
    table["observed_order"] = observed
    return table


def write_validation_json(summary: PoiseuilleValidationSummary, path: str | Path) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(asdict(summary), handle, indent=2)
        handle.write("\n")
    return path


def _main(argv: list[str] | None = None) -> int:
    """`python -m cfdapp.validation.poiseuille --results <dir> ...`
    (section 30). Exit code 0 = validation passed, nonzero = failed or
    bad input (section 51).
    """
    parser = argparse.ArgumentParser(description="Validate a CFDApp Poiseuille-flow result.")
    parser.add_argument("--results", required=True, help="path to a case's results/ directory")
    parser.add_argument("--mean-velocity", type=float, required=True)
    parser.add_argument("--dynamic-viscosity", type=float, required=True)
    parser.add_argument("--channel-height", type=float, required=True)
    parser.add_argument("--profile-x", type=float, required=True)
    parser.add_argument("--pressure-fit-x-min", type=float, required=True)
    parser.add_argument("--pressure-fit-x-max", type=float, required=True)
    parser.add_argument("--output", help="write validation.json here (default: <results>/analysis)")
    parser.add_argument("--velocity-l2-max", type=float, default=0.05)
    args = parser.parse_args(argv)

    try:
        result = load_case_results(args.results)
        summary = validate_poiseuille_case(
            result,
            mean_velocity=args.mean_velocity,
            dynamic_viscosity=args.dynamic_viscosity,
            channel_height=args.channel_height,
            profile_x=args.profile_x,
            pressure_fit_x_range=(args.pressure_fit_x_min, args.pressure_fit_x_max),
        )
    except Exception as exc:  # noqa: BLE001 -- CLI boundary, report and fail clearly.
        print(f"Poiseuille validation error: {exc}", file=sys.stderr)
        return 2

    output_dir = Path(args.output) if args.output else Path(args.results) / "analysis"
    write_validation_json(summary, output_dir / "validation.json")

    print(f"case: {summary.case}")
    print(f"converged: {summary.converged}")
    print(f"velocity L2: {summary.velocity_l2:.6g}")
    print(f"pressure gradient relative error: {summary.pressure_gradient_relative_error:.6g}")
    print(f"mass conservation pass: {summary.mass_conservation_pass}")

    passed = (
        summary.converged
        and summary.mass_conservation_pass
        and summary.velocity_l2 < args.velocity_l2_max
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(_main())
