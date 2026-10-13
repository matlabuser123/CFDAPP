# P18.3 — formulation (§2–§9)

```text
FROZEN BEFORE IMPLEMENTATION AND BEFORE ANY MEASUREMENT OF THE IMPLEMENTATION.
The one measurement that preceded it -- the floating-point behaviour of three algebraically
equivalent routes to R_mix -- is recorded in audit.md §3 and is a property of IEEE doubles, not of
any P18.3 code.
```

---

## 1. Authoritative species thermodynamic data (§2)

The **smallest physically correct set**, with everything else derived:

```text
STORED        M_i     molecular weight            [kg / mol]
              cp_i(T) as a P15 SensibleEnthalpy, which carries the cp(T) model, T_ref and h_ref

DERIVED       R_i      = R_u / M_i                [J / (kg K)]
              cv_i(T)  = cp_i(T) - R_i
              gamma_i(T) = cp_i(T) / cv_i(T)
              h_i(T)   = the integral P15 already performs, NOT cp_i(T) * T
```

`R_i` is derived rather than stored (§2's stated preference), so `R_i` and `M_i` cannot drift apart.
`gamma_i` is never stored or interpolated (§7).

```text
R_u = 8.31446261815324 J/(mol K)     CODATA 2018, exact by the 2019 SI redefinition.
      Placed with the species thermodynamic data, NOT in Constants.hpp, which deliberately holds no
      tunable physical constants (audit.md F1).
```

**Rejected at construction** (§2, §21), with the offending species named:

```text
M_i <= 0 or non-finite · cp_i(T) <= 0 or non-finite anywhere it is evaluated
cv_i(T) <= 0  -- which is gamma_i <= 1, and is rejected as such
```

## 2. Mixture molecular weight (§3)

```text
    1 / M_mix = sum_i ( Y_i / M_i )        therefore     M_mix = 1 / sum_i ( Y_i / M_i )
```

This is the **mass-fraction** (harmonic) relation. The arithmetic form `M = Σ Y_i M_i` is the
mole-fraction formula misapplied and is **wrong**; control N1 exists to detect it and uses species
whose molecular weights differ by a factor of ~22 (H₂ against CO₂) so the error is unmistakable.

```text
SINGLE-SPECIES LIMIT: M_mix = 1/(1/M_1). audit.md §3(a) measured that 1/(1/x) is NOT bitwise for
17.198% of random doubles, so BITWISE RECOVERY OF M_1 IS NOT CLAIMED. It is measured and reported.
P17 has no molecular weight, so G5's bitwise clause -- which binds "P17's existing single-species
value" -- does not reach M.
```

## 3. Mixture gas constant (§4) — and the formulation the bitwise gate forces

```text
    R_mix = sum_i Y_i R_i                 <-- THE COMPUTED FORM
    R_mix = R_u / M_mix                   <-- the equivalent form, CROSS-CHECKED against it
```

Both are exact for an ideal-gas mixture. The first is computed because it is the only one that is
**bitwise exact in the single-species limit**: at `Y_1 = 1` it is `1.0 * R_1 = R_1`, and
multiplication by exactly 1.0 is exact. `R_u * Σ(Y_i/M_i)` is not — measured to fail for oxygen
(audit.md §3(b)).

The two forms are verified against each other and the **maximum closure error is reported** (§4).
Derived bound: `8 * eps * R_mix`, the G5 allowance for an algebraic identity in double precision.

## 4. Mixture cp (§5)

```text
    cp_mix(T, Y) = sum_i Y_i cp_i(T)
```

**Mass-weighted**, because `cp_i` is mass-based `[J/(kg K)]`. Multiplying by mole fractions would be
a basis error; control N3 detects the wrong weighting. `cp_i(T)` comes from P15's existing
temperature-dependent property — no new polynomial logic (§5).

```text
SINGLE-SPECIES LIMIT: 1.0 * cp_1(T) = cp_1(T) BITWISE.
```

## 5. Mixture cv, gamma, and the EOS — all inherited, none re-implemented (§6, §7, §9)

```text
    cv_mix    = cp_mix - R_mix
    gamma_mix = cp_mix / cv_mix
    rho       = p / (R_mix T)      and its inverses p(rho,T), T(p,rho)
```

**None of these is implemented by P18.3.** The mixture layer constructs the existing
`cfd::compressible::ThermodynamicProperties(R_mix, cp_mix)`, which already derives `cv` and `gamma`
at construction and already owns an `IdealGasEOS`. Consequences, and they are the reason the phase
is small:

```text
* sections 6 and 7 are satisfied BY CONSTRUCTION -- cp_mix, cv_mix and R_mix cannot drift apart,
  because only two of them are ever stored and the third is derived from those two.
* gamma_mix EMERGES from cp and cv (§7) and is never interpolated from gamma_i.
* section 9 is satisfied -- no separate MixtureEOS exists; IdealGasEOS is reached with a
  composition-dependent R.
* THE SINGLE-SPECIES LIMIT IS BITWISE FOR cv, gamma AND DENSITY AUTOMATICALLY: if R_mix and cp_mix
  are bitwise R_1 and cp_1, the constructed object is bit-identical to the one P17 builds, so every
  quantity it derives is too. That is not a coincidence to be tested for -- it is why this
  architecture was chosen.
```

## 6. Enthalpy and internal energy (§16)

```text
    h_mix(T, Y) = sum_i Y_i h_i(T)        each h_i from P15's integrated SensibleEnthalpy
    e_mix(T, Y) = h_mix - R_mix T
```

`e = h - RT` is the same relation `TotalEnergyState::specificInternalEnergy` already uses, so the
mixture form is that relation with `R_mix` substituted. P15's reference conventions (`T_ref`,
`h_ref`) are preserved per species and not re-based (§15).

