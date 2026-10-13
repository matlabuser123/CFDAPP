# P18.6 — the reaction energy source (§6, §7, §10–§13, §26–§30, §43–§45)

```text
    S_E = - sum_r q_r Delta H_r^0        [W/m^3]       the MOLAR route
        = - sum_i h_f,i omega_i                        the MASS route, G7's own formula

ALGEBRAICALLY IDENTICAL -- see energy_accounting.md section 4 -- and implemented INDEPENDENTLY so
the two can be compared. They share no arithmetic beyond h_f,i.

SIGN: Delta H_r^0 < 0 exothermic, q_r > 0 forward, so S_E > 0 and the thermal equation's
rhs[P] += Q_P V_P adds energy. Derived, never determined experimentally (section 6).
```

---

## 1. No new energy equation (§1, §44)

```text
P18.6 PRODUCES A ScalarField OF W/m^3 AND HANDS IT TO AN ASSEMBLER THAT HAS EXISTED SINCE
P12-NUM-006:

    assembleThermalSourceContribution(mesh, const ScalarField& volumetricHeatSource, rhs)
    assembleEnergyEquation(..., const ScalarField& volumetricHeatSource, ...)

verified present at include/cfd/thermal/EnergyEquation.hpp:259 and :312. DIRECT REUSE, nothing
extended, no second energy path.

AND NO PRESSURE WORK IS RE-ADDED (section 44). P17's total-energy equation treats pressure work
through its conservative flux; P18.6 does not touch that path at all. The P15 thermal equation it
DOES drive has no p div(U) term to begin with. S_E is built from h_f,i and omega_i only, which an
integrity grep confirms with a positive control.
```

## 2. Exothermic and endothermic — both measured (§10, §11)

**Measured** (`logs/06_probe.log`), `A->B`, `Delta H_r^0 = −125 000 J/mol`:

| q [mol/(m³s)] | `S_E` [W/m³] | expected | rel error | routes agree |
|---:|---:|---:|---:|---:|
| 0 | **0** | 0 | 0 | 0 |
| 1 | **125 000** | 125 000 | **0** | **0** |
| 2 | 250 000 | 250 000 | 0 | 0 |
| 7.5 | 937 500 | 937 500 | 0 | 0 |
| 1000 | 1.25e8 | 1.25e8 | **0** | **0** |

```text
MAX source error        : 0.000e+00  (0.00 eps)
MAX route disagreement  : 0.000e+00  (0.00 eps)

THE TWO ROUTES AGREE BITWISE at every rate tested. That is stronger than the round-off agreement
the algebra guarantees, and it happens because the dyadic weights make both summation orders exact.
```

`A->C`, endothermic (`+187 500 J/mol`): `S_E` = −187 500, −375 000, −1 406 250, −1.875e8 at
q = 1, 2, 7.5, 1000 — **negative at every rate**, so the reaction REMOVES energy.

```text
SECTION 11 EXISTS TO CATCH A GLOBAL SIGN REVERSAL, which an exothermic-only test cannot see: a
flipped sign makes an exothermic reaction cool and an endothermic one heat, and both look
self-consistent in isolation. The two signs together pin it.
```

## 3. Thermoneutral and zero-rate (§12, §13)

| case | `S_E` | species sources |
|---|---|---|
| `Delta H_r^0 = 0`, q = 1, 1000, 1e6, −5 | **exactly 0** at every rate | **non-zero** — progress is happening |
| q = 0 (both reactions) | **exactly 0**, every cell | **exactly 0**, every species, every cell |

```text
SECTION 12 SEPARATES PROGRESS FROM RELEASE: a thermoneutral reaction converts species while
releasing nothing, and the test asserts BOTH halves -- zero heat AND non-zero omega. Asserting
only the zero heat would pass for an implementation that had stopped reacting.

SECTION 13 IS THE BACKWARD-COMPATIBILITY LIMIT: with q = 0 the reacting path must reduce to the
non-reacting one IDENTICALLY, not approximately. See nonreacting_limit.md.
```

## 4. Linearity (§27)

**Measured**: `S_E(c q) = c S_E(q)` for `c` = 2, 0.5, 1e6, −1, 0 against `q = 3.25`:

