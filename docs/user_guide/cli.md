# Command-line interface

```text
cfdapp --case <case-directory>
cfdapp --case <dir> --checkpoint <file> [--checkpoint-every <n>]
cfdapp --case <dir> --resume <file>
cfdapp --help
cfdapp --version
```

`--case` is the only way to run a simulation: point it at a directory
holding a valid case (see [case_format.md](case_format.md)). Everything
else -- solver selection, physics, mesh, boundary conditions -- comes
from that case's own JSON files, never from additional CLI flags; a
case is the one, complete, reproducible description of a run.

The three checkpoint flags are the exception, and they are deliberately
*not* case keys: where a checkpoint lives and how often it is written are
properties of a particular run, not of the problem being solved. Two runs
of the same case with different checkpoint flags produce the same answer.

## Checkpoint and resume

A long run can be stopped and continued without losing numerical state.

```bash
# write a checkpoint every 500 outer iterations
cfdapp --case cases/lid_driven_cavity --checkpoint run.ckpt --checkpoint-every 500

# ... the machine reboots, the job is killed, the GPU node dies ...

cfdapp --case cases/lid_driven_cavity --resume run.ckpt
```

The resumed run continues the same solve: the same iteration numbering, an
unbroken residual history, and the same final answer a single uninterrupted
run would have produced -- bit for bit, on both the CPU and the GPU backend.

`--checkpoint <file>` names the file. It is always the same file, rewritten
in place: a checkpoint is the state *now*, not a history of states.

`--checkpoint-every <n>` additionally writes it every `n` completed outer
iterations. Without it the checkpoint is written once, when the run ends.
Writing periodically has a cost -- the solve is executed as consecutive
segments, each of which repeats the per-solve setup -- so an interval in the
hundreds is usually right and an interval of 1 is legal and slow.

`--resume <file>` continues from a checkpoint. The report then carries two
extra lines:

```text
Resumed from iteration: 500
Checkpoints written: 1 -> run.ckpt
```

### What a resume requires

The checkpoint records which case and which mesh it came from, and refuses to
be resumed into anything else. Both are exact checks, and both name what
differs:

* the **mesh** must be the same mesh -- not merely the same cell and face
  counts, which two different meshes can share;
* the **case configuration** must be the same, with exactly one exception:
  `solver.json`'s `max_iterations`.

That exception is the point. Raising the budget and finishing a run that hit
its iteration limit is the ordinary reason to resume:

```bash
cfdapp --case mycase --checkpoint run.ckpt     # stops at max_iterations, exit 3
$EDITOR mycase/solver.json                     # raise max_iterations
cfdapp --case mycase --resume run.ckpt         # continues where it stopped
```

Changing anything else -- a viscosity, a boundary value, a relaxation factor,
a tolerance, a scheme -- makes it a different problem, and the resume is
refused with `case identity mismatch`. Editing `case.json`'s `name` or
`description` is fine; those are documentation.

### Limits

* **Transport-equation turbulence is not supported.** A case whose
  `physics.json` selects `k_epsilon`, `k_omega` or `sst` is refused at both
  ends, when writing and when resuming, because the model's own k /
  epsilon / omega state is not part of the checkpoint format and a resumed
  run would silently continue from the wrong turbulence state. The case runs
  normally without the checkpoint flags.
* Checkpointing applies to the steady SIMPLE flow solve. A case's thermal,
  species, multiphase or compressible pass runs after that solve, from its
  converged result, and needs no state of its own.
* A checkpoint is portable between the CPU and the GPU backend: it holds no
  device data. Which backend wrote it is recorded in the file.
* A checkpoint failure is never silent. If the file cannot be written, read,
  or validated, the run stops with exit code 1 and a message naming the file
  and the reason.

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

## What the CLI does not do, and why

The CLI and the GUI drive the **same** solver backend (`cfd::app::ProjectRunner`)
and produce bit-for-bit identical results for the same case — that is checked,
not assumed (`results/p13/cli-gui/`). They are not identical *clients*, though,
and one asymmetry is deliberate:

**The CLI cannot cancel a running solve.** The GUI has a Stop button because it
runs the solve on a worker thread with an event loop to receive the click. The
CLI has neither: it is a single thread that calls the solver and prints the
result. Adding cancellation would mean adding a signal handler and a second
thread — a new feature, not a consistency fix — and a `cfdapp` run is stopped
the way any other command-line program is, with Ctrl-C.

Exit code 5 (`Cancelled`) therefore never comes from `cfdapp` itself. It exists
because the GUI and any other client of the same backend can produce it.

Everything else the GUI can do, the CLI can do: checkpointing and resuming are
available to both, through the same options, and a checkpoint written by one is
readable by the other.

## Diagnostics: `--log-level`

`cfdapp` keeps its two output channels strictly apart:

* **stdout is the report.** The run summary and nothing else. It is byte-for-byte
  stable for a given case and binary, so it can be redirected, diffed and parsed.
* **stderr is for messages about the run** -- the CLI's own error lines, and the
  diagnostic trail when you ask for it.

```
cfdapp --case <dir> --log-level debug 2> run.log
```

| level | what it adds |
|---|---|
| `error` | nothing from the run itself (see below) |
| `warning` | a request that could not be honoured -- e.g. GPU discretization declined, with the reason |
| `info` | one completion summary per run; checkpoint-written and resumed-from lines |
| `debug` | the trail: which directory was loaded, the case/solver/mesh/backend at solve start, why a case was rejected, which solve failed, why an export failed |
| `trace` | reserved; nothing is emitted at this level today |

The default is `warning`, so a run's stderr is exactly what it was before
diagnostics existed. Scripts that treat "stderr is non-empty" as "something went
wrong" keep working.

**`--log-level error` shows nothing from the run, and that is deliberate.** Every
failure is *returned* by the solver backend and printed by the CLI
unconditionally -- the `Converged:` line on stdout, `Case error:` / `Run error:`
on stderr, and the exit code. The log level controls the diagnostic **trail**,
never the outcome, so no choice of level can hide a failure from you.

An unrecognised level is rejected rather than ignored:

```
$ cfdapp --case cavity --log-level shout
Error: --log-level must be one of trace, debug, info, warning, error, got "shout"
```

`--log-level` is a CLI option; the GUI has no equivalent control, for the same
reason as the asymmetry above -- it is a feature of a command-line client, and
the GUI surfaces the same information in its own UI rather than on a stream.

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
