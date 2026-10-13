# P18.7 — the batch reactor (sections 16-21, 60)

```text
A CLOSED, HOMOGENEOUS, ADIABATIC, CONSTANT-VOLUME REACTOR. U = 0, no mass flux, no diffusion, no
boundary heat flux -- so only chemistry, species conversion and the energy/temperature coupling
remain. The cleanest integrated P18 benchmark, and the one that isolates the TEMPORAL error
exactly, because there is no spatial error to isolate it from.
```

---

## 1. The independent reference (sections 17, 19, 46)

```text
tools/batch_reference.py -- mpmath at 50 digits, and NOT A TIME INTEGRATION.

CFDApp's update is explicit Euler stepping in time. The reference uses the energy invariant to make
T an EXACT algebraic function of Y_A,

    T(Y_A) = T_0 + (Y_A0 - Y_A) * (-h_f,B) / cp

which collapses the system to one autonomous ODE, then SEPARATES VARIABLES:

    t(Y_A) = INTEGRAL_{Y_A}^{Y_A0}  dy / ( k(T(y)) y )        <-- QUADRATURE

evaluated by mpmath and inverted by bisection. THERE IS NO TIME-STEPPING LOOP ANYWHERE IN IT, so
the two methods share no algorithm -- not merely a different tolerance of the same one, which is
what section 19 asks for and what section 46 forbids violating.

SELF-CHECK: t -> Y_A -> t round-trip residuals ~1e-45.
ENERGY: exactly constant along the reference, by construction.
```

## 2. The case

```text
    A -> B, irreversible, first order     M = 0.0625 kg/mol both species (dyadic)
    cp = 1000 J/(kg K) both species        rho = 1 kg/m^3       T_0 = 1000 K
    h_f,A = 0, h_f,B = -2e6 J/kg           Delta H_r^0 = -125000 J/mol  (exothermic)
    A = 5 1/s, n = 0, Ea = 3e4 J/mol       k(T_0) = 0.13550689710042101 1/s

CHOSEN SO THE CHEMISTRY IS ACTIVE BUT NOT STIFF: 21.5% conversion and T 1000 -> 1430 K over 1 s,
with k growing only 37x. Section 21 forbids stretching the explicit integrator for drama.
```

## 3. The reference table (section 60) — checkpoints, not only the final state

| t [s] | `Y_A` | `Y_B` | `T` [K] | `q` [mol/(m³s)] |
|---:|---:|---:|---:|---:|
| 0.05 | 0.99307948811552110 | 0.0069205118844789043 | 1013.8410237689578 | 2.2618213475815522 |
| 0.10 | 0.98585848306055263 | 0.014141516939447370 | 1028.2830338788947 | 2.3604602914101322 |
| 0.20 | 0.97045151039151934 | 0.029548489608480658 | 1059.0969792169613 | 2.5733174696295062 |
| 0.30 | 0.95364625409540410 | 0.046353745904595900 | 1092.7074918091918 | 2.8081276031960563 |
| 0.50 | 0.91527271443601291 | 0.084727285563987095 | 1169.4545711279742 | 3.3472816345424667 |
| 0.75 | 0.85689113779210307 | 0.14310886220789693 | 1286.2177244157939 | 4.1467534381352313 |
| 1.00 | 0.78521237887865421 | 0.21478762112134579 | 1429.5752422426916 | 5.0343258645590687 |

CFDApp is compared at **every** one of these, for `Y_A`, `Y_B` and `T`.

## 4. Temporal refinement (section 20)

```text
dt, dt/2, dt/4 = 4e-3, 2e-3, 1e-3, each integrated to t = 1.0 s and compared at the checkpoints.

THE GATE CRITERION IS THAT THE ERROR FALLS, and it does -- monotonically, for species,
temperature and the final conversion.

THE OBSERVED ORDER IS REPORTED, NOT GATED, and brackets first order (0.7 to 1.5). THE PRODUCTION
UPDATE IS EXPLICIT EULER, so first order is the DESIGN PREDICTION; section 20 explicitly forbids
forcing second order onto it. A bracket is used rather than a point value because a single
refinement pair does not establish an asymptotic order.
```

## 5. Stiffness (section 21)

```text
Explicit Euler on dY/dt = -k Y is stable for dt < 2/k, and k GROWS as the reactor heats, so the
binding value is k at the FINAL temperature. Asserted:

    every dt used satisfies   dt < 0.05 * (2 / k_final)
    k_final / k_initial < 100                            -- the case is NOT stiff

P18 HAS NO STIFF INTEGRATOR. This test records that the benchmark was chosen to respect that,
rather than hiding an instability behind an undocumented tiny time step.
```

## 6. The step budget (known_limitations.md section 4)

```text
A 10000-step march at dt = 1e-4 was REFUSED by P18.1's sum-to-one invariant. The budget for TWO
species is 4*eps*2 = 8 eps, TIGHTER than P18.6's three-species 12 eps.

1000 steps at dt = 1e-3 is inside it and reaches t = 1.0 s, so every reference checkpoint is
resolved. TheSumToOneBudgetBoundsTheMarch pins both halves of the behaviour.
```
