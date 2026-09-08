"""Launches the compiled cfdapp CLI as a subprocess (TODO.md "P1 --
Python Tooling" sections 31-33).
"""

from __future__ import annotations

import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

#: cfdapp's own documented exit codes (apps/cli/main.cpp's ExitCode enum
#: / docs/user_guide/case_format.md) -- the one place this package
#: encodes them, so a change to the C++ side is a one-line diff here.
_EXIT_CODE_OUTCOMES = {
    0: "converged",
    1: "application_error",
    2: "invalid_case",
    3: "did_not_converge",
    4: "numerical_failure",
}


@dataclass(frozen=True)
class RunResult:
    case_path: Path
    exit_code: int
    stdout: str
    stderr: str
    runtime_seconds: float

    @property
    def outcome(self) -> str:
        """One of "converged"/"application_error"/"invalid_case"/
        "did_not_converge"/"numerical_failure", or "unknown_exit_code"
        for anything the documented contract doesn't cover (section 32:
        distinguish "application failed" / "solver failed" / "solver
        converged" from exit status).
        """
        return _EXIT_CODE_OUTCOMES.get(self.exit_code, "unknown_exit_code")

    @property
    def converged(self) -> bool:
        return self.exit_code == 0


def run_case(
    executable: str | Path, case_path: str | Path, *, extra_args: tuple[str, ...] = ()
) -> RunResult:
    """Runs `<executable> --case <case_path> [extra_args...]`, captures
    exit code/stdout/stderr/wall-clock runtime (section 32).

    Always passes the executable and arguments as a list (section 33:
    never a shell string), so there is no quoting/injection risk from
    case_path.
    """
    executable = str(executable)
    case_path = Path(case_path)
    command = [executable, "--case", str(case_path), *extra_args]

    start = time.perf_counter()
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    runtime_seconds = time.perf_counter() - start

    return RunResult(
        case_path=case_path,
        exit_code=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
        runtime_seconds=runtime_seconds,
    )
