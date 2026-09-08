"""Residual history plotting (TODO.md "P1 -- Python Tooling" sections
8-10).
"""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

from cfdapp.results import ResultData

#: (column, legend label) pairs, always plotted in this order.
_SERIES = (
    ("u_residual", "U residual"),
    ("v_residual", "V residual"),
    ("p_residual", "P residual"),
    ("continuity_residual", "Continuity residual"),
)


def plot_residual_history(
    result: ResultData,
    output_path: str | Path,
    *,
    tolerances: dict[str, float] | None = None,
) -> Path:
    """Writes residual_history.png: all four residual histories against
    outer SIMPLE iteration, semilogy (section 8 -- residuals span many
    orders of magnitude), with the case name, a legend, and a grid
    (section 9). Plots the *actual* stored history verbatim -- no
    smoothing or oscillation removal (section 9).

    `tolerances`, if given, draws each named tolerance
    ("u"/"v"/"p"/"continuity") as a horizontal reference line (section
    10) -- values must come from the case's own solver settings/
    metadata, never invented here.
    """
    residuals: pd.DataFrame = result.residuals

    fig, ax = plt.subplots(figsize=(8, 5))
    for column, label in _SERIES:
        ax.semilogy(residuals["iteration"], residuals[column], label=label)

    if tolerances:
        tolerance_labels = {
            "u": "U tol",
            "v": "V tol",
            "p": "P tol",
            "continuity": "Continuity tol",
        }
        for key, value in tolerances.items():
            ax.axhline(value, linestyle="--", linewidth=0.8, color="gray")
            ax.annotate(
                tolerance_labels.get(key, key),
                xy=(residuals["iteration"].iloc[-1], value),
                xytext=(2, 2),
                textcoords="offset points",
                fontsize=7,
                color="gray",
            )

    ax.set_xlabel("Iteration")
    ax.set_ylabel("Residual")
    ax.set_title(f"Residual history -- {result.case_name}")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path)
    plt.close(fig)
    return output_path
