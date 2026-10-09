# P18.6 — energy accounting (§2, §6, §7, §8, §9, §43, §44, §45)

```text
FROZEN BEFORE THE HEAT-RELEASE SOURCE IS WRITTEN, as §2 and §9 require. Nothing here was
determined experimentally -- §6 says "Derive it explicitly; do not determine the sign
experimentally", and the sign below is derived from a conservation statement, then tested.

THIS IS THE PHASE'S PRIMARY DESIGN GATE (§8). Getting it wrong produces a model that is plausible,
runs, conserves nothing, and is off by the sensible enthalpy of every species converted.
```

---

## 1. The transported energy — measured from the code, not assumed

```text
include/cfd/thermal/EnergyEquation.hpp:347
    "The conserved quantity is rho*cp*T"
:351-352
    a_P += rho_P^{n+1} cp_P^{n+1} V_P / dt
    b_P += rho_P^n     cp_P^n     T_P^n V_P / dt

    TRANSPORTED ENERGY DENSITY       u_sens = rho * cp(T,Y) * T          [J/m^3]
    UNKNOWN                          T
    ZERO OF THE SCALE                T = 0
    CHEMICAL CONTENT                 NONE -- no formation energy appears anywhere in CFDApp
```

The source enters as `rhs[P] += Q_P * V_P` with the governing equation written
`rho*cp*(U.grad)T - k*Lap(T) = Q`, so **positive `Q` adds thermal energy and raises `T`**. That is
the sign convention every formula below is expressed in.

## 2. §8's fork, resolved

```text
A. transported energy INCLUDES chemical formation energy
     -> reaction merely redistributes it; no explicit heat source
B. transported energy is SENSIBLE/THERMAL ONLY
     -> an explicit chemical heat source IS REQUIRED

CFDApp IS CASE B. rho*cp*T contains no formation term, and nothing in the repository carries one --
the P18.0 audit found no h_f anywhere, and P18.3 added M_i and a sensible enthalpy per species
without adding one (audit.md §5).

THEREFORE DOUBLE COUNTING OF FORMATION ENERGY IS STRUCTURALLY IMPOSSIBLE HERE: the explicit source
adds a quantity the transported state does not contain. That is not an assumption -- it follows
from there being no h_f in the transported variable, which is checkable by grep and is checked.
```

## 3. The derivation

Define the conserved total energy density of the reacting mixture:

```text
    E  =  rho * cp(T,Y) * T  +  rho * sum_i Y_i h_f,i                     [J/m^3]
          \________________/     \______________________/
           SENSIBLE                CHEMICAL
           what the thermal         what the reaction moves in and out of
           equation transports      the sensible pool
```

For a closed, adiabatic, source-only system (`U = 0`, no boundary flux, no work), `dE/dt = 0`:

```text
    d(rho cp T)/dt  +  d/dt[ rho sum_i Y_i h_f,i ]  =  0

    d(rho cp T)/dt  =  - sum_i h_f,i d(rho Y_i)/dt
                    =  - sum_i h_f,i omega_i                        since omega_i = d(rho Y_i)/dt|_chem
```

and the thermal equation's source is exactly the right-hand side of its own balance, so

```text
    ======================================================
      S_E  =  - sum_i h_f,i omega_i          [W/m^3]
      h_f,i  in [J/kg]      omega_i in [kg/(m^3 s)]
    ======================================================
```

**This is G7's `measured` clause verbatim** — `q = -sum_i h_f,i omega_i`. The gate's formula and
CFDApp's transported variable agree, and the reason they agree is specific: `rho*cp*T` plays the
role of the sensible pool, so the chemical pool is `rho*sum Y_i h_f,i` and nothing else.

## 4. The molar form, and why it is the same thing (§6, §7)

P18.4 gives `omega_i = sum_r M_i nu_{i,r} q_r`. Substituting:

```text
    S_E = - sum_i h_f,i sum_r M_i nu_{i,r} q_r
        = - sum_r q_r [ sum_i nu_{i,r} (M_i h_f,i) ]
        = - sum_r q_r  Delta H_r

    with   Delta H_r  =  sum_i nu_{i,r} hbar_f,i        and   hbar_f,i = M_i h_f,i   [J/mol]
```

