# P18.3 — negative controls (§28)

```text
All eight are PERMANENT TESTS, not source mutations. Every defect §28 names is expressible through
the production API or as an alternative arithmetic expression evaluated alongside the correct one, so
each control runs in every regression and cannot rot.

Each control reports its MARGIN -- how far the wrong answer is from the right one -- so "detected" is
quantified rather than asserted. logs/03_closure_probe.log.
```

---

## 1. The eight

| # | Defect | Margin measured | Result |
|---|---|---|---|
| **N1** | arithmetic-average molecular weight | wrong / correct = **5.9690×** | **DETECTED** |
| **N2** | `R_mix` left constant | `R(pure H2)/R(pure CO2)` = **21.83×** | **DETECTED** |
| **N3** | `cp_mix` wrong weighting basis | simple mean / mass-weighted = **1.7995×** | **DETECTED** |
| **N4** | `cv_mix` computed inconsistently | closure vs species sum agree to **1.75e-16** | **DETECTED** |
| **N5** | `gamma_mix` averaged directly | 1.346709 vs 1.397972 = **3.67% error** | **DETECTED** |
| **N6** | density still uses single-species `R` | `rho(CO2)/rho(H2)` = **21.83×** at identical p, T | **DETECTED** |
| **N7** | invalid composition accepted | refused by P18.1's type, 3 ways | **DETECTED** |
| **N8** | single-species P17 result changes | **0** bitwise mismatches, 5T × 3p × 5 quantities | **DETECTED** |

## 2. N1 — and the trap that makes it easy to write a vacuous version

```text
WRONG   M = sum_i Y_i M_i         (the mole-fraction formula misapplied)
RIGHT   1/M = sum_i Y_i / M_i

50/50 H2/CO2:   correct 3.855393e-03   wrong 2.301300e-02   ratio 5.9690x
```

```text
THE TRAP, and it is recorded as its own test: NEAR A PURE SPECIES BOTH FORMULAS AGREE.
For 1e-6 CO2 in H2 the reference gives a ratio of 1.0000. A control built on a near-pure composition
would PASS while detecting nothing at all.

N1 therefore deliberately uses 50/50, and a companion test
(N1_IsVacuousNearAPureSpeciesAndThatIsWhyItUsesAMixture) asserts the near-pure agreement explicitly
so the reason for the choice is in the suite rather than only in this document.
```

The species were chosen for this: **H2 and CO2 differ by 21.8× in molecular weight**, so the error
cannot be mistaken for round-off.

## 3. N4 — a control whose criterion is AGREEMENT, not disagreement

```text
cv_mix has two expressions:  cp_mix - R_mix   and   sum_i Y_i cv_i
They are ALGEBRAICALLY IDENTICAL (thermodynamic_closure.md section 3), so they must agree to
round-off -- measured at 1.745889e-16.

So N4 detects its defect by the check FAILING if either weighting were wrong, not by the two values
differing. The defect §28 names -- "cv calculated independently/inconsistently" -- is structurally
impossible here: cv is never stored, it is derived by ThermodynamicProperties from cp and R. The
cross-check is what proves that derivation matches the independent species sum.
```

## 4. N5 — `gamma` must emerge, never be averaged

```text
correct (from cp and cv)          1.397972
wrong (mass-weighted gamma_i)     1.346709        3.67% error
```

3.67% is ~1.6e13 times the 8-eps closure allowance, so the criterion cannot fail to discriminate.
`gamma` is not stored anywhere in the mixture path — `ThermodynamicProperties` computes `cp/cv` — so
the averaged form can only be produced deliberately, which is what the test does alongside the
correct one.

## 5. N2 and N6 — composition must reach the EOS

```text
Identical p = 101325 Pa and T = 300 K, four compositions:
    rho(pure CO2) / rho(pure H2) = 21.8304x
and every rho was checked to equal p/(R_mix T) computed from the REPORTED R_mix, so density is
provably routed through the composition rather than through a constant.

Section 18: "A mixture API that calculates R_mix but leaves density using the old constant R is
incomplete." That is exactly what these two controls would catch, and the 21.83x margin is the full
molecular-weight ratio.

N2 additionally asserts a DERIVED property rather than only a spread: R_mix = sum Y_i R_i is LINEAR
in Y, so the 50/50 value must be the exact arithmetic mean of the two pure values -- checked to the
closure tolerance. A constant-R substitution would make all three equal; they are not.
```

## 6. N7 — the invalid composition cannot reach the mixture at all

```text
The mixture takes its per-cell composition from a P18.1 MassFractionState, which REFUSES an invalid
composition at construction. Three rejections asserted:
    sum = 0.9              refused
    {-0.1, 1.1}            refused (out of bounds, sum EXACTLY 1, so only the bounds check can fire)
    {NaN, 1.0}             refused

So an invalid composition cannot reach the property evaluation through the typed path. That is
STRONGER than checking one, and it is why section 20's "do not duplicate P18.1 validation logic" is
satisfiable rather than a compromise.

AND NOTHING IS RENORMALISED: a valid composition comes back out of the state untouched, asserted
bitwise (0.25 and 0.75 exactly).
```

## 7. N8 — the control G5 names first

```text
"a mixture of one species that does NOT reproduce P17's value bitwise must FAIL"

Checked over a MATRIX -- 5 temperatures x 3 pressures x 5 quantities plus the two EOS inverses --
rather than at one point, because a single point could agree by luck.
Result: 0 mismatches.

The comparison object is built directly the way P17 builds it, from (R_1, cp_1). It is not a
recorded number, so the control cannot pass because a stale constant was transcribed from the
implementation.
```

## 8. Why no source-mutation harness this time

```text
P18.1 used a source-mutation harness (mutate, rebuild, verify detection, restore under sha256)
because its invariants were unconditional checks with no API surface to disable.

P18.3's defects are all ARITHMETIC ALTERNATIVES -- a different averaging formula, a constant instead
of a function, an averaged ratio instead of a derived one. Every one can be written as an expression
in the test alongside the correct one and compared, which is strictly better for durability: the
control runs in every regression, needs no restore step, and cannot leave production source mutated.

The one defect class that is NOT an arithmetic alternative -- accepting an invalid composition (N7) --
is prevented by a type, and the control asserts the refusal.
```