```text
  c=       2.0  S_E=            812500   c*S_E=            812500   rel 0   BITWISE
  c=       0.5  S_E=            203125   c*S_E=            203125   rel 0   BITWISE
  c= 1000000.0  S_E=        4.0625e+11   c*S_E=        4.0625e+11   rel 0   BITWISE
  c=      -1.0  S_E=           -406250   c*S_E=           -406250   rel 0   BITWISE
  c=       0.0  S_E=                 0   c*S_E=                 0   rel 0   BITWISE
  MAX linearity error : 0.000e+00  (0.00 eps)

BITWISE AT EVERY FACTOR, including 1e6 which is not dyadic. This catches hidden nonlinear scaling
in the source conversion -- a defect that a single-rate test cannot see at all.
```

## 5. Superposition (§28)

**Measured**, `A->B` at q = 2 and `A->C` at q = 0.5:

```text
    only A->B   S_E =  250000   (exothermic)
    only A->C   S_E =  -93750   (endothermic)
    both        S_E =  156250
    and  both == onlyAB + onlyAC   BITWISE

THE TWO HAVE OPPOSITE SIGNS, so this is not a same-sign superposition that could hide a sign error
in one term. Two EXOTHERMIC reactions are checked as well: q = 1 and 3 give exactly 4 x 125000.
```

## 6. One-step balance (§26)

```text
    Delta u_sens = dt * S_E
```

**Measured** (`logs/06_probe.log`) — a direct hand-arithmetic check of the source wiring:

| q | dt | measured `Delta u_sens` | expected | rel error |
|---:|---:|---:|---:|---:|
| 1 | 1e-3 | **125** | 125 | **0** |
| 100 | 1e-5 | **125** | 125.00000000000001 | 1.137e-16 |
| 0.5 | 2 | **125 000** | 125 000 | **0** |

```text
MAX one-step balance error : 1.137e-16  (0.51 eps)

Two of the three are exact; the third carries half an eps from forming dt*S_E with dt = 1e-5,
which is not dyadic.
```

## 7. One `q_r`, shared (§30) — and G7's own control

```text
THE API TAKES THE PROGRESS RATES AS AN ARGUMENT rather than recomputing them:

    reactionHeatRelease(mechanism, mixture, formation, const ReactionProgressRates& rates, cells)

so the q_r that produced P18.4's species sources is STRUCTURALLY the same q_r that produces the
energy source. Evaluating kinetics twice per step could yield two different numbers for one step,
and no amount of care makes that safe if the API permits it.
```

G7's second control, verbatim: *"the reaction rate fed to species but NOT to energy must FAIL —
this is the control that the two are actually coupled rather than separately plausible."*

**Measured** (`N8_UsingADifferentProgressRateForEnergyIsDetected`): species at q = 2, energy at
q = 5.

| | value |
|---|---:|
| coupled pair (both q = 2) | 250 000, routes agree to **0 eps** |
| mismatched (energy q = 5) | 625 000 |
| relative gap | **150 %** |

```text
THE CROSS-CHECK IS THE DETECTOR. Comparing the molar route (which takes q) against the mass route
(which takes omega) catches a mismatch that NEITHER ROUTE ALONE could see, because each is
individually plausible. That is precisely why both routes are implemented.
```

## 8. Units, and the mol/kmol control (§43)

```text
    h_f,i [J/kg]   omega_i [kg/(m^3 s)]   M_i [kg/mol]   q_r [mol/(m^3 s)]
    Delta H_r [J/mol]                     S_E [W/m^3]

    [J/kg][kg/(m^3 s)]   = J/(m^3 s)   OK
    [mol/(m^3 s)][J/mol] = J/(m^3 s)   OK
```

**Measured** (`N9_AKmolFormationEnthalpyIsWrongByExactlyAThousand`): `h_f` entered per gram
instead of per kilogram — the realistic direction of the mistake, since tables are published both
ways.

| | `S_E` at q = 1 |
|---|---:|
| correct, J/kg | **125 000** |
| 1000× too small | **125** |
| ratio | **exactly 1000** |

Exact, because the dyadic weights make the scaling exact.

## 9. `Sigma omega_i` — the invariant P18.6 depends on

**Measured** (`logs/06_probe.log`), via P18.4's own diagnostic, formula **not** duplicated:

```text
  q=     1.00  max|sum omega| = 0.000e+00   scale 0.0625
  q=     7.50  max|sum omega| = 0.000e+00   scale 0.46875
  q=  1000.00  max|sum omega| = 0.000e+00   scale 62.5
  MAX |sum omega| : 0.000e+00

EXACTLY ZERO, which is what makes the reference-state invariance of section 33 exact rather than
approximate -- the two facts are the same fact.
```