```text
NO REACTION HEAT RELEASE, and no formation enthalpy. h_f,i is what P18.6 needs and it is NOT this:
P15's referenceEnthalpy is a SENSIBLE offset, and the P18.0 audit warns specifically against
repurposing it as a heat of formation. P18.3 adds neither.
```

## 7. Units, stated once

```text
M_i, M_mix       [kg / mol]
R_u              [J / (mol K)]
R_i, R_mix       [J / (kg K)]
cp, cv           [J / (kg K)]
gamma            [-]
h, e             [J / kg]
T                [K]
p                [Pa], ABSOLUTE
rho              [kg / m^3]
Y_i              [-]
```

## 8. Composition validity (§20)

Every mixture-property evaluation takes a **P18.1 `MassFractionState`**, which has already enforced
finiteness, `0 ≤ Y_i ≤ 1` and `|ΣY_i − 1| ≤ 4·eps·N` at construction. P18.3 therefore:

```text
* duplicates NO validation logic (§20's second clause);
* performs NO renormalisation, silent or otherwise (§20's first clause) -- there is no
  normalisation call anywhere in the new code;
* cannot be handed an invalid composition through the typed API at all, which is stronger than
  checking one.
```

## 9. Derived thresholds, all before measurement

```text
identity closure       |lhs - rhs| <= 8 * eps * |scale|, with scale the magnitude of the quantity.
                       INHERITED VERBATIM from G5's acceptance clause. Not chosen here.
single-species limit   BITWISE for R, cp, cv, gamma, density -- G5's own clause, and achievable
                       because of the formulation choice in §3.
                       MEASURED AND REPORTED, not claimed bitwise, for M_mix -- see §2.
EOS round trip         |x_recovered - x| <= 8 * eps * |x| for each of rho, p, T (§19).
                       Same 8-eps algebraic allowance; each round trip is two divisions.
```

## 10. Deliberately not in this formulation

```text
reactions, stoichiometry, Arrhenius, omega_i, heat release    P18.4-P18.6 (§23)
real-gas or non-ideal EOS                                     out of scope; ideal gas only
mixture-averaged TRANSPORT properties (D_i, mu, k)            not thermodynamic closure; P18.2 takes
                                                              D_i as given and P18.3 does not change that
Soret / Dufour                                                absent
the TotalEnergyState cp(T) refusal                            NOT deleted. See audit.md §5 and
                                                              single_species_compatibility.md for the
                                                              named extension point.
any change to P18.2 transport                                 §24 -- none made
```
