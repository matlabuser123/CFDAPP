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
