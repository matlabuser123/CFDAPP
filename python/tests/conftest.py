from pathlib import Path

import pytest

DATA_DIR = Path(__file__).parent / "data"


@pytest.fixture
def tiny_results_dir() -> Path:
    """A tiny, committed, synthetic 2x2 results/ directory fitting
    CFDApp's exact CSV/JSON contract (section 42: unit tests use tiny
    stored fixtures, never an actual CFD solve).

    Known-exact fields, chosen so multiple tests can check exact
    arithmetic:
      - velocity_x = 2*y + 1 (a linear field, section 45's interpolation
        test convention).
      - pressure = 5 - 0.08*x (section 46's pressure-fit test convention).
    """
    return DATA_DIR / "tiny_results"


@pytest.fixture
def tiny_thermal_results_dir() -> Path:
    """P2-THERMAL-004: a tiny, committed, synthetic 2x2 thermal-enabled
    results/ directory -- same convention as tiny_results_dir, plus a
    "temperature" fields.csv column and a "thermal" metadata.json block.

    temperature is the *exact* analytical 1D-conduction profile
    T(x) = 310 - 20*x on length=1.0 (Th=310 at x=0, Tc=290 at x=1):
    T(0.25)=305, T(0.75)=295 -- chosen so a validation test against this
    fixture can assert exactly zero error, not just "small".
    """
    return DATA_DIR / "tiny_thermal_results"
