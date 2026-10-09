# P16 eddy-viscosity framework -- turbulent heat transfer: audit

Read-only. No production code, test, build file or case file was changed while
this was written.

---

## Headline

The coupling point is **one place**, and it already exists: the `assemble`
lambda inside `ThermalSolver::solve`'s variable-property overload
(`src/thermal/ThermalSolver.cpp:311-323`), where `conductivity` and
`specificHeat` are re-evaluated per outer Picard iteration.

Adding `k_t = mu_t c_p / Pr_t` there gives §6 and §7 for free: `c_p` is the
runtime local value at that iterate, and `k_mol(T)` is too, so
`k_eff(T) = k_mol(T) + mu_t c_p(T)/Pr_t` is what the assembler receives. No
thermal assembler needs to be duplicated, and no second nonlinear loop appears.

**The coupling is ONE-WAY and that is not a lag.** See §6 below -- this is the
single most important thing to record before implementing, because §8 of the
task asks whether a one-iteration lag exists, and the honest answer is that the
production path has something stronger than a lag.

---

## 1. Thermal conductivity representation

Two forms, both already supported by the energy assembler:

| form | entry point | note |
|---|---|---|
| scalar constant | `assembleEnergyEquation(mesh, T, massFlux, Real conductivity, ...)` | pre-P15, the common path |
| **per-cell field** | `assembleEnergyEquation(mesh, T, massFlux, const ScalarField& conductivity, ...)` | P3-PHYS-003 |

`EnergyEquation.hpp:183` -- the field overload face-interpolates conductivity on
internal faces via `discretization::interpolateInternalFace` and uses the owner
cell's own value on boundary faces. It rejects a non-finite or `<= 0` entry at
the point it enters assembly.

**The field overload is the coupling vehicle.** `k_eff` is a per-cell field by
construction, so nothing new is needed on the assembler side.

## 2. cp representation

Same two forms. Note from `EnergyEquation.hpp:205`: *"Unlike conductivity's
diffusion contribution, cp is not face-interpolated"* -- cp enters convection
and the transient term per cell. That asymmetry does not affect this work, but
it means `k_t = mu_t c_p/Pr_t` must use the **cell** cp, which is what the
`assemble` lambda has.

## 3. Variable-property path

`cfd::physics::TemperatureProperty` (interface) with `ConstantProperty`,
`LinearProperty`, `TabulatedProperty`, and P15's polynomial
`TemperatureDependentProperty`. `evaluatePropertyField(mesh, T, model)` produces
the per-cell field and rejects non-finite/non-positive values; `ThermalSolver`
translates that rejection into `NumericalError` so it is reported through
`status` rather than escaping.

## 4. Energy / temperature equation assembly

```
rho cp (dT/dt + u.grad T) = div(k_eff grad T) + S_T
```

`assembleEnergyEquation` combines diffusion + convection + source. Convection is
`assembleThermalConvectionContribution(mesh, cp, massFlux, ...)`; the same
function, with `specificHeat = 1.0`, is what `cfd::transport` uses for a bare
scalar (the P14 migration relies on that).

## 5. Turbulence model `mu_t` lifetime

`TurbulenceModel::turbulentViscosity()` (`TurbulenceModel.hpp:59`) is pure
virtual and returns a `const ScalarField&` owned by the model. The model
outlives the flow solve in `ProjectRunner` (declared at line ~428, thermal solve
at ~573), so **`mu_t` is live and converged at the point the thermal solve
runs**. No copy or re-computation is required.

`effectiveViscosity(Real molecularViscosity)` already exists on the interface
and is deliberately non-virtual, for the momentum side. The thermal analogue
should follow the same shape: a small, separately-tested formula, not a virtual.

## 6. SIMPLE / thermal coupling order -- READ THIS BEFORE IMPLEMENTING

`ProjectRunner.cpp:538` calls it what it is:

> *"Same one-way, best-available thermal solve as the pre-P5 CLI"*

The actual production ordering is:

```
SIMPLE (momentum + pressure + turbulence correction, to convergence)
        |
        v
mu_t  (converged, frozen)
        |
        v
massFlux (converged, frozen)
        |
        v
ThermalSolver.solve  -- its own Picard loop over k(T), cp(T)
        |
        v
(no return path to momentum or turbulence)
```

