# Command-line interface

```text
cfdapp --case <case-directory>
cfdapp --help
cfdapp --version
```

`--case` is the only way to run a simulation: point it at a directory
holding a valid case (see [case_format.md](case_format.md)). Everything
else -- solver selection, physics, mesh, boundary conditions -- comes
from that case's own JSON files, never from additional CLI flags; a
case is the one, complete, reproducible description of a run.

## What it prints

```text
CFDApp

Case: Lid-Driven Cavity Re=100
Mesh: 20 x 20
Solver: SIMPLE

Converged: yes
Iterations: 342

U residual: 8.1e-09
V residual: 7.4e-09
P residual: 9.9e-08
Continuity: 2.1e-10
Mass imbalance: 0
NaN/Inf: no

Results:
  JSON:      cases/lid_driven_cavity/results/metadata.json
  Residuals: cases/lid_driven_cavity/results/residuals.csv
  CSV:       cases/lid_driven_cavity/results/fields.csv
  VTK:       cases/lid_driven_cavity/results/solution.vtk
```

If the case also has a `thermal` block in `physics.json`, two more
lines (`Thermal converged:`/`Thermal iterations:`) appear before the
`Results:` section.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Converged |
| 1 | Application error (bad usage, or a result-export I/O failure) |
| 2 | Invalid case (rejected by the case reader/builder) |
| 3 | Did not converge (hit the iteration limit) |
| 4 | Numerical failure (a linear solve failed, or the state went non-finite) |
| 5 | Cancelled (only ever produced by a caller that requested cancellation -- the CLI itself never does) |

These map exactly to `cfd::app::ProjectRunStatus`/`exitCodeFor()` --
the same backend the GUI drives, so a script checking `$?` after a
`cfdapp --case ...` run is checking the identical status the GUI's own
"State" label would show for that same case.

## Scripting

Because the CLI never touches a display and its exit code alone is
enough to know whether a run converged, it's the right tool for CI,
batch/parameter sweeps, and remote execution -- the GUI is an
additional client on top of the same backend (see
[getting_started.md](getting_started.md)), never a replacement for
headless use.