```text
THE TWO FORMULATIONS §6 AND §7 ASK FOR ARE ALGEBRAICALLY IDENTICAL, not merely consistent. They
differ only in summation order, so they agree to round-off and the difference is a round-off
measurement rather than a modelling check. Both are implemented and compared, because an
implementation can still get one of them wrong.

SIGN CONVENTION, FROZEN (§5): with P18.4's net nu_i = nu''_i - nu'_i,
    Delta H_r < 0  is EXOTHERMIC
    q_r > 0        is forward progress
    => S_E = -q_r Delta H_r > 0  => energy ADDED  => T rises.          CONSISTENT.
```

Worked check with real numbers, `2 H2 + O2 -> 2 H2O`, `hbar_f(H2O) = -241818 J/mol`,
`hbar_f(H2) = hbar_f(O2) = 0`:

```text
    Delta H_r = 2(-241818) - 2(0) - 1(0) = -483636 J/mol     < 0   exothermic    OK
    q_r > 0  =>  S_E = -q_r (-483636) > 0                           heat added    OK
    mass form: omega_H2O > 0 and h_f,H2O < 0, so -h_f,H2O omega_H2O > 0  OK, same sign
```

## 5. §45 — the sensible-enthalpy double-count trap, and the exact condition

```text
THE TRAP: using the TOTAL species enthalpy h_i(T) = h_f,i + Delta h_i(T) in the source,

    S_E^wrong = - sum_i [ h_f,i + Delta h_i(T) ] omega_i  =  S_E - sum_i Delta h_i(T) omega_i

The extra term is the SENSIBLE enthalpy carried by the species being converted. Whether it belongs
in the source depends entirely on what the equation transports:

    transported rho * integral(cp dT)   -> the sensible term IS needed; source = -sum_i h_i(T) omega_i
    transported rho * cp * T            -> the sensible term is NOT needed; source = -sum_i h_f,i omega_i

CFDApp TRANSPORTS rho*cp*T, so the FORMATION-ONLY form is correct and adding Delta h_i(T) would
double-count. A code review that "corrected" S_E to use total enthalpies would be introducing the
defect, which is why this paragraph is frozen before implementation rather than argued afterwards.

MAGNITUDE OF THE TRAP, so it is not dismissed as second-order: at 1500 K with cp ~ 1000 J/(kg K)
and T_ref = 298.15, Delta h_i ~ 1.2e6 J/kg, against h_f,H2O = -1.34e7 J/kg. The spurious term is
~9% of the real one -- large enough to matter, small enough to look like a discretization error.
This is exactly the kind of defect a PASS/FAIL conservation test would hide and a QUANTITATIVE one
exposes.

AND IT IS A NEGATIVE CONTROL: N5 (double-count chemical energy) and N7 (wrong species enthalpy)
both attack it.
```

## 6. §44 — pressure work is not re-added

```text
P17's total-energy equation treats pressure work through its conservative energy flux. P18.6 does
not touch that path at all (audit.md §1), and the P15 thermal equation it DOES drive has no p*div(U)
term to begin with -- its governing form is rho*cp*(U.grad)T - k*Lap(T) = Q.

So there is no pressure-work term anywhere near the reaction source, and the reaction source
contains none: S_E is built from h_f,i and omega_i only. Checked by an integrity grep (no `p *`,
no `divergence`, no `pressureWork` in the new source) WITH a positive control that the grep reads
the file.
```

## 7. Units, one system throughout (§43)

```text
    h_f,i        [J/kg]              MASS-specific. omega_i is mass-based, so this pairs directly.
    hbar_f,i     [J/mol]  = M_i h_f,i    the molar form, for Delta H_r
    M_i          [kg/mol]             P18.3's, never duplicated
    omega_i      [kg/(m^3 s)]         P18.4's
    q_r          [mol/(m^3 s)]        P18.5's
    Delta H_r    [J/mol]              = sum_i nu_i hbar_f,i
    S_E          [W/m^3] = [J/(m^3 s)]

DIMENSIONAL CHECK, both routes:
    [J/kg][kg/(m^3 s)]           = J/(m^3 s)   OK
    [mol/(m^3 s)][J/mol]         = J/(m^3 s)   OK

mol, NEVER kmol -- the same system P18.5 froze, and R_u comes from P18.3. A 1000x mol/kmol error is
control N9.
```

