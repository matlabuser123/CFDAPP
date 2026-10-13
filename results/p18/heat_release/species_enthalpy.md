# P18.6 — species enthalpy (§3, §4, §14, §15)

```text
ONE AUTHORITATIVE SPECIES ENTHALPY, AND IT IS NOT NEW. h_i(T) is P18.3's
SpeciesThermodynamics::enthalpy, which is P15's integrated SensibleEnthalpy. P18.6 adds no second
enthalpy implementation and never evaluates h_i = cp_i * T -- P15's own Enthalpy.hpp measures that
shortcut as 26-103% wrong where cp varies.

P18.6 ADDS EXACTLY ONE THERMOCHEMICAL QUANTITY: h_f,i, the standard enthalpy of formation.
```

---

## 1. The two quantities, and why they must not be confused

```text
    h_i(T)   = integral_{T_ref}^{T} cp_i dtheta  +  h_ref,i      SENSIBLE      P15 / P18.3
    h_f,i                                                        FORMATION     P18.6, NEW

h_ref,i IS NOT h_f,i. The sensible reference is an OFFSET on one substance's scale: it cancels out
of every physical result. A heat of formation does not cancel -- it IS the heat release.

G7 MAKES THIS A NEGATIVE CONTROL BY NAME, and it was written two phases before this one:
    "h_f,i replaced by the SENSIBLE reference enthalpy must FAIL. This control matters because
     those are different quantities that the existing API could easily conflate
     (audit/thermal_energy_coupling.md section 1)"
```

**Measured** (`N2_IsDistinctFromTheSensibleReferenceEnthalpy`): with the sensible references set to
the values a confused implementation would read as formation enthalpies (111, 222, 333 J/kg),

| quantity | value |
|---|---:|
| `Delta H_r^0` from the **formation** enthalpies | **−125 000 J/mol** |
| what the **sensible** references would give | **6.9375 J/mol** |
| ratio | **> 1.8e4×** |

and `Delta H_r^0` is **bitwise identical** whether the sensible references are 111/222/333 or
0/0/0 — which is the formal statement that a sensible offset is unobservable.

## 2. Why `h_f` is a separate type

```text
P18.6 adds class FormationEnthalpies, bound to the P18.1 registry, rather than a field on P18.3's
SpeciesThermodynamics. Two reasons, and the first is the one that matters:

(a) IT MAKES THE CONFUSION HARD TO WRITE, not merely detectable. A `referenceEnthalpy` and a
    `formationEnthalpy` sitting next to each other on one class invites exactly the substitution
    G7 names. In a differently-named type the mistake has to be deliberate.
(b) P18.3's FILES STAY BITWISE UNTOUCHED, so its preservation claim needs no re-derivation.
    Measured at logs/05_preservation.log: P18.3 34/34, and its sources are unmodified.

THE COST, STATED: species thermochemistry is now split across two types. A caller must supply both
a MixtureThermodynamics and a FormationEnthalpies, and nothing forces them to describe the same
species list beyond the registry match that both check. That is recorded as technical debt.
```

## 3. Molar and mass forms

```text
    hbar_f,i = M_i h_f,i     [J/mol]        M_i READ from P18.3, never stored here

Published thermochemical tables give molar values, so FormationEnthalpies::fromMolar() exists and
divides by P18.3's molecular weights. The mass form is primary because omega_i is mass-based, so
S_E = -sum_i h_f,i omega_i needs no unit juggling at the point of use.
```

**Measured** (`logs/06_probe.log`): with the dyadic weights `M = 2^-4`,

```text
    hbar_f,A =       0   expected       0   rel 0
    hbar_f,B = -125000   expected -125000   rel 0
    hbar_f,C =  187500   expected  187500   rel 0
    MAX species-enthalpy reference error : 0.000e+00  (0.00 eps)
```

**exactly zero**, and `fromMolar` round-trips **bitwise** — because `M = 2^-4` makes the division
exact. That is a deliberate property of the chosen mechanism, not luck: a non-dyadic weight would
make this comparison approximate and the sharpness would be lost.

## 4. Mixture enthalpy (§15)

```text
    h_mix(T,Y) = sum_i Y_i h_i(T)        P18.3's MixtureThermodynamics::enthalpy, REUSED
    cp_mix(T,Y) = sum_i Y_i cp_i(T)      P18.3's, REUSED
    R_mix(Y)    = sum_i Y_i R_i          P18.3's, REUSED

P18.6 CREATES NO MIXTURE-PROPERTY IMPLEMENTATION. The single-species limit, binary and ternary
mixtures and the temperature variation were all verified at P18.3 close (G5 PASS, every identity
inside 8 eps, two exactly zero, single-species limit bitwise) and are re-run here unchanged --
34/34 at logs/05_preservation.log.
```

The one place P18.6 extends rather than reuses is the **inverse**: P15's
`SensibleEnthalpy::temperature(h)` inverts for one substance. For a mixture the integrand is
`sum_i Y_i cp_i(T)`, so `temperatureFromSensibleEnergy` solves
`rho cp(T,Y) T = u_sens` by bisection over the species' **common cp validity range**. See
`invalid_states.md` §3 for why the range matters and what the first version got wrong.

## 5. One enthalpy, used everywhere (§14)

| consumer | source of `h_i(T)` |
|---|---|
| mixture enthalpy `h_mix` | P18.3 `MixtureThermodynamics::enthalpy` |
| reaction enthalpy `Delta H_r(T)` | the same `SpeciesThermodynamics::enthalpy` |
| the energy source `S_E` | **neither** — it uses `h_f,i` only, by derivation |
| temperature recovery | the same `cp_i(T)` that `h_i` integrates |

```text
THE SOURCE USING h_f ALONE IS THE POINT OF energy_accounting.md section 5, not an inconsistency:
CFDApp transports rho cp T, so the sensible part of the species enthalpy is already being evolved
and adding it to the source would double-count. Control N5 measures the defect at 9.6% of the
reaction enthalpy.
```
