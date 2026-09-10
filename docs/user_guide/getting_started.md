# Getting started

```text
Install  ->  Launch  ->  Open example  ->  Run  ->  View result
```

## Command line (always available)

CFDApp ships as a single executable, `cfdapp`, that never needs a
display:

```bash
cfdapp --case cases/lid_driven_cavity
```

That's the whole workflow for the CLI: point it at a case directory (see
[case_format.md](case_format.md) for what one contains) and it reads,
validates, solves, and writes `results/` under that same directory --
`metadata.json`, `residuals.csv`, `fields.csv`, and `solution.vtk`
(open the last one in ParaView, see [paraview.md](paraview.md)).

A few ready-to-run examples already live under `cases/`:
`lid_driven_cavity`, `poiseuille_flow`, `heated_cavity`, `channel_flow`,
`backward_facing_step`. None of them need editing -- run any of them
exactly as shown above.

## GUI (optional)

If the application was built with the GUI enabled (see
[installation.md](installation.md)), launch `cfdapp_gui`. It opens to an
empty window; the same journey the CLI's single command performs is
spread across a few buttons -- see [gui.md](gui.md) for the full
workflow (New/Open, Validate, Run, Stop, watch residuals update live).

The GUI is a second front end to the *same* solver backend the CLI
uses (`cfd::app::ProjectRunner` -- see
[architecture](../architecture/) if you're reading the source, not just
running it) -- it never runs a different solver, and a case it opens or
saves is exactly the same case format the CLI reads, so either one can
pick up where the other left off.

## Where results go

Every run writes into `<case directory>/results/`, never anywhere else
-- the original case files are never modified by running a case. Re-run
the same case and its `results/` directory is simply overwritten in
place (fixed filenames, not `solution1.vtk`/`solution2.vtk`/...).
