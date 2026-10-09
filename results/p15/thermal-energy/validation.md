# P15 — validation evidence

Written for: a reviewer checking that each P15 claim is backed by something that
would fail if the claim were false.

Every case below names **what would break it**. A test whose baseline already
satisfies its criterion proves nothing — that vacuity trap has been hit eight
times across P13–P15, so it is checked for explicitly here and in
`negative_controls.md`.

---

## Suites and counts

| Suite | Tests | Result |
|---|---|---|
| `tests/unit/thermal/CFDThermalTests` | 126 | **126 pass** |
| `tests/integration/thermal/CFDThermalValidationTests` | 16 | **16 pass** |
| `tests/unit/io` — `ThermalPropertyCase.*` | 23 | **23 pass** |
| `tests/integration/case` — `VariablePropertyProductionCase.*` | 5 | **5 pass** |
| `tests/unit/gpu` — `BoundaryEncoding.*` | 9 | **9 pass** |
| Plan-level GPU probe (criteria 3–5) | 1 | **PASS, 0 failures** |

The thermal unit count was **125 with 1 failing** before the stale-contract test
described in `implementation.md` §6 was corrected; it is 126 passing now because
that correction added a test rather than deleting one.

---

## Analytical cases

### Case A — steady conduction, Dirichlet both ends

Exact solution is affine, and the FVM reproduces an affine field exactly, so the
bound is tight: **relative** error < `1e-11`.

*Relative, not absolute, and that was a correction.* The first version demanded
an absolute `1e-9` and measured `1.390e-9` on a field of magnitude 500 — which
is `2.8e-12` relative, below what the linear solver's own `1e-13` relative
tolerance can deliver once the outer iteration has amplified it. An absolute
bound is unreachable for a field that large. The relative bound is **tighter**
than an absolute `1e-9` implies for an order-one field, so this is a correction
of units, not a loosening. (Same class of error as P14's absolute convergence
criterion.)

### Case B — volumetric heat generation

Parabolic profile against the closed form.
**Broken by:** NC2 (source disabled) → fails.

### Case C — mixed Dirichlet / Neumann

Flux-prescribed wall against the closed form.

### Case D — convection–diffusion, known Péclet

*This case caught a defect in its own first version.* The mass flux was set to
`+2.0` on every x-normal face. A boundary face's area vector points **outward**,
so for left-to-right flow the left boundary must carry a **negative** flux.
Setting one positive value everywhere made the inlet read as an outflow, the
inlet temperature was never applied, and the field collapsed to 6.557 K where
300–400 K was expected. Fixed by deriving the flux from the face's own area
vector — the definition, not a patch.

### Case E — Robin wall

Three tests:
- matches the series-resistance solution;
- the `h_c → 0` and `h_c → ∞` limits **bracket** the adiabatic and Dirichlet
  solutions;
- works with a variable conductivity.

**Broken by:** NC1 (conductance sign reversed) → fails.

### Case F — variable conductivity differs from constant

The non-vacuity case for variable properties, at assembly level.
**Broken by:** NC3 (k(T) collapsed to a constant) → fails.

### Case G — enthalpy round trip over the solved field

`T → h → T` over every cell of a converged solution.
**Broken by:** NC6 (inversion broken) → fails.

### Naive-form guard

`NaiveCpTimesTemperatureIsWrongForVariableCp` asserts the `cp·T` shortcut and
the true integral differ by **more than 10 % relative**. Without this, an
implementation that silently used `cp·T` would pass every other enthalpy test
that only checks self-consistency.
**Broken by:** NC5 (integral replaced by `cp·T`) → fails.

---

## Production-path validation (`VariablePropertyProductionCase`)

The unit tests prove `physics.json` **parses** a model. That is not the same as
the model reaching the solver, and the difference is the exact failure P15
forbids. These five run the real `ProjectRunner` against a real case directory.

### The closed form

