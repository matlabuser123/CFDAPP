# Case file format

A CFDApp case is a directory containing a manifest (`case.json`) plus one
JSON file per configuration area, e.g.:

```text
cases/lid_driven_cavity/
├── case.json
├── geometry.json
├── mesh.json
├── physics.json
├── boundaries.json
├── solver.json
└── results/
```

Run one with:

```bash
cfdapp --case cases/lid_driven_cavity
```

Every file is validated (structure, types, ranges, cross-file
consistency) before anything is built or solved -- see
`src/io/case/*.cpp` for the exact rules and
[schemas/README.md](../../schemas/README.md) for why that validation is
native C++ rather than an executed JSON Schema. An invalid case is
rejected with a message naming the offending file and field, e.g.:

```text
cases/lid_driven_cavity/solver.json: field "pressure_relaxation" must satisfy 0 < value <= 1; received 1.5
```

## `case.json` -- the manifest

```json
{
  "name": "Lid-Driven Cavity Re=100",
  "description": "2D lid-driven cavity at Re=100, 20x20 structured Cartesian mesh",
  "format_version": 1,
  "geometry": "geometry.json",
  "mesh": "mesh.json",
  "physics": "physics.json",
  "boundaries": "boundaries.json",
  "solver": "solver.json",
  "initial_conditions": { "velocity": [0.0, 0.0], "pressure": 0.0 }
}
```

- `name` is required; `description` and `initial_conditions` are
  optional (`initial_conditions` defaults to zero velocity/pressure).
- `geometry`/`mesh`/`physics`/`boundaries`/`solver` are filenames
  resolved *inside the case directory only* -- a reference that would
  escape it (e.g. `../../../etc/passwd`) is rejected, and they need not
  be the literal names shown above (any case-local filename works).
- `format_version` is optional (defaults to `1`); `1` is the only
  currently-supported value.
- Unknown top-level fields are rejected (a typo should never look like it
  was silently accepted).

## `geometry.json`

Only a 2D rectangle is supported (matching the mesh generator):

```json
{ "type": "rectangle", "length": 1.0, "height": 1.0 }
```

`length`/`height` must be finite and `> 0`.

## `mesh.json`

Only the structured Cartesian generator is supported:

```json
{ "type": "structured_cartesian", "nx": 20, "ny": 20 }
```

`nx`/`ny` must be positive integers (a fractional value like `20.5` is
rejected, not truncated).

## `physics.json`

```json
{
  "model": "incompressible_laminar",
  "density": 1.0,
  "dynamic_viscosity": 0.01,
  "reynolds_number": 100
}
```

`density`/`dynamic_viscosity` must be finite and `> 0`. `reynolds_number`
is optional, reporting-only metadata -- it is never used to derive
viscosity; density and viscosity are always the explicit physical inputs.

## `boundaries.json`

Keyed by patch name under `"patches"`; the generated mesh always has
exactly `left`, `right`, `bottom`, `top` (every one of the four must be
configured, and no other name is recognized). Velocity and pressure are
configured separately per patch, since they are different fields with
different supported condition types:

```json
{
  "patches": {
    "left":   { "velocity": { "type": "wall" },
                "pressure": { "type": "fixed_gradient", "value": 0.0 } },
    "right":  { "velocity": { "type": "wall" },
                "pressure": { "type": "fixed_gradient", "value": 0.0 } },
    "bottom": { "velocity": { "type": "wall" },
                "pressure": { "type": "fixed_gradient", "value": 0.0 } },
    "top":    { "velocity": { "type": "moving_wall", "value": [1.0, 0.0] },
                "pressure": { "type": "fixed_gradient", "value": 0.0 } }
  }
}
```

**Velocity** `type`: `wall`, `moving_wall`, `inlet`, `outlet`, `symmetry`.
Only `moving_wall` and `inlet` take a `"value"` (a 2-component finite
array, `[vx, vy]`); every other type must *not* have one.

**Pressure** `type`: `fixed_value` (Dirichlet -- e.g. a channel outlet) or
`fixed_gradient` (Neumann -- the usual pairing with `wall`/`moving_wall`/
`inlet`/`symmetry` velocity patches). Both always take a numeric
`"value"` (the fixed pressure, or the gradient -- `0.0` for the common
zero-gradient case).

## `solver.json`

```json
{
  "type": "SIMPLE",
  "max_iterations": 6000,
  "velocity_relaxation": 0.7,
  "pressure_relaxation": 0.3,
  "velocity_tolerance": 1e-6,
  "pressure_tolerance": 1e-6,
  "continuity_tolerance": 1e-6,
  "momentum_linear_solver": {
    "type": "BiCGSTAB", "absolute_tolerance": 1e-10,
    "relative_tolerance": 1e-8, "max_iterations": 500
  },
  "pressure_linear_solver": {
    "type": "BiCGSTAB", "absolute_tolerance": 1e-10,
    "relative_tolerance": 1e-8, "max_iterations": 2000
  }
}
```

`type` must be `SIMPLE` (the only implemented pressure-velocity coupling
algorithm); both linear solvers' `type` must be `BiCGSTAB`. Relaxation
factors must satisfy `0 < value <= 1`; every tolerance and every
`max_iterations` must be `> 0`.

The pressure-correction reference cell (null-space gauge for a fully
closed domain) is *not* configurable here -- it stays the deterministic
default, cell `0`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Converged |
| 1 | CLI usage error (missing/unknown argument) |
| 2 | Invalid case (a file could not be read/parsed, or its content failed validation) |
| 3 | Solver did not converge within `max_iterations` |
| 4 | Solver numerical failure (momentum/pressure-correction solve failure, non-finite state, invalid solver configuration) |
