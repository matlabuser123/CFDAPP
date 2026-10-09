# P18.3 — thermodynamic closure (§8) — the principal G5 criterion

```text
G5 acceptance: each identity to <= 8 * eps * scale, with scale the magnitude of the quantity.
INHERITED VERBATIM from the frozen gate. 8 * eps = 1.7763568394002505e-15.
Measured over 9 compositions x 5 temperatures x 3 pressures -- logs/03_closure_probe.log.
```

---

## 1. Measured

| Identity | Max relative error | In eps | Allowance |
|---|---|---:|---|
| `cp − cv = R` | **2.207671e-16** | 0.99 | 8 eps |
| `gamma = cp / cv` | **0.000000e+00** | 0 | 8 eps |
| `R_mix` — the two formulations | **2.205243e-16** | 0.99 | 8 eps |
| `cv_mix` — closure vs `Σ Y_i cv_i` | **1.745889e-16** | 0.79 | 8 eps |
| single-species `M` recovery | **1.238495e-16** | 0.56 | 8 eps |
| EOS `rho` vs `p/(R T)` | **0.000000e+00** | 0 | 8 eps |
| EOS `p` round trip | **2.872325e-16** | 1.29 | 8 eps |
| EOS `T` round trip | **3.031649e-16** | 1.37 | 8 eps |

```text
TWO OF THESE ARE EXACTLY ZERO, AND THAT IS ARCHITECTURAL RATHER THAN LUCKY:

  gamma = cp/cv   is exactly 0 because gamma is NOT STORED. ThermodynamicProperties computes
                  cp/cv once at construction and returns it, so the "identity" is the definition.
                  There is no second path that could disagree.
  rho = p/(R T)   is exactly 0 because density comes from the SAME IdealGasEOS the check compares
                  against, reached with R_mix. There is no separate mixture density formula.

Both zeros are therefore evidence that NO DUPLICATE CLOSURE EXISTS -- which is what sections 6, 7
and 9 ask for -- rather than evidence that the arithmetic is unusually accurate.
```

## 2. The matrix covered (§8, §26)

```text
pure species          3 -- H2, CO2, N2, each at Y_i = 1 with the others exactly 0
binary mixtures       50/50 and 25/75 H2/CO2
ternary mixtures      0.2/0.3/0.5 and 1/3 each
near-pure             1 - 2e-9 H2 with 1e-9 of each other species
equal mixtures        1/3 each
temperatures          200, 300, 700, 1500, 3000 K
pressures             1000, 101325, 5e6 Pa
temperature-dependent a separate two-term cp(T) = a0 + a1 T case, verified at 300/600/1200/2400 K,
                      with the closure re-checked at every temperature
```

The species were chosen so a wrong averaging basis is unmistakable: **H2 and CO2 differ by 21.8× in
molecular weight and 17.0× in cp.** At every point `cv > 0` and `gamma > 1` were asserted, not
assumed — an inadmissible ideal-gas state is refused rather than reported.

## 3. Why `cv_mix` has two forms, and why they must AGREE

§6 asks for `cv_mix = cp_mix − R_mix` to be verified independently against `Σ Y_i cv_i`. The two are
**algebraically identical**, and saying so is part of the evidence:

```text
    sum_i Y_i cv_i = sum_i Y_i (cp_i - R_i) = sum_i Y_i cp_i - sum_i Y_i R_i = cp_mix - R_mix
```

So they must agree to round-off — measured at 1.745889e-16 — and the check is a **consistency** test,
not a physics test. A disagreement beyond round-off would mean one of the weightings is wrong, which
is exactly what controls N3 and N4 guard. Labelled as consistency per CLAUDE.md §4, which forbids
calling self-consistency validation.

## 4. Independent reference (§27)

Every expected value in the tests was computed **outside CFDApp**, with Python's `decimal` module at
50 significant digits, and embedded as a literal. The generator and its full output are preserved at
`data/reference_values.txt`. No production mixture-property function was used to build any reference.

```text
binary 50/50 H2/CO2      M_mix 0.003855393038717247   R_mix 2156.579766227829
                         cp_mix 7575.5                cv_mix 5418.920233772171
                         gamma 1.3979722293728265     rho(101325,300) 0.15661372942897156
binary 25/75             M_mix 0.007089708737864077   R_mix 1172.7509444426266
ternary 0.2/0.3/0.5      M_mix 0.008072876581236042   R_mix 1029.9256447792998
```

## 5. What the closure does NOT establish

```text
It is ALGEBRA, evaluated in double precision. It establishes that the mixture relations are
implemented correctly and consistently. It does NOT establish that the ideal-gas mixture model is
the right physics for any particular gas -- that is a modelling question, and for a real mixture at
high pressure the answer is no. P18.3 claims an IDEAL-GAS mixture and nothing more.

NO REACTING RESULT IS CLAIMED. G5 is the closure gate; G6, G7 and G8 are untouched.
```
