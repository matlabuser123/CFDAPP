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

A 2D rectangle, for the `structured_cartesian` and `structured_quad` mesh
types:

```json
{ "type": "rectangle", "length": 1.0, "height": 1.0 }
```

`length`/`height` must be finite and `> 0`.

For a `multiblock` mesh (P12-MESH-003, below) the domain is whatever the
blocks cover, and `geometry.json` says only that:

```json
{ "type": "mesh_defined" }
```

(`length`/`height` must then be absent). `mesh_defined` is required with a
`multiblock` mesh and rejected with any other mesh type.

A 3D box (P12-MESH-006, see *Three-dimensional cases* below):

```json
{ "type": "box", "length": 6.0, "height": 1.0, "depth": 1.0 }
```

`depth` (the z extent) is required, finite and `> 0` for a box, and rejected
for every other geometry type.

## `mesh.json`

Three structured 2D quadrilateral mesh types. `structured_cartesian` and
`structured_quad` have `nx * ny` cells numbered `cell(i, j) = j * nx + i`
over the `geometry.json` rectangle and the four boundary patches `left`,
`right`, `bottom`, `top`; `multiblock` (see *Multi-block meshes* below)
describes a general, non-rectangular 2D domain with named patches. A
`structured_cartesian` mesh with `nz` over a `geometry.json` box is a 3D
hexahedral mesh (P12-MESH-006, see *Three-dimensional cases* below).

**`structured_cartesian`** -- Cartesian cells over the `geometry.json`
rectangle, uniform (unchanged; every existing case) or graded toward the
boundaries with the optional `"grading"` object (see *Graded meshes*
below):

```json
{ "type": "structured_cartesian", "nx": 20, "ny": 20 }
```

**`structured_quad`** (P12-MESH-001) -- arbitrary, possibly non-orthogonal
and skewed quadrilaterals, given by the `(nx + 1) x (ny + 1)` vertex grid,
row-major with `i` fastest (`vertices[j * (nx + 1) + i]` is vertex
`(i, j)`):

```json
{
  "type": "structured_quad",
  "nx": 2,
  "ny": 1,
  "vertices": [[0.0, 0.0], [0.5, 0.0], [1.0, 0.0],
               [0.0, 1.0], [0.6, 1.0], [1.0, 1.0]]
}
```

`nx`/`ny` must be positive integers (a fractional value like `20.5` is
rejected, not truncated). `vertices` is required for `structured_quad`
and rejected for `structured_cartesian`. A `structured_quad` mesh is
accepted only if:

- there are exactly `(nx + 1) * (ny + 1)` vertices, each an array of two
  finite numbers `[x, y]`;
