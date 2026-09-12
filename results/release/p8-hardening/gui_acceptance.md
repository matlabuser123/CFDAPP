# P8 — Manual GUI Acceptance Checklist

**Status: COMPLETE — 17/17 PASS.**

These results are **human-executed manual acceptance results, self-reported by
the user (project owner) running `cfdapp_gui.exe` directly**. Claude Code did
not launch, click through, or visually inspect the GUI itself for these
rows — it has no screen-capture or input-automation tool for a native Qt
window in this environment (see the "Automated evidence" section below for
what Claude Code *did* independently verify). One real bug was found and
fixed mid-run: the Case page's Directory field failed to open a path copied
via Windows Explorer's "Copy as path" (which wraps the path in literal
double quotes) — fixed in `apps/gui/SimulationController.cpp`
(`sanitizeCaseDirectory()`, applied in `openCase()`/`saveAs()`), with a
regression test (`SimulationControllerTest.OpenCaseAcceptsQuotedAndPaddedPath`
in `apps/gui/tests/test_simulation_controller.cpp`). All 17 steps below were
run against the rebuilt binary that includes this fix.

## What was verified instead (automated, not a substitute)

- `apps/gui` builds successfully as a native Windows executable
  (`cfdapp_gui.exe`) under a genuine MSVC 19.44 + Qt 6.9.3 configuration —
  see `../../../build/windows-msvc` and the P8 final report's "Windows build"
  section.
- `CFDGuiControllerTests.exe` (the GUI's backend-logic test suite —
  `SimulationControllerTest.*` and `CaseEditingTest.*` in
  `apps/gui/tests/`) built and passed as part of the 1289/1289 full
  regression suite, on both Linux and Windows, and again (40/40) after the
  quoted-path fix above. These test `SimulationController`/`CaseModelAdapter`
  directly (opening/running/editing/validating a case, residual/result
  access, contour/vector/probe queries) without any Qt window, mouse, or
  rendering involved — real coverage of the GUI's *logic*, zero coverage of
  its actual rendered UI, layout, or interactive behavior. This is why the
  manual checklist below was still required.

## Manual checklist — results

Environment: Windows machine with `cfdapp_gui.exe` built (see README.md's
"Building" section, GUI variant; the specific binary tested was rebuilt at
`build\windows-msvc\apps\gui\Debug\cfdapp_gui.exe` during this session,
including the quoted-path fix above).

| # | Step | Result | Notes |
|---|---|---|---|
| 1 | Launch `cfdapp_gui.exe` | PASS | |
| 2 | Open an existing case (`cases/lid_driven_cavity`) via the Case page | PASS | Initially failed with "case directory not found" — root cause was a path pasted with surrounding quotes (Explorer "Copy as path"); fixed via `sanitizeCaseDirectory()`, rebuilt, retested PASS. |
| 3 | Inspect mesh settings in the Mesh editor | PASS | |
| 4 | Inspect fluid/material properties in the Physics editor | PASS | |
| 5 | Inspect boundary conditions in the Boundary editor | PASS | |
| 6 | Inspect solver settings in the Solver editor | PASS | |
| 7 | Edit one field (e.g. a mesh cell count) and save | PASS | |
| 8 | Reload the case and confirm the edit persisted | PASS | |
| 9 | Run the solver from the GUI | PASS | |
| 10 | Watch live convergence/residual status update during the run | PASS | |
| 11 | View field results (pressure, velocity) after completion | PASS | |
| 12 | Use contour visualization on a field | PASS | |
| 13 | Use vector-glyph visualization on velocity | PASS | |
| 14 | Use the probe/line-sample post-processing panel | PASS | |
| 15 | Trigger a validation error (e.g. invalid mesh value) and confirm the Validation panel reports it clearly | PASS | |
| 16 | Open a case that will fail to solve (e.g. an unreachable convergence tolerance) and confirm the failure is reported clearly, not silently | PASS | |
| 17 | Close and reopen the application; confirm no crash and state is as expected | PASS | |

Also checked throughout: no crashes, no frozen UI, no broken/unresponsive
controls, no stale field values after an edit or run, no incorrect file
paths, no unusable layout at the window's default size, no broken
visualizations (blank/garbled contour or vector plots).

## Outcome

17/17 PASS. `TODO.md`'s P8 manual GUI acceptance item and P9's "GUI
acceptance passes" gate are marked `[x]` on the basis of this human-executed
result.

---

# P11-GUI-005 — Case-Creation-From-Scratch Addendum

**Status: COMPLETE — 13/13 PASS.**

The 17-step checklist above only ever opened an *existing* case (step 2);
it never exercised creating a brand-new case from scratch end to end. This
is that distinct workflow, per `ROADMAP.md`'s P11-GUI-005 gap and
`CLAUDE.md`'s own rule that this is a separate gate from the P8 checklist
above, not satisfiable by reusing it.

As with the checklist above, **these results are human-executed and
self-reported** — Claude Code has no screen-capture or input-automation
tool for a native Qt window in this environment, so it did not click
through this itself. It built the exact binary below from a verified-clean
working tree and launched it; every Result/Notes cell below was reported
by the person who actually ran the workflow.

**Commit under test:** `c25115faefd676ce59ce04d83769c80b9a2d2d3c`
**Binary under test:** `build\windows-release\apps\gui\cfdapp_gui.exe`,
rebuilt fresh from a clean working tree at the commit above (MSVC via
Visual Studio 2022 "18"/Community's toolset, Qt 6.9.3, `windeployqt`-bundled
runtime) — not a stale/reused build from an earlier commit.
**New case path:** `cases/gui_manual_test` (deleted by the tester as
cleanup after this run — confirmed no longer present on disk; the
workflow completed against it as reported below before deletion).
**Tester:** project owner, 2026-09-13.

| # | Step | Result | Notes |
|---|---|---|---|
| 1 | Launch `cfdapp_gui.exe`, start a brand-new case (not opening an existing one) | PASS | |
| 2 | Configure mesh (geometry/resolution) in the Mesh editor | PASS | |
| 3 | Configure physics (fluid model, any of thermal/turbulence/buoyancy/species/multiphase/compressible) in the Physics editor | PASS | |
| 4 | Configure boundary conditions for every patch in the Boundary editor | PASS | |
| 5 | Configure solver settings in the Solver editor | PASS | |
| 6 | "Save As" to a new case directory | PASS | |
| 7 | Close the application (or close the case) | PASS | |
| 8 | Reopen the saved case | PASS | |
| 9 | Confirm round-trip: every value configured in steps 2-5 reads back exactly as set (no silently-dropped or defaulted fields) | PASS | |
| 10 | Run "Validate" and confirm it reports the case as valid (or, if you deliberately entered an invalid value, that it reports the error clearly) | PASS | |
| 11 | Run the solver from the GUI | PASS | |
| 12 | Watch live residual/progress status update during the run | PASS | |
| 13 | Inspect the output/results (fields, residual history, exported files) after completion | PASS | |

**Template selection:** present — a template/preset-selection option
exists in the new-case flow (resolves `ROADMAP.md`'s P11-GUI-005 disclosed
"not confirmed present" note).

**Warnings/errors observed:** none.

**Final result:** PASS (13/13).

## Outcome

13/13 PASS. `TODO.md`'s P11-GUI-005 item and `ROADMAP.md`'s P11 acceptance
are marked `[x]` on the basis of this human-executed result.
