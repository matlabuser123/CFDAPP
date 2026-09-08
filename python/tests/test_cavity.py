import json

import pandas as pd
import pytest

from cfdapp.results import ResultData
from cfdapp.validation.cavity import (
    DEFAULT_GHIA_DIR,
    CavityValidationSummary,
    GhiaReference,
    cavity_grid_refinement,
    load_ghia_reference,
    validate_cavity_case,
    write_cavity_validation_json,
)


def test_load_ghia_reference_loads_the_real_published_data():
    ghia = load_ghia_reference(DEFAULT_GHIA_DIR)
    assert len(ghia.u) == 17
    assert len(ghia.v) == 17
    assert list(ghia.u.columns) == ["y", "u"]
    assert list(ghia.v.columns) == ["x", "v"]
    # Table I: u=1 at the lid (y=1), u=0 at the bottom wall (y=0).
    assert ghia.u.iloc[-1]["u"] == pytest.approx(1.0)
    assert ghia.u.iloc[0]["u"] == pytest.approx(0.0)


def test_load_ghia_reference_missing_directory_raises():
    with pytest.raises(FileNotFoundError):
        load_ghia_reference("this/directory/does/not/exist")


def _make_result(fields: pd.DataFrame, metadata: dict) -> ResultData:
    residuals = pd.DataFrame(
        {
            "iteration": [1],
            "u_residual": [1e-7],
            "v_residual": [1e-7],
            "p_residual": [1e-7],
            "continuity_residual": [1e-10],
            "global_mass_imbalance": [1e-10],
        }
    )
    return ResultData(fields=fields, residuals=residuals, metadata=metadata, source_dir=None)


def test_validate_cavity_case_zero_error_when_numerical_exactly_matches_reference():
    # A structured 2x2 mesh whose u(x=0.5, y) profile is exactly linear
    # (u = y), so interpolating to a synthetic 2-point Ghia reference at
    # the same line reproduces it exactly -> l2/linf/mae all exactly 0.
    fields = pd.DataFrame(
        {
            "cell_id": [0, 1, 2, 3],
            "x": [0.25, 0.75, 0.25, 0.75],
            "y": [0.25, 0.25, 0.75, 0.75],
            "velocity_x": [0.25, 0.25, 0.75, 0.75],
            "velocity_y": [0.0, 0.0, 0.0, 0.0],
            "velocity_magnitude": [0.25, 0.25, 0.75, 0.75],
            "pressure": [0.0, 0.0, 0.0, 0.0],
        }
    )
    metadata = {
        "case": {"name": "Synthetic"},
        "mesh": {"cells": 4, "faces": 12, "nx": 2, "ny": 2},
        "physics": {"density": 1.0, "dynamic_viscosity": 0.1},
        "solver": {"type": "SIMPLE", "converged": True, "status": "Converged", "iterations": 1},
        "residuals": {"u": 1e-7, "v": 1e-7, "p": 1e-7, "continuity": 1e-10},
        "conservation": {"global_mass_imbalance": 1e-10},
        "numerics": {"finite": True},
    }
    result = _make_result(fields, metadata)
    ghia = GhiaReference(
        u=pd.DataFrame({"y": [0.25, 0.75], "u": [0.25, 0.75]}),
        v=pd.DataFrame({"x": [0.25, 0.75], "v": [0.0, 0.0]}),
    )

    summary, u_centerline, v_centerline = validate_cavity_case(result, ghia, lid_velocity=1.0)
    assert summary.u_l2 == pytest.approx(0.0, abs=1e-12)
    assert summary.u_linf == pytest.approx(0.0, abs=1e-12)
    assert summary.v_l2 == pytest.approx(0.0, abs=1e-12)
    assert list(u_centerline.columns) == ["y", "u_numerical", "u_reference", "error"]
    assert list(v_centerline.columns) == ["x", "v_numerical", "v_reference", "error"]


def test_validate_cavity_case_requires_fields():
    metadata = {"conservation": {"global_mass_imbalance": 0.0}}
    result = ResultData(fields=None, residuals=pd.DataFrame(), metadata=metadata, source_dir=None)
    ghia = GhiaReference(
        u=pd.DataFrame({"y": [0.0], "u": [0.0]}), v=pd.DataFrame({"x": [0.0], "v": [0.0]})
    )
    with pytest.raises(ValueError):
        validate_cavity_case(result, ghia)


def _summary(nx: int, ny: int, u_l2: float, v_l2: float) -> CavityValidationSummary:
    return CavityValidationSummary(
        case="Test",
        nx=nx,
        ny=ny,
        reynolds_number=100.0,
        converged=True,
        global_mass_imbalance=0.0,
        u_l2=u_l2,
        u_linf=u_l2 * 2,
        u_mae=u_l2 * 0.5,
        v_l2=v_l2,
        v_linf=v_l2 * 2,
        v_mae=v_l2 * 0.5,
    )


def test_cavity_grid_refinement_passes_when_error_decreases_monotonically():
    summaries = {
        (20, 20): _summary(20, 20, u_l2=0.02, v_l2=0.014),
        (40, 40): _summary(40, 40, u_l2=0.01, v_l2=0.006),
        (80, 80): _summary(80, 80, u_l2=0.004, v_l2=0.0035),
    }
    table, passed = cavity_grid_refinement(summaries)
    assert passed is True
    assert list(table["nx"]) == [20, 40, 80]


def test_cavity_grid_refinement_fails_when_error_increases():
    summaries = {
        (20, 20): _summary(20, 20, u_l2=0.01, v_l2=0.01),
        (40, 40): _summary(40, 40, u_l2=0.02, v_l2=0.005),  # u_l2 got worse.
    }
    _table, passed = cavity_grid_refinement(summaries)
    assert passed is False


def test_write_cavity_validation_json_matches_section_22_shape(tmp_path):
    summaries = {
        (20, 20): _summary(20, 20, u_l2=0.0211, v_l2=0.0145),
        (40, 40): _summary(40, 40, u_l2=0.0102, v_l2=0.00645),
    }
    path = write_cavity_validation_json(summaries, tmp_path / "cavity_validation.json")
    doc = json.loads(path.read_text())
    assert doc["case"] == "Test"
    assert doc["reynolds_number"] == 100.0
    assert doc["grids"]["20x20"] == {"u_l2": 0.0211, "v_l2": 0.0145}
    assert doc["grids"]["40x40"] == {"u_l2": 0.0102, "v_l2": 0.00645}
    assert doc["refinement_pass"] is True
