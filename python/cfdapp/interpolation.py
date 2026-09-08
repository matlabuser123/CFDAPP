"""Deterministic linear interpolation (TODO.md "P1 -- Python Tooling"
section 12: never nearest-cell lookup when comparing against exact
reference coordinates).

``extract_profile`` mirrors the same bracket/lerp convention the C++
validation code already uses
(tests/integration/cavity/CavityValidationUtils.cpp,
tests/integration/poiseuille/PoiseuilleValidationUtils.cpp): for a
structured mesh, bracket the fixed coordinate between the two nearest
column/row values, then linearly interpolate between them at each
distinct value of the varying coordinate.
"""

from __future__ import annotations

import numpy as np
import numpy.typing as npt


def _bracket(sorted_unique: npt.NDArray[np.float64], target: float) -> tuple[int, int]:
    """Indices of the pair in `sorted_unique` straddling `target`,
    clamped to the interior interval -- never extrapolates past the
    first/last sample (same convention as the C++ `bracket()` helpers).
    """
    n = len(sorted_unique)
    if n < 2:
        raise ValueError("_bracket: need at least two distinct coordinate values")
    hi = 1
    while hi < n - 1 and sorted_unique[hi] < target:
        hi += 1
    return hi - 1, hi


def _lerp(x0: float, y0: float, x1: float, y1: float, x: float) -> float:
    if x1 == x0:
        return y0
    weight = (x - x0) / (x1 - x0)
    return y0 + weight * (y1 - y0)


def linear_interpolate(
    x_known: npt.ArrayLike, y_known: npt.ArrayLike, x_query: npt.ArrayLike
) -> npt.NDArray[np.float64]:
    """Piecewise-linear interpolation of (x_known, y_known) at x_query.

    x_known must be sorted ascending. A query outside [x_known[0],
    x_known[-1]] is clamped to the nearest endpoint's value (never
    extrapolated) -- equivalent to numpy.interp's default, used here
    directly for that reason.
    """
    x_known_arr = np.asarray(x_known, dtype=float)
    if np.any(np.diff(x_known_arr) <= 0):
        raise ValueError("linear_interpolate: x_known must be strictly increasing")
    return np.interp(
        np.asarray(x_query, dtype=float), x_known_arr, np.asarray(y_known, dtype=float)
    )


def extract_profile(
    x: npt.ArrayLike,
    y: npt.ArrayLike,
    values: npt.ArrayLike,
    *,
    fixed_axis: str,
    fixed_value: float,
) -> tuple[npt.NDArray[np.float64], npt.NDArray[np.float64]]:
    """Extracts a 1D profile from scattered structured-mesh cell data by
    holding one coordinate fixed (section 11: vertical/horizontal/
    arbitrary constant-x/constant-y lines).

    `fixed_axis` is "x" (profile varies in y, e.g. a vertical centerline
    at x=0.5) or "y" (profile varies in x). `fixed_value` need not
    coincide with an actual column/row coordinate -- it is bracketed
    between the two nearest ones and interpolated, exactly like the C++
    convention this mirrors.

    Returns (varying_coordinate, value), both sorted ascending by the
    varying coordinate. Raises ValueError if the data does not fit a
    structured grid (a row/column intersection is missing).
    """
    x_arr = np.asarray(x, dtype=float)
    y_arr = np.asarray(y, dtype=float)
    values_arr = np.asarray(values, dtype=float)

    if fixed_axis == "x":
        fixed_coord, vary_coord = x_arr, y_arr
    elif fixed_axis == "y":
        fixed_coord, vary_coord = y_arr, x_arr
    else:
        raise ValueError(f"extract_profile: fixed_axis must be 'x' or 'y', got {fixed_axis!r}")

    fixed_unique = np.array(sorted(set(fixed_coord.tolist())))
    i0, i1 = _bracket(fixed_unique, fixed_value)
    f0, f1 = fixed_unique[i0], fixed_unique[i1]

    vary_unique = np.array(sorted(set(vary_coord.tolist())))
    result = np.empty_like(vary_unique)
    for k, v in enumerate(vary_unique):
        mask0 = np.isclose(fixed_coord, f0) & np.isclose(vary_coord, v)
        mask1 = np.isclose(fixed_coord, f1) & np.isclose(vary_coord, v)
        if not mask0.any() or not mask1.any():
            raise ValueError(
                "extract_profile: no cell found at the expected row/column intersection -- "
                "data does not fit a structured grid"
            )
        result[k] = _lerp(
            f0, float(values_arr[mask0][0]), f1, float(values_arr[mask1][0]), fixed_value
        )

    return vary_unique, result


def reshape_structured_field(
    x: npt.ArrayLike, y: npt.ArrayLike, values: npt.ArrayLike
) -> tuple[npt.NDArray[np.float64], npt.NDArray[np.float64], npt.NDArray[np.float64]]:
    """Reconstructs a 2D field from scattered structured-mesh cell data
    (TODO.md "P1 -- Python Tooling" section 14: "reconstruct the 2D field
    using sorted unique coordinates").

    Returns (x_unique, y_unique, grid) with grid.shape == (len(y_unique),
    len(x_unique)) and grid[j, i] == the value at (x_unique[i],
    y_unique[j]) -- the row/column convention matplotlib's
    contourf/pcolormesh expect. Raises ValueError if a grid cell is
    missing (data does not fit a structured grid) or duplicated.
    """
    x_arr = np.asarray(x, dtype=float)
    y_arr = np.asarray(y, dtype=float)
    values_arr = np.asarray(values, dtype=float)

    x_unique = np.array(sorted(set(x_arr.tolist())))
    y_unique = np.array(sorted(set(y_arr.tolist())))
    if len(x_arr) != len(x_unique) * len(y_unique):
        raise ValueError(
            "reshape_structured_field: cell count does not match a full structured grid "
            f"({len(x_arr)} cells, expected {len(x_unique) * len(y_unique)} for "
            f"{len(x_unique)}x{len(y_unique)})"
        )

    grid = np.full((len(y_unique), len(x_unique)), np.nan)
    x_index = {value: i for i, value in enumerate(x_unique)}
    y_index = {value: j for j, value in enumerate(y_unique)}
    for xi, yi, value in zip(x_arr, y_arr, values_arr):
        i = x_index[xi]
        j = y_index[yi]
        if not np.isnan(grid[j, i]):
            raise ValueError(f"reshape_structured_field: duplicate cell at (x={xi}, y={yi})")
        grid[j, i] = value
    if np.isnan(grid).any():
        raise ValueError(
            "reshape_structured_field: a grid cell is missing (not a full structured grid)"
        )

    return x_unique, y_unique, grid