This is **not the interleaved loop** the task sketch describes, and it is **not
a one-iteration lag**. It is a full one-way decoupling:

* temperature never feeds back into momentum on this path -- SIMPLE is
  constructed with `/*temperature=*/nullptr` at `ProjectRunner.cpp:267` and
  `:365`. (Boussinesq buoyancy is a *different* path that does pass a
  temperature; it is not active here.)
* turbulence is not re-corrected after the thermal solve, so `mu_t` cannot
  respond to the temperature field.

**Consequence for this work:** `k_eff` will be built from a converged, constant
`mu_t`, re-evaluated per Picard iteration only because `c_p(T)` and `k_mol(T)`
change. That is self-consistent and needs no new loop -- but it MUST be
documented as a limitation rather than implied to be two-way. A buoyant or
strongly property-coupled turbulent case would need the outer loop that does not
exist today.

## 7. Case-file configuration

`physics.json` `"thermal"` currently accepts exactly three keys
(`PhysicsConfigParser.cpp:143`):

```
conductivity, specific_heat, initial_temperature
```

`turbulent_prandtl` has to be added to a key list somewhere. **Open decision,
flagged rather than assumed:** it could live under `thermal` (it is a thermal
property) or under `turbulence` (it only means anything when a turbulence model
exists). The `turbulence` block is the better home on the "reject rather than
ignore" principle this codebase already applies -- a `Pr_t` with no turbulence
model is dead configuration and should be refused, and putting it in the
`turbulence` block makes that refusal structural rather than a cross-block
check.

## 8. CPU / GPU thermal state

**There is no GPU thermal path at all** -- `include/cfd/gpu/` contains no
thermal or energy header. So this work is CPU-only by construction, and no
CPU/GPU equivalence question arises for it. That is consistent with GPU
turbulence being a later milestone.

## 9. Existing conservation tests

| file | relevance |
|---|---|
| `tests/integration/thermal/test_thermal_validation.cpp` | thermal validation incl. conservation |
| `tests/integration/thermal/test_two_material_conduction.cpp` | conjugate path |
| `tests/unit/thermal/test_energy_equation.cpp` | assembler-level |
| `tests/unit/thermal/test_thermal_transient.cpp` | transient term |
| `tests/unit/thermal/test_thermal_boundary_consistency.cpp` | boundary handling |
| `tests/integration/thermal/CFDConjugateHeatTransferValidationTests` | built target |
| `tests/integration/thermal/CFDHeatedCavityValidationTests` | built target |
| `tests/integration/thermal/CFDNaturalConvectionValidationTests` | built target |
| `tests/integration/thermal/CFDBoussinesqCouplingTests` | built target |

126 thermal unit tests currently green.

---

## Planned coupling, stated before implementing

```cpp
// per cell, per outer Picard iteration, inside the EXISTING assemble lambda
k_eff[i] = k_mol(T[i]) + mu_t[i] * cp(T[i]) / Pr_t;
```

* `mu_t == 0` -> `k_eff == k_mol` **exactly** (`+ 0.0` on a finite double is
  the identity), which is what makes the §4 bitwise gate achievable rather than
  approximate.
* laminar (no turbulence model) -> no `mu_t` field at all -> the existing
  overloads are called unchanged, byte-for-byte.

## Risks identified before writing code

1. **Silent non-coupling.** The single most likely failure is computing `k_t`
   correctly and never delivering it to the assembler -- exactly the class of
   defect P15 hit with `CaseBuilder` dropping a parsed property model. The §5
   non-vacuity gate is the guard, and it must drive the real case path.
2. **Frozen cp.** Using a reference cp rather than the iterate's cp would be
   invisible for constant-cp cases and wrong for variable-cp ones. §6's test
   exists for this.
3. **`k_eff` positivity.** The field overload already rejects `<= 0`; since
   `mu_t >= 0`, `cp > 0`, `Pr_t > 0`, `k_eff >= k_mol > 0` holds structurally.
   Worth asserting rather than assuming.
4. **Scope creep into laminar.** §12 -- laminar must not invent turbulent
   conductivity. The check is that no `mu_t` source exists, not that `mu_t`
   happens to be zero.
