# P18.3 — mixture molecular weight and gas constant (§3, §4)

```text
1 / M_mix = sum_i ( Y_i / M_i )            the MASS-FRACTION (harmonic) relation
R_mix     = sum_i Y_i R_i                  the COMPUTED form,  R_i = R_u / M_i
R_mix     = R_u / M_mix                    the equivalent form, CROSS-CHECKED against it
R_u       = 8.31446261815324 J/(mol K)     CODATA 2018, exact by the 2019 SI redefinition
```

---

## 1. Why molecular weight is new here

```text
0 occurrences of molecularWeight or molarMass in the repository before P18.3. P17's IdealGasEOS
holds ONE gasConstant_ and no molar information at all, so both M_i and R_u are new authoritative
data. R_i is DERIVED from M_i and never stored beside it (section 2's stated preference), so the two
cannot drift apart.
```

## 2. Measured against the independent reference

Reference values computed with Python `decimal` at 50 digits, outside CFDApp
(`data/reference_values.txt`):

| Case | `M_mix` | `R_mix` |
|---|---|---|
| binary 50/50 H2/CO2 | 0.003855393038717247 | 2156.579766227829 |
| binary 25/75 H2/CO2 | 0.007089708737864077 | 1172.7509444426266 |
| ternary 0.2/0.3/0.5 | 0.008072876581236042 | 1029.9256447792998 |
| pure H2 | 0.002016 | 4124.237409798234 |
| pure CO2 | 0.04401 | 188.9221226574242 |
| pure N2 | 0.0280134 | 296.80305204485137 |

All within the 8·eps closure allowance. Worst single-species `M` recovery: **1.238495e-16** relative.

## 3. The two `R_mix` formulations agree — §4

```text
max relative closure error over 10 compositions x 5 temperatures : 2.205243e-16   (0.99 eps)
```

Both are exact for an ideal-gas mixture, and they are computed by genuinely different routes — one
sums `Y_i R_i`, the other divides `R_u` by a reciprocal sum. Agreement to round-off is therefore
evidence; disagreement beyond it would mean one is wrong. `gasConstantFromMolecularWeight()` exists
**only** so this check can be made rather than assumed, and nothing else in the codebase calls it.

## 4. THE FORMULATION CHOICE, forced by measurement

```text
R_mix = sum_i Y_i R_i   AND NOT   R_u * sum_i(Y_i / M_i)

Both are algebraically identical. Only the first is BITWISE exact in the single-species limit, which
G5 requires. Measured before any code was written (audit.md section 3):

    R_u * (1/M) == R_u / M   FALSE for M = 0.032 kg/mol -- OXYGEN
    1 / (1 / x)  == x        FALSE for 34397/200000 = 17.198% of random doubles

At Y_1 = 1, sum_i Y_i R_i is 1.0 * R_1 = R_1 exactly, because multiplication by exactly 1.0 is exact
-- the property G5's own note relies on.

NOTE ON HOW NEARLY THIS WAS MISSED: air (0.0289647), CO2 (0.04401), H2 (0.002016) and CH4 (0.016043)
ALL round-trip fine through R_u*(1/M). A test using only those species would have passed while the
formulation was wrong for oxygen. The failure was found by sampling, not by trying a species list.
```

## 5. Composition symmetry (§3)

`MolecularWeightIsCompositionSymmetric` builds the mixture with the species **and** their mass
fractions swapped and requires the identical `M_mix` — bitwise. This catches an index/ordering error
that a single-ordering test cannot see.

## 6. Control N1 — and why it uses 50/50 rather than a near-pure mixture

```text
WRONG  M = sum Y_i M_i           RIGHT  1/M = sum Y_i/M_i
50/50 H2/CO2:  correct 3.855393e-03   wrong 2.301300e-02   ratio 5.9690x

NEAR-PURE IS VACUOUS: for 1e-6 CO2 in H2 the two formulas agree to a ratio of 1.0000, so a control
built there would pass while detecting nothing. That is asserted as its own test, so the reason for
the 50/50 choice lives in the suite and not only in this document.
```

## 7. Rejected inputs (§2, §21)

```text
M_i <= 0, non-finite M_i                      refused at SpeciesThermodynamics construction
sum_i Y_i / M_i not finite or not > 0         refused -- an all-zero composition cannot define a
                                              molecular weight, and returning inf would be worse
composition length != registry size           refused
```

An all-zero composition is unreachable through `MassFractionState`, but the free function still
refuses it rather than returning `inf`.
