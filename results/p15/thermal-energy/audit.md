# P15 — Thermal / Energy: audit

Measured on the tree that closed P14. The TODO's description is not taken as authoritative; the
code is.

---

## The headline: this is mostly a REFACTOR, and one item is not started at all

```text
thermal production code                  1817 lines (5 sources, 6 headers)
thermal unit test files                  11
thermal integration/validation files     4  (+ De Vahl Davis 1983 reference data)
transient references in the energy path  0   <-- the energy equation is STEADY ONLY
Robin / convective BC                    0 files  <-- absent
enthalpy h(T) / T(h)                     absent   <-- the transported quantity is cp*T
device thermal kernels                   0
```

## A1 — What already exists, and is validated

```text
EnergyEquation            assembleEnergyEquation x3 overloads
                          assembleThermalDiffusionContribution  (const k AND ScalarField k)
                          assembleThermalConvectionContribution (const cp AND ScalarField cp)
                          assembleThermalSourceContribution     (const AND field)
                          boundaryDiffusionContribution, thermalBoundaryCorrectionGradient
ThermalSolver             375 lines, Picard outer iteration over variable properties
ThermalInterface          conjugate solid/fluid interface, region-aware diffusion
ThermalRegion(Map)        multi-material regions
ThermalProperties         CONSTANT k and cp, validated at construction; density deliberately
                          NOT duplicated here (FluidProperties owns it)
Boussinesq                S_b = -rho_ref*beta*(T-T_ref)*g, sign convention documented at length
Thermal BCs               FixedTemperature (Dirichlet), HeatFlux (Neumann), Adiabatic
Case format               thermal.json: conductivity, specific_heat, initial_temperature
Validation                heated cavity, natural convection vs De Vahl Davis 1983,
                          Boussinesq coupling, two-material conduction, conjugate interface,
                          non-orthogonal thermal, 3D sparse assembly
```

**Spatially varying properties already work.** The field-based overloads accept
`ScalarField conductivity` and `ScalarField specificHeat`, and `ThermalSolver` already runs a
Picard loop over them. What is missing is not the *capability* to vary but the *models* that
produce the variation.

## A2 — What is genuinely missing

### M1. Transient. Not "partially" — absent.

```text
grep -c "TimeDerivative|dt|transient"   src/thermal/EnergyEquation.cpp  -> 0
                                        src/thermal/ThermalSolver.cpp   -> 0
```

The energy equation is steady-state only. P14 supplies the conservative implicit-Euler machinery
(`TransportTimeState`, the variable-density overload), so this is wiring, not derivation — but it
is real work and it is required by validation case C.

### M2. Enthalpy. The transported quantity is `cp*T`, not `h`.

`assembleThermalConvectionContribution` scales the mass flux by `cp` because, in its own words,
*"the transported quantity here is cp*T rather than T itself"*. There is no `h(T)`, no `T(h)`, no
reference enthalpy, and no integral of a varying `cp`. For constant `cp` the two coincide; for
`cp(T)` they do not, which is precisely the case the brief asks for:

```text
h(T) = h_ref + integral_{T_ref}^{T} cp(theta) dtheta      NOT  h = cp*T
```

### M3. Robin / convective boundary condition. Absent.

`-k dT/dn = h_c(T - T_inf)` has no representation. `FixedTemperature`, `HeatFlux` and `Adiabatic`
are Dirichlet and Neumann only. P14 recorded Robin as belonging here, and it is the boundary a
real cooled wall needs.

### M4. Temperature-dependent property MODELS. Absent.

```text
polynomial / temperatureDependent / k(T) / cp(T) in thermal or case parsing   -> 0 matches
```

A caller can pass a varying field, but nothing computes `k(T) = a0 + a1 T + a2 T^2` from a case
file. The case format accepts a single scalar `conductivity` and `specific_heat`.

### M5. No GPU thermal path.

```text
device thermal kernels   0
```

P14's `assembleScalarTransportDevice` is the natural vehicle — thermal is `phi = T`,
`Gamma = k/cp` — but nothing currently connects them.

### M6. The energy equation does not use P14.

`EnergyEquation` carries its own convection/diffusion/source assemblers: 4 of the 9 duplicated
scalar assemblers the P14 audit counted. P14 exists so that P15 can consume it.

---

## A3 — The risk that dominates this milestone

**`thermal::assembleThermalConvectionContribution` is not merely a duplicate to be deleted — P14's
generic layer CALLS it** (with `cp = 1`), and so does `turbulence`. It is currently the shared
implementation, reached through a wrong-way `transport -> thermal` dependency that P14 recorded as
debt for exactly this phase.

So "refactor thermal onto P14" is not a one-way move. The correct direction is:

```text
P14 transport  OWNS the scalar convection loop
thermal        consumes it, with cp folded into the flux by the CALLER
turbulence     unchanged (already goes through transport)
```

That reverses the dependency and removes the debt — but it edits a function on the SIMPLE-adjacent
thermal path, so it must be proved bitwise, not merely tested.

## A4 — Preservation instruments available

```text
P13-001 SIMPLE fingerprint, 10 cases           already used by P14: IDENTICAL
natural convection vs De Vahl Davis 1983       existing accepted benchmark
heated cavity, two-material, conjugate         existing integration tests
11 thermal unit test files                     including two variable-property files
```

Any thermal refactor must leave all of these unchanged. The natural-convection benchmark is the
one that would catch a subtle coupling error that unit tests would not.

---

## Dry-run record, measured on the unchanged tree

```text
M1  transient in the energy path            0 references       -> implemented and verified
M2  enthalpy h(T)/T(h)                      absent             -> present, round-trip verified
M3  Robin/convective BC                     absent             -> present, analytically checked
M4  k(T)/cp(T) property models              0 matches          -> present, case-configurable
M5  GPU thermal path                        0 kernels          -> via P14, or scope stated
M6  thermal assemblers independent of P14   4 duplicated       -> consuming P14
PRESERVE  SIMPLE fingerprint                 IDENTICAL          -> must stay IDENTICAL
PRESERVE  natural convection benchmark       passing            -> must stay passing
```

Every M row has a measured baseline differing from its target. The PRESERVE rows already pass,
which is correct for that kind and is not counted as an achievement.

## What the audit rules out

* **No new thermal physics module.** 1817 lines exist and are validated against a published
  benchmark.
* **No rewrite of the conjugate/region machinery.** Out of scope and working.
* **No total-energy / high-Mach formulation.** The brief defers it to P17 explicitly.
* **No tolerance invented for conservation.** The energy imbalance bound must come from the same
  kind of measurement P14's did, not from what makes the current code pass.
