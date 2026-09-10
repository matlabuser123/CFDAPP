"""Natural-convection (differentially heated cavity) validation analysis
(P3-PHYS-002).

Same convention as channel_flow.py: this module does not recompute the
Nusselt number/heat balance/velocity extrema from raw C++ field data --
that logic already lives once, in
tests/integration/thermal/NaturalConvectionValidationUtils.cpp, with
access to mesh geometry Python's exported CSV/JSON does not carry.
Instead this module loads that C++ layer's own evidence -- the per-grid
``validation.json`` and ``u_centerline.csv``/``v_centerline.csv``/
``nusselt_hot_wall.csv`` files written under
``results/validation/natural_convection/Ra<ra>/<grid>/`` -- and provides
the *cross-run* analysis a single C++ test cannot: a grid-refinement
table and comparison figures.

Rayleigh/Prandtl number reference values (validated against, not
recomputed here from raw inputs -- physics.json for this case does not
exist yet, case-system integration being explicitly out of this task's
scope, see SimulationSetup.hpp's own comment) are the de Vahl Davis
(1983) benchmark table, mirrored from
tests/integration/thermal/DeVahlDavis1983.hpp -- see
validation/literature/natural_convection/README.md for the full
provenance disclosure.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

import pandas as pd

# de Vahl Davis (1983) reference table -- must be kept numerically in
# sync with tests/integration/thermal/DeVahlDavis1983.hpp and
# validation/literature/natural_convection/de_vahl_davis_1983.json.
PRANDTL_NUMBER = 0.71
REFERENCE_TABLE = {
    1.0e3: {"nu_avg": 1.12, "u_max": 3.634, "v_max": 3.679},
    1.0e4: {"nu_avg": 2.243, "u_max": 16.2, "v_max": 19.51},
    1.0e5: {"nu_avg": 4.52, "u_max": 34.81, "v_max": 68.22},
    1.0e6: {"nu_avg": 8.8, "u_max": 65.33, "v_max": 216.75},
}


def rayleigh_number(
    gravity_magnitude: float,
    beta: float,
    delta_t: float,
    length: float,
    kinematic_viscosity: float,
    thermal_diffusivity: float,
) -> float:
    """Ra = g*beta*deltaT*L^3/(nu*alpha) -- the single canonical
    calculation this task's own section 4 requires.
    """
    if kinematic_viscosity <= 0 or thermal_diffusivity <= 0:
        raise ValueError("rayleigh_number: kinematic_viscosity and thermal_diffusivity must be > 0")
    return (
        gravity_magnitude * beta * delta_t * length**3 / (kinematic_viscosity * thermal_diffusivity)
    )


def prandtl_number(kinematic_viscosity: float, thermal_diffusivity: float) -> float:
    """Pr = nu/alpha."""
    if thermal_diffusivity <= 0:
        raise ValueError("prandtl_number: thermal_diffusivity must be > 0")
    return kinematic_viscosity / thermal_diffusivity


@dataclass(frozen=True)
class NaturalConvectionRun:
    """One (Ra, grid) run's evidence, loaded from its validation.json
    (section 37-style record) written by
    NaturalConvectionValidationUtils.cpp's own writeValidationJson.
    """

    ra: float
    pr: float
    grid_label: str
    nx: int
    ny: int
    flow_converged: bool
    thermal_converged: bool
    outer_iterations: int
    global_mass_imbalance: float
    q_hot: float
    q_cold: float
    heat_imbalance: float
    nu_avg_computed: float
    nu_avg_reference: float
    nu_avg_error: float
    u_max_computed: float
    u_max_reference: float
    u_max_error: float
    v_max_computed: float
    v_max_reference: float
    v_max_error: float
    runtime_seconds: float


def load_natural_convection_run(grid_dir: str | Path) -> NaturalConvectionRun:
    """Loads one (Ra, grid) run's evidence from
    ``<grid_dir>/validation.json`` (the exact layout
    NaturalConvectionValidationUtils.cpp's ``writeValidationJson`` writes
    under ``results/validation/natural_convection/Ra<ra>/<grid>/``).
    """
    grid_dir = Path(grid_dir)
    with (grid_dir / "validation.json").open(encoding="utf-8") as handle:
        data = json.load(handle)

    return NaturalConvectionRun(
        ra=data["ra"],
        pr=data["pr"],
        grid_label=grid_dir.name,
        nx=data["mesh"]["nx"],
        ny=data["mesh"]["ny"],
        flow_converged=data["solver"]["flow_converged"],
        thermal_converged=data["solver"]["thermal_converged"],
        outer_iterations=data["solver"]["outer_iterations"],
        global_mass_imbalance=data["conservation"]["global_mass_imbalance"],
        q_hot=data["conservation"]["q_hot"],
        q_cold=data["conservation"]["q_cold"],
        heat_imbalance=data["conservation"]["heat_imbalance"],
        nu_avg_computed=data["validation"]["nu_avg_computed"],
        nu_avg_reference=data["validation"]["nu_avg_reference"],
        nu_avg_error=data["validation"]["nu_avg_error"],
        u_max_computed=data["validation"]["u_max_computed"],
        u_max_reference=data["validation"]["u_max_reference"],
        u_max_error=data["validation"]["u_max_error"],
        v_max_computed=data["validation"]["v_max_computed"],
        v_max_reference=data["validation"]["v_max_reference"],
        v_max_error=data["validation"]["v_max_error"],
        runtime_seconds=data["solver"]["runtime_seconds"],
    )


def grid_refinement_table(runs: list[NaturalConvectionRun]) -> pd.DataFrame:
    """One row per grid (coarsest to finest, by cell count), for a single
    Ra's coarse/medium/fine runs (section 14).
    """
    rows = [
        {
            "grid": run.grid_label,
            "cells": run.nx * run.ny,
            "nu_avg_error": run.nu_avg_error,
            "u_max_error": run.u_max_error,
            "v_max_error": run.v_max_error,
            "heat_imbalance": run.heat_imbalance,
            "global_mass_imbalance": run.global_mass_imbalance,
            "outer_iterations": run.outer_iterations,
            "runtime_seconds": run.runtime_seconds,
        }
        for run in runs
    ]
    return pd.DataFrame(rows).sort_values("cells").reset_index(drop=True)


def _main(argv: list[str] | None = None) -> int:
    """``python -m cfdapp.validation.natural_convection --grid-dir <dir>``:
    reports one (Ra, grid) run's key metrics and exits nonzero if it did
    not converge or its mass/heat imbalance exceeds the given tolerance.
    """
    parser = argparse.ArgumentParser(
        description="Report a CFDApp natural-convection validation run."
    )
    parser.add_argument(
        "--grid-dir",
        required=True,
        help="a results/validation/natural_convection/Ra<ra>/<grid> directory",
    )
    parser.add_argument("--mass-imbalance-max", type=float, default=1e-6)
    parser.add_argument("--heat-imbalance-max", type=float, default=0.05)
    args = parser.parse_args(argv)

    try:
        run = load_natural_convection_run(args.grid_dir)
    except Exception as exc:  # noqa: BLE001 -- CLI boundary, report and fail clearly.
        print(f"natural-convection validation error: {exc}", file=sys.stderr)
        return 2

    print(f"Ra={run.ra:.3g}  Pr={run.pr:.3g}  grid={run.grid_label} ({run.nx}x{run.ny})")
    print(f"flow converged: {run.flow_converged}  thermal converged: {run.thermal_converged}")
    print(
        f"Nu_avg: {run.nu_avg_computed:.4f} (ref {run.nu_avg_reference:.4f}, "
        f"error {run.nu_avg_error:.3%})"
    )
    print(
        f"u_max: {run.u_max_computed:.4f} (ref {run.u_max_reference:.4f}, "
        f"error {run.u_max_error:.3%})"
    )
    print(
        f"v_max: {run.v_max_computed:.4f} (ref {run.v_max_reference:.4f}, "
        f"error {run.v_max_error:.3%})"
    )
    print(f"heat imbalance: {run.heat_imbalance:.3%}")
    print(f"global mass imbalance: {run.global_mass_imbalance:.3e}")

    passed = (
        run.flow_converged
        and run.thermal_converged
        and abs(run.global_mass_imbalance) < args.mass_imbalance_max
        and run.heat_imbalance < args.heat_imbalance_max
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(_main())
