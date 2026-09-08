import numpy as np
import pandas as pd
import pytest

from cfdapp.results import ResultData
from cfdapp.validation import analytical
from cfdapp.validation.poiseuille import (
    fit_pressure_gradient,
    poiseuille_grid_refinement,
    validate_poiseuille_case,
)


def _pressure_fields(offset: float, slope: float) -> pd.DataFrame:
    xs = [0.0, 1.0, 2.0, 3.0]
    rows = []
    for x in xs:
        for y in (0.0, 1.0):  # pressure uniform in y -- only x matters.
            rows.append({"x": x, "y": y, "pressure": offset + slope * x})
    return pd.DataFrame(rows)


def test_fit_pressure_gradient_matches_worked_example():
    # TODO.md "P1 -- Python Tooling" section 46: p(x) = 5 - 0.08x.
    fields = _pressure_fields(offset=5.0, slope=-0.08)
    slope = fit_pressure_gradient(fields, x_min=0.0, x_max=3.0)
    assert slope == pytest.approx(-0.08)


def test_fit_pressure_gradient_is_independent_of_offset():
    # Same slope, a very different constant offset (section 46: "require
    # the same slope" -- pressure gauge freedom must not matter).
    fields = _pressure_fields(offset=105.0, slope=-0.08)
    slope = fit_pressure_gradient(fields, x_min=0.0, x_max=3.0)
    assert slope == pytest.approx(-0.08)


def test_fit_pressure_gradient_rejects_empty_window():
    fields = _pressure_fields(offset=5.0, slope=-0.08)
    with pytest.raises(ValueError):
        fit_pressure_gradient(fields, x_min=100.0, x_max=200.0)


def test_fit_pressure_gradient_rejects_single_column():
    fields = _pressure_fields(offset=5.0, slope=-0.08)
    with pytest.raises(ValueError):
        fit_pressure_gradient(fields, x_min=0.0, x_max=0.0)


def _exact_poiseuille_result(
    channel_height: float, mean_velocity: float, dynamic_viscosity: float, channel_length: float
) -> ResultData:
    """A ResultData whose fields are *exactly* the analytical solution --
    so a validation run against it should report ~zero error, proving the
    pipeline doesn't introduce error of its own.
    """
    nx, ny = 8, 4
    dx, dy = channel_length / nx, channel_height / ny
    dpdx = analytical.pressure_gradient_from_mean_velocity(
        channel_height, dynamic_viscosity, mean_velocity
    )

    rows = []
    for j in range(ny):
        y = (j + 0.5) * dy
        u = analytical.velocity_from_mean_velocity(y, channel_height, mean_velocity)
        for i in range(nx):
            x = (i + 0.5) * dx
            rows.append(
                {
                    "cell_id": j * nx + i,
                    "x": x,
                    "y": y,
                    "velocity_x": u,
                    "velocity_y": 0.0,
                    "velocity_magnitude": abs(u),
                    "pressure": dpdx * x,  # p(0)=0 gauge, exact analytical gradient.
                }
            )
    fields = pd.DataFrame(rows)
    metadata = {
        "case": {"name": "Exact Poiseuille"},
        "mesh": {"cells": nx * ny, "faces": 0, "nx": nx, "ny": ny},
        "physics": {"density": 1.0, "dynamic_viscosity": dynamic_viscosity},
        "solver": {"type": "SIMPLE", "converged": True, "status": "Converged", "iterations": 1},
        "residuals": {"u": 1e-8, "v": 1e-8, "p": 1e-8, "continuity": 1e-10},
        "conservation": {"global_mass_imbalance": 1e-10},
        "numerics": {"finite": True},
    }
    residuals = pd.DataFrame(
        {
            "iteration": [1],
            "u_residual": [1e-8],
            "v_residual": [1e-8],
            "p_residual": [1e-8],
            "continuity_residual": [1e-10],
            "global_mass_imbalance": [1e-10],
        }
    )
    return ResultData(fields=fields, residuals=residuals, metadata=metadata, source_dir=None)


def test_validate_poiseuille_case_against_exact_analytical_fields():
    result = _exact_poiseuille_result(
        channel_height=1.0, mean_velocity=1.0, dynamic_viscosity=0.01, channel_length=4.0
    )
    summary = validate_poiseuille_case(
        result,
        mean_velocity=1.0,
        dynamic_viscosity=0.01,
        channel_height=1.0,
        profile_x=2.0,
        pressure_fit_x_range=(0.5, 3.5),
    )
    assert summary.velocity_l2 == pytest.approx(0.0, abs=1e-10)
    assert summary.u_max_relative_error == pytest.approx(0.0, abs=1e-8)
    assert summary.pressure_gradient_relative_error == pytest.approx(0.0, abs=1e-8)
    assert summary.mass_conservation_pass is True
    assert summary.converged is True


def test_validate_poiseuille_case_requires_fields():
    metadata = {"conservation": {"global_mass_imbalance": 0.0}}
    result = ResultData(fields=None, residuals=pd.DataFrame(), metadata=metadata, source_dir=None)
    with pytest.raises(ValueError):
        validate_poiseuille_case(
            result,
            mean_velocity=1.0,
            dynamic_viscosity=0.01,
            channel_height=1.0,
            profile_x=2.0,
            pressure_fit_x_range=(0.5, 3.5),
        )


def test_poiseuille_grid_refinement_reports_observed_order_and_sorts_by_ny():
    summaries = {}
    for ny in (4, 8, 16):
        result = _exact_poiseuille_result(
            channel_height=1.0, mean_velocity=1.0, dynamic_viscosity=0.01, channel_length=4.0
        )
        # Perturb velocity_x by an error that halves with each refinement
        # (an artificial, controlled 1st-order trend) so observed_order
        # is checkable against a known value rather than the exact-field
        # case above, which would divide by ~0.
        result.fields["velocity_x"] += 0.1 / ny
        summary = validate_poiseuille_case(
            result,
            mean_velocity=1.0,
            dynamic_viscosity=0.01,
            channel_height=1.0,
            profile_x=2.0,
            pressure_fit_x_range=(0.5, 3.5),
        )
        summaries[(8, ny)] = summary

    table = poiseuille_grid_refinement(summaries)
    assert list(table["ny"]) == [4, 8, 16]
    assert np.isnan(table["observed_order"].iloc[0])
    # h halves each step and the injected error also halves -> observed
    # order ~1.
    assert table["observed_order"].iloc[1] == pytest.approx(1.0, abs=0.05)
    assert table["observed_order"].iloc[2] == pytest.approx(1.0, abs=0.05)
