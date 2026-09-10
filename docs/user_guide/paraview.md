# ParaView workflow

Every run (CLI or GUI) writes `results/solution.vtk` -- a legacy ASCII
VTK `UNSTRUCTURED_GRID` file -- alongside the case, ready to open
directly.

```text
1. Run a case:            cfdapp --case cases/lid_driven_cavity
2. Locate the file:       cases/lid_driven_cavity/results/solution.vtk
3. Open it in ParaView:   File -> Open -> solution.vtk -> OK
4. Click Apply
5. Color by a field:      the "Coloring" dropdown -- Pressure,
                           Temperature (thermal cases only), or
                           Velocity_Magnitude
6. Vectors:                Filters -> Alphabetical -> Glyph, set
                           "Vectors" to Velocity, Glyph Type to Arrow
7. Contours:                Filters -> Alphabetical -> Contour, pick the
                           scalar field and level(s)
```

## Fields present in every export

| VTK field | Type | Notes |
|---|---|---|
| `pressure` | scalar | |
| `velocity` | vector | a real 3-component vector (z=0) -- Glyph works directly, no need to combine separate U/V scalars by hand |
| `velocity_magnitude` | scalar | derived, `|velocity|` |
| `temperature` | scalar | only present when the case has a `thermal` block in `physics.json` |

All four (when present) are `CELL_DATA` -- this solver's own fields are
cell-centered, not interpolated to vertices, so ParaView will show
per-cell (not smoothly-shaded) values unless you apply ParaView's own
"Cell Data to Point Data" filter first.

## Automated verification

`tests/integration/io/test_paraview_smoke.cpp` runs a real case through
the production backend and checks the resulting `solution.vtk` actually
contains the mesh structure (`DATASET UNSTRUCTURED_GRID`, `POINTS`,
`CELLS`, `CELL_TYPES`) and all the field names above -- this is checked
by every `ctest` run, not just documented here.
