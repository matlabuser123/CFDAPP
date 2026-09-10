"""Turbulent channel-flow validation analysis (P2-TURB-007).

Unlike poiseuille.py/cavity.py, this module does not reconstruct the
wall-units profile from raw C++ field data itself -- computing wall
shear/y+/u+ requires the mesh-aware, model-aware logic already
implemented once in
tests/integration/turbulence_channel/ChannelFlowValidationUtils.cpp
(section 39: "analyze C++ output", not reimplement the C++ solver-side
computation a second time in Python). Instead this module loads that
C++ layer's own evidence -- the per-(model, grid) ``validation.json``
and ``wall_units_profile.csv``/``velocity_profile.csv`` files written
under ``results/validation/turbulence/channel_flow/<model>/<grid>/`` --
and provides the *cross-run* analysis a single C++ test cannot: grid-
refinement tables across all three tiers, side-by-side model-comparison
tables, and figures.

The law-of-the-wall reference functions below are a direct, independently
re-derived Python mirror of
ChannelFlowValidationUtils.cpp's own lawOfTheWallUPlus (same formulas,
same source -- ChannelReTau180.hpp / validation/data/turbulence/
channel_flow/README.md), used here to re-generate the reference curve
for plotting without needing to re-parse the C++ CSV's own
``reference_u_plus`` column (which only exists at the profile's sampled
y+ values, too sparse for a smooth overlay curve).
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt
import pandas as pd

from cfdapp.metrics import ErrorMetrics, compute_error_metrics

# Re_tau=180 literature reference (Kim, Moin & Moser 1987; Moser, Kim &
# Mansour 1999) -- must be kept numerically in sync with
# tests/integration/turbulence_channel/ChannelReTau180.hpp and
# validation/data/turbulence/channel_flow/reference_summary.json (the
# human-readable documented copy, including full provenance disclosure).
RE_TAU_REFERENCE = 180.0
RE_BULK_REFERENCE = 5600.0
KAPPA = 0.41
ADDITIVE_B = 5.0
VISCOUS_SUBLAYER_MAX_Y_PLUS = 5.0
BUFFER_LAYER_MAX_Y_PLUS = 30.0


def u_plus_viscous_sublayer(y_plus: npt.ArrayLike) -> npt.NDArray[np.float64]:
    """u+ = y+ (section 15)."""
    return np.asarray(y_plus, dtype=float)


def u_plus_log_law(
    y_plus: npt.ArrayLike, *, kappa: float = KAPPA, additive_b: float = ADDITIVE_B
) -> npt.NDArray[np.float64]:
    """u+ = (1/kappa)*ln(y+) + B (section 15)."""
    y_plus_arr = np.asarray(y_plus, dtype=float)
    if np.any(y_plus_arr <= 0):
        raise ValueError("u_plus_log_law: y_plus must be > 0")
    return (1.0 / kappa) * np.log(y_plus_arr) + additive_b


def law_of_the_wall(
    y_plus: npt.ArrayLike,
    *,
    kappa: float = KAPPA,
    additive_b: float = ADDITIVE_B,
    viscous_sublayer_max_y_plus: float = VISCOUS_SUBLAYER_MAX_Y_PLUS,
    buffer_layer_max_y_plus: float = BUFFER_LAYER_MAX_Y_PLUS,
) -> npt.NDArray[np.float64]:
    """Piecewise reference curve: viscous sublayer below
    ``viscous_sublayer_max_y_plus``, log law above
    ``buffer_layer_max_y_plus``, and a linear (not physical -- a plotting/
    interpolation convenience only, matching
    ChannelFlowValidationUtils.cpp's own ``lawOfTheWallUPlus`` comment)
    blend of the two boundary values in between.
    """
    y_plus_arr = np.asarray(y_plus, dtype=float)
    if np.any(y_plus_arr <= 0):
        raise ValueError("law_of_the_wall: y_plus must be > 0")

    result = np.empty_like(y_plus_arr)
    sublayer_boundary_value = viscous_sublayer_max_y_plus
    log_boundary_value = (1.0 / kappa) * np.log(buffer_layer_max_y_plus) + additive_b

    for i, y_p in np.ndenumerate(y_plus_arr):
        if y_p < viscous_sublayer_max_y_plus:
            result[i] = y_p
        elif y_p > buffer_layer_max_y_plus:
            result[i] = (1.0 / kappa) * np.log(y_p) + additive_b
        else:
            weight = (y_p - viscous_sublayer_max_y_plus) / (
                buffer_layer_max_y_plus - viscous_sublayer_max_y_plus
            )
            result[i] = sublayer_boundary_value + weight * (
                log_boundary_value - sublayer_boundary_value
            )
    return result


@dataclass(frozen=True)
class ChannelFlowRun:
    """One (model, grid) run's evidence, loaded from its validation.json
    + wall_units_profile.csv (section 37-38).
    """

    model: str
    grid_label: str
    nx: int
    ny: int
    converged: bool
    iterations: int
    global_mass_imbalance: float
    development_relative_diff: float
    u_tau: float
    first_cell_y_plus: float
    achieved_re_tau: float
    re_tau_relative_error: float
    outer_l2: float
    runtime_seconds: float
    wall_units_profile: pd.DataFrame  # columns: y_plus, numerical_u_plus, reference_u_plus.


def load_channel_flow_run(grid_dir: str | Path) -> ChannelFlowRun:
    """Loads one (model, grid) run's evidence from
    ``<grid_dir>/validation.json`` and ``<grid_dir>/wall_units_profile.csv``
    (the exact layout ChannelFlowValidationUtils.cpp's ``writeValidationJson``/
    ``computeWallUnitsErrors`` write under
    ``results/validation/turbulence/channel_flow/<model>/<grid>/``).
    """
    grid_dir = Path(grid_dir)
    with (grid_dir / "validation.json").open(encoding="utf-8") as handle:
        data = json.load(handle)
    wall_units = pd.read_csv(grid_dir / "wall_units_profile.csv")

    return ChannelFlowRun(
        model=data["model"],
        grid_label=grid_dir.name,
        nx=data["mesh"]["nx"],
        ny=data["mesh"]["ny"],
        converged=data["solver"]["converged"],
        iterations=data["solver"]["iterations"],
        global_mass_imbalance=data["solver"]["global_mass_imbalance"],
        development_relative_diff=data["development"]["relative_diff_between_stations"],
        u_tau=data["wall"]["u_tau"],
        first_cell_y_plus=data["wall"]["first_cell_y_plus"],
        achieved_re_tau=data["wall"]["achieved_re_tau"],
        re_tau_relative_error=data["wall"]["re_tau_relative_error"],
        outer_l2=data["wall_units_error"]["outer"]["l2"],
        runtime_seconds=data["solver"]["runtime_seconds"],
        wall_units_profile=wall_units,
    )


def wall_units_error_metrics(run: ChannelFlowRun) -> ErrorMetrics:
    """Re-derives the overall u+ error metrics from the loaded profile
    against ``law_of_the_wall`` (an independent cross-check of the C++
    side's own ``computeWallUnitsErrors``, not a duplicate source of
    truth -- the C++ JSON's own ``wall_units_error`` block is the
    authoritative record, this is Python-side analysis of the same
    underlying profile data).
    """
    y_plus = run.wall_units_profile["y_plus"].to_numpy()
    numerical = run.wall_units_profile["numerical_u_plus"].to_numpy()
    reference = law_of_the_wall(y_plus)
    return compute_error_metrics(numerical, reference)


def grid_refinement_table(runs: list[ChannelFlowRun]) -> pd.DataFrame:
    """One row per grid (coarsest to finest, by cell count), for a single
    model's coarse/medium/fine runs (section 23).
    """
    rows = [
        {
            "grid": run.grid_label,
            "nx": run.nx,
            "ny": run.ny,
            "cells": run.nx * run.ny,
            "first_cell_y_plus": run.first_cell_y_plus,
            "achieved_re_tau": run.achieved_re_tau,
            "re_tau_relative_error": run.re_tau_relative_error,
            "outer_l2": run.outer_l2,
            "global_mass_imbalance": run.global_mass_imbalance,
            "iterations": run.iterations,
            "runtime_seconds": run.runtime_seconds,
        }
        for run in runs
    ]
    return pd.DataFrame(rows).sort_values("cells").reset_index(drop=True)


def model_comparison_table(runs: list[ChannelFlowRun]) -> pd.DataFrame:
    """One row per model, for a fixed grid tier (section 22)."""
    rows = [
        {
            "model": run.model,
            "converged": run.converged,
            "iterations": run.iterations,
            "global_mass_imbalance": run.global_mass_imbalance,
            "achieved_re_tau": run.achieved_re_tau,
            "re_tau_relative_error": run.re_tau_relative_error,
            "outer_l2": run.outer_l2,
            "runtime_seconds": run.runtime_seconds,
        }
        for run in runs
    ]
    return pd.DataFrame(rows)


def _main(argv: list[str] | None = None) -> int:
    """``python -m cfdapp.validation.channel_flow --grid-dir <dir>``:
    reports one (model, grid) run's key metrics and exits nonzero if it
    did not converge or its mass imbalance exceeds the given tolerance
    (mirrors poiseuille.py's own ``_main`` CLI convention).
    """
    parser = argparse.ArgumentParser(
        description="Report a CFDApp turbulent-channel-flow validation run."
    )
    parser.add_argument(
        "--grid-dir",
        required=True,
        help="a results/validation/turbulence/channel_flow/<model>/<grid> directory",
    )
    parser.add_argument("--mass-imbalance-max", type=float, default=1e-6)
    args = parser.parse_args(argv)

    try:
        run = load_channel_flow_run(args.grid_dir)
    except Exception as exc:  # noqa: BLE001 -- CLI boundary, report and fail clearly.
        print(f"channel-flow validation error: {exc}", file=sys.stderr)
        return 2

    metrics = wall_units_error_metrics(run)
    print(f"model: {run.model}  grid: {run.grid_label} ({run.nx}x{run.ny})")
    print(f"converged: {run.converged}  iterations: {run.iterations}")
    print(
        f"achieved Re_tau: {run.achieved_re_tau:.2f} (target {RE_TAU_REFERENCE:.0f}, "
        f"relative error {run.re_tau_relative_error:.3f})"
    )
    print(f"outer-layer u+ L2 error: {metrics.l2:.4f}")
    print(f"global mass imbalance: {run.global_mass_imbalance:.3e}")

    passed = run.converged and abs(run.global_mass_imbalance) < args.mass_imbalance_max
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(_main())
