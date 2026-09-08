"""Shared result loader -- the one Python API boundary onto a CFDApp
results/ directory (TODO.md "P1 -- Python Tooling" sections 5-7). Every
other module (plotting, validation) consumes ``ResultData`` from here
rather than parsing CSV/JSON itself.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path

import pandas as pd

# The exact contract CFDApp's C++ CSVWriter/JSONWriter/ResultExporter
# write (src/io/CSVWriter.cpp, src/io/JSONWriter.cpp). Kept as named
# constants, not scattered string literals, so a real contract change is
# a one-line diff here plus whatever test catches it.
FIELDS_COLUMNS = ("cell_id", "x", "y", "velocity_x", "velocity_y", "velocity_magnitude", "pressure")
RESIDUALS_COLUMNS = (
    "iteration",
    "u_residual",
    "v_residual",
    "p_residual",
    "continuity_residual",
    "global_mass_imbalance",
)
_REQUIRED_METADATA_PATHS = (
    ("case", "name"),
    ("mesh", "cells"),
    ("mesh", "faces"),
    ("mesh", "nx"),
    ("mesh", "ny"),
    ("physics", "density"),
    ("physics", "dynamic_viscosity"),
    ("solver", "type"),
    ("solver", "converged"),
    ("solver", "status"),
    ("solver", "iterations"),
    ("residuals", "u"),
    ("residuals", "v"),
    ("residuals", "p"),
    ("residuals", "continuity"),
    ("conservation", "global_mass_imbalance"),
    ("numerics", "finite"),
)


class ResultLoadError(Exception):
    """A results/ directory does not satisfy the exporter contract.

    Raised instead of letting a missing column / malformed file surface
    as a bare ``KeyError``/``pandas`` exception -- TODO.md section 6:
    "fail clearly if the C++ exporter contract changes unexpectedly."
    """


@dataclass(frozen=True)
class ResultData:
    """A parsed, validated CFDApp results/ directory.

    ``fields`` is ``None`` exactly when the solve's fields were
    non-finite and CFDApp's ResultExporter therefore did not write
    fields.csv/solution.vtk at all (see its own failed-solve export
    policy, TODO.md "P1 -- Result Export" section 29) -- callers that
    need field data should check for this rather than assume it always
    exists just because ``residuals``/``metadata`` do.
    """

    fields: pd.DataFrame | None
    residuals: pd.DataFrame
    metadata: dict
    source_dir: Path

    @property
    def case_name(self) -> str:
        return self.metadata["case"]["name"]

    @property
    def converged(self) -> bool:
        return bool(self.metadata["solver"]["converged"])

    @property
    def status(self) -> str:
        return str(self.metadata["solver"]["status"])

    @property
    def nx(self) -> int:
        return int(self.metadata["mesh"]["nx"])

    @property
    def ny(self) -> int:
        return int(self.metadata["mesh"]["ny"])


def _require_columns(frame: pd.DataFrame, required: tuple[str, ...], file_label: str) -> None:
    missing = [c for c in required if c not in frame.columns]
    if missing:
        raise ResultLoadError(f"{file_label}: missing required column(s) {missing}")


def _require_finite(frame: pd.DataFrame, columns: tuple[str, ...], file_label: str) -> None:
    for column in columns:
        if not pd.api.types.is_numeric_dtype(frame[column]):
            continue
        if not frame[column].apply(math.isfinite).all():
            raise ResultLoadError(f"{file_label}: column '{column}' contains a non-finite value")


def _require_metadata_keys(metadata: dict, path: Path) -> None:
    for keys in _REQUIRED_METADATA_PATHS:
        node = metadata
        for key in keys:
            if not isinstance(node, dict) or key not in node:
                raise ResultLoadError(f"{path}: missing required key '{'.'.join(keys)}'")
            node = node[key]


def load_case_results(results_dir: str | Path) -> ResultData:
    """Loads and validates one CFDApp results/ directory.

    Requires ``residuals.csv`` and ``metadata.json`` (CFDApp always
    writes both, regardless of solver outcome -- TODO.md "P1 -- Result
    Export" section 29). ``fields.csv`` is loaded if present, otherwise
    ``ResultData.fields`` is ``None``.

    Raises ``ResultLoadError`` for anything that fails this package's
    understanding of the exporter contract: a missing required file, a
    missing required column, a non-finite exported value, or an
    inconsistent residual-history length. Raises ``FileNotFoundError``
    if ``results_dir`` itself does not exist.
    """
    results_dir = Path(results_dir)
    if not results_dir.is_dir():
        raise FileNotFoundError(f"results directory not found: {results_dir}")

    metadata_path = results_dir / "metadata.json"
    residuals_path = results_dir / "residuals.csv"
    fields_path = results_dir / "fields.csv"

    if not metadata_path.is_file():
        raise ResultLoadError(f"required file not found: {metadata_path}")
    if not residuals_path.is_file():
        raise ResultLoadError(f"required file not found: {residuals_path}")

    with metadata_path.open(encoding="utf-8") as handle:
        try:
            metadata = json.load(handle)
        except json.JSONDecodeError as exc:
            raise ResultLoadError(f"{metadata_path}: malformed JSON: {exc}") from exc
    _require_metadata_keys(metadata, metadata_path)

    residuals = pd.read_csv(residuals_path)
    _require_columns(residuals, RESIDUALS_COLUMNS, str(residuals_path))
    _require_finite(residuals, RESIDUALS_COLUMNS, str(residuals_path))
    if len(residuals) == 0:
        raise ResultLoadError(f"{residuals_path}: no residual history rows")
    # section 9's C++-side invariant, re-checked here since Python treats
    # the CSV as the source of truth rather than re-deriving anything:
    # iteration column must be exactly 1..N with no gaps.
    expected_iterations = list(range(1, len(residuals) + 1))
    if list(residuals["iteration"]) != expected_iterations:
        raise ResultLoadError(
            f"{residuals_path}: iteration column is not a contiguous 1..N sequence"
        )

    fields: pd.DataFrame | None = None
    if fields_path.is_file():
        fields = pd.read_csv(fields_path)
        _require_columns(fields, FIELDS_COLUMNS, str(fields_path))
        _require_finite(fields, FIELDS_COLUMNS, str(fields_path))
        if fields["cell_id"].duplicated().any():
            raise ResultLoadError(f"{fields_path}: duplicate cell_id values")
        expected_cell_ids = list(range(len(fields)))
        if sorted(fields["cell_id"]) != expected_cell_ids:
            raise ResultLoadError(f"{fields_path}: cell_id values are not exactly 0..N-1")
    elif metadata.get("numerics", {}).get("finite", False):
        # metadata says the fields *were* finite, so fields.csv should
        # exist -- a missing file here means the exporter contract
        # changed, not a legitimate failed-solve case.
        raise ResultLoadError(
            f"{fields_path}: not found, but metadata.json reports numerics.finite=true"
        )

    return ResultData(fields=fields, residuals=residuals, metadata=metadata, source_dir=results_dir)
