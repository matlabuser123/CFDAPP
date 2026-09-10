import pandas as pd

from cfdapp.plotting.contours import (
    plot_temperature_contour,
    plot_u_velocity_contour,
    plot_v_velocity_contour,
    plot_velocity_magnitude_contour,
)
from cfdapp.plotting.pressure import plot_pressure_contour
from cfdapp.plotting.residuals import plot_residual_history
from cfdapp.plotting.velocity import plot_velocity_profile, plot_velocity_vectors
from cfdapp.results import load_case_results

# TODO.md "P1 -- Python Tooling" section 47: never pixel-compare
# matplotlib images -- only that the function completes, the output file
# exists and is non-empty, and the input data was not mutated.


def _assert_nonempty_file(path):
    assert path.is_file()
    assert path.stat().st_size > 0


def test_plot_residual_history_writes_nonempty_file(tiny_results_dir, tmp_path):
    result = load_case_results(tiny_results_dir)
    residuals_before = result.residuals.copy()

    output = plot_residual_history(result, tmp_path / "residual_history.png")
    _assert_nonempty_file(output)
    pd.testing.assert_frame_equal(result.residuals, residuals_before)


def test_plot_residual_history_with_tolerances_still_completes(tiny_results_dir, tmp_path):
    result = load_case_results(tiny_results_dir)
    output = plot_residual_history(
        result,
        tmp_path / "residual_history.png",
        tolerances={"u": 1e-6, "v": 1e-6, "p": 1e-6, "continuity": 1e-8},
    )
    _assert_nonempty_file(output)


def test_plot_velocity_profile_writes_nonempty_file(tmp_path):
    coordinate = [0.0, 0.5, 1.0]
    numerical = [0.0, 1.0, 0.0]
    output = plot_velocity_profile(
        coordinate,
        numerical,
        reference_coordinate=[0.0, 0.5, 1.0],
        reference_value=[0.0, 0.95, 0.0],
        coordinate_label="y",
        value_label="u",
        title="test",
        output_path=tmp_path / "profile.png",
    )
    _assert_nonempty_file(output)


def test_plot_velocity_profile_without_reference_still_completes(tmp_path):
    output = plot_velocity_profile(
        [0.0, 1.0],
        [0.0, 1.0],
        coordinate_label="x",
        value_label="u",
        title="t",
        output_path=tmp_path / "p.png",
    )
    _assert_nonempty_file(output)


def test_plot_velocity_vectors_writes_nonempty_file(tiny_results_dir, tmp_path):
    result = load_case_results(tiny_results_dir)
    fields_before = result.fields.copy()

    output = plot_velocity_vectors(result.fields, tmp_path / "vectors.png", subsample=1)
    _assert_nonempty_file(output)
    pd.testing.assert_frame_equal(result.fields, fields_before)


def test_plot_velocity_vectors_rejects_invalid_subsample(tiny_results_dir, tmp_path):
    result = load_case_results(tiny_results_dir)
    try:
        plot_velocity_vectors(result.fields, tmp_path / "vectors.png", subsample=0)
        raised = False
    except ValueError:
        raised = True
    assert raised


def test_plot_pressure_contour_writes_nonempty_file(tiny_results_dir, tmp_path):
    result = load_case_results(tiny_results_dir)
    fields_before = result.fields.copy()

    output = plot_pressure_contour(result.fields, tmp_path / "pressure.png")
    _assert_nonempty_file(output)
    pd.testing.assert_frame_equal(result.fields, fields_before)


def test_contour_plots_write_nonempty_files(tiny_results_dir, tmp_path):
    result = load_case_results(tiny_results_dir)
    outputs = [
        plot_velocity_magnitude_contour(result.fields, tmp_path / "mag.png"),
        plot_u_velocity_contour(result.fields, tmp_path / "u.png"),
        plot_v_velocity_contour(result.fields, tmp_path / "v.png"),
    ]
    for output in outputs:
        _assert_nonempty_file(output)


def test_plot_temperature_contour_writes_nonempty_file(tiny_thermal_results_dir, tmp_path):
    result = load_case_results(tiny_thermal_results_dir)
    assert result.has_temperature
    fields_before = result.fields.copy()

    output = plot_temperature_contour(result.fields, tmp_path / "temperature.png")
    _assert_nonempty_file(output)
    pd.testing.assert_frame_equal(result.fields, fields_before)
