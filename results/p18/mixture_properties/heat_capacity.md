# P18.3 — mixture heat capacities and gamma (§5, §6, §7, §15, §16)

```text
cp_mix(T, Y) = sum_i Y_i cp_i(T)        MASS-weighted, because cp_i is mass-based [J/(kg K)]
cv_mix       = cp_mix - R_mix           derived by ThermodynamicProperties, never stored
gamma_mix    = cp_mix / cv_mix          derived by ThermodynamicProperties, never interpolated
h_mix(T, Y)  = sum_i Y_i h_i(T)         each h_i INTEGRATED by P15's SensibleEnthalpy
e_mix(T, Y)  = h_mix - R_mix T
```

---

## 1. `cp_mix` is mass-weighted, and the basis matters

```text
cp_i is MASS-based, so the weights must be MASS fractions. Multiplying by mole fractions would be a
basis error -- the kind that produces a plausible number and a wrong answer.

MEASURED against the independent reference:
  binary 50/50 H2/CO2    7575.5
  binary 25/75 H2/CO2    4209.75
  ternary 0.2/0.3/0.5    3634.6
```

**Control N3** substitutes a simple unweighted mean: for the 25/75 mixture that gives 7575.5 against
the correct 4209.75 — a ratio of **1.7995×**, some 12 orders of magnitude outside the closure
allowance. The species were chosen so this cannot be mistaken for round-off: **cp(H2) and cp(CO2)
differ by 17.0×.**

## 2. `cv` and `gamma` are not implemented by P18.3 at all

```text
The mixture layer computes ONLY R_mix and cp_mix, then constructs the EXISTING
cfd::compressible::ThermodynamicProperties(R_mix, cp_mix). That type already derives
cv = cp - R and gamma = cp/cv at construction.

CONSEQUENCES, and they are why sections 6 and 7 need no separate machinery:
  * cp_mix, cv_mix and R_mix CANNOT DRIFT APART -- only two of them are ever stored, and the third
    is computed from those two. Section 6's requirement is structural, not tested.
  * gamma_mix EMERGES from cp and cv. It is never averaged from gamma_i (section 7), and the
    identity gamma = cp/cv measures EXACTLY 0 error -- because it is the definition, not a
    coincidence.
  * the single-species limit is bitwise for cv and gamma automatically, once it is bitwise for R and
    cp.
```

**Control N5** averages the species gammas instead: 1.346709 against the correct 1.397972 — a
**3.67%** error.

## 3. `cv` cross-checked against the species sum — §6

§6 asks for an independent verification against `Σ Y_i cv_i`, and the discrepancy reported.

```text
sum_i Y_i cv_i = sum_i Y_i (cp_i - R_i) = cp_mix - R_mix      ALGEBRAICALLY IDENTICAL

max relative discrepancy over 5 compositions x 3 temperatures : 1.745889e-16   (0.79 eps)
```

Labelled **consistency**, not validation, per CLAUDE.md §4 — re-deriving the same algebra by a
different route is not independent physical evidence. Its value is that a wrong weighting in either
expression would break it, which is what N3 and N4 guard.

## 4. Admissibility is enforced, not reported

```text
cv <= 0 is gamma <= 1, i.e. not an admissible ideal gas. REFUSED, with the species and temperature
named, at both levels:
  SpeciesThermodynamics::specificHeatVolume(T)   for one species
  MixtureThermodynamics::properties(Y, T)        for the mixture, before ThermodynamicProperties
                                                 sees it, so the message can name Y and T

Tested by giving a light-molecule species an implausibly small cp (cp = 100 << R = 4124), which makes
cv negative. Both accessors throw.
```

## 5. Temperature dependence reuses P15 — §15

```text
cp_i(T) comes from P15's TemperatureDependentProperty -- a polynomial with a validity range. NO new
polynomial logic was written.

h_i(T) comes from P15's SensibleEnthalpy, which INTEGRATES cp(T). It is NOT cp(T)*T, a substitution
P15's own header measures as 26-103% wrong. P15's reference conventions (T_ref, h_ref) are preserved
per species and never re-based.

VERIFIED with a genuine two-term model cp(T) = a0 + a1 T per species:
  cp_mix measured against a reference computed here from the coefficients, at 300/600/1200/2400 K
  cp_mix increases with T, so the test is not passing on a constant
  the closure cp - cv = R re-checked at EVERY temperature
  gamma(300 K) != gamma(2400 K)  -- so gamma genuinely varies with T
```

That last line matters beyond §15: **`gamma_mix` varying with both `T` and `Y` is exactly the
"variable-gamma EOS" that `TotalEnergyState`'s refusal names as its prerequisite.** See
`single_species_compatibility.md` §4.

## 6. Enthalpy and internal energy — §16

```text
h_mix = sum Y_i h_i(T), reference computed independently as cp*(T - 298.15) for the constant-cp case
e_mix = h_mix - R_mix T, the same relation TotalEnergyState::specificInternalEnergy uses, with R_mix
        substituted for the single-species R

Single-species h_mix matches P15 EXACTLY (bitwise) at 300, 500 and 1500 K -- so the mixture does not
re-base P15's reference conventions.
```

```text
NO HEAT RELEASE AND NO FORMATION ENTHALPY. P15's referenceEnthalpy is a SENSIBLE offset; the P18.0
audit warns specifically against repurposing it as an h_f, and P18.3 does not. Reaction enthalpy is
P18.6's.
```

## 7. Composition sensitivity — §14

A 41-point sweep of `Y_A` from 0 to 1:

```text
ASSERTED MONOTONE, because it is DERIVED:
  R_mix     linear in Y_A (R_mix = Y_A R_A + Y_B R_B), so strictly increasing
  rho       = p/(R_mix T), so strictly decreasing
NOT ASSERTED MONOTONE, deliberately:
  gamma_mix is a ratio of two affine functions of Y. Section 14 says not to impose monotonicity that
  has not been derived, so only gamma > 1 is asserted.
ENDPOINTS  bitwise equal to the pure species values.
```
