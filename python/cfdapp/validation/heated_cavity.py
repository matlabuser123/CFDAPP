"""Heated-cavity (pure-conduction) validation workflow (P2-THERMAL-004):
load -> compare every cell's temperature against the closed-form
analytical profile -> error metrics -> grid refinement -> summary.

Deliberately does not recompute heat balance from field data here -- the
C++ validation
(tests/integration/thermal/test_heated_cavity_validation.cpp) already
computes and checks it directly from the solved fields with access to
mesh face geometry Python's exported CSV does not carry; this module
sticks to what fields.csv/metadata.json actually expose, matching every
other validation module's "Python analyzes exported C++ results, never
re-derives solver-internal geometry" convention.

Explicitly NOT natural convection -- see this module's own docstring
precedent in TODO.md "P2 -- Thermal": pure conduction only (analytical
T(x) is independent of y because velocity is zero everywhere in this
case, not because of any y-averaging done here).
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np
import pandas as pd

from cfdapp.metrics import ErrorMetrics, compute_error_metrics, observed_order
from cfdapp.results import ResultData, load_case_results


def analytical_temperature(
    x: np.ndarray | float, hot_temperature: float, cold_temperature: float, length: float
) -> np.ndarray | float:
    """T(x) = Th + (Tc - Th) * x / L -- the 1D steady-conduction profile
    a differentially-heated cavity with adiabatic top/bottom reduces to
    (independent of y).
    """
    return hot_temperature + (cold_temperature - hot_temperature) * (np.asarray(x) / length)


@dataclass(frozen=True)
class HeatedCavityValidationSummary:
    case: str
    length: float
    hot_temperature: float
    cold_temperature: float
    temperature_l2: float
    temperature_linf: float
    temperature_mae: float
    within_bounds: bool
    converged: bool


def validate_heated_cavity_case(
    result: ResultData,
    *,
    hot_temperature: float,
    cold_temperature: float,
    length: float,
) -> HeatedCavityValidationSummary:
    if result.fields is None:
        raise ValueError(
            "validate_heated_cavity_case: result has no fields.csv (non-finite solve) -- "
            "cannot validate"
        )
    if not result.has_temperature:
        raise ValueError(
            "validate_heated_cavity_case: fields.csv has no 'temperature' column -- "
            "not a thermal-enabled, finite result"
        )
    fields = result.fields

    x = fields["x"].to_numpy(dtype=float)
    numerical = fields["temperature"].to_numpy(dtype=float)
    analytical = analytical_temperature(x, hot_temperature, cold_temperature, length)
    metrics: ErrorMetrics = compute_error_metrics(numerical, analytical)

    lo, hi = sorted((hot_temperature, cold_temperature))
    # A small tolerance above/below the wall values -- discretization
    # error, not a hard physical bound, so this is deliberately not an
    # exact [lo, hi] clamp.
    tolerance = 1e-3 * abs(hi - lo)
    within_bounds = bool(
        np.all(numerical >= lo - tolerance) and np.all(numerical <= hi + tolerance)
    )

    return HeatedCavityValidationSummary(
        case=result.case_name,
        length=length,
        hot_temperature=hot_temperature,
        cold_temperature=cold_temperature,
        temperature_l2=metrics.l2,
        temperature_linf=metrics.linf,
        temperature_mae=metrics.mae,
        within_bounds=within_bounds,
        converged=result.converged and bool(result.metadata["thermal"]["converged"]),
    )


def heated_cavity_grid_refinement(
    summaries_by_grid: dict[tuple[int, int], HeatedCavityValidationSummary],
) -> pd.DataFrame:
    """One row per grid, sorted coarsest-to-finest by nx, with the
    observed order between each grid and the next-finer one -- reported
    for completeness, but not expected to show a *meaningful* order here:
    this discretization is exact for an affine analytical field (see the
    C++ validation test's own comment), so every grid's error already
    sits at the outer-loop convergence floor rather than shrinking with
    h, and `observed_order` will reflect that (small/noisy), not a
    genuine truncation-error trend.
    """
    rows = []
    for (nx, ny), summary in summaries_by_grid.items():
        rows.append(
            {
                "nx": nx,
                "ny": ny,
                "h": summary.length / nx,
                "l2": summary.temperature_l2,
                "linf": summary.temperature_linf,
            }
        )
    table = pd.DataFrame(rows).sort_values("nx").reset_index(drop=True)

    observed = [np.nan]
    for i in range(1, len(table)):
        ratio = table["h"].iloc[i - 1] / table["h"].iloc[i]
        try:
            observed.append(observed_order(table["l2"].iloc[i - 1], table["l2"].iloc[i], ratio))
        except ValueError:
            observed.append(np.nan)
    table["observed_order"] = observed
    return table


def write_validation_json(summary: HeatedCavityValidationSummary, path: str | Path) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(asdict(summary), handle, indent=2)
        handle.write("\n")
    return path


def _main(argv: list[str] | None = None) -> int:
    """`python -m cfdapp.validation.heated_cavity --results <dir> ...`.
    Exit code 0 = validation passed, nonzero = failed or bad input.
    """
    parser = argparse.ArgumentParser(description="Validate a CFDApp heated-cavity result.")
    parser.add_argument("--results", required=True, help="path to a case's results/ directory")
    parser.add_argument("--hot-temperature", type=float, required=True)
    parser.add_argument("--cold-temperature", type=float, required=True)
    parser.add_argument("--length", type=float, required=True)
    parser.add_argument("--output", help="write validation.json here (default: <results>/analysis)")
    parser.add_argument("--temperature-l2-max", type=float, default=1e-3)
    args = parser.parse_args(argv)

    try:
        result = load_case_results(args.results)
        summary = validate_heated_cavity_case(
            result,
            hot_temperature=args.hot_temperature,
            cold_temperature=args.cold_temperature,
            length=args.length,
        )
    except Exception as exc:  # noqa: BLE001 -- CLI boundary, report and fail clearly.
        print(f"Heated-cavity validation error: {exc}", file=sys.stderr)
        return 2

    output_dir = Path(args.output) if args.output else Path(args.results) / "analysis"
    write_validation_json(summary, output_dir / "validation.json")

    print(f"case: {summary.case}")
    print(f"converged: {summary.converged}")
    print(f"temperature L2: {summary.temperature_l2:.6g}")
    print(f"within bounds: {summary.within_bounds}")

    passed = (
        summary.converged
        and summary.within_bounds
        and summary.temperature_l2 < args.temperature_l2_max
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(_main())
