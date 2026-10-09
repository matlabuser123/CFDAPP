# P18.6 — reaction enthalpy (§5, §31, §32, §33)

```text
TWO DISTINCT QUANTITIES, deliberately named differently:

    standardReactionEnthalpy   Delta H_r^0 = sum_i nu_i M_i h_f,i            THE SOURCE USES THIS
    reactionEnthalpy(T)        Delta H_r(T) = sum_i nu_i M_i [h_f,i + h_i(T)]   NOT the source

Using the second as the source would double-count the sensible enthalpy the thermal equation
already evolves (energy_accounting.md section 5). The names make the distinction unmissable and
control N5 measures the defect.
```

---

## 1. The sign convention, frozen before implementation (§5)

```text
With P18.4's net nu_i = nu''_i - nu'_i,

    Delta H_r^0 < 0   EXOTHERMIC
    Delta H_r^0 > 0   ENDOTHERMIC
    Delta H_r^0 = 0   THERMONEUTRAL

and with q_r > 0 as forward progress, S_E = -q_r Delta H_r^0 > 0 for an exothermic reaction, which
the thermal equation's rhs[P] += Q_P V_P then ADDS. Derived in energy_accounting.md section 3,
never determined experimentally.
```

**Measured** (`logs/06_probe.log`), against the mpmath reference:

| reaction | `Delta H_r^0` [J/mol] | expected | rel error | kind |
|---|---:|---:|---:|---|
| `A->B` | **−125 000** | −125 000 | **0** | exothermic |
| `A->C` | **+187 500** | +187 500 | **0** | endothermic |
| `B->C` | +312 500 | +312 500 | 0 | endothermic |

Exact, because the weights and formation enthalpies are dyadic.

## 2. Thermoneutral by cancellation, not by vacuity

```text
A THERMONEUTRAL TEST BUILT WITH h_f = 0 EVERYWHERE WOULD PASS FOR AN IMPLEMENTATION THAT IGNORES
h_f ENTIRELY. So the thermoneutral case gives the two species ONE SHARED NON-ZERO value,
h_f,A = h_f,B = -750000 J/kg, and the result is exactly 0 because the terms CANCEL:

    Delta H_r^0 = (-1)(0.0625)(-750000) + (+1)(0.0625)(-750000) = 0     EXACTLY

and the test asserts the individual molar terms are NOT zero, which is what makes it a
cancellation rather than an absence.
```

## 3. Catalysts contribute exactly nothing

A species with equal reactant and product coefficients has net `nu_i = 0`. In
`A->B (C catalyses)` the catalyst C carries the **largest** formation enthalpy in the set
(+3e6 J/kg), so a defect that counted it would be unmistakable.

**Measured**: `Delta H_r^0` is bitwise the un-catalysed value, and `netStoichiometry(2) == 0`.

## 4. Temperature dependence (§31)

```text
    Delta H_r(T) = sum_i nu_i M_i [ h_f,i + h_i(T) ]

with h_i(T) P15's INTEGRATED sensible enthalpy. Measured with cp_i(T) = a0 + a1 T, different per
species so Delta Cp_r is not zero and the dependence is real.
```

**Measured** (`logs/06_probe.log`), reaction `A->B`:

| T [K] | `Delta H_r(T)` [J/mol] | expected | rel error |
|---:|---:|---:|---:|
| 400 | −124 615.70721875 | −124 615.70721875 | **0** |
| 800 | −125 615.70721875 | −125 615.70721875 | **0** |
| 1500 | −136 990.70721875 | −136 990.70721875 | **0** |
| 2500 | −174 490.70721875 | −174 490.70721875 | **0** |

```text
MAX reaction-enthalpy error : 0.000e+00  (0.00 eps)

EXACTLY ZERO at all four temperatures, against values computed independently in mpmath at 60
digits. The closed-form polynomial integral is exactly representable for these coefficients, which
is why the agreement is bitwise rather than within a tolerance.
```

## 5. Kirchhoff's relation (§32)

