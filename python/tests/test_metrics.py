import math

import pytest

from cfdapp.metrics import compute_error_metrics, observed_order


def test_compute_error_metrics_matches_hand_derivation():
    # TODO.md "P1 -- Python Tooling" section 44's exact example.
    reference = [1, 2, 3]
    numerical = [1, 3, 2]
    # errors: [0, 1, -1]
    metrics = compute_error_metrics(numerical, reference)
    assert metrics.mae == pytest.approx(2.0 / 3.0)
    assert metrics.l2 == pytest.approx(math.sqrt(2.0 / 3.0))
    assert metrics.linf == pytest.approx(1.0)


def test_compute_error_metrics_zero_error_is_all_zero():
    metrics = compute_error_metrics([1.0, 2.0, 3.0], [1.0, 2.0, 3.0])
    assert metrics.l2 == 0.0
    assert metrics.linf == 0.0
    assert metrics.mae == 0.0


def test_compute_error_metrics_rejects_shape_mismatch():
    with pytest.raises(ValueError):
        compute_error_metrics([1.0, 2.0], [1.0, 2.0, 3.0])


def test_compute_error_metrics_rejects_empty_input():
    with pytest.raises(ValueError):
        compute_error_metrics([], [])


def test_observed_order_second_order_halving_error():
    # error halves each refinement -> should not be exactly p=1; a
    # genuine second-order case: error quarters when h halves.
    assert observed_order(
        error_coarse=0.04, error_fine=0.01, refinement_ratio=2.0
    ) == pytest.approx(2.0)


def test_observed_order_first_order():
    assert observed_order(
        error_coarse=0.08, error_fine=0.04, refinement_ratio=2.0
    ) == pytest.approx(1.0)


def test_observed_order_rejects_non_positive_errors():
    with pytest.raises(ValueError):
        observed_order(0.0, 0.01)
    with pytest.raises(ValueError):
        observed_order(0.01, -0.001)


def test_observed_order_rejects_bad_refinement_ratio():
    with pytest.raises(ValueError):
        observed_order(0.04, 0.01, refinement_ratio=1.0)
