# P18.5 — negative controls (§39)

```text
ALL TEN ARE PERMANENT TESTS, not source mutations. Every defect §39 names is expressible either
through the production API or as an alternative arithmetic expression evaluated alongside the
correct one, so each control runs in every regression and cannot rot.

EACH REPORTS ITS MARGIN, so "detected" is quantified rather than asserted. Measured at
logs/04_probe.log; the assertions live in tests/unit/species/test_arrhenius_kinetics.cpp.

TWO MORE CONTROLS ARE CARRIED FOR AMENDMENT A1 (NV1, NV2) and five for the comparison instrument
itself (logs/06_comparator_nonvacuity.log). A gate is only as good as the instrument reading it.
```

---

## 1. The ten §39 controls

| # | Defect | Margin measured | Result |
|---|---|---|---|
| **N1** | flip the Arrhenius exponent sign | **5.08e8×** | **DETECTED** |
| **N2** | omit the activation-energy term | **2.25e4×**, and the Arrhenius slope goes to exactly 0 | **DETECTED** |
| **N3** | omit the `T^n` term | **38.7×** (`sqrt(1500)`) | **DETECTED** |
| **N4** | use `R` in J/(kmol K), ×1000 | **3.33e6×** | **DETECTED** |
| **N5** | ignore `A` | **1.50e8×** (exactly `A`) | **DETECTED** |
| **N6** | use a global `T` instead of the cell's | cell-to-cell ratio **7.12e5×**, would be exactly 1 | **DETECTED** |
| **N7** | omit the concentration dependence | `q(2 rho)/q(rho) = 8` **exactly** (`2^3`, total order 3) | **DETECTED** |
| **N8** | map kinetics to the wrong reaction | binding is **by name**; reversed input still binds correctly | **UNREPRESENTABLE** |
| **N9** | bypass P18.4's source conversion | P18.5 exposes **no** `omega_i` API | **UNREPRESENTABLE** |
| **N10** | accept an invalid/non-finite temperature | refused, 5 values × 3 entry points | **DETECTED** |

## 2. N1 — G6's own first control

G6 names it directly: *"Arrhenius exponent sign flipped must FAIL."*

```text
At T = 1500 K, from the mpmath reference (data/reference_values.txt):
    correct   exp(-Ea/(R_u T)) = 4.4381126684893403e-5
    flipped   exp(+Ea/(R_u T)) = 22532.100347519644
    ratio                        5.076955e+08

HALF A BILLION. There is no threshold at which this is ambiguous.
```

The control is **also structural**: `exponentialArgument()` is exposed precisely so the *sign* can
be asserted directly rather than inferred from `k`, and `exponentDiagnostics()` reports
`anyPositive` over a field. `N1_FlippingTheExponentSignIsDetected` asserts `x < 0`,
`!diagnostics.anyPositive`, and the two reference values.

## 3. N2 — and the distinction that matters

G6's second control: *"activation energy set to 0 must FAIL (removes the T dependence entirely)."*

```text
THE SAME PARAMETER VALUE IS A LEGITIMATE REDUCED CASE AND A DEFECT, depending on what is being
claimed, and the two are kept apart deliberately:

    ZeroActivationEnergyReducesToThePowerLaw     Ea = 0 is the INPUT; k = A T^n is CORRECT
    N2_OmittingTheActivationEnergyTermIsDetected Ea = 1.25e5 is the input; dropping the term
                                                 from the evaluation is a DEFECT, off by 2.25e4x

AND THE SIGNATURE G6 POINTS AT IS MEASURED: with n = 0, the Arrhenius slope d(ln k)/d(1/T) is
    correct    -15034.044380340756 = -Ea/R_u
    defective   0.0  EXACTLY
so the defect does not merely shift k, it removes the temperature dependence -- which is the
thing G6 says to detect.
```

## 4. N4 — the mol/kmol control

§33 calls this *"one of the most damaging silent chemistry errors"*, and it is: the result is
plausible, finite, and wrong by millions.

```text
At T = 1000 K, Ea = 1.25e5 J/mol (data/reference_values.txt):
    correct, R_u in J/(mol K)    2.9566334426841137e-07
    R 1000x too large            0.98507840264838826
    ratio                        3.331757e+06

The wrong value is ~1, i.e. "no activation barrier at all" -- a mechanism that would appear to run
instantly everywhere. One system of units is used throughout and R_u comes from P18.3 rather than
a local copy, which the control asserts: kUniversalGasConstant == 8.31446261815324 exactly.
```

## 5. N6 — the defect a correct-looking cached optimisation would introduce

Caching one rate coefficient per reaction is the obvious optimisation and it is **wrong** for a
spatially varying temperature field. The control uses a **uniform composition** so temperature is
the only varying input:

```text
8 cells, T = 800 .. 2550 K, same Y_i everywhere:
    q[last] / q[first] = 7.124537e+05
    a cached global coefficient would make this ratio EXACTLY 1
```

