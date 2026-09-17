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

After the `Mesh:` line the production mesh-quality report is printed
(P12-MESH-004; see *Mesh quality* in [case_format.md](case_format.md)):
the status, the cell count and area range, the maximum of every metric
with its worst cell or face, then one line per issue:

```text
Mesh quality: valid_with_warnings
  cells: 16, cell area: 0.0357143 .. 0.0892857, degenerate cells: 0, invalid faces: 0
  max aspect ratio: 1.75 (cell 12 at (0.125, 0.928571))
  max non-orthogonality (deg): 0 (face 1 at (0.25, 0.0714286))
  max skewness: 0 (face 1 at (0.25, 0.0714286))
  max expansion ratio: 2.5 (face 32 at (0.125, 0.857143))
  warning expansion_ratio: expansion_ratio 2.5 > 2 at face 32 (0.125, 0.857143) (8 faces above the warning threshold): abrupt cell-size change; interpolation there is only first-order accurate [value 2.5; threshold 2; at (0.125, 0.857143)]
```

An invalid mesh is not solved: the run stops with exit code 2 and a
`Case configuration error:` naming the defect and where it is.

A 3D case (P12-MESH-006; see *Three-dimensional cases* in
[case_format.md](case_format.md)) prints the mesh as `nx x ny x nz (3D)`,
the W residual, and after the residuals the face-flux scheme, the global
mass balance and the cell continuity (from
`tests/data/cases/valid_duct3d_cli_smoke`):

```text
Mesh: 12 x 4 x 4 (3D)
...
U residual: 9.30732e-07
V residual: 1.83166e-07
W residual: 1.83166e-07
P residual: 5.19568e-07
Continuity: 3.27227e-13
Mass imbalance: 4.33853e-11
NaN/Inf: no

Face flux: rhie_chow
Mass balance: inflow 1, outflow 1, relative imbalance 4.33853e-11
Cell continuity: max 6.70373e-13, rms 3.27227e-13, normalized 3.27227e-13
```

A malformed 3D case is rejected with exit code 2 before anything is
solved. Examples: a box without `nz`, a 2-component velocity, or a physics
block 3D does not support. The ctest fixtures `CFDAppCli3D_*` check these
(`tests/CMakeLists.txt`).

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
