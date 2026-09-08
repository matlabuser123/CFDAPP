"""Velocity contour plotting (TODO.md "P1 -- Python Tooling" section 15)."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

from cfdapp.interpolation import reshape_structured_field


def _plot_scalar_contour(
    fields: pd.DataFrame, column: str, output_path: str | Path, *, title: str, colorbar_label: str
) -> Path:
    x_unique, y_unique, grid = reshape_structured_field(fields["x"], fields["y"], fields[column])

    fig, ax = plt.subplots(figsize=(6, 5))
    contour = ax.contourf(x_unique, y_unique, grid, levels=20, cmap="viridis")
    fig.colorbar(contour, ax=ax, label=colorbar_label)
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


def plot_velocity_magnitude_contour(
    fields: pd.DataFrame, output_path: str | Path, *, title: str = "Velocity magnitude"
) -> Path:
    """|U| = sqrt(u^2+v^2) is the one postprocessing quantity this
    package derives rather than reads (section 15) -- CFDApp's own
    CSVWriter already exports it as `velocity_magnitude` (see
    src/io/CSVWriter.cpp), used directly here rather than recomputed, so
    this plot always agrees with fields.csv's own column exactly.
    """
    return _plot_scalar_contour(
        fields, "velocity_magnitude", output_path, title=title, colorbar_label="|U|"
    )


def plot_u_velocity_contour(
    fields: pd.DataFrame, output_path: str | Path, *, title: str = "U velocity"
) -> Path:
    return _plot_scalar_contour(fields, "velocity_x", output_path, title=title, colorbar_label="u")


def plot_v_velocity_contour(
    fields: pd.DataFrame, output_path: str | Path, *, title: str = "V velocity"
) -> Path:
    return _plot_scalar_contour(fields, "velocity_y", output_path, title=title, colorbar_label="v")
