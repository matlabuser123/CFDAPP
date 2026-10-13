# P18.5 — units and the gas-constant convention (§2, §5, §19)

```text
FROZEN BEFORE IMPLEMENTATION.
```

---

## 1. The convention, stated once

```text
T        [K]
Ea       [J / mol]
R_u      [J / (mol K)]     = 8.31446261815324,  REUSED from P18.3's kUniversalGasConstant
Ea/(R T) [-]               DIMENSIONLESS, which is the check that matters
M_i      [kg / mol]        REUSED from P18.3. P18.5 stores no molecular weights.
rho      [kg / m^3]
Y_i      [-]
C_i      [mol / m^3]       = rho Y_i / M_i
q        [mol / (m^3 s)]   P18.4's progress-rate unit, unchanged
omega_i  [kg / (m^3 s)]    P18.4's source unit, unchanged
```

```text
MOL, NEVER KMOL, throughout -- the same single system P18.3 and P18.4 adopted. Ea is per MOL and is
paired with R_u per MOL, so the exponent is dimensionless. Pairing J/mol with J/(kmol K) would make
the exponent 1000x too small and every rate wrong; control N4 exists for exactly that and the
authorization calls it "one of the most damaging silent chemistry errors" (§33).
```

## 2. `A` and `k` — the units DEPEND ON REACTION ORDER (§2)

§2 warns against claiming one dimensional form for every reaction. It is right:

```text
For an elementary mass-action reaction with total reactant order

    m = sum_i nu'_i

the progress rate is  q = k Pi_i C_i^(nu'_i), so

    [k] = [q] / [C]^m = (mol/(m^3 s)) / (mol/m^3)^m = (m^3/mol)^(m-1) / s

    m = 1   [k] = 1/s                        first order
    m = 2   [k] = m^3/(mol s)                second order
    m = 3   [k] = m^6/(mol^2 s)              third order

and A carries the same units as k, times K^(-n) so that A T^n has k's units.
```

```text
CONSEQUENCE, AND IT IS WHY NO UNIT IS STORED: the code cannot validate A's dimension, because the
dimension is a property of the REACTION's stoichiometry, not of the parameter. What the code CAN and
does validate is that A >= 0 and finite, that the exponent is dimensionless by construction (Ea and
R_u are both per mol, from one source), and that the resulting rate is finite.

A caller supplying A in the wrong order's units gets a wrong answer that no dimensional check could
catch. That is a real limitation and is stated rather than papered over. The protection available is
the ANALYTICAL REFERENCE: a mechanism whose rates are compared against an independent mpmath
calculation at known A, n, Ea will expose a mis-scaled A immediately.
```

## 3. Concentration basis (§19)

```text
    C_i = rho Y_i / M_i          [mol / m^3]

Every input is reused from the authoritative source:
    rho    the caller's density field
    Y_i    P18.1's MassFractionState -- already validated finite, in [0,1], summing to 1
    M_i    P18.3's SpeciesThermodynamics

NOTHING IS DUPLICATED. P18.5 stores no composition and no molecular weight, so the basis cannot
drift from the rest of the phase chain.
```

## 4. The one factor-of-1000 test (§5, §33)

```text
A test must be able to detect a mol/kmol mistake. The detectable signature:

    Ea = 2.0e5 J/mol, T = 1000 K
    correct   Ea/(R_u T) = 2.0e5 / (8.31446261815324 * 1000) = 24.05...
              exp(-24.05) = 3.56e-11
    with R in J/(kmol K), i.e. 1000x larger
              Ea/(R T) = 0.02405
              exp(-0.02405) = 0.9762

    ratio = 2.7e10

Twenty-seven billion times. Not a subtle error, and the control asserts the margin rather than
only that the two differ.
```

## 5. What is NOT given units here

```text
equilibrium constants        none exist -- reverse kinetics are independently supplied (§20, §22)
reaction enthalpy            P18.6's
heat release, energy source  P18.6's
```
