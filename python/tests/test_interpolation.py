import numpy as np
import pytest

from cfdapp.interpolation import extract_profile, linear_interpolate, reshape_structured_field


def test_linear_interpolate_reproduces_exact_linear_field():
    # TODO.md "P1 -- Python Tooling" section 45: u(y) = 2y + 1, arbitrary
    # query points should reproduce exact values to floating-point
    # tolerance.
    y_known = [0.0, 1.0, 2.0, 3.0]
    u_known = [2.0 * y + 1.0 for y in y_known]
    queries = [0.5, 1.25, 2.9]
    result = linear_interpolate(y_known, u_known, queries)
    for q, r in zip(queries, result):
        assert r == pytest.approx(2.0 * q + 1.0)


def test_linear_interpolate_clamps_outside_domain():
    result = linear_interpolate([0.0, 1.0], [10.0, 20.0], [-5.0, 5.0])
    assert result[0] == pytest.approx(10.0)
    assert result[1] == pytest.approx(20.0)


def test_linear_interpolate_rejects_non_increasing_x():
    with pytest.raises(ValueError):
        linear_interpolate([1.0, 0.0], [1.0, 2.0], [0.5])


def test_extract_profile_reproduces_exact_linear_field_along_y():
    # Structured 2x3 grid: x in {0, 1}, y in {0, 1, 2}, u = 2y + 1
    # (independent of x, so bracketing/interpolating in x is exact).
    xs, ys, us = [], [], []
    for x in (0.0, 1.0):
        for y in (0.0, 1.0, 2.0):
            xs.append(x)
            ys.append(y)
            us.append(2.0 * y + 1.0)

    coordinate, value = extract_profile(xs, ys, us, fixed_axis="x", fixed_value=0.5)
    assert list(coordinate) == pytest.approx([0.0, 1.0, 2.0])
    for y, u in zip(coordinate, value):
        assert u == pytest.approx(2.0 * y + 1.0)


def test_extract_profile_interpolates_between_columns():
    # u depends on x this time: u = 10*x, sampled at x=0 and x=1 only,
    # profile at fixed x=0.25 should linearly interpolate to 2.5.
    xs = [0.0, 1.0, 0.0, 1.0]
    ys = [0.0, 0.0, 1.0, 1.0]
    us = [0.0, 10.0, 0.0, 10.0]
    coordinate, value = extract_profile(xs, ys, us, fixed_axis="x", fixed_value=0.25)
    assert np.allclose(value, 2.5)


def test_extract_profile_rejects_invalid_axis():
    with pytest.raises(ValueError):
        extract_profile([0.0], [0.0], [1.0], fixed_axis="z", fixed_value=0.0)


def test_extract_profile_rejects_missing_structured_intersection():
    # Not a full grid: (x=1, y=1) is missing.
    xs = [0.0, 1.0, 0.0]
    ys = [0.0, 0.0, 1.0]
    us = [1.0, 2.0, 3.0]
    with pytest.raises(ValueError):
        extract_profile(xs, ys, us, fixed_axis="x", fixed_value=0.5)


def test_reshape_structured_field_grid_orientation():
    # 2x2 grid: value = 10*x + y, verify grid[j, i] matches (x[i], y[j]).
    xs, ys, values = [], [], []
    for x in (0.0, 1.0):
        for y in (0.0, 1.0):
            xs.append(x)
            ys.append(y)
            values.append(10.0 * x + y)

    x_unique, y_unique, grid = reshape_structured_field(xs, ys, values)
    assert list(x_unique) == [0.0, 1.0]
    assert list(y_unique) == [0.0, 1.0]
    for j, y in enumerate(y_unique):
        for i, x in enumerate(x_unique):
            assert grid[j, i] == pytest.approx(10.0 * x + y)


def test_reshape_structured_field_rejects_incomplete_grid():
    with pytest.raises(ValueError):
        reshape_structured_field([0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [1.0, 2.0, 3.0])
