# Field visualization and post-processing (library)

The building blocks for contour extraction, vector-field sampling,
point/line probing, and derived fields live in `include/cfd/viz/` as
plain C++ -- no Qt, no mesh-drawing code -- so they're usable from a
future GUI view, a script, or a test, all reading exactly the same
solver output.

| Header | What it does |
|---|---|
| `MarchingSquares.hpp` | Deterministic marching-squares contour extraction (`extractContourSegments`) over a structured grid of scalar samples, plus `automaticContourLevels` for evenly-spaced levels. |
| `DerivedFields.hpp` | `velocityMagnitude`, and `vorticity2D` (a Green-Gauss cell gradient of the result velocity field -- see that header's own note on its boundary-face simplification). |
| `FieldProbe.hpp` | `nearestCell`/`probeScalar` (point inspection) and `sampleLine` (evenly-spaced samples along a line, for a centerline plot or a CSV export). |
| `VectorSampling.hpp` | `sampleVectorField` -- every Nth cell's velocity, for arrow/vector-plot rendering at a chosen density; the physical vector is never rescaled by the caller's own display scale. |

Each is exercised directly against synthetic fields with a known
analytical answer in `tests/unit/viz/` (e.g. a contour of `phi(x,y) = x`
at level 0.5 is checked to land at `x = 0.5`; a solid-body-rotation
velocity field's vorticity is checked against its exact constant
`omega_z = 2`), not just visual inspection.

## Using these directly (e.g. from a script or a future GUI view)

```cpp
#include "cfd/viz/DerivedFields.hpp"
#include "cfd/viz/FieldProbe.hpp"

const auto speed = cfd::viz::velocityMagnitude(result.velocity);
const auto vorticity = cfd::viz::vorticity2D(mesh, result.velocity);
const auto centerline = cfd::viz::sampleLine(mesh, speed, {0.0, 0.5}, {1.0, 0.5}, 50);
```

## What's not built yet

None of this is rendered inside `cfdapp_gui` yet (no contour/vector
overlay, no probe/line-sample panel) -- see [gui.md](gui.md)'s own "What's
not built yet" section. For visual inspection today, export VTK and use
ParaView (see [paraview.md](paraview.md)), which already supports
contours, glyphs (vectors), and everything else this library's own
algorithms would otherwise need to draw by hand.
