# P18.6 — the non-reacting limit (§13, §34, §35, §58)

```text
Section 34, verbatim: "Where existing fingerprints expect bitwise equality, require bitwise
equality. Do not tolerate a chemistry-off perturbation merely because it is small."

THE FIRST IMPLEMENTATION FAILED THIS, and the failure is preserved at
logs/03_four_failures_PRESERVED.log. It is the primary backward-compatibility test and it caught a
real defect.
```

---

## 1. The defect it caught

```text
With EXACTLY ZERO progress rates, ten steps moved the state:

    Y: 0.2421875  ->  0.24218749999999997
       0.4765625  ->  0.47656249999999994
    T: 1160       ->  1159.9999999999995

about 1 ulp per step. TWO CAUSES, both of which look harmless in isolation:

(a) THE SPECIES UPDATE WENT THROUGH rho:  (rho Y_i + dt omega_i) / rho.
    With dt omega_i = 0 that is fl(fl(rho*Y)/rho), which is NOT Y for most values. A multiply and
    divide by the same number is not the identity in floating point.

(b) TEMPERATURE WAS RE-RECOVERED BY BISECTION even when nothing had changed, returning a value a
    fraction of an ulp from the input.

NEITHER IS "SMALL BUT ACCEPTABLE". A chemistry-off run must reproduce the non-reacting path
exactly, or every existing thermal fingerprint becomes approximate the moment species exist.
```

## 2. The fix

```text
(a) THE INCREMENT IS APPLIED IN MASS-FRACTION SPACE:  Y_i + dt omega_i / rho
    so a zero increment is Y + 0.0 == Y, BITWISE. This also removes the round-trip through rho
    entirely, which independently fixed the sum-to-one drift that P18.1 had refused
    (closed_reactor.md section 4).

(b) THE NO-OP IS TAKEN EXPLICITLY. When the source is exactly zero AND the composition is bitwise
    unchanged, rho cp(T,Y) T is bitwise unchanged, so T is unchanged -- and the bisection is
    skipped rather than relied upon to return the same bits.

THIS IS THE SAME PRINCIPLE P15's OWN TRANSIENT TERM STATES, quoted from EnergyEquation.hpp:359:
    "A steady solve passes `transient = false` and the assembled system is bitwise what the
     existing overloads produce -- no zero-valued term is added, the code is not executed at all."
P18.6 follows the precedent rather than inventing a tolerance.
```

## 3. Measured, after the fix (`logs/06_probe.log`)

10 zero-rate steps, 32 cells, a **non-uniform** state (varying `Y_i`, `T` from 800 to 1420 K,
varying `rho`) so a defect cannot hide behind uniformity:

```text
  max |dT|   = 0 K
  max |dY|   = 0
  values differing bitwise : 0   (MUST be 0)
```

and in the test, additionally:

| quantity | result |
|---|---|
| `T` every cell | **bitwise equal** |
| `rho` every cell | **bitwise equal** |
| `Y_i` every species, every cell | **bitwise equal** |
| `E_total`, `E_sensible`, `E_chemical` | **bitwise equal** |

## 4. Non-vacuity — the stepper is not simply inert

```text
A BITWISE-UNCHANGED RESULT WOULD ALSO BE PRODUCED BY A STEPPER THAT DOES NOTHING AT ALL, so the
same test drives the SAME STATE with a NON-zero rate and asserts it DOES move:

    EXPECT_NE(moved.temperature[0], state.temperature[0])
        << "without this, the bitwise check above would pass for a stepper that does nothing";

That assertion is the difference between proving a limit and proving an absence.
```

## 5. The thermoneutral case, with its expectation documented first (§35)

```text
Section 35 asks for the expected result to be documented BEFORE testing, and warns about exactly
the confound that arises here. Stated before measuring:

  Delta H_r = 0 AND EQUAL cp_i
      the explicit heat source is exactly 0, and cp_mix is composition-independent, so the
      sensible pool rho cp T is unchanged and T is EXACTLY unchanged while Y changes.

  Delta H_r = 0 AND UNEQUAL cp_i
      the heat source is STILL exactly 0, but cp_mix CHANGES with composition. Since rho cp T is
      the conserved sensible quantity, T MUST MOVE. That is a composition-induced thermodynamic
      effect, NOT reaction heat.

THE TWO ARE MEASURED SEPARATELY so they can never be confused -- which is what section 35 means by
"carefully distinguish that from explicit reaction heat".
```

**Measured** (`ZeroHeatReleaseChangesSpeciesButNotTemperature`), thermoneutral `A->B` with
`h_f,A = h_f,B = -750 000 J/kg`, 50 % conversion in one step:

| case | `S_E` | species | temperature |
|---|---|---|---|
| **equal** `cp` (1000, 1000, 1000) | **exactly 0** | `Y_B` 0 → 0.5 | **bitwise unchanged** at 1000 K |
| **unequal** `cp` (1000, 2500, 1000) | **exactly 0** | `Y_B` 0 → 0.5 | **moves**, to `T_0·cp_0/cp_1` |

```text
AND THE MOVE IS PREDICTED QUANTITATIVELY, not merely observed: cp_mix goes 1000 -> 1750, and
rho cp T = const gives T_1 = T_0 * 1000/1750 = 571.43 K, which the test asserts to 1e-9 relative.

SO THE COMPOSITION EFFECT IS NOT A DEFECT AND NOT AN ANOMALY -- it is the formulation's own
prediction, and it is verified against the formulation rather than explained away.
```

## 6. Zero-rate limit, separately (§13)

```text
With q = 0:  omega_i = 0 EXACTLY and S_E = 0 EXACTLY, every species, every cell -- measured in
N6_ZeroRateGivesExactlyZeroSourceAndZeroOmega over 8 cells and two reactions.

This is the algebraic half of the same claim; section 3 above is the behavioural half. Both are
needed: exact zero sources do not by themselves prove the STEPPER leaves the state alone, which is
precisely the defect section 1 describes.
```

## 7. P15 and P17 suites (§58)

```text
P18.6 MODIFIES NO PRE-EXISTING PRODUCTION SOURCE -- only two build-list additions -- so P15 and P17
preservation is structural. It is measured anyway:

    CFDThermalTests       163/163   (baseline at logs/00: 163)
    CFDCompressibleTests  215/215   (baseline: 215)
    CFDTransportTests      22/22    (baseline: 22)

and P17's TotalEnergyState refusal of a temperature-dependent cp is NOT lifted (audit.md
section 1), so the compressible path is untouched by construction as well as by measurement.
Section 58's "Do not repair unrelated P17 qualification debt" is observed -- none was touched.
```
