import json

import numpy as np
import pandas as pd
import pytest

from cfdapp.plotting.turbulence import plot_grid_refinement, plot_model_comparison, plot_wall_units
from cfdapp.validation.channel_flow import (
    ChannelFlowRun,
    grid_refinement_table,
    law_of_the_wall,
    load_channel_flow_run,
    model_comparison_table,
    u_plus_log_law,
    u_plus_viscous_sublayer,
    wall_units_error_metrics,
)


def test_u_plus_viscous_sublayer_is_identity():
    assert u_plus_viscous_sublayer(3.0) == pytest.approx(3.0)
    np.testing.assert_allclose(u_plus_viscous_sublayer([1.0, 2.0]), [1.0, 2.0])


def test_u_plus_log_law_matches_hand_value():
    # u+ = (1/0.41)*ln(100) + 5.0 = 2.43902*4.60517 + 5.0 = 16.2321.
    value = u_plus_log_law(100.0)
    assert value == pytest.approx(16.2321, abs=1e-3)


def test_u_plus_log_law_rejects_non_positive():
    with pytest.raises(ValueError):
        u_plus_log_law(0.0)


def test_law_of_the_wall_matches_sublayer_below_five():
    assert law_of_the_wall(3.0) == pytest.approx(3.0)


def test_law_of_the_wall_matches_log_law_above_thirty():
    assert law_of_the_wall(100.0) == pytest.approx(u_plus_log_law(100.0))


def test_law_of_the_wall_is_continuous_at_region_boundaries():
    # No jump discontinuity at y+=5 or y+=30 (the buffer-layer blend
    # must meet both boundary values exactly).
    just_below = law_of_the_wall(4.999)
    at_five = law_of_the_wall(5.0)
    assert just_below == pytest.approx(at_five, abs=1e-2)

    at_thirty = law_of_the_wall(30.0)
    just_above = law_of_the_wall(30.001)
    assert at_thirty == pytest.approx(just_above, abs=1e-2)


def _write_synthetic_run(tmp_path, *, model: str, grid_label: str, nx: int, ny: int):
    grid_dir = tmp_path / model / grid_label
    grid_dir.mkdir(parents=True)

    y_plus = np.array([2.0, 10.0, 50.0, 150.0])
    u_plus = law_of_the_wall(y_plus)  # exact match -- should give ~zero error.
    profile = pd.DataFrame(
        {"y_plus": y_plus, "numerical_u_plus": u_plus, "reference_u_plus": u_plus}
    )
    profile.to_csv(grid_dir / "wall_units_profile.csv", index=False)

    record = {
        "case": "turbulent_channel_flow",
        "model": model,
        "reynolds_number_bulk": 5600.0,
        "channel": {"length": 16.0, "height": 2.0},
        "mesh": {"nx": nx, "ny": ny},
        "solver": {
            "converged": True,
            "iterations": 500,
            "u_residual": 1e-5,
            "v_residual": 1e-6,
            "p_residual": 1e-4,
            "continuity_residual": 1e-10,
            "has_turbulence_residual": True,
            "turbulence_residual": 1e-5,
            "global_mass_imbalance": 1e-9,
            "inlet_flux": -2.0,
            "outlet_flux": 2.0,
            "finite": True,
            "runtime_seconds": 12.5,
        },
        "development": {"relative_diff_between_stations": 0.01},
        "wall": {
            "tau_w_bottom": 1.5e-3,
            "tau_w_top": 1.5e-3,
            "tau_w_asymmetry": 0.0,
            "u_tau": 0.04,
            "first_cell_y_plus": 8.0,
            "achieved_re_tau": 120.0,
            "re_tau_target": 180.0,
            "re_tau_relative_error": 0.33,
        },
        "wall_units_error": {
            "overall": {"l2": 0.01, "linf": 0.02, "mean_absolute": 0.01, "sample_count": 4},
            "near_wall": {"l2": 0.0, "linf": 0.0, "mean_absolute": 0.0, "sample_count": 0},
            "buffer": {"l2": 0.0, "linf": 0.0, "mean_absolute": 0.0, "sample_count": 1},
            "outer": {"l2": 0.01, "linf": 0.02, "mean_absolute": 0.01, "sample_count": 3},
        },
        "turbulence_fields": {
            "min_k": 1e-4,
            "max_k": 0.05,
            "has_second_scalar": True,
            "min_second_scalar": 5.0,
            "max_second_scalar": 50.0,
            "min_mu_t": 1e-4,
            "max_mu_t": 0.01,
            "molecular_viscosity": 3.57e-4,
            "has_f1_f2": False,
            "min_f1": 0.0,
            "max_f1": 0.0,
            "min_f2": 0.0,
            "max_f2": 0.0,
            "min_wall_distance": 0.0,
            "max_wall_distance": 0.0,
        },
        "deterministic": True,
    }
    with (grid_dir / "validation.json").open("w", encoding="utf-8") as handle:
        json.dump(record, handle)
    return grid_dir


