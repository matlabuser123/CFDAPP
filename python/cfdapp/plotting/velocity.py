"""Velocity profile and vector plotting (TODO.md "P1 -- Python Tooling"
sections 11, 16).
"""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy.typing as npt
import pandas as pd


def plot_velocity_profile(
    coordinate: npt.ArrayLike,
    numerical: npt.ArrayLike,
    *,
    reference_coordinate: npt.ArrayLike | None = None,
    reference_value: npt.ArrayLike | None = None,
    reference_label: str = "Reference",
    coordinate_label: str,
    value_label: str,
    title: str,
    output_path: str | Path,
) -> Path:
    """One numerical profile curve, plus an optional scattered reference
    (Ghia points, or an analytical curve) -- used by both the cavity and
    Poiseuille validation modules (sections 20, 25) so the two don't each
    reimplement plotting.
    """
    fig, ax = plt.subplots(figsize=(6, 5))
    ax.plot(coordinate, numerical, "-", color="C0", label="CFDApp (numerical)")
    if reference_coordinate is not None and reference_value is not None:
        ax.plot(
            reference_coordinate,
            reference_value,
            "o",
            color="C1",
            markersize=5,
            label=reference_label,
        )
    ax.set_xlabel(coordinate_label)
    ax.set_ylabel(value_label)
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path)
    plt.close(fig)
    return output_path


def plot_velocity_vectors(
    fields: pd.DataFrame,
    output_path: str | Path,
    *,
    subsample: int = 4,
    title: str = "Velocity vectors",
) -> Path:
    """Quiver plot of (velocity_x, velocity_y), deterministically
    subsampled (section 16: never one arrow per cell on a large mesh --
    "every 4th cell" by default, matching the section's own example).

    Subsampling takes every `subsample`-th row of `fields` *as already
    cell-id-ordered by the exporter* (CSVWriter writes cell 0..N-1 in
    order -- see results.py), not a random or unordered selection, so
    the same input always produces the same plotted arrows.
    """
    if subsample < 1:
        raise ValueError("plot_velocity_vectors: subsample must be >= 1")
    subset = fields.iloc[::subsample]

    fig, ax = plt.subplots(figsize=(6, 6))
    ax.quiver(subset["x"], subset["y"], subset["velocity_x"], subset["velocity_y"])
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(title)
    ax.set_aspect("equal")
    fig.tight_layout()

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path)
    plt.close(fig)
    return output_path
