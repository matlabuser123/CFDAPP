# P18.3 — integration with transported composition (§25)

```text
P18.1 owns Y_i and its invariants. P18.2 transports rho Y_i. P18.3 turns a local Y_i into local
thermodynamic properties. This document is the join.
```

---

## 1. The entry point

```text
evaluateMixtureFields(mixture, state, pressure, temperature) -> MixtureFields
    molecularWeight · gasConstant · specificHeatPressure · specificHeatVolume ·
    specificHeatRatio · density            -- one ScalarField each, per cell
```

Each cell's properties come from **that cell's own composition**, obtained from the P18.1
`MassFractionState`. There is no domain-average composition and no single representative mixture.

## 2. Measured — a spatially varying composition gives spatially varying properties

```text
8-cell strip, Y_H2 graded 0.05 -> 0.95, Y_CO2 the exact complement (so sum Y = 1 by construction):

  R_mix[last]  / R_mix[first]   > 5x
  cp_mix[last] / cp_mix[first]  > 5x
  rho[first]   / rho[last]      > 5x
  M_mix[first] / M_mix[last]    > 5x
```

Every field is asserted to **actually vary**, not merely to exist — a field of constants would pass a
"was it populated" check and fail these.

## 3. The field path and the scalar path cannot diverge

```text
For EVERY cell, the field value is asserted BITWISE equal to a direct evaluation of that cell's own
composition through MixtureThermodynamics::properties(). Checked for gasConstant,
specificHeatVolume, specificHeatRatio and density.

So evaluateMixtureFields is provably a loop over the scalar entry point rather than a second
implementation that could drift from it.
```

## 4. Ordering is checked, not assumed

```text
evaluateMixtureFields REFUSES a state whose SpeciesRegistry differs from the mixture's.

WHY IT MATTERS: ordering is part of a registry's value (P18.1). A mismatch would read every
composition against the wrong species and produce entirely plausible numbers -- H2's mass fraction
applied to CO2's molecular weight is not an error any downstream check would notice.

Asserted by RejectsAStateWhoseRegistryDiffersFromTheMixture, which builds a state with H2 and CO2
swapped and requires a throw.
```

## 5. What is NOT integrated

```text
NO SOLVER CONSUMES THE MIXTURE PROPERTIES. The join above is library-level:
  * no case-format key selects a mixture EOS;
  * CompressibleSIMPLE and the density-based solver still take a single-species
    ThermodynamicProperties;
  * P18.2's transport takes D_i as given and does not ask the mixture for anything.

STATED AS A LIMIT. P18.3 delivers the closure and the per-cell evaluation; wiring it into a solver
changes a qualified path and is not in this phase's scope (§17, §24, §32).

P18.2 WAS NOT REOPENED (§24). rho Y_i transport, the sum J_i = 0 closure, the boundary-lag fix and
species conservation are all untouched -- 0 changes to src/species/SpeciesTransport.cpp, and its
tests pass unchanged.
```
