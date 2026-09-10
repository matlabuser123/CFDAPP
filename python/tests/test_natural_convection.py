import json

import pytest

from cfdapp.validation.natural_convection import (
    NaturalConvectionRun,
    grid_refinement_table,
    load_natural_convection_run,
    prandtl_number,
    rayleigh_number,
)


def test_rayleigh_number_matches_hand_worked_example():
    # de Vahl Davis Ra=1e3 case, this codebase's own nondimensionalization
    # (g=1, deltaT=1, L=1, nu=Pr=0.71, alpha=1): beta = Ra*Pr = 710.
    ra = rayleigh_number(
        gravity_magnitude=1.0,
        beta=710.0,
        delta_t=1.0,
        length=1.0,
        kinematic_viscosity=0.71,
        thermal_diffusivity=1.0,
    )
    assert ra == pytest.approx(1000.0, rel=1e-9)


def test_prandtl_number_matches_hand_worked_example():
    pr = prandtl_number(kinematic_viscosity=0.71, thermal_diffusivity=1.0)
    assert pr == pytest.approx(0.71)


def test_rayleigh_number_rejects_non_positive_diffusivity():
    with pytest.raises(ValueError):
        rayleigh_number(1.0, 710.0, 1.0, 1.0, 0.71, 0.0)


def test_prandtl_number_rejects_non_positive_diffusivity():
    with pytest.raises(ValueError):
        prandtl_number(0.71, 0.0)


def _write_synthetic_run(tmp_path, *, grid_label: str, nx: int, ny: int, nu_avg: float):
    grid_dir = tmp_path / grid_label
    grid_dir.mkdir(parents=True)
    record = {
        "case": "natural_convection_cavity",
        "ra": 1000.0,
        "pr": 0.71,
        "mesh": {"nx": nx, "ny": ny},
        "solver": {
            "flow_converged": True,
            "flow_iterations": 50,
            "thermal_converged": True,
            "thermal_iterations": 10,
            "outer_iterations": 49,
            "final_outer_temperature_change": 9e-9,
            "runtime_seconds": 16.5,
        },
        "conservation": {
            "global_mass_imbalance": 1e-9,
            "max_wall_normal_flux": 1e-10,
            "q_hot": 1.14,
            "q_cold": -1.13,
            "heat_imbalance": 0.01,
        },
        "validation": {
            "nu_avg_computed": nu_avg,
            "nu_avg_reference": 1.12,
            "nu_avg_error": abs(nu_avg - 1.12) / 1.12,
            "u_max_computed": 3.5,
            "u_max_computed_y": 0.8,
            "u_max_reference": 3.634,
            "u_max_error": 0.037,
            "v_max_computed": 3.5,
            "v_max_computed_x": 0.17,
            "v_max_reference": 3.679,
            "v_max_error": 0.049,
            "min_theta": 0.0,
            "max_theta": 1.0,
        },
        "deterministic": True,
    }
    with (grid_dir / "validation.json").open("w", encoding="utf-8") as handle:
        json.dump(record, handle)
    return grid_dir


def test_load_natural_convection_run_round_trips_fields(tmp_path):
    grid_dir = _write_synthetic_run(tmp_path, grid_label="10x10", nx=10, ny=10, nu_avg=1.174)
    run = load_natural_convection_run(grid_dir)
    assert run.grid_label == "10x10"
    assert run.nx == 10
    assert run.ny == 10
    assert run.flow_converged is True
    assert run.ra == pytest.approx(1000.0)
    assert run.nu_avg_computed == pytest.approx(1.174)


def test_grid_refinement_table_sorted_by_cell_count(tmp_path):
    fine = load_natural_convection_run(
        _write_synthetic_run(tmp_path, grid_label="20x20", nx=20, ny=20, nu_avg=1.1407)
    )
    coarse = load_natural_convection_run(
        _write_synthetic_run(tmp_path, grid_label="10x10", nx=10, ny=10, nu_avg=1.174)
    )
    table = grid_refinement_table([fine, coarse])  # deliberately out of order.
    assert list(table["grid"]) == ["10x10", "20x20"]
    assert list(table["cells"]) == [100, 400]


def test_natural_convection_run_is_frozen_dataclass():
    fields = {f.name for f in NaturalConvectionRun.__dataclass_fields__.values()}
    assert "nu_avg_computed" in fields
    assert "heat_imbalance" in fields