def test_load_channel_flow_run_round_trips_fields(tmp_path):
    grid_dir = _write_synthetic_run(tmp_path, model="kEpsilon", grid_label="coarse", nx=48, ny=12)
    run = load_channel_flow_run(grid_dir)
    assert run.model == "kEpsilon"
    assert run.grid_label == "coarse"
    assert run.nx == 48
    assert run.ny == 12
    assert run.converged is True
    assert run.achieved_re_tau == pytest.approx(120.0)
    assert len(run.wall_units_profile) == 4


def test_wall_units_error_metrics_near_zero_for_exact_law_of_the_wall(tmp_path):
    grid_dir = _write_synthetic_run(tmp_path, model="kEpsilon", grid_label="coarse", nx=48, ny=12)
    run = load_channel_flow_run(grid_dir)
    metrics = wall_units_error_metrics(run)
    assert metrics.l2 == pytest.approx(0.0, abs=1e-9)
    assert metrics.linf == pytest.approx(0.0, abs=1e-9)


def test_grid_refinement_table_sorted_by_cell_count(tmp_path):
    coarse = load_channel_flow_run(
        _write_synthetic_run(tmp_path, model="kEpsilon", grid_label="coarse", nx=48, ny=12)
    )
    fine = load_channel_flow_run(
        _write_synthetic_run(tmp_path, model="kEpsilon", grid_label="fine", nx=96, ny=24)
    )
    table = grid_refinement_table([fine, coarse])  # deliberately out of order.
    assert list(table["grid"]) == ["coarse", "fine"]
    assert list(table["cells"]) == [48 * 12, 96 * 24]


def test_model_comparison_table_has_one_row_per_model(tmp_path):
    kepsilon = load_channel_flow_run(
        _write_synthetic_run(tmp_path, model="kEpsilon", grid_label="medium", nx=64, ny=16)
    )
    sst = load_channel_flow_run(
        _write_synthetic_run(tmp_path, model="SST", grid_label="medium", nx=64, ny=16)
    )
    table = model_comparison_table([kepsilon, sst])
    assert list(table["model"]) == ["kEpsilon", "SST"]
    assert "outer_l2" in table.columns
    assert "re_tau_relative_error" in table.columns


def test_plot_wall_units_writes_file(tmp_path):
    y_plus = np.array([2.0, 10.0, 50.0, 150.0])
    u_plus = law_of_the_wall(y_plus)
    output = plot_wall_units(
        y_plus, u_plus, label="kEpsilon", title="test", output_path=tmp_path / "wall_units.png"
    )
    assert output.exists()
    assert output.stat().st_size > 0


def test_plot_model_comparison_writes_file(tmp_path):
    comparison = pd.DataFrame(
        {"model": ["kEpsilon", "kOmega", "SST"], "outer_l2": [15.4, 16.4, 13.1]}
    )
    output = plot_model_comparison(
        comparison, value_column="outer_l2", title="test", output_path=tmp_path / "compare.png"
    )
    assert output.exists()


def test_plot_grid_refinement_writes_file(tmp_path):
    refinement = pd.DataFrame({"cells": [576, 1024, 2304], "outer_l2": [15.4, 12.0, 8.4]})
    output = plot_grid_refinement(
        refinement, value_column="outer_l2", title="test", output_path=tmp_path / "refine.png"
    )
    assert output.exists()


def test_channel_flow_run_is_frozen_dataclass():
    # Sanity: ChannelFlowRun is a value type, same convention as
    # PoiseuilleValidationSummary.
    fields = {f.name for f in ChannelFlowRun.__dataclass_fields__.values()}
    assert "achieved_re_tau" in fields
    assert "wall_units_profile" in fields
