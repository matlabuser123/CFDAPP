import json
import shutil
from pathlib import Path

import pytest

from cfdapp.results import FIELDS_COLUMNS, RESIDUALS_COLUMNS, ResultLoadError, load_case_results


@pytest.fixture
def working_copy(tiny_results_dir: Path, tmp_path: Path) -> Path:
    """A mutable copy of the tiny fixture, so each test can corrupt
    exactly one file without touching the committed fixture."""
    target = tmp_path / "results"
    shutil.copytree(tiny_results_dir, target)
    return target


def test_load_case_results_valid_fixture(tiny_results_dir: Path):
    result = load_case_results(tiny_results_dir)
    assert result.case_name == "Tiny Synthetic Case"
    assert result.converged is True
    assert result.status == "Converged"
    assert result.nx == 2
    assert result.ny == 2
    assert result.fields is not None
    assert len(result.fields) == 4
    assert len(result.residuals) == 3
    assert list(result.fields.columns[: len(FIELDS_COLUMNS)]) == list(FIELDS_COLUMNS)
    assert list(result.residuals.columns) == list(RESIDUALS_COLUMNS)


def test_load_case_results_missing_directory():
    with pytest.raises(FileNotFoundError):
        load_case_results("this/directory/does/not/exist")


# --- P2-THERMAL-004: thermal_enabled / has_temperature --------------------


def test_nonthermal_fixture_reports_thermal_disabled(tiny_results_dir: Path):
    result = load_case_results(tiny_results_dir)
    assert result.thermal_enabled is False
    assert result.has_temperature is False


def test_thermal_fixture_reports_thermal_enabled(tiny_thermal_results_dir: Path):
    result = load_case_results(tiny_thermal_results_dir)
    assert result.thermal_enabled is True
    assert result.has_temperature is True
    assert "temperature" in result.fields.columns


def test_thermal_enabled_defaults_false_for_metadata_without_thermal_key(working_copy: Path):
    # An older results/ directory written before "thermal" existed in
    # metadata.json at all -- must not raise, must read as disabled.
    metadata_path = working_copy / "metadata.json"
    with metadata_path.open() as handle:
        metadata = json.load(handle)
    assert "thermal" not in metadata  # sanity: the base fixture predates this field.
    result = load_case_results(working_copy)
    assert result.thermal_enabled is False


def test_load_case_results_missing_metadata(working_copy: Path):
    (working_copy / "metadata.json").unlink()
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_missing_residuals(working_copy: Path):
    (working_copy / "residuals.csv").unlink()
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_missing_fields_is_allowed_when_metadata_says_non_finite(
    working_copy: Path,
):
    (working_copy / "fields.csv").unlink()
    metadata = json.loads((working_copy / "metadata.json").read_text())
    metadata["numerics"]["finite"] = False
    (working_copy / "metadata.json").write_text(json.dumps(metadata))

    result = load_case_results(working_copy)
    assert result.fields is None


def test_load_case_results_missing_fields_but_metadata_says_finite_is_an_error(working_copy: Path):
    (working_copy / "fields.csv").unlink()
    # metadata.json still says finite=true -- contradicts the missing file.
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_malformed_metadata_json(working_copy: Path):
    (working_copy / "metadata.json").write_text("{ not valid json")
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_missing_required_metadata_key(working_copy: Path):
    metadata = json.loads((working_copy / "metadata.json").read_text())
    del metadata["solver"]["status"]
    (working_copy / "metadata.json").write_text(json.dumps(metadata))
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_missing_fields_column(working_copy: Path):
    text = (working_copy / "fields.csv").read_text()
    text = text.replace("velocity_x,", "")  # drop a required column.
    (working_copy / "fields.csv").write_text(text)
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_missing_residuals_column(working_copy: Path):
    text = (working_copy / "residuals.csv").read_text()
    text = text.replace("continuity_residual,", "")
    (working_copy / "residuals.csv").write_text(text)
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_non_finite_field_value(working_copy: Path):
    lines = (working_copy / "fields.csv").read_text().splitlines()
    lines[1] = lines[1].replace("1.5", "nan", 1)
    (working_copy / "fields.csv").write_text("\n".join(lines) + "\n")
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_duplicate_cell_id(working_copy: Path):
    lines = (working_copy / "fields.csv").read_text().splitlines()
    lines[2] = lines[2].replace("1,0.75", "0,0.75", 1)  # duplicate cell_id 0.
    (working_copy / "fields.csv").write_text("\n".join(lines) + "\n")
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_non_contiguous_iteration_column(working_copy: Path):
    lines = (working_copy / "residuals.csv").read_text().splitlines()
    lines[2] = lines[2].replace("2,", "5,", 1)  # skip iteration 2.
    (working_copy / "residuals.csv").write_text("\n".join(lines) + "\n")
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)


def test_load_case_results_empty_residuals(working_copy: Path):
    header = (working_copy / "residuals.csv").read_text().splitlines()[0]
    (working_copy / "residuals.csv").write_text(header + "\n")
    with pytest.raises(ResultLoadError):
        load_case_results(working_copy)
