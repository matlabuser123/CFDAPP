# P18.5 — temperature sensitivity (§12–§14, §34, §35)

```text
WHAT IS BEING CHECKED: that k(T) has the right SHAPE, not merely the right value at one point.
A rate that matches at 1500 K and has the wrong temperature dependence is the most damaging kind
of chemistry error, because every steady check passes and the coupled behaviour is wrong.
```

---

## 1. The range actually exercised

`logs/04_probe.log`, 300 K to 3000 K:

| T [K] | `x = -Ea/(R_u T)` | `k(T)` |
|---:|---:|---:|
| 300 | -50.113481267802517 | 4.4734594277900097e-13 |
| 500 | -30.068088760681508 | 0.00029320490484950899 |
| 800 | -18.792555475425946 | 29.250487507690035 |
| 1200 | -12.528370316950630 | 18822.607500755610 |
| 1500 | -10.022696253560504 | 257831.04680477042 |
| 1800 | -8.3522468779670866 | 1501041.7815468791 |
| 2500 | -6.0136177521363026 | 18339194.535154287 |
| 3000 | -5.0113481267802520 | 54733226.208860584 |

**k spans 1.22e20 — twenty orders of magnitude — over the range.** `TheRateSpansThirteenOrders...`
asserts at least thirteen; the measured span is far wider. Every point is within 32 eps of mpmath.

## 2. Monotonicity, and where it legitimately fails

```text
d(ln k)/dT = n/T + Ea/(R_u T^2)

For Ea >= 0 and n >= 0 this is strictly positive, so k is strictly increasing. ASSERTED by
RateIncreasesWithTemperatureForPositiveEaAndZeroN.

FOR NEGATIVE n IT IS NOT. n/T + Ea/(R_u T^2) = 0 at T = -Ea/(n R_u), which for n < 0 and Ea > 0 is
a positive temperature -- a genuine interior maximum. ANegativeExponentCanMakeTheRateNonMonotone
asserts the rate is non-monotone there rather than asserting a monotonicity that does not hold.

THIS MATTERS because a test asserting "k always increases with T" would have been WRONG, would
have passed for the reference parameters, and would have failed the first time anyone used a
negative exponent -- which the API explicitly allows and which is ordinary in real mechanisms.
```

## 3. dk/dT — two independent routes

The closed form `dk/dT = k [ n/T + Ea/(R_u T^2) ]` is checked against a **central finite
difference of `rateCoefficient()` itself**, so the two routes share no arithmetic beyond `k`.

```text
THE STEP AND THE BOUND ARE DERIVED, NOT FITTED. A central difference carries
    truncation ~ h^2 |f'''| / 6        round-off ~ eps |f| / h
which are minimised together near h ~ T eps^(1/3), and the floor on the RELATIVE agreement is then

    eps^(2/3) = 3.6668528625010315e-11

THE FIRST ATTEMPT GUESSED h = T * 1e-5 WITH A 1e-9 BOUND and measured 8.45e-9 -- it would have
passed, with a bound that meant nothing. Scanning h OUTSIDE CFDApp found the optimum at
h = T * 1e-6, giving 3.8e-11 to 1.27e-10, which is eps^(2/3) to within a small factor. The bound
was then set to 8 * eps^(2/3) -- the dimensional floor times a small integer, not the observation.
```

Measured (`logs/04_probe.log`), `h = T * 1e-6`:

| T [K] | closed form | central difference | relative | × eps^(2/3) |
|---:|---:|---:|---:|---:|
| 400 | 1.357719954326285e-08 | 1.3577199546612594e-08 | 2.467e-10 | **6.73** |
| 600 | 0.0020536049740331667 | 0.0020536049739547368 | 3.819e-11 | 1.04 |
| 1000 | 21.785788623290543 | 21.785788623446933 | 7.179e-12 | 0.20 |
| 1500 | 1808.7185268427604 | 1808.7185269784338 | 7.501e-11 | 2.05 |
| 2000 | 14621.370744253574 | 14621.370743960142 | 2.007e-11 | 0.55 |
| 2400 | 39418.498500355643 | 39418.498495360836 | 1.267e-10 | 3.46 |
| 3000 | 100551.28791294787 | 100551.28791928291 | 6.300e-11 | 1.72 |

**Worst 6.73 × eps^(2/3), bound 8.** The margin is 1.19× — tight, and deliberately so: a bound
derived from the dimensional floor should sit just above the measurement, or it is not measuring
the floor. The worst point is the coldest, where `|f'''/f|` is largest.

`dk/dT` is **also** compared against mpmath directly at 600, 1500 and 2400 K
(`AnalyticalDerivativeMatchesTheIndependentReference`), so the closed form is validated
independently as well as cross-checked.

## 4. Parameter sensitivity — each parameter moved alone

| what moves | analytical ratio | asserted by |
|---|---|---|
| `A` → `c A` | `k` scales by exactly `c` | `RateScalesLinearlyWithThePreExponentialFactor` |
| `Ea` → `Ea + dEa` | `exp(-dEa/(R_u T))` | `HigherActivationEnergyReducesTheRateByTheAnalyticalRatio` |
| `n` → `n + dn` | `T^dn` | `ExponentSensitivityFollowsThePowerLawRatio` |

Each asserts the **analytical ratio**, not merely the direction of change. A test asserting only
"a higher Ea gives a lower rate" would pass for an implementation off by any positive factor.

## 5. The Arrhenius plot (§35)

See `analytical_reference.md` §7: slope recovered to 1.09 eps, intercept **bitwise**. This is a
consistency check on the functional form — it re-derives the same closed form — and is labelled as
such rather than as validation.