```text
    d(Delta H_r)/dT = sum_i nu_i M_i cp_i(T) = Delta Cp_r(T)

TWO INDEPENDENT ROUTES: one differentiates the implemented reaction enthalpy by central
difference, the other sums nu_i M_i cp_i directly. This is a strong thermodynamic-consistency
check on the enthalpy integration itself -- if the integral and its integrand disagreed, the
enthalpy model would be internally inconsistent whatever its absolute values.

THE STEP AND BOUND ARE DERIVED, not fitted, exactly as P18.5's derivative test derived them:
a central difference is optimal near h ~ T eps^(1/3) and the floor on the RELATIVE agreement is
eps^(2/3) = 3.6668528625010315e-11. The bound is 8 x that.

THE COMPARISON IS SCALED BY THE TERMS, NOT BY THEIR DIFFERENCE. Delta Cp_r is tens, formed from
terms of ~1e5, so the subtraction has already lost those digits; demanding precision relative to
the difference would be demanding precision the arithmetic cannot carry.
```

**Measured** (`logs/06_probe.log`):

| T [K] | `Delta Cp_r` | central difference | rel | × eps^(2/3) |
|---:|---:|---:|---:|---:|
| 400 | 2.5 | 2.5 | 6.890e-14 | 0.0019 |
| 700 | −5.0 | −5.0 | 9.580e-14 | 0.0026 |
| 1200 | −17.5 | −17.5 | 4.234e-14 | 0.0012 |
| 1800 | −32.5 | −32.5 | 5.241e-14 | 0.0014 |
| 2500 | −50.0 | −50.0 | **0** | 0 |

**Worst 9.58e-14 = 0.0026 × eps^(2/3)**, against a bound of 8 — a margin of ~3000×. `Delta Cp_r`
is asserted non-zero at 1500 K, so the test is not comparing two zeros.

## 6. Reference-state invariance (§33) — exact, and its non-invariance too

```text
SHIFTING EVERY MASS-SPECIFIC h_f,i BY ONE COMMON CONSTANT c [J/kg] CHANGES NOTHING:

    Delta H_r^0 -> Delta H_r^0 + c sum_i M_i nu_i = Delta H_r^0 + c * 0

because P18.4 enforces sum_i M_i nu_i = 0 PER REACTION, on the definition, EXACTLY. The invariance
is not approximate -- it rests on a predecessor's exact invariant.
```

**Measured**: invariant for `c` = 1e5, −2.5e6, 0.125, 1e9; and **bitwise** for the dyadic
`c = 0.125`, with no tolerance at all.

```text
AND THE NON-INVARIANCE IS AS IMPORTANT, because without it the test above would pass for a
quantity insensitive to everything -- including to the formation enthalpies themselves.

A 1:1 REACTION CANNOT SHOW IT. For A->B, sum_i nu_i = -1 + 1 = 0, so a MOLAR shift cancels too,
for the wrong reason. THE FIRST VERSION OF THE REFERENCE SCRIPT WAS VACUOUS FOR EXACTLY THIS
REASON and reported "invariant: True" for the molar shift.

FIXED with A + B -> C, which conserves mass but NOT moles:
    sum_i M_i nu_i = -0.0625 - 0.125 + 0.1875 = 0        EXACTLY   (mass conserved)
    sum_i nu_i     = -1 - 1 + 1 = -1                     NOT 0     (moles not conserved)
all three weights dyadic.
```

**Measured** on that mechanism:

| shift | `Delta H_r^0` | invariant? |
|---|---:|---|
| none | −750 000 | — |
| **mass**-specific, c = 1e5 | −750 000 | **yes** |
| **mass**-specific, c = −2.5e6 | −750 000 | **yes** |
| **molar**, c = 1e5 | −850 000 | **no**, correctly |
| **molar**, c = −2.5e6 | +1 750 000 | **no**, correctly |
| one species only (`h_f,B` + 1e5) | −118 750 | **no**, correctly |

```text
EXACTLY THE INVARIANCE THE FORMULATION PREDICTS, and no more. §33 warns against using this test
where the reference enthalpies "encode actual formation chemistry that must not be shifted
independently" -- which is why the single-species shift is asserted NOT to cancel.
```