- it discretizes exactly the `geometry.json` rectangle: the `j = 0` row
  lies on `y = 0`, the `j = ny` row on `y = height`, the `i = 0` column on
  `x = 0`, the `i = nx` column on `x = length` (to `1e-9` of the domain
  size), each boundary edge traversed monotonically (so the patches are the
  rectangle's four sides);
- every cell is a strictly convex quadrilateral with counter-clockwise
  corners (no folded, self-intersecting, clockwise, zero-area or
  collapsed-edge cell), and the cells tile the rectangle exactly.

Every built mesh (any type) then passes the mesh-quality gate (see *Mesh
quality* below) before any solver runs; a violation is a case error
naming the offending vertex, cell or face and the defect.

For a genuinely non-orthogonal `structured_quad` mesh, set
`non_orthogonal_corrections` to `1` (below). Without it the solver keeps
only the orthogonal part of every diffusive flux: measured on
`cases/poiseuille_distorted` (up to ~48 degrees non-orthogonality) the
error doubled at 64x8 and the SIMPLE solve diverged on the refined
grids. A `convection_scheme` other than `upwind` is also recommended:
first-order upwind has an O(h) error wherever the flow crosses the grid
lines obliquely, and `"CG"` for `pressure_linear_solver` (the pressure-
correction matrix is symmetric; unpreconditioned BiCGSTAB broke down on
the refined distorted grids) -- see `results/p12-mesh-001/summary.md` and
`cases/poiseuille_distorted`.

The GUI keeps a `structured_quad` case's vertex grid exactly as loaded
(there is no vertex editor; changing `nx`/`ny` there without a matching
vertex grid makes the case invalid), and its mesh preview and field heat
map draw the logical `nx x ny` grid, not the true vertex positions -- use
the exported VTK file (true vertices) for the physical geometry.

### Graded (stretched) meshes -- `"grading"` (P12-MESH-002)

A `structured_cartesian` mesh may cluster cells toward its boundaries with
an optional `"grading"` object, one entry per axis (an absent axis, or an
absent `"grading"`, is uniform -- existing cases are unaffected and build
the identical mesh):

```json
{
  "type": "structured_cartesian",
  "nx": 48,
  "ny": 24,
  "grading": {
    "x": {"type": "uniform"},
    "y": {"type": "geometric", "ratio": 1.2, "cluster": "both"}
  }
}
```

- `"type": "uniform"` -- equal widths (takes no other key).
- `"type": "geometric"` -- `ratio` r (a finite number `>= 1`) is the growth
  factor of **adjacent** cell widths moving away from the clustered
  boundary; `cluster` says where the small cells go: `"left"` / `"right"`
  (x) or `"bottom"` / `"top"` (y) -- the boundary patch names -- or `"both"`
  (symmetric, both boundaries of that axis). With n cells:
  - one side (cluster at the start): widths `w_k = w_0 r^k`, k = 0..n-1;
    the other side is the mirror image;
  - `"both"`: widths `w_k = w_0 r^min(k, n-1-k)` (an even n has two equal
    largest cells at the centre, an odd n one);

  and `w_0` is chosen so the widths sum exactly to the domain length. Node
  coordinates are evaluated in closed form (`expm1`/`log1p`), so the first
  node is exactly 0, the last exactly the length, and a ratio close to 1 is
  well conditioned; `"ratio": 1` is the uniform mesh bit for bit.

The mesh stays exactly orthogonal (rectangular cells), so no
non-orthogonal correction is needed. Rejected (a mesh.json error naming
`grading.<axis>.<key>`): an unknown axis, type, cluster name or key; a
missing or non-numeric `ratio`/`cluster`; `ratio < 1` (choose the side with
`cluster` instead); `ratio`/`cluster` on a uniform axis; `"grading"` on a
`structured_quad` mesh (grade its explicit vertices instead); and, checked
against `geometry.json` before any mesh is built, any distribution that
overflows (e.g. ratio 10 over 400 cells), produces a non-positive width, or
makes the smallest cell narrower than `1e-8` of the axis length ("reduce
the ratio or the cell count").

To keep the same grading law under grid refinement, keep `r^m` constant
(m = cells in the clustered direction: n for one side, n/2 for `"both"`),
i.e. `r_new = r_old^(m_old / m_new)` -- the nodes then sample the same
mapping. See `cases/channel_transpiration_graded` and
`results/p12-mesh-002/summary.md`: at equal cell count, wall clustering
cut the wall-shear error of a wall-boundary-layer channel to 0.29-0.38x
the uniform mesh's; for plain Poiseuille flow (constant curvature) it does
**not** help -- the uniform grid is already optimal there.

The GUI mesh editor exposes grading per axis (type, ratio, cluster), shows
the resulting smallest/largest cell size, and draws the graded lines in its
preview.

### Multi-block meshes -- `"multiblock"` (P12-MESH-003)

A general 2D fluid domain -- non-rectangular, curved, with internal solid
regions -- built from **conformal structured quadrilateral blocks**. Each
block is a `structured_quad` vertex grid; blocks are joined along whole
sides by declared interfaces; boundary patches are named and made of whole
block sides. There are no masks or blocked cells: a region no block covers
(e.g. an obstacle surrounded by blocks) is simply not part of the domain,
and its edges are boundary patches. This is a structured multi-block
format, not an unstructured mesher (no triangles, polygons, CAD import or
automatic meshing).

```json
{
  "type": "multiblock",
  "blocks": [
    {"name": "upstream", "nx": 16, "ny": 8, "vertices": [[0.0, 0.5], ...]},
    {"name": "upper", "nx": 96, "ny": 8, "vertices": [[2.0, 0.5], ...]},
    {"name": "lower", "nx": 96, "ny": 8, "vertices": [[2.0, 0.0], ...]}
  ],
  "interfaces": [
    {"first": {"block": "upstream", "side": "right"},
     "second": {"block": "upper", "side": "left"}, "orientation": "aligned"},
    {"first": {"block": "lower", "side": "top"},
     "second": {"block": "upper", "side": "bottom"}}
  ],
  "patches": [
    {"name": "inlet", "sides": [{"block": "upstream", "side": "left"}]},
    {"name": "outlet", "sides": [{"block": "upper", "side": "right"},
                                 {"block": "lower", "side": "right"}]},
    {"name": "top_wall", "sides": [{"block": "upstream", "side": "top"},
                                   {"block": "upper", "side": "top"}]},
    {"name": "bottom_wall", "sides": [{"block": "upstream", "side": "bottom"},
                                      {"block": "lower", "side": "bottom"}]},
    {"name": "step", "sides": [{"block": "lower", "side": "left"}]}
  ]
}
```

- **Blocks**: a unique `name` (1-64 characters of `[A-Za-z0-9_-]`),
  `nx`, `ny` > 0 and `(nx + 1) * (ny + 1)` finite `[x, y]` vertices,
  row-major with `i` fastest. Every cell must be a strictly convex
  counter-clockwise quadrilateral (the block's `i` direction followed by its
  `j` direction turns left). Cells are numbered block by block, local
  `j * nx + i`. The four sides, each traversed in increasing local index:
  `bottom` = vertices `(i, 0)`, `top` = `(i, ny)`, `left` = `(0, j)`,
  `right` = `(nx, j)`.
- **Interfaces** join two block sides with the same number of faces and
  **identical vertices** (the same floating-point values -- generate shared
  vertices once and copy them into both blocks). `"orientation"`:
  `"aligned"` (default: first side's k-th vertex = second side's k-th) or
  `"reversed"` (= the second side's (N - k)-th). A block may be joined to
  itself (e.g. `bottom` to `top` for a closed O-grid ring). An interface
  becomes ordinary internal faces: one face per pair, owner in the `first`
  block, neighbour in the `second` -- there is no interpolation and no flux
  mismatch across it.
- **Patches**: a unique name and a non-empty list of block sides. Every
  side of every block is used **exactly once**, in one interface or in one
  patch.

Rejected, with the offending block/side/vertex named: structural errors
(unknown or duplicate names, unknown sides, a side used twice or not at
all, wrong vertex count, non-finite vertices); invalid cells (folded,
clockwise, degenerate); interfaces whose face counts differ or whose
vertices do not coincide (even by one ulp) in the declared orientation;
two blocks touching along a side without an interface ("must be joined by
an interface"); overlapping blocks, hanging vertices and non-conformal
contacts (block boundaries may meet only at shared vertices -- checked on
the whole boundary curve and by the winding number of every cell centre);
and, by the `MeshQuality` gate, a domain made of disconnected regions.

`boundaries.json` must configure exactly the mesh's patch names. The VTK
export writes every block's vertex grid (vertices on an interface appear
once per block) and one quad per cell; the metadata JSON reports
`"nx": 0, "ny": 0` plus a `"blocks"` list. The GUI loads, validates, saves
and runs such a case and edits its boundary conditions; the mesh itself is
shown read-only (blocks, cells) and edited in mesh.json; its result page
has no nx x ny field map (use the exported VTK/CSV). The committed cases
`curved_channel_multiblock` (the quantitative benchmark),
`step_channel_multiblock`, `obstacle_channel_multiblock` (internal solid)
and `annular_sector_conduction_multiblock` were generated by
`results/p12-mesh-003/generate_multiblock_cases.py`; see
`results/p12-mesh-003/summary.md`.

### Mesh quality (P12-MESH-004)

Every built mesh is evaluated once by the production mesh-quality report
(`cfd::mesh::MeshQuality`), which classifies it as `valid`,
`valid_with_warnings` or `invalid`. An `invalid` mesh is rejected before any
solver runs; a warning never stops a case.

| metric | definition | per | warning above |
| --- | --- | --- | --- |
| cell area | cell area (2D volume) | cell | -- |
| face length | face length (2D area) | face | -- |
| aspect ratio | longest / shortest face of the cell (rotation-invariant) | cell | 100 |
| non-orthogonality | angle between the face normal and the owner-neighbour line, degrees | internal face | 70 |
| skewness | distance from the face centre to where the owner-neighbour line crosses the face, / owner-neighbour distance | internal face | 0.5 |
| expansion ratio | larger / smaller of the two cell areas sharing the face | internal face | 2 |

Each metric reports min, max, mean, RMS, the worst cell or face (id and
centroid) and how many exceed the warning threshold. Warnings are aggregated
per metric (one entry naming the worst entity and the count), never one
line per cell. Rationale of the thresholds:

- 70° is where the non-orthogonal flux part (tan 70° = 2.7× the orthogonal
  part) dominates the implicit flux;
- 0.5 is the skewness at which each skewness-correction sweep removes at most
  half of the gradient error;
- aspect ratio 100 means coefficient anisotropy of 10⁴, where Krylov
  iteration counts grow by orders of magnitude;
- expansion ratio 2 makes the interpolation's first-order error term a third
  of the second-order one.

These are warnings rather than hard limits: there is no universal failure
value, and the P12-MESH-004 campaign measured velocity errors growing well
before any threshold is reached (`results/p12-mesh-004/summary.md`). A
non-orthogonal mesh (above 10°) also gets an information item recommending
`non_orthogonal_corrections >= 1`.

**Invalid (fatal) conditions**, not configurable:

- a cell with non-finite or non-positive area, a non-finite centroid, fewer
  than 3 faces, or area vectors that do not close;
- a face with non-finite or zero length, an invalid or identical
  owner/neighbour, reversed orientation, or an owner-neighbour line (nearly)
  in the face (≥ 89.9999°);
- a boundary face not in exactly one patch;
- more than one connected region.

The structured builders also reject a folded, inverted, zero-area or
degenerate-edge quadrilateral and name it, for example `cell (1,0) is not
a strictly convex counter-clockwise quadrilateral: not convex at corner
(i+1,j+1)`. An invalid-mesh error carries the summary line and every fatal
issue (at most 20, then a count of the rest).

**Where it is reported:**

- **CLI:** a `Mesh quality:` block after the `Mesh:` line (see `cli.md`).
- **GUI:** the Mesh page's *Mesh quality* box; warnings also appear in the
  validation panel.
- **Results metadata:** `metadata.json` key `"mesh_quality"`:

```json
"mesh_quality": {
  "status": "valid_with_warnings",
  "cells": 16, "faces": 40, "internal_faces": 24, "boundary_faces": 16,
  "cell_area": {"count": 16, "min": 0.036, "max": 0.089, "mean": 0.0625, "rms": 0.068,
                "worst_id": 0, "worst_location": [0.125, 0.071], "above_warning": 0,
                "warning_threshold": null},
  "face_length": { ... }, "aspect_ratio": { ... }, "non_orthogonality_deg": { ... },
  "skewness": { ... }, "expansion_ratio": { ... },
  "degenerate_cells": 0, "invalid_faces": 0, "connected_components": 1,
  "issues": [{"severity": "warning", "metric": "expansion_ratio", "entity": "face", "id": 32,
              "location": [0.125, 0.857], "value": 2.5, "threshold": 2, "count": 8,
              "message": "expansion_ratio 2.5 > 2 at face 32 ..."}]
}
```

Non-finite numbers, and statistics of a metric with no entities, are
`null`.

## Three-dimensional cases (P12-MESH-006)

A 3D case is a `geometry.json` **box** meshed by a `structured_cartesian`
mesh with **`nz`**. That builds uniform Cartesian hexahedra, `nx x ny x nz`
cells over `length x height x depth`, and the six patches `xmin`, `xmax`,
`ymin`, `ymax`, `zmin`, `zmax`. Committed examples:

- `cases/duct_3d` -- square duct, Re = 10, 48 x 8 x 8;
- `cases/lid_driven_cavity_3d` -- lid-driven cube, Re = 100, 16^3;
- `cases/lid_driven_cavity_3d_re1000` -- lid-driven cube, Re = 1000, 32^3.

`geometry.json` and `mesh.json` of `cases/duct_3d`:

```json
{ "type": "box", "length": 6.0, "height": 1.0, "depth": 1.0 }
```

```json
{ "type": "structured_cartesian", "nx": 48, "ny": 8, "nz": 8 }
```

**Case files.**

- `nz` must be a positive integer, and is allowed only with
  `structured_cartesian` over a box. A box requires `nz`. `grading`,
  `structured_quad` and `multiblock` are rejected with a box.
- `boundaries.json` configures exactly the six patches above. Every velocity
  `"value"` (`moving_wall`, `inlet`) has three components `[vx, vy, vz]` in a
  3D case and two in a 2D case.
- `case.json` `initial_conditions.velocity` likewise has three components in
  3D and two in 2D.
- **Physics:** laminar incompressible flow only. `thermal`, `turbulence`
  (other than `"model": "laminar"`), `buoyancy`, `species`, `multiphase` and
  `compressible` are rejected in a 3D case before anything is built, with a
  message naming `physics.json` and the block.

**Solver.** The same `SIMPLE` solves u, v and w with one shared pressure
correction. `solver.json` `face_flux` (optional) selects the predictor face
mass flux:

- `automatic` (the default): linear interpolation in 2D, exactly as before
  this key existed, and Rhie–Chow in 3D;
- `linear`;
- `rhie_chow`: momentum interpolation, F = F_lin − (D_f/α_u)[(p_N − p_P) −
  (∇p)_f·d]. It vanishes for linear pressure and suppresses the
  pressure checkerboard.

A 3D case should use Rhie–Chow. On `cases/duct_3d` the explicit linear flux
did not converge (the undamped odd-even pressure mode).

**Output.**

- `fields.csv` has the columns `cell_id,x,y,z,velocity_x,velocity_y,velocity_z,velocity_magnitude,pressure`.
- `residuals.csv` adds a `w_residual` column.
- `solution.vtk` holds VTK_HEXAHEDRON cells with `pressure`,
  `velocity_magnitude` and the 3-component `velocity`.
- `metadata.json` has `mesh.dimension` = 3, `nx`/`ny`/`nz`, the extents
  `lx`/`ly`/`lz`, `solver.face_flux`, `residuals.w`, and a `conservation`
  block: inflow, outflow, net boundary flux, relative imbalance, max and RMS
  cell imbalance, flux scale, normalized continuity.
- The CLI prints the mesh as `nx x ny x nz (3D)`, the W residual, the face
  flux, the mass balance and the cell continuity.

**GUI.** It opens, validates (the mesh-quality map reports dimension 3),
edits and runs a 3D case:

- the mesh page edits depth and nz;
- the boundary page edits the z velocity;
- the residual plot shows W.

The field map, contours, vectors, probe and line sampler are 2D views and
show nothing for a 3D result; open `results/solution.vtk` in ParaView instead.

**Verification** (`results/p12-mesh-006/summary.md`):

- a genuinely 3D manufactured solution, second order;
- the analytical square duct: CFDApp matches an independent exact solution of
  the same discrete equations to 8e-8, and the fully developed error converges
  at order 2 (acceptance-gate amendment A3; the original absolute thresholds
  failed and were investigated, see `results/p12-mesh-006/g5-investigation/`);
- x-, y- and z-directed ducts agree to ≤ 7e-11 (velocity, pressure, pressure
  gradient);
- the Re = 1000 cube against Albensoeder & Kuhlmann (2005), max deviation
  0.053 U at 64^3.

**Limitations.**

- Uniform Cartesian boxes only (no 3D grading, vertex, multi-block or
  unstructured meshes).
- Steady laminar incompressible SIMPLE only (no PISO/transient, turbulence,
  heat transfer, species, multiphase or compressible flow in 3D).
- CPU only (no GPU 3D SIMPLE).
- The boundary-condition lookup costs O(boundary faces) per face, about 25–30 %
  of the runtime at 64^3; it is not optimized.

## `physics.json`

```json
{
  "model": "incompressible_laminar",
  "density": 1.0,
  "dynamic_viscosity": 0.01,
  "reynolds_number": 100
}
```

`model` must be `incompressible_laminar` (the only base flow model --
turbulence and compressible effects, below, are opt-in additions on top of
it, not separate `model` values). `density`/`dynamic_viscosity` must be
finite and `> 0`. `reynolds_number` is optional, reporting-only metadata
-- it is never used to derive viscosity; density and viscosity are always
the explicit physical inputs.

**Units:** the parser enforces no unit system at all -- every numeric
field here is a plain `double` (`cfd::Real`), checked only for
finiteness/sign/range, never dimensional consistency. Every example in
this document and every case shipped under `cases/` is written in
SI-consistent units by convention (kg, m, s, K, Pa) -- e.g. `1000.0`/
`0.001` for water's density/viscosity, `287.05`/`101325.0` for air's gas
constant/standard pressure -- but nothing stops a case from using a
different self-consistent unit system; get every quantity in the same
system and the numerics work.

Six further blocks are all optional, each enabled purely by the presence
of its key (there is no separate `"enabled": true/false` anywhere in
`physics.json` -- an absent block always means "off", matching the plain
example above):

### `thermal`

```json
"thermal": { "conductivity": 0.6, "specific_heat": 4180.0, "initial_temperature": 300.0 }
```

Solves the energy equation one-way alongside the flow (the converged
velocity/pressure field is used as-is; temperature never feeds back into
momentum unless `buoyancy` is also present). `conductivity`/
`specific_heat` must be `> 0`; `initial_temperature` may be any finite
value. Enabling `thermal` requires every `boundaries.json` patch to
configure a `"temperature"` key (see below).

`adiabatic` and `heat_flux` walls conduct exactly their prescribed heat
(zero, or q'' per unit area) in the assembled energy equation, and a solve
is reported converged only when the returned temperature satisfies its own
equation (P12-MESH-003 fix: these walls previously used the previous outer
iteration's wall temperature, so a converged field could still leak up to
conductance x 1e-8 per wall face and pure conduction needed hundreds of
outer iterations; it now needs two). See `results/p12-mesh-003/summary.md`.

### `turbulence`

```json
"turbulence": { "model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005 }
```

`model` is one of `laminar` (explicit no-op -- identical to omitting the
block), `k_epsilon`, `k_omega`, or `sst`. `initial_k` is always required
and must be `> 0`. Exactly one of `initial_epsilon` (with `k_epsilon`) or
`initial_omega` (with `k_omega`/`sst`) is required, matching the chosen
model -- never both, never neither. `k_relaxation`,
`epsilon_relaxation`/`omega_relaxation` are optional relaxation factors in
`(0, 1]` (default `0.7`); the epsilon/omega one may only be given
alongside the matching `initial_epsilon`/`initial_omega`. No per-patch
boundary configuration is needed -- wall behavior for k/epsilon/omega is
derived automatically from each patch's existing velocity type.

**Invalid example** (model/field mismatch -- both fields given at once):

```json
"turbulence": { "model": "k_epsilon", "initial_k": 0.02,
                "initial_epsilon": 0.005, "initial_omega": 0.01 }
```

Rejected: `field "physics.json turbulence" must satisfy have exactly one
of initial_epsilon/initial_omega; received both present`.

### `buoyancy`

```json
"buoyancy": { "model": "boussinesq", "beta": 0.0034, "reference_temperature": 300.0, "gravity": [0.0, -9.81] }
```

Adds a Boussinesq buoyancy source to the momentum equation. `model` must
be `boussinesq` (the only implemented model). `beta` (thermal expansion
coefficient) must be finite and `>= 0`; `reference_temperature` may be any
finite value; `gravity` is a required 2-component finite `[gx, gy]`
vector. **Requires a `thermal` block** -- a buoyancy source needs a real
temperature field to evaluate against, and `thermal` is this codebase's
only source of one.

**Invalid example** (no `thermal` block):

```json
"buoyancy": { "model": "boussinesq", "beta": 0.0034,
              "reference_temperature": 300.0, "gravity": [0.0, -9.81] }
```

Rejected (with no `"thermal"` key anywhere in the same `physics.json`):
`field "buoyancy" must satisfy be present only alongside a "thermal"
block; received no "thermal" block was given`.

### `species`

```json
"species": [
  { "name": "CO2", "diffusivity": 2.0e-3, "initial_concentration": 0.0 }
]
```

A JSON array (unlike every other block, which is a single object) --
each entry is one independently transported, passive, non-reacting scalar
species, advected/diffused using the already-converged flow field
(one-way, same as `thermal`; species never feed back into momentum).
`name` must be non-empty and unique across the array; `diffusivity` must
be `>= 0` (`0` means pure advection, deliberately allowed); `initial_concentration`
may be any finite value (no `0 <= Y <= 1` or `sum(Y) = 1` enforcement at
this layer). Enabling `species` requires every `boundaries.json` patch to
configure a `"species"` object with an entry for each declared name (see
below). An absent key and a present-but-empty `"species": []` array are
equivalent (both mean "no species").

### `multiphase`

```json
"multiphase": {
  "phase1": { "name": "water", "density": 1000.0, "viscosity": 0.001 },
  "phase2": { "name": "air", "density": 1.0, "viscosity": 1.8e-5 },
  "initial_alpha": 0.5,
  "transport_time_step": 0.01
}
```

A single implicit-Euler volume-fraction-transport step, evaluated once
after the flow converges (not an outer-iterated solve). Exactly two
phases, `phase1`/`phase2`, each with a non-empty, mutually distinct
`name` and finite `density`/`viscosity > 0`. `initial_alpha` (phase1's
uniform starting volume fraction, `1` = pure phase1, `0` = pure phase2)
must be in `[0, 1]`. `transport_time_step` must be finite and `> 0` --
the one place a transient parameter appears in an otherwise-steady case.
Mixture viscosity feeds SIMPLE's effective-viscosity injection point (the
same slot `turbulence` uses); **top-level `dynamic_viscosity` must be `<=`
the smaller of the two phase viscosities**, so that injected difference
can never go negative. Enabling `multiphase` requires every
`boundaries.json` patch to configure an `"alpha"` key (see below). See
"Physics compatibility" below for what `multiphase` excludes.

**Invalid example** (top-level viscosity above both phases):

```json
"dynamic_viscosity": 0.01,
"multiphase": { "phase1": { "name": "water", "density": 1000.0, "viscosity": 0.001 },
                "phase2": { "name": "air", "density": 1.0, "viscosity": 1.8e-5 },
                "initial_alpha": 0.5, "transport_time_step": 0.01 }
```

Rejected: `field "dynamic_viscosity" must satisfy be <= the smaller of
multiphase.phase1.viscosity/phase2.viscosity (0.000018) -- it is used as
the molecular-viscosity baseline the mixture-viscosity coupling adds
mu_mix-baseline on top of, and that difference must never be negative;
received 0.010000`.

### `compressible`

```json
"compressible": {
  "gas_constant": 287.05, "specific_heat_pressure": 1005.0,
  "reference_pressure": 101325.0, "temperature": 300.0,
  "coupled": false
}
```

`gas_constant`/`reference_pressure` must be finite and `> 0`;
`specific_heat_pressure` must be `>` `gas_constant` (so `cv = cp - R > 0`).
Exactly one of `temperature` (a constant, isothermal reinterpretation,
must be `> 0`) or `thermal_coupled: true` (reuse the case's own converged
`thermal` field instead -- **requires a `thermal` block**) must be given.
`coupled` is optional and defaults to `false`.

**`coupled: false` (default) -- post-hoc, not a coupled flow solver.**
Once the incompressible SIMPLE result converges, this mode triggers a
post-hoc, one-way reinterpretation of that already-converged result:
absolute pressure (`reference_pressure + gauge pressure`), ideal-gas EOS
density, per-cell Mach number, a compressible mass flux, and a diagnostic
continuity imbalance -- exported, never fed back into the flow solve.

**`coupled: true` -- a genuinely coupled compressible solve.** Dispatches
to `cfd::compressible::CompressibleSIMPLE` instead: density is iterated
state, updated from the EOS at the corrected pressure every outer
iteration, solving a compressible pressure-correction equation (warm-started
from an incompressible SIMPLE solve, but that warm start's own convergence
is not the reported result -- the coupled solve's own status is). Disclosed
scope limit: this solver has no turbulence-model or buoyancy-source
injection point yet, so `coupled: true` together with a `turbulence` or
`buoyancy` block is rejected at load time (see the compatibility matrix
below) rather than silently ignoring that physics.

**Invalid example** (both `temperature` and `thermal_coupled` given):

```json
"compressible": { "gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                   "reference_pressure": 101325.0, "temperature": 300.0,
                   "thermal_coupled": true }
```

Rejected: `field "physics.json compressible" must satisfy have exactly
one of temperature/thermal_coupled; received both present`.

## Physics compatibility

The blocks above are not all freely combinable. The full compatibility
matrix (enforced in exactly one place,
`validatePhysicsCompatibility` in `src/io/case/PhysicsConfigParser.cpp`
-- this table must stay in sync with that function's own header comment):

| Rule | Kind |
|---|---|
| `buoyancy` | requires `thermal` |
| `compressible.thermal_coupled: true` | requires `thermal` |
| `multiphase` | excludes `turbulence` (both want SIMPLE's one effective-viscosity injection point) |
| `multiphase` | excludes `compressible` (a two-phase mixture and an ideal-gas EOS reinterpretation describe incompatible fluids) |
| `compressible.coupled: true` | excludes `turbulence` (`CompressibleSIMPLE` has no turbulence-model injection point yet) |
| `compressible.coupled: true` | excludes `buoyancy` (`CompressibleSIMPLE` has no buoyancy-source injection point yet) |
| `species` | no exclusions -- compatible with everything |
| everything else | supported (e.g. `thermal`+`turbulence`+`buoyancy`+`species`+`compressible` (`coupled` absent/`false`) all together is valid, as long as `multiphase` is absent) |

An unsupported combination is rejected at load time with a message naming
the two conflicting blocks, the same way any other invalid `physics.json`
field is reported. Three invalid examples (the excludes rules -- the
requires rules are illustrated in each block's own section above; the
`compressible.coupled`/`buoyancy` exclusion follows the same pattern as
the `compressible.coupled`/`turbulence` one below):

```json
"turbulence": { "model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005 },
"multiphase": { "phase1": { "name": "water", "density": 1000.0, "viscosity": 0.001 },
                "phase2": { "name": "air", "density": 1.0, "viscosity": 1.8e-5 },
                "initial_alpha": 0.5, "transport_time_step": 0.01 }
```

Rejected: `field "multiphase" must satisfy be present only without a
"turbulence" block (both would need SIMPLE's one effective-viscosity
injection point); received a "turbulence" block was also given`.

```json
"compressible": { "gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                   "reference_pressure": 101325.0, "temperature": 300.0 },
"multiphase": { "phase1": { "name": "water", "density": 1000.0, "viscosity": 0.001 },
                "phase2": { "name": "air", "density": 1.0, "viscosity": 1.8e-5 },
                "initial_alpha": 0.5, "transport_time_step": 0.01 }
```

Rejected: `field "multiphase" must satisfy be present only without a
"compressible" block (a two-phase mixture and an ideal-gas EOS
reinterpretation describe incompatible fluids); received a "compressible"
block was also given`.

```json
"turbulence": { "model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005 },
"compressible": { "gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                   "reference_pressure": 101325.0, "temperature": 300.0,
                   "coupled": true }
```

Rejected: `field "compressible.coupled" must satisfy be true only without
a "turbulence" block (CompressibleSIMPLE has no turbulence-model
injection point yet); received a "turbulence" block was also given`.

## `boundaries.json`

Keyed by patch name under `"patches"`; every patch of the generated mesh
must be configured and no other name is recognized. A
`structured_cartesian` or `structured_quad` mesh has exactly `left`,
`right`, `bottom`, `top`; a 3D box (P12-MESH-006) has exactly `xmin`,
`xmax`, `ymin`, `ymax`, `zmin`, `zmax`; a `multiblock` mesh has the names its
`mesh.json` `"patches"` declare. Velocity and pressure are
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
Only `moving_wall` and `inlet` take a `"value"` (a finite array,
`[vx, vy]` in 2D and `[vx, vy, vz]` in a 3D case); every other type must
*not* have one.

**Pressure** `type`: `fixed_value` (Dirichlet -- e.g. a channel outlet) or
`fixed_gradient` (Neumann -- the usual pairing with `wall`/`moving_wall`/
`inlet`/`symmetry` velocity patches). Both always take a numeric
`"value"` (the fixed pressure, or the gradient -- `0.0` for the common
zero-gradient case).

### Per-patch keys required by optional `physics.json` blocks

Three more per-patch keys exist, each **required on every patch when the
corresponding `physics.json` block is enabled, and forbidden on every
patch when it is not** -- there is no "leave it absent and get a default"
option once the block is on, so a case can never silently run with an
unconfigured thermal/species/alpha boundary.

**`temperature`** (required by every patch iff `thermal` is enabled).
`type`: `fixed_temperature`/`heat_flux` (Dirichlet/Neumann, both take a
numeric `"value"`) or `adiabatic` (zero-gradient, no `"value"`):

```json
"temperature": { "type": "fixed_temperature", "value": 310.0 }
```

**`species`** (required by every patch iff `species` is enabled) -- an
object keyed by species name, one entry per name declared in
`physics.json`'s `species` array. Each entry's `type` is `fixed_value` or
`fixed_gradient` (both take a numeric `"value"`):

```json
"species": {
  "CO2": { "type": "fixed_value", "value": 1.0 },
  "O2":  { "type": "fixed_gradient", "value": 0.0 }
}
```

**`alpha`** (required by every patch iff `multiphase` is enabled) --
phase1's volume fraction at that patch. `type`: `fixed_value` or
`fixed_gradient` (both take a numeric `"value"`):

```json
"alpha": { "type": "fixed_value", "value": 1.0 }
```

These three combine freely with each other and with velocity/pressure --
e.g. a patch with `thermal`+`species` both enabled configures
`"velocity"`, `"pressure"`, `"temperature"`, and `"species"` all on the
same patch object. `turbulence` and `compressible` need no per-patch
boundary key at all (turbulent wall behavior is derived from the existing
velocity type; `compressible` only ever reinterprets an already-converged
result).

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
  },
  "convection_scheme": "upwind",
  "gradient_scheme": "green_gauss",
  "non_orthogonal_corrections": 0
}
```

`type` must be `SIMPLE` (the only implemented pressure-velocity coupling
algorithm; transient PISO and moving meshes, P12-MESH-007, exist only in the
C++ API and cannot be selected here); each linear solver's own `type` is `BiCGSTAB`, `CG`, or
`GMRES` (P12-NUM-004: restarted GMRES(30), CPU only -- a `GPU` request for
it falls back to the CPU with a logged warning; valid for any nonsingular
matrix) (its optional `backend` is `CPU`, the default, or `GPU`). Relaxation factors
must satisfy `0 < value <= 1`; every tolerance and every `max_iterations`
must be `> 0`.

`convection_scheme` (P12-NUM-001, optional, default `upwind`) selects the
momentum-equation convection scheme: `upwind` / `central` /
`linear_upwind` / `quick`. Only `upwind`'s implicit coefficients ever
differ; the other three add an explicit, boundedness-limited correction
on top of the same upwind matrix (see `results/p12-num-001/summary.md`
for the full formulation and measured convergence behavior, including
why higher-order schemes measure a *lower* global grid-refinement order
than `upwind` due to boundary treatment).

`gradient_scheme` (P12-NUM-002, optional, default `green_gauss`) selects
the reconstruction used by the pressure-gradient source term and the
SIMPLE velocity-correction step: `green_gauss` or `least_squares`. Both
are exact for a linear field on any mesh (including a distorted one);
`least_squares` additionally stays exact for a linear field where
`green_gauss` does not (a distorted/non-orthogonal mesh) -- see
`results/p12-num-002/summary.md`.

`non_orthogonal_corrections` (P12-NUM-003, optional, integer `>= 0`,
default `0`; a negative value is rejected) controls the non-orthogonal
mesh correction. `0` is the uncorrected solver, exactly the behavior
before this key existed. `N >= 1` enables the over-relaxed non-orthogonal
correction (orthogonal part implicit, `S_nonorth . grad(phi)` explicit, on
internal faces and on value-prescribing boundary faces) in every diffusion
term -- momentum viscous terms, thermal conduction, species diffusion and
the k/epsilon/omega diffusion of every turbulence model -- and runs `N`
momentum-predictor passes and `N` pressure-correction passes per SIMPLE
(and compressible SIMPLE) iteration; pressure passes 2..N add the explicit
non-orthogonal part of the pressure-correction flux. Unlike OpenFOAM's
`nNonOrthogonalCorrectors`, `0` here means *off*, not "one corrected
solve". The correction's gradient is the one selected by `gradient_scheme`:
`least_squares` makes the corrected operators exact for linear fields on a
skewed mesh; `green_gauss` (skewness-corrected) is within ~1e-9. On a
`structured_cartesian` grid every correction term is exactly zero (results
are bit-identical for `0` and `1`); on a `structured_quad` grid they are
what makes the solution converge to the right answer (see `mesh.json`
above). See `results/p12-num-003/summary.md` and
`results/p12-mesh-001/summary.md`.

`face_flux` (P12-MESH-006, optional, default `automatic`) selects the
predictor face mass flux:

- `automatic` resolves to `linear` in 2D (bit-identical to a file without the
  key) and `rhie_chow` in 3D;
- `linear` is the explicit linear interpolation of the predicted velocity;
- `rhie_chow` is momentum interpolation (see *Three-dimensional cases* above).

The pressure-correction reference cell (null-space gauge for a fully
closed domain) is *not* configurable here -- it stays the deterministic
default, cell `0`.

### `robustness` (P12-NUM-004, optional)

Every key is optional. **An absent block, an empty block, or any absent
key keeps today's behavior**: the absolute convergence criterion and every
feature below disabled (results are bit-identical to a file without the
block). The same settings apply to SIMPLE and, for a
`compressible.coupled` case, to the coupled compressible solve.

```json
"robustness": {
  "convergence_criterion": "absolute",
  "normalization": {"reference_iterations": 5, "velocity_tolerance": 1e-4,
                    "pressure_tolerance": 1e-4},
  "stagnation_detection": {"enabled": false, "window": 50,
                           "min_relative_improvement": 0.01, "start_iteration": 100},
  "divergence_detection": {"enabled": false, "window": 10, "growth_factor": 10.0,
                           "start_iteration": 10},
  "adaptive_relaxation": {"enabled": false, "min_velocity": 0.1, "max_velocity": 0.9,
                          "min_pressure": 0.05, "max_pressure": 0.7},
  "linear_solver_fallback": {"enabled": false, "max_attempts": 1}
}
```

(the values shown are the defaults).

- **Normalized residuals** are always computed and reported (results JSON
  `robustness.normalized_residuals`): each residual divided by its
  *reference*, the largest value over the first `reference_iterations`
  outer iterations, floored at that residual's absolute tolerance (so a
  residual that starts at exactly 0 never divides by zero -- it is then
  measured in multiples of its tolerance).
- `convergence_criterion`: `absolute` (default) -- converged when every
  residual is below its absolute tolerance (the pre-existing rule);
  `normalized` -- u, v and p converge when `residual <= max(tolerance_n x
  reference, absolute tolerance)` with the `normalization` tolerances
  (`0 < value < 1`), while continuity and global mass imbalance keep their
  **absolute** `continuity_tolerance` gate (a small normalized residual
  never converges a solve whose mass conservation is unacceptable).
- `stagnation_detection`: stops the solve with status **Stagnated**
  (instead of running to `max_iterations`) when the best "convergence
  distance" (the largest residual/tolerance ratio) improved by less than
  `min_relative_improvement` (`0 < value < 1`) over the last `window`
  (`2..10000`) iterations, from `start_iteration` on. A solve contracting
  by a factor `rho` per iteration is classified slow-but-converging, not
  stagnating, while `1 - rho^window >= min_relative_improvement`.
- `divergence_detection`: stops with status **Diverging** when, from
  `start_iteration + window` on, a residual stayed `>= growth_factor`
  (`> 1`) times its best value (since `start_iteration`) for `window`
  consecutive iterations, or increased at every one of the last `window`
  iterations by `>= growth_factor` in total, or its norm overflowed. A
  single spike never triggers it.
- `adaptive_relaxation`: `velocity_relaxation`/`pressure_relaxation`
  become the **initial** values (they must lie within the bounds, each
  bound `0 < value <= 1`, min <= max). After each completed iteration the
  factors are reduced by 30% on a >20% residual jump or a growing
  oscillation, and raised by 5% after 5 consecutive improvements, never
  above 90% of a factor that previously proved unstable. The factors never
  change inside an iteration.
- `linear_solver_fallback`: when a momentum or pressure-correction linear
  solve fails with a *Breakdown* (or a non-finite residual from finite
  inputs), retry with up to `max_attempts` (`0..3`) alternative methods:
  CG only if the matrix is proven symmetric positive definite, otherwise
  GMRES (a BiCGSTAB primary tries CG then GMRES on an SPD matrix). Each
  retry keeps the same accuracy target and preconditioner. Fallbacks are
  counted in the results JSON (`robustness.linear_solver_fallbacks`,
  `..._recoveries`) and printed by the CLI; a failed fallback still ends
  the solve as a momentum/pressure-correction failure. (Since P12-MESH-004
  BiCGSTAB detects breakdown scale-invariantly and restarts its Krylov
  sequence itself after a numerical breakdown once it has made progress, so
  it reports *Breakdown* only when it has not -- a small residual or matrix
  scale alone never causes one; see `results/p12-mesh-004/summary.md`
  section 23.)

Invalid values (for example a `window` below 2, `growth_factor <= 1`, a
relaxation bound outside `(0, 1]`, `min > max`, `max_attempts` outside
`0..3`, a non-boolean `enabled`, or an unknown key) are rejected with the
offending field named. See `results/p12-num-004/summary.md`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Converged |
| 1 | CLI usage error (missing/unknown argument) |
| 2 | Invalid case (a file could not be read/parsed, or its content failed validation) |
| 3 | Solver did not converge within `max_iterations`, or stopped as stagnated (P12-NUM-004 stagnation detection) |
| 4 | Solver numerical failure (momentum/pressure-correction solve failure, non-finite state, invalid solver configuration, or a diverging residual history detected by P12-NUM-004 divergence detection) |