`cases/heated_species_diffusion` is quiescent — every patch is a wall — so
temperature is one-dimensional steady conduction:

```
d/dx ( k(T) dT/dx ) = 0
```

`k dT/dx` is then constant, so the **Kirchhoff transform** `F(T) = ∫k dT` is
linear in x:

```
F(T(x)) = F(T_left) + [F(T_right) − F(T_left)]·x/L
```

For `k(T) = a + bT` this is `F(T) = aT + bT²/2`, and the quadratic inverts in
closed form. The discriminant is an **exact square**, `a² + 2bF(T) = (a + bT)²`,
so the inversion is exact rather than fitted.

Coefficients `a = −8.4, b = 0.03` give `k(300) = 0.6` — exactly the constant the
committed case uses — with k varying 3× across the domain.

### Why that choice is the detector

The model and the constant **agree at the initial temperature** and disagree
everywhere else. A run that dropped the model would land on the linear profile,
which departs from the closed form by ~2.4 K at mid-domain. The test asserts
both directions:

- error vs the closed form < `6e-2` K (derived — see below);
- departure from the constant-k profile > `1.0` K.

A constant-k result cannot satisfy both. A model-consuming result satisfies both
by a factor of ~40.

### The bound is derived, not chosen

The first version guessed `2e-2` and measured `5.266e-2`. The guess was replaced
by a **measurement**, not by a larger guess: the refinement study established
that the error sequence is second order, so `5.266e-2` at nx = 20 is what a
correct discretisation produces here. The bound is that value plus ~14 % for
platform floating-point variation. See `mesh_convergence.md`.

### The other three

- **`RunsEndToEndAndExports`** — converges, and `fields.csv` / `metadata.json`
  exist. A capability that solves but cannot be written out is not usable.
- **`ConstantFormIsUnaffected`** — the committed case, unpatched, still produces
  the linear profile to `1e-6`.
- **`OutOfRangeExcursionIsReportedNotExtrapolated`** — a range that brackets
  `initial_temperature` (so the case *parses*) but excludes the 310 K wall must
  not report a converged temperature field. This exercises the **runtime**
  channel, not the parse-time one.

---

## Case-format rejections (23 tests)

Both accepted forms, plus every rejection the P15 brief enumerated. Each
rejection asserts **both** the human-readable message and the machine-readable
`CaseDiagnostic` — file, field and constraint.

Rejected, each naming its exact field:

| Input | Field named |
|---|---|
| unknown model name | `…conductivity.model` |
| missing `model` key | `…conductivity.model` |
| empty coefficient list | `…conductivity.coefficients` |
| `coefficients` not an array | `…conductivity.coefficients` |
| non-numeric coefficient | `…conductivity.coefficients[1]` |
| null coefficient | `…conductivity.coefficients[1]` |
| inverted range | `…conductivity.max_temperature` |
| degenerate range (min == max) | `…conductivity.max_temperature` |
| missing range bound | `…conductivity.min_temperature` |
| initial temperature outside range | `…min_temperature/max_temperature` |
| non-positive value at T₀ | `…conductivity.coefficients` |
| unknown key inside the model | key `reference`, context in the constraint |
| string where a property belongs | `…conductivity` |
| array where a property belongs | `…conductivity` |
| negative constant | `…conductivity` |
| zero constant specific heat | `…specific_heat` |
| bad model on `specific_heat` | `…specific_heat.coefficients` |

The last exists because both properties run through one function; it pins that
the second is not silently skipped.

**The one that matters most** is the unknown model name. A case asking for
physics this build does not have must **fail**, not quietly solve a different
problem with a constant.

One expectation here was **wrong and was corrected to match reality, not the
other way round**: `rejectUnknownKeys` names the offending *key* as the
structured field and puts the containing context in the constraint. That is the
convention it has used at ~140 call sites since P13-002, and P15 is not the
place to change it. The test now checks the parent field where it actually
appears.
