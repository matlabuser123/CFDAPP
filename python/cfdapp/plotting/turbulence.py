"""Turbulence-benchmark figures (P2-TURB-007 sections 40-41)."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import numpy.typing as npt
import pandas as pd

from cfdapp.validation.channel_flow import law_of_the_wall


def plot_wall_units(
    y_plus: npt.ArrayLike,
    u_plus: npt.ArrayLike,
    *,
    label: str,
    title: str,
    output_path: str | Path,
) -> Path:
    """u+ vs y+ on a log-x axis (section 41), overlaying the numerical
    profile against the viscous-sublayer line (u+=y+) and the log law --
    each labeled separately (section 41: "do not visually imply either
    approximate law is DNS data").
    """
    y_plus_arr = np.asarray(y_plus, dtype=float)
    u_plus_arr = np.asarray(u_plus, dtype=float)

    y_smooth = np.logspace(np.log10(max(y_plus_arr.min(), 0.1)), np.log10(y_plus_arr.max()), 200)
    reference_smooth = law_of_the_wall(y_smooth)
    sublayer_y = y_smooth[y_smooth <= 12.0]

    fig, ax = plt.subplots(figsize=(6, 5))
    ax.plot(y_plus_arr, u_plus_arr, "o-", color="C0", label=label)
    ax.plot(sublayer_y, sublayer_y, "--", color="C2", label="viscous sublayer (u+ = y+)")
    ax.plot(y_smooth, reference_smooth, ":", color="C1", label="log law (kappa=0.41, B=5.0)")
    ax.set_xscale("log")
    ax.set_xlabel("y+")
    ax.set_ylabel("u+")
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path)
    plt.close(fig)
    return output_path


def plot_model_comparison(
    comparison: pd.DataFrame, *, value_column: str, title: str, output_path: str | Path
) -> Path:
    """Bar chart of `value_column` (e.g. outer_l2, re_tau_relative_error)
    across models -- section 22's "one direct table" restated visually.
    """
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.bar(comparison["model"], comparison[value_column], color="C0")
    ax.set_ylabel(value_column)
    ax.set_title(title)
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path)
    plt.close(fig)
    return output_path


def plot_grid_refinement(
    refinement: pd.DataFrame, *, value_column: str, title: str, output_path: str | Path
) -> Path:
    """`value_column` vs cell count across coarse/medium/fine grids
    (section 23), log-x since cell counts span more than a decade.
    """
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.plot(refinement["cells"], refinement[value_column], "o-", color="C0")
    ax.set_xscale("log")
    ax.set_xlabel("cell count")
    ax.set_ylabel(value_column)
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout()

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path)
    plt.close(fig)
    return output_path
