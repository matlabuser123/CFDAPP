import numpy as np
import pytest

from cfdapp.results import load_case_results
from cfdapp.validation.heated_cavity import (
    analytical_temperature,
    heated_cavity_grid_refinement,
    validate_heated_cavity_case,
    write_validation_json,
)


def test_analytical_temperature_matches_worked_example():
    # T(x) = Th + (Tc-Th)*x/L: Th=310, Tc=290, L=1 -> T(0.25)=305,
    # T(0.75)=295 (the same worked numbers this module's own fixture and
    # the C++ validation test both use).
    assert analytical_temperature(0.25, 310.0, 290.0, 1.0) == pytest.approx(305.0)
    assert analytical_temperature(0.75, 310.0, 290.0, 1.0) == pytest.approx(295.0)


def test_analytical_temperature_endpoints_are_wall_values():
    assert analytical_temperature(0.0, 310.0, 290.0, 1.0) == pytest.approx(310.0)
    assert analytical_temperature(1.0, 310.0, 290.0, 1.0) == pytest.approx(290.0)


def test_validate_heated_cavity_case_reports_zero_error_for_exact_fixture(
    tiny_thermal_results_dir,
):
    result = load_case_results(tiny_thermal_results_dir)
    assert result.thermal_enabled
    assert result.has_temperature

    summary = validate_heated_cavity_case(
        result, hot_temperature=310.0, cold_temperature=290.0, length=1.0
    )
    assert summary.temperature_l2 == pytest.approx(0.0, abs=1e-12)
    assert summary.temperature_linf == pytest.approx(0.0, abs=1e-12)
    assert summary.within_bounds
    assert summary.converged


def test_validate_heated_cavity_case_detects_nonzero_error(tiny_thermal_results_dir):
    result = load_case_results(tiny_thermal_results_dir)
    # A deliberately wrong hot/cold pair -- must NOT report zero error,
    # proving the comparison is real (not silently always passing).
    summary = validate_heated_cavity_case(
        result, hot_temperature=400.0, cold_temperature=200.0, length=1.0
    )
    assert summary.temperature_l2 > 1.0


def test_validate_heated_cavity_case_requires_temperature_column(tiny_results_dir):
    # tiny_results_dir (the plain, nonthermal fixture) has no temperature
    # column at all.
    result = load_case_results(tiny_results_dir)
    assert not result.thermal_enabled
    assert not result.has_temperature
    with pytest.raises(ValueError):
        validate_heated_cavity_case(
            result, hot_temperature=310.0, cold_temperature=290.0, length=1.0
        )


def test_heated_cavity_grid_refinement_table_shape():
    # Build summaries directly rather than through validate_* (no fixture
    # per grid) -- exercise the table-building/order-computation logic in
    # isolation with hand-picked l2 values.
    from cfdapp.validation.heated_cavity import HeatedCavityValidationSummary

    summaries = {
        (10, 10): HeatedCavityValidationSummary(
            case="c",
            length=1.0,
            hot_temperature=310.0,
            cold_temperature=290.0,
            temperature_l2=1e-6,
            temperature_linf=2e-6,
            temperature_mae=1e-6,
            within_bounds=True,
            converged=True,
        ),
        (20, 20): HeatedCavityValidationSummary(
            case="c",
            length=1.0,
            hot_temperature=310.0,
            cold_temperature=290.0,
            temperature_l2=5e-7,
            temperature_linf=1e-6,
            temperature_mae=5e-7,
            within_bounds=True,
            converged=True,
        ),
    }
    table = heated_cavity_grid_refinement(summaries)
    assert list(table["nx"]) == [10, 20]
    assert np.isnan(table["observed_order"].iloc[0])
    assert table["observed_order"].iloc[1] == pytest.approx(1.0, rel=0.05)


def test_write_validation_json_roundtrips(tmp_path, tiny_thermal_results_dir):
    result = load_case_results(tiny_thermal_results_dir)
    summary = validate_heated_cavity_case(
        result, hot_temperature=310.0, cold_temperature=290.0, length=1.0
    )
    path = write_validation_json(summary, tmp_path / "validation.json")
    assert path.is_file()
    import json

    with path.open() as handle:
        data = json.load(handle)
    assert data["case"] == summary.case
    assert data["temperature_l2"] == pytest.approx(summary.temperature_l2)