and each cell is additionally compared against a direct single-point evaluation at **its own**
temperature, within 32 eps. The companion `SpatiallyVaryingCompositionGivesVaryingRates` holds `T`
uniform and varies composition instead, so the two dimensions are separated.

## 6. N7 — exactly 8, and why that is the sharp form

```text
Total reactant order for 2 H2 + O2 is nu'_H2 + nu'_O2 = 3. Doubling rho doubles every C_i, so

    q(2 rho) / q(rho) = 2^3 = 8   EXACTLY, bitwise

and it measures exactly 8. This is bitwise rather than approximate because 2 is a power of two:
multiplying a double by 2 is exact, and so is pow(2c, 2.0) / pow(c, 2.0). A control that lands on
an exact value is worth more than one that lands within a tolerance, because there is no slack in
which a small error can hide.
```

`ZeroReactantGivesExactlyZeroProgress` is the complementary sharp case: `C_H2 = 0` gives `q = 0`
exactly, not "small".

## 7. N8 and N9 — made unrepresentable rather than merely detected

```text
N8  "map kinetics to wrong reaction". ReactionRateParameters carries a reaction NAME, and
    ReactionKinetics reorders the supplied sets to mechanism order. A POSITIONAL binding would be
    silently wrong whenever the orders differ -- plausible rates, all attached to the wrong
    reaction, with nothing to detect.

    THE CONTROL SUPPLIES THEM IN REVERSE ORDER with deliberately different activation energies
    (5e4 and 1.5e5) and asserts forward(0).activationEnergy() == 5e4 -- i.e. that reaction 0 keeps
    its own parameters. Positional binding would return 1.5e5. Construction additionally refuses
    an unknown name, a duplicate, and a wrong count.

N9  "bypass P18.4 source conversion". P18.5 has NO omega_i API at all -- the header exposes
    progress rates and nothing else, and the mass-conservation formula appears nowhere in
    src/species/ArrheniusKinetics.cpp. The only route from q to omega_i is P18.4's
    reactionSpeciesSources(), so a bypass would have to be newly written rather than mistakenly
    taken. The control drives the real chain end to end over a 16-cell field with a
    spatially varying T and asserts mass closure through P18.4's OWN diagnostic, whose formula is
    not duplicated in the test either.

BOTH ARE MARKED "UNREPRESENTABLE" RATHER THAN "DETECTED" IN THE TABLE. That is a stronger claim and
it should be read as a different one: there is no margin to quote because there is no reachable
defective path, not because the margin is large.
```

## 8. N10 — invalid temperature

See `invalid_states.md`. 5 invalid values (`0`, `-1`, `-300`, NaN, `+inf`) × 3 entry points
(`rateCoefficient`, `logRateCoefficient`, `exponentialArgument`), each refused **before** any
division or `exp()`, never inferred from their output. The field-level entry point refuses too and
**names the cell and the reaction**.

## 9. The two controls amendment A1 required

A1 changed a decision that had been frozen before measurement, so it carries its own non-vacuity
checks (`formulation_A1.md` §4):

| control | purpose | measured |
|---|---|---|
| **NV1** | the two evaluation forms must agree where both are safe — otherwise the branch selection is observable as a discontinuity in `k` | worst **18.04 eps** at T = 2700 K over a 300–3000 K sweep, inside G6's 32 |
| **NV2** | **the log-space fallback must still be live.** A1 is worthless if it turned the original formulation's robustness into dead code | `A T^n = inf` while `k = 1.9913135012679248e+263` is finite — the fallback **is** taken and is correct |

```text
NV2 IS THE ONE THAT MATTERED. Without it, A1 could have silently deleted the overflow protection
the original formulation existed for, and every other criterion would still have passed.
```

## 10. The comparison instrument's own controls

`compare_probe.py` reported PASS. That is worth nothing until the same instrument is shown to fail
on a log it should reject (CLAUDE.md §5.2). `tools/comparator_nonvacuity.sh` injects five
corruptions into a **copy** of the real log — the original is never modified —
`logs/06_comparator_nonvacuity.log`:

| # | corruption | result | reason given |
|---|---|---|---|
| 0 | none (control) | **PASS** | — |
| 1 | one `omega_H2` off by ~100 eps | **FAIL** | `omega_H2 ...: 90.77 eps > 32 eps` |
| 2 | an exactly-zero `q` made `1e-300` | **FAIL** | `1e-300 is not exactly 0` |
| 3 | a matrix row deleted, `MATRIX_ROWS` left stale | **FAIL** | row count disagreement |
| 4 | **an empty log** | **FAIL** | no comparable rows |
| 5 | every zero-rate row removed | **FAIL** | zero-concentration case not covered |

```text
CASES 4 AND 5 ARE THE RECURRING FAILURE MODE IN THIS PROJECT, which is why they are here: a probe
that reads a missing file, greps a renamed symbol, or parses nothing prints exactly what success
looks like. Case 0 is not decoration either -- without it, five FAILs would be consistent with an
instrument that rejects everything.
```
