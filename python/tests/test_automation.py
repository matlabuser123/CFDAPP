import shutil
from pathlib import Path

import pytest

from cfdapp.automation.benchmark import (
    BenchmarkRun,
    compute_fingerprint,
    summarize_runtime,
    write_benchmark_csv,
)
from cfdapp.automation.runner import RunResult, run_case

# The actual compiled CLI, if this checkout has been built (CMakeLists.txt
# puts it at build/debug/apps/cli/cfdapp.exe -- see apps/cli/CMakeLists.txt).
# One real subprocess test runs against it when present; every other test
# in this module is a pure unit test with no C++ dependency (section 42).
REPO_ROOT = Path(__file__).resolve().parents[2]
CFDAPP_EXE = REPO_ROOT / "build" / "debug" / "apps" / "cli" / "cfdapp.exe"
VALID_CASE = REPO_ROOT / "tests" / "data" / "cases" / "valid_cavity"


# --- RunResult / run_case outcome mapping (pure unit tests) ----------------


@pytest.mark.parametrize(
    "exit_code,expected_outcome,expected_converged",
    [
        (0, "converged", True),
        (1, "application_error", False),
        (2, "invalid_case", False),
        (3, "did_not_converge", False),
        (4, "numerical_failure", False),
        (99, "unknown_exit_code", False),
    ],
)
def test_run_result_outcome_mapping(exit_code, expected_outcome, expected_converged):
    result = RunResult(
        case_path=Path("some/case"), exit_code=exit_code, stdout="", stderr="", runtime_seconds=1.0
    )
    assert result.outcome == expected_outcome
    assert result.converged is expected_converged


@pytest.mark.skipif(not CFDAPP_EXE.is_file(), reason="cfdapp CLI not built")
def test_run_case_against_real_binary_and_real_case():
    result = run_case(CFDAPP_EXE, VALID_CASE)
    assert result.exit_code == 0
    assert result.outcome == "converged"
    assert "Converged: yes" in result.stdout
    assert result.runtime_seconds > 0.0


# --- benchmark.py ------------------------------------------------------------


def test_compute_fingerprint_is_deterministic(tiny_results_dir):
    a = compute_fingerprint(tiny_results_dir)
    b = compute_fingerprint(tiny_results_dir)
    assert a == b
    assert a is not None
    assert len(a) == 64  # SHA-256 hex digest length.


def test_compute_fingerprint_changes_when_fields_change(tiny_results_dir, tmp_path):
    working = tmp_path / "results"
    shutil.copytree(tiny_results_dir, working)
    original = compute_fingerprint(working)

    text = (working / "fields.csv").read_text().replace("1.5", "1.6", 1)
    (working / "fields.csv").write_text(text)
    changed = compute_fingerprint(working)
    assert changed != original


def test_compute_fingerprint_is_none_without_fields_csv(tiny_results_dir, tmp_path):
    working = tmp_path / "results"
    shutil.copytree(tiny_results_dir, working)
    (working / "fields.csv").unlink()
    assert compute_fingerprint(working) is None


def _run(runtime, converged=True, finite=True, mass_imbalance=1e-10) -> BenchmarkRun:
    return BenchmarkRun(
        case="test",
        grid="4x4",
        run_index=0,
        runtime_seconds=runtime,
        iterations=100,
        converged=converged,
        finite=finite,
        u_residual=1e-8,
        v_residual=1e-8,
        p_residual=1e-8,
        continuity_residual=1e-10,
        global_mass_imbalance=mass_imbalance,
        fingerprint="deadbeef",
    )


def test_benchmark_run_valid_for_performance_requires_converged_finite_and_conserved():
    assert _run(1.0).valid_for_performance is True
    assert _run(1.0, converged=False).valid_for_performance is False
    assert _run(1.0, finite=False).valid_for_performance is False
    assert _run(1.0, mass_imbalance=1.0).valid_for_performance is False


def test_summarize_runtime_excludes_invalid_runs():
    # TODO.md "P1 -- Python Tooling" section 37: a fast but failed run
    # must never count as a valid performance measurement.
    runs = [_run(1.0), _run(2.0), _run(3.0), _run(0.001, converged=False)]
    stats = summarize_runtime(runs)
    assert stats.valid_run_count == 3
    assert stats.total_run_count == 4
    assert stats.minimum == pytest.approx(1.0)
    assert stats.median == pytest.approx(2.0)
    assert stats.mean == pytest.approx(2.0)
    assert stats.maximum == pytest.approx(3.0)


def test_summarize_runtime_raises_when_no_valid_runs():
    with pytest.raises(ValueError):
        summarize_runtime([_run(1.0, converged=False)])


def test_write_benchmark_csv_contents(tmp_path):
    runs = [_run(1.0), _run(2.0, converged=False)]
    path = write_benchmark_csv(runs, tmp_path / "results.csv")
    assert path.is_file()
    text = path.read_text()
    assert "case,grid,run,runtime_seconds,iterations,converged" in text
    assert text.count("\n") == 3  # header + 2 rows (+ trailing newline).
