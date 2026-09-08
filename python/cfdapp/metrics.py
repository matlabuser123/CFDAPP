"""Shared error metrics (TODO.md "P1 -- Python Tooling" section 19/44).

Every validation module (cavity, poiseuille) computes its errors with
these, so the formula is defined exactly once.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt


@dataclass(frozen=True)
class ErrorMetrics:
    l2: float
    linf: float
    mae: float


def compute_error_metrics(numerical: npt.ArrayLike, reference: npt.ArrayLike) -> ErrorMetrics:
    """L2 = sqrt(mean(e^2)), Linf = max(abs(e)), MAE = mean(abs(e)), for
    e = numerical - reference (section 19). Not percentage error --
    section 19 explicitly rules that out, since these reference values
    pass through zero.
    """
    numerical_arr = np.asarray(numerical, dtype=float)
    reference_arr = np.asarray(reference, dtype=float)
    if numerical_arr.shape != reference_arr.shape:
        raise ValueError(
            f"compute_error_metrics: shape mismatch {numerical_arr.shape} vs {reference_arr.shape}"
        )
    if numerical_arr.size == 0:
        raise ValueError("compute_error_metrics: empty input")

    error = numerical_arr - reference_arr
    return ErrorMetrics(
        l2=float(np.sqrt(np.mean(error**2))),
        linf=float(np.max(np.abs(error))),
        mae=float(np.mean(np.abs(error))),
    )


def observed_order(error_coarse: float, error_fine: float, refinement_ratio: float = 2.0) -> float:
    """p = log(E_h / E_(h/2)) / log(refinement_ratio) (section 28).

    Both errors must be strictly positive (an exactly-zero error, e.g. a
    trivial/degenerate case, makes the order undefined -- raise rather
    than silently return inf/nan).
    """
    if not (error_coarse > 0.0 and error_fine > 0.0):
        raise ValueError("observed_order: both errors must be strictly positive")
    if not (refinement_ratio > 1.0):
        raise ValueError("observed_order: refinement_ratio must be > 1")
    return float(np.log(error_coarse / error_fine) / np.log(refinement_ratio))
