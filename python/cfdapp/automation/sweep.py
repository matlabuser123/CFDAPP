"""Runs the same executable across several case directories and
collects one combined table -- e.g. a grid-refinement sweep, built on
top of `runner`/`benchmark` rather than duplicating their subprocess
logic.
"""

from __future__ import annotations

from pathlib import Path

import pandas as pd

from cfdapp.automation.runner import run_case


def run_sweep(
    executable: str | Path, case_paths: dict[str, str | Path], *, results_subdir: str = "results"
) -> pd.DataFrame:
    """Runs `<executable> --case <path>` once for each (label, path) in
    `case_paths`, in the given order (a plain dict, so iteration order is
    whatever the caller provided -- deterministic, not re-sorted).

    Returns a DataFrame with one row per case: label, exit_code, outcome,
    runtime_seconds, and (when the run produced one) the results
    directory path -- a thin summary, not a replacement for
    `automation.benchmark` when actual performance measurement is
    needed.
    """
    rows = []
    for label, case_path in case_paths.items():
        case_path = Path(case_path)
        result = run_case(executable, case_path)
        results_dir = case_path / results_subdir
        rows.append(
            {
                "label": label,
                "case_path": str(case_path),
                "exit_code": result.exit_code,
                "outcome": result.outcome,
                "runtime_seconds": result.runtime_seconds,
                "results_dir": str(results_dir) if results_dir.is_dir() else None,
            }
        )
    return pd.DataFrame(rows)
