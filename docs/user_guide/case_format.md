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
algorithm); each linear solver's own `type` is `BiCGSTAB`, `CG`, or
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
structured Cartesian grid -- every mesh a case file can currently build --
every correction term is exactly zero (results are bit-identical for
`0` and `1`). See `results/p12-num-003/summary.md`.

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
  the solve as a momentum/pressure-correction failure.

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
