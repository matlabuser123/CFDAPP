# GUI

`cfdapp_gui` (built only with `-DCFDAPP_BUILD_GUI=ON`, see
[installation.md](installation.md)) is a thin Qt/QML front end over the
same backend the CLI uses -- opening/running/saving a case through the
GUI is indistinguishable, from the solver's own point of view, from
doing it via `cfdapp --case`. It never runs a second, GUI-only solver.

## The workflow

```text
New / Open  ->  Validate  ->  Run  ->  Monitor  ->  Stop (optional)
```

- **New** starts an empty, in-memory case (not yet saved anywhere --
  use **Save**, which prompts for a directory the first time, to give
  it one). **Open** reads an existing case directory (type or paste its
  path into the field next to the buttons, e.g.
  `cases/lid_driven_cavity`) -- anything `cfdapp --case` can run, the
  GUI can open, and vice versa (a case the GUI saves is a plain case
  directory the CLI reads unmodified).
- **Save** writes to the case's current directory; if it doesn't have
  one yet (a brand-new case), it saves into whatever path is in the
  directory field.
- **Validate** runs the exact same case-validation the solver itself
  would run before solving (`CaseBuilder::build`) -- catching a bad
  case (missing boundary condition, invalid solver setting, ...) with
  the same message the CLI would print, without actually starting a
  solve.
- **Run** starts solving on a background thread -- the window stays
  responsive throughout (see the *Progress* panel: iteration count and
  the solver's own continuity residual update live, straight from the
  solve in progress, not a second GUI-computed estimate).
- **Stop** requests cancellation. The solve stops at its next safe
  point (the top of the next outer iteration, never mid-iteration) and
  the case's state becomes `Cancelled` -- its result files are never
  left partially written, and the case itself is never corrupted; you
  can `Run` again from there.

## State

The "State:" label always names one of: `Empty`, `Loaded`, `Modified`,
`Validated`, `Running`, `Completed`, `Failed`, `Cancelled`. Buttons
disable themselves automatically when their action isn't currently
valid (e.g. `Run`/`Stop` while a solve is already `Running`) -- there is
no separate "is this allowed right now" logic to second-guess in QML;
it all comes from `SimulationController`'s own properties, which read
the same `cfd::app::CaseSession` state machine the case-manager backend
itself defines.

A trailing `*` after the case name means the in-memory case has unsaved
edits (`isModified`).

## What's not built yet

Interactive mesh/physics/boundary-condition *editing* inside the GUI
(the window shown today opens/saves/validates/runs a case exactly as
its JSON files already describe it -- editing them still means editing
the JSON directly, same as the CLI-only workflow), the contour/vector
field-visualization view, and post-processing/probe/line-sampling views
are not wired into the QML UI yet -- their underlying algorithms exist
and are unit-tested (`include/cfd/viz/`), but nothing in `apps/gui/`
renders them yet. See the repository's own P5 status note for the
current, disclosed scope.