## 8. §33 — reference-state invariance, and it is EXACT

```text
Shift every species' MASS-SPECIFIC formation enthalpy by one common constant c [J/kg]:

    S_E -> - sum_i (h_f,i + c) omega_i  =  S_E - c sum_i omega_i  =  S_E - c*0  =  S_E

because P18.4 PROVES sum_i omega_i = 0. Equivalently in molar form,

    Delta H_r -> sum_i nu_i M_i (h_f,i + c) = Delta H_r + c sum_i M_i nu_i = Delta H_r + c*0

because P18.4 enforces sum_i M_i nu_i = 0 PER REACTION, on the definition, exactly.

SO THE INVARIANCE IS NOT APPROXIMATE -- it rests on a property a predecessor phase already
establishes to round-off, and with dyadic weights and a dyadic c it should hold BITWISE.

WHAT IS *NOT* INVARIANT, and the authorization's §33 warns about exactly this: shifting a SINGLE
species' h_f, or shifting the MOLAR hbar_f by a common constant. The molar shift gives
Delta H_r + c sum_i nu_i, and sum_i nu_i is NOT zero (moles are not conserved; for 2H2+O2->2H2O it
is -1). Both non-invariances are asserted as well, so the test cannot pass by the quantity being
insensitive to everything.
```

## 9. The conserved quantity for the closed-reactor test (§24)

```text
    E_total = sum_cells V_c [ rho_c cp(T_c,Y_c) T_c  +  rho_c sum_i Y_{i,c} h_f,i ]

DEFINED EXPLICITLY, as §24 requires, and it is the quantity §3's derivation conserves. Reporting
only the sensible part would be wrong -- exothermic chemistry is SUPPOSED to move energy from the
chemical pool to the sensible one, so sensible energy must rise while E_total stays fixed.

G7's acceptance: |imbalance| / |E_total| <= 8 * eps * N_cells, "the same derived floor as G3 and
for the same reason" -- a sum of N_cells terms each carrying round-off.

NOTE THE cp(T,Y) DEPENDENCE. Because the sensible pool is rho*cp(T,Y)*T, a change in composition
changes it at FIXED T. A thermoneutral reaction (Delta H_r = 0) therefore conserves E_total with
S_E = 0 exactly, yet T CAN still move if cp_mix changes. §35 anticipates this. The thermoneutral
test is built with EQUAL cp_i so T is exactly unchanged, and the cp-driven effect is measured
SEPARATELY with unequal cp_i so the two are never confused.
```

## 10. Coupling order, frozen (§19)

```text
Explicit operator splitting. NOT implicit, and no claim of implicit coupling is made (§18).

  1. thermochemical state at level n:  rho^n, Y_i^n, T^n
  2. ONE evaluation of q_r at T^n, Y_i^n, rho^n                      P18.5
  3. omega_i from THAT q_r                                           P18.4
  4. S_E from THE SAME q_r / omega_i                                 P18.6   <-- §30
  5. advance species with omega_i                                    P18.2
  6. advance energy with S_E                                         P15
  7. recover T^{n+1} from the UPDATED composition Y_i^{n+1}          §17
  8. next step's q_r is evaluated at T^{n+1}, Y_i^{n+1}              §18, §20

STEP 2 HAPPENS ONCE. §30 forbids evaluating kinetics separately for species and for energy, which
could yield two different numbers for the same step; the implementation takes the progress rates as
an argument rather than recomputing them, so sharing is structural rather than careful.

STEP 7 USES THE NEW COMPOSITION. §17 forbids recovering T from stale Y, and N4 is the control.
STEP 8 USES THE NEW TEMPERATURE. §20 forbids stale-T chemistry, and N3 is the control.
```
