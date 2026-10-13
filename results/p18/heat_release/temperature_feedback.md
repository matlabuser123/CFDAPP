# P18.6 — temperature feedback into the kinetics (§18, §20, §21, §22)

```text
THE LOOP THIS PHASE EXISTS TO CLOSE:

    reaction  ->  heat release  ->  T rises  ->  k(T) rises  ->  next reaction

P18.5 READ a temperature and never wrote one; the dependency was strictly one-way. P18.6 makes it
a loop, and section 21 is explicit about the purpose: "The purpose is simply to prove temperature
feedback reaches the kinetics path." NO RUNAWAY OR IGNITION IS ASSERTED.
```

---

## 1. Exothermic feedback — measured against the analytical prediction (§20, §21)

`N3_HeatReleaseRaisesTheRateAtTheNextEvaluation`. Arrhenius `A = 1e4`, `n = 0`,
`Ea = 6e4 J/mol`; `A->B`, exothermic; `T_0 = 1200 K`, one explicit step at `dt = 1e-2`.

```text
  step 2   q evaluated ONCE at (T^n, Y^n, rho^n)                  q_0 > 0
  steps 3-7 species and energy advanced, T recovered from Y^{n+1}
  step 8   q re-evaluated at T^{n+1}
```

| quantity | result |
|---|---|
| `T_0 -> T_1` | **rises** (asserted `T_1 > T_0`) |
| `k(T_0) -> k(T_1)` | **rises** (asserted `k_1 > k_0`) |
| measured `k_1/k_0` vs `exp(-(Ea/R)(1/T_1 - 1/T_0))` | agree within **32 eps** |

```text
THE QUANTITATIVE COMPARISON IS MADE ON k, NOT ON q, AND THE REASON MATTERS.

q = k * [A], and the concentration FELL as A was consumed while k rose. So q is the product of two
opposing effects and its direction is not predictable from Ea alone. Asserting "q increased" would
have been asserting something that need not be true, and would have failed or passed for reasons
unrelated to the feedback.

k IS where the prediction is unambiguous: for Ea > 0 and n = 0 the rate coefficient is strictly
increasing in T, and the RATIO has a closed form. Comparing against that closed form makes this a
quantitative verification rather than a direction check.
```

## 2. The stale-temperature control (N3)

```text
THE DEFECT: re-evaluate the next step's rates at T^n instead of T^{n+1}.
```

**Measured**: a deliberately stale evaluation differs from the correct one by **> 0.1 %** of the
rate — three orders of magnitude above round-off, so the control discriminates rather than
chasing noise. The test asserts both `EXPECT_NE` on the bitwise values and the relative gap.

```text
WHY THE DEFECT IS NOT REPRESENTABLE IN PRODUCTION: P18.5's arrheniusProgressRates takes the
temperature field as an ARGUMENT. P18.6 adds no cache, no stored temperature and no carried state,
so there is nowhere for a stale temperature to live -- the caller necessarily passes whichever
field it holds. The control shows what the mistake would cost if a caller made it.
```

## 3. Endothermic feedback (§22)

```text
Section 22 exists to catch "a one-sided or hard-coded exothermic implementation" -- code that
heats correctly and is silently wrong in the other direction.
```

`EndothermicFeedbackLowersTheRate`: `A->C`, endothermic (`Delta H_r^0 = +187 500 J/mol`),
`T_0 = 1800 K`.

| quantity | result |
|---|---|
| `T_0 -> T_1` | **falls** (asserted `T_1 < T_0`) |
| `k(T_1)` vs `k(T_0)` | **falls** |
| predicted ratio `exp(-(Ea/R)(1/T_1 - 1/T_0))` | **< 1**, and measured agrees within 32 eps |

```text
THE STEP SIZE IS DERIVED FROM THE RATE, NOT GUESSED, and the first version got this wrong:

    dt = 1e-2  would have removed 1.815 kg/m^3 of A from a cell holding 1.0 kg/m^3

and the production guard refused the step -- correctly. The failure is preserved at
logs/03_four_failures_PRESERVED.log. The test now computes

    dt = 0.1 * rho Y_A / |omega_A|

so the step is inside the explicit stability limit by construction, with margin, while still
moving T measurably. That is sizing the test to the physics rather than tuning a number until it
passed.
```

## 4. What is NOT claimed

```text
NO IGNITION, NO RUNAWAY, NO FLAME. Section 21 says "Do not assert runaway or ignition behaviour
unless explicitly validated", and none is. A single controlled step in each direction is what is
measured and all that is claimed.

NO IMPLICIT COUPLING. The loop is explicit and operator-split (coupling_order.md). A stiff
exothermic system would need sub-stepping P18.6 does not have, and the step-size limit is
documented rather than worked around.

NO REACTING-SHOCK BEHAVIOUR (section 46). The feedback is coupled to the P15 THERMAL path only;
P17's compressible TotalEnergyState still refuses a temperature-dependent cp and P18.6 does not
lift that refusal. See audit.md section 1.
```
