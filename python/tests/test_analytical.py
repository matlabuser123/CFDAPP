import pytest

from cfdapp.validation import analytical

H = 1.0
MU = 0.01
DPDX = -0.08


def test_velocity_from_pressure_gradient_matches_worked_example():
    # TODO.md "P1 -- Python Tooling" section 43's exact example: should
    # pass at essentially floating-point precision.
    assert analytical.velocity_from_pressure_gradient(0.0, H, MU, DPDX) == pytest.approx(
        0.0, abs=1e-12
    )
    assert analytical.velocity_from_pressure_gradient(H, H, MU, DPDX) == pytest.approx(
        0.0, abs=1e-12
    )
    assert analytical.velocity_from_pressure_gradient(0.5, H, MU, DPDX) == pytest.approx(1.0)


def test_max_velocity_matches_worked_example():
    assert analytical.max_velocity(H, MU, DPDX) == pytest.approx(1.0)


def test_mean_velocity_matches_worked_example():
    assert analytical.mean_velocity_from_pressure_gradient(H, MU, DPDX) == pytest.approx(2.0 / 3.0)


def test_flow_rate_matches_worked_example():
    u_mean = analytical.mean_velocity_from_pressure_gradient(H, MU, DPDX)
    assert analytical.flow_rate(u_mean, H) == pytest.approx(2.0 / 3.0)


def test_pressure_gradient_from_mean_velocity_is_the_inverse():
    u_mean = analytical.mean_velocity_from_pressure_gradient(H, MU, DPDX)
    recovered_dpdx = analytical.pressure_gradient_from_mean_velocity(H, MU, u_mean)
    assert recovered_dpdx == pytest.approx(DPDX)


def test_velocity_from_mean_velocity_matches_velocity_from_pressure_gradient():
    # Two independent parameterizations of the same physical profile
    # (mirrors the same cross-check the C++ side makes between
    # PressureCorrectionEquation's D-coefficient path and its own
    # analytical helper) -- must agree everywhere, not just at y=H/2.
    u_mean = analytical.mean_velocity_from_pressure_gradient(H, MU, DPDX)
    for y in (0.0, 0.1, 0.3, 0.5, 0.7, 0.9, 1.0):
        from_gradient = analytical.velocity_from_pressure_gradient(y, H, MU, DPDX)
        from_mean = analytical.velocity_from_mean_velocity(y, H, u_mean)
        assert from_gradient == pytest.approx(from_mean, abs=1e-12)


def test_velocity_from_mean_velocity_integrates_to_the_mean():
    import numpy as np

    u_mean = 2.0
    y = np.linspace(0.0, H, 2001)
    u = analytical.velocity_from_mean_velocity(y, H, u_mean)
    assert float(np.trapezoid(u, y)) / H == pytest.approx(u_mean, rel=1e-6)
