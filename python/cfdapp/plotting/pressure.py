"""Pressure contour plotting (TODO.md "P1 -- Python Tooling" section 14)."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

from cfdapp.interpolation import reshape_structured_field


def plot_pressure_contour(
    fields: pd.DataFrame, output_path: str | Path, *, title: str = "Pressure"
) -> Path:
    """Writes pressure_contour.png from the exported cell-center pressure
    directly (section 14/21 -- never interpolated to vertices first)."""
    x_unique, y_unique, pressure_grid = reshape_structured_field(
        fields["x"], fields["y"], fields["pressure"]
    )

    fig, ax = plt.subplots(figsize=(6, 5))
    contour = ax.contourf(x_unique, y_unique, pressure_grid, levels=20, cmap="viridis")
    fig.colorbar(contour, ax=ax, label="pressure")
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
