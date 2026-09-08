"""Benchmark runner: repeated timed runs of fixed cases, with
correctness gated separately from performance (TODO.md "P1 -- Python
Tooling" sections 34-41). Measurement infrastructure only -- never tunes
solver settings (section 34).
"""

from __future__ import annotations

import hashlib
import statistics
from dataclasses import dataclass
from pathlib import Path

import pandas as pd

from cfdapp.automation.runner import RunResult, run_case
from cfdapp.results import load_case_results


def compute_fingerprint(results_dir: str | Path) -> str | None:
    """SHA-256 over fields.csv + residuals.csv's bytes, in that fixed
    order (section 41: "same numerical solution?" independent of
    runtime). Returns None if fields.csv does not exist (a non-finite
    solve, section 29) -- residuals.csv alone is not treated as a full
    numerical fingerprint since it says nothing about the final field
    values.
    """
    results_dir = Path(results_dir)
    fields_path = results_dir / "fields.csv"
    residuals_path = results_dir / "residuals.csv"
    if not fields_path.is_file():
        return None

    digest = hashlib.sha256()
    digest.update(fields_path.read_bytes())
    digest.update(residuals_path.read_bytes())
    return digest.hexdigest()


@dataclass(frozen=True)
class BenchmarkRun:
    case: str
    grid: str
    run_index: int
    runtime_seconds: float
    iterations: int
    converged: bool
    finite: bool
    u_residual: float
    v_residual: float
    p_residual: float
    continuity_residual: float
    global_mass_imbalance: float
    fingerprint: str | None

    @property
    def valid_for_performance(self) -> bool:
        """A benchmark run is only a valid *performance* measurement if
        it is also correct (section 37): converged, finite, and mass is
        conserved. A fast run that failed numerically must never count.
        """
        return self.converged and self.finite and abs(self.global_mass_imbalance) < 1e-6


def benchmark_case(
    executable: str | Path,
    case_path: str | Path,
    *,
    case_label: str,
    grid_label: str,
    results_subdir: str = "results",
    warmup_runs: int = 1,
    measured_runs: int = 3,
) -> list[BenchmarkRun]:
    """Runs `case_path` `warmup_runs` times (discarded) then
    `measured_runs` times (recorded), reading each run's own
    results/{metadata.json,fields.csv,residuals.csv} for the correctness/
    fingerprint data a raw ``RunResult`` cannot provide by itself
    (section 38: document the warm-up policy explicitly, don't mix it
    into the measured set).
    """
    case_path = Path(case_path)

    for _ in range(warmup_runs):
        run_case(executable, case_path)

    runs: list[BenchmarkRun] = []
    for run_index in range(measured_runs):
        run_result: RunResult = run_case(executable, case_path)
        results_dir = case_path / results_subdir

        if run_result.exit_code in (1, 2):
            # Application/case error -- no results/ to read at all.
            runs.append(
                BenchmarkRun(
                    case=case_label,
                    grid=grid_label,
                    run_index=run_index,
                    runtime_seconds=run_result.runtime_seconds,
                    iterations=0,
                    converged=False,
                    finite=False,
                    u_residual=float("nan"),
                    v_residual=float("nan"),
                    p_residual=float("nan"),
                    continuity_residual=float("nan"),
                    global_mass_imbalance=float("nan"),
                    fingerprint=None,
                )
            )
            continue

        loaded = load_case_results(results_dir)
        runs.append(
            BenchmarkRun(
                case=case_label,
                grid=grid_label,
                run_index=run_index,
                runtime_seconds=run_result.runtime_seconds,
                iterations=int(loaded.metadata["solver"]["iterations"]),
                converged=loaded.converged,
                finite=bool(loaded.metadata["numerics"]["finite"]),
                u_residual=float(loaded.metadata["residuals"]["u"]),
                v_residual=float(loaded.metadata["residuals"]["v"]),
                p_residual=float(loaded.metadata["residuals"]["p"]),
                continuity_residual=float(loaded.metadata["residuals"]["continuity"]),
                global_mass_imbalance=float(
                    loaded.metadata["conservation"]["global_mass_imbalance"]
                ),
                fingerprint=compute_fingerprint(results_dir),
            )
        )
    return runs


@dataclass(frozen=True)
class RuntimeStatistics:
    minimum: float
    median: float
    mean: float
    maximum: float
    valid_run_count: int
    total_run_count: int


def summarize_runtime(runs: list[BenchmarkRun]) -> RuntimeStatistics:
    """min/median/mean/max wall-clock runtime (section 39), computed
    *only* over runs `valid_for_performance` (section 37) -- a failed
    run's timing is excluded, not averaged in.
    """
    valid_runtimes = [r.runtime_seconds for r in runs if r.valid_for_performance]
    if not valid_runtimes:
        raise ValueError("summarize_runtime: no valid_for_performance runs to summarize")
    return RuntimeStatistics(
        minimum=min(valid_runtimes),
        median=statistics.median(valid_runtimes),
        mean=statistics.mean(valid_runtimes),
        maximum=max(valid_runtimes),
        valid_run_count=len(valid_runtimes),
        total_run_count=len(runs),
    )


def write_benchmark_csv(runs: list[BenchmarkRun], path: str | Path) -> Path:
    """Writes benchmarks/results.csv (section 36)."""
    frame = pd.DataFrame(
        [
            {
                "case": r.case,
                "grid": r.grid,
                "run": r.run_index,
                "runtime_seconds": r.runtime_seconds,
                "iterations": r.iterations,
                "converged": r.converged,
                "u_residual": r.u_residual,
                "v_residual": r.v_residual,
                "p_residual": r.p_residual,
                "continuity": r.continuity_residual,
                "mass_imbalance": r.global_mass_imbalance,
                "valid_for_performance": r.valid_for_performance,
                "fingerprint": r.fingerprint,
            }
            for r in runs
        ]
    )
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    frame.to_csv(path, index=False)
    return path
