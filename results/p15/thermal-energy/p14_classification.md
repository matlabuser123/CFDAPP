# P15 — how the thermal path relates to P14's generic transport

Written for: an engineer deciding which assembler to use, and a reviewer
checking that P15 did not quietly create a second numerical convention.

P14's binding rule was *"consolidate existing validated numerics; do not create
another numerical convention."* This file records where thermal stands against
that rule after P15.

**Verdict: hybrid, and the split is structural rather than historical.** The
generic path owns every scalar transport term thermal needs except one, and the
exception is not a matter of effort.

---

## Term by term

| Term | Generic `cfd::transport` | `cfd::thermal` | Same numerics? |
|---|---|---|---|
| Diffusion, scalar Γ | yes | yes | yes |
| Diffusion, **field** Γ | yes | yes | yes |
| Face Γ interpolation (arithmetic / harmonic) | yes | yes | yes |
| Upwind convection on a face mass flux | yes | yes | yes |
| Non-orthogonal correction | yes | yes | yes |
| Patankar source `Su + Sp·φ` | yes | via `volumetricHeatSource` | compatible |
| Conservative implicit Euler | yes | yes | **same form, two implementations** |
| Dirichlet / Neumann / adiabatic boundaries | yes | yes | yes |
| **Convective (Robin) boundary** | **refuses** | **only here** | n/a |

---

## The one term the generic path cannot take

```
G = 1 / (d/k + 1/h_c)
```

needs the **runtime diffusivity** to form the coefficient. The generic layer
*has* Γ — but the boundary query that would consume it,
`ScalarBoundaryCondition::boundaryValue`, is not given it, and changing that
signature touches 7 scalar BCs, 5 vector BCs and 51 call sites. That is Option B
in `robin_architecture_decision.md`, deliberately outside P15, and the user
closed the decision on **Option C**.

`cfd::thermal`'s `assembleThermalBoundaryFaceContribution` can do it because it
handles the Robin case **by type, at a point where `conductivity` is already in
scope**. That is the whole of the difference.

So the generic path does not approximate a Robin wall, and does not silently
treat it as adiabatic — it **throws**, naming the condition and the
runtime-diffusivity cause. `ConvectiveBoundaryIsRefusedByTheGenericTransportPath`
is what holds that, and it checks the message says *why*, not merely that it
refused.

---

## What the transient duplication does and does not mean

Both layers implement conservative implicit Euler in the same form, with the old
level on the right-hand side. Two implementations of one convention is not the
thing P14's rule prohibits — that rule is about two *conventions*. Still, it is
duplication, and it is listed here so it is not forgotten:

- `cfd::transport::applyTransientContribution`
- `cfd::thermal::assembleThermalTransientContribution`

They agree by construction (same form, same level assignment) and each is pinned
by its own tests, including a bitwise steady-state check that the term adds
nothing when `transient` is false. Merging them would mean routing thermal's
boundary handling through the generic assembler, which the Robin term prevents.
**When Option B lands, this merge becomes possible and should happen then** —
not before, because a partial merge would leave thermal calling a generic
assembler for some terms and its own for others, which is strictly worse than
either.

---

## What P15 did *not* do

It did not add a property abstraction parallel to the existing one.
`cfd::thermal::TemperatureDependentProperty` **implements**
`cfd::physics::TemperatureProperty` (P3-PHYS-003), so there is one property
interface, one `evaluatePropertyField`, and one Picard loop.

It exists alongside `ConstantProperty` / `LinearProperty` / `TabulatedProperty`
for a reason the interface cannot express: `SensibleEnthalpy` integrates cp in
**closed form**, which needs the coefficients symbolically. A virtual `value(T)`
cannot supply them. Anything that only needs `value(T)` uses the interface and
does not care which implementation it got.

---

## Guidance

**Use `cfd::transport`** for a new scalar (species, turbulence quantity, passive
tracer, volume fraction) — it is the validated, consolidated path, and its
`LegacyMatrixEquivalence` test holds it bitwise identical to the assembler it
replaced.

**Use `cfd::thermal`** for temperature. Not because the generic path is
inadequate for the interior, but because thermal owns the Robin boundary, the
enthalpy formulation and the property models, and splitting a single equation
across two assemblers to save a little duplication would be the worse trade.
