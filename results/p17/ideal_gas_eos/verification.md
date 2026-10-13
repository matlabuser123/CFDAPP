# P17.2 — verification

Consolidates the identity residuals, invalid-state guards, determinism,
constant-property compatibility and P17.1 integration. Measured numbers, not verdicts.

Raw: `logs/00_dryrun.log` (before), `logs/08_verify.log` (after), `logs/01_gate.log`,
`logs/07_controls.log`, `logs/09_control_N6b.log`. Probes in `tools/`.

---

## 1. Identity residuals — worst case over 27 states

3 gases (air, helium, water vapour) x 3 pressures (1e3, 101325, 5e6 Pa) x 3 temperatures
(200, 288.15, 1500 K). Measured **after** the change, at `%.17g`.

```text
identity                                  worst error             in eps   bound     verdict
-----------------------------------------------------------------------------------------------
p - rho*R*T                  (relative)   2.2737367544323206e-16   1.02    1e-12     WITHIN
temperature(p,rho) - T       (relative)   1.9727023724035406e-16   0.89    1e-12     WITHIN
(cp - cv) - R                (absolute)   5.6843418860808015e-14   0.89    4 eps     WITHIN
gamma - cp/cv                (absolute)   0                        —       EXACT     EXACT
sqrt(gamma R T) - sqrt(gamma p/rho) (rel) 2.0051667127585680e-16   0.90    8 eps     WITHIN
round trip (p,T)   -> rho -> p    (rel)   2.2737367544323206e-16   1.02    1e-12     WITHIN
round trip (rho,T) -> p   -> rho  (rel)   2.1263572769214529e-16   0.96    1e-12     WITHIN
round trip (p,rho) -> T   -> p    (rel)   1.4361623714154308e-16   0.65    1e-12     WITHIN
```

Every residual is **about one double-eps** — the floor for this arithmetic. Nothing sits
near its bound, which is what a correct closure should look like.

### Why three different bound styles, and not one

```text
1e-12   the state relations and round trips. INHERITED verbatim from the earlier P17.2
        gate, where it was derived from two roundings of a double (~4500x eps) and proved
        load-bearing by a control perturbing R by 1e-6. Re-deriving a tighter bound per
        item would make results incomparable across P17.
4 eps   (cp - cv) - R. DERIVED: cv is stored as cp - R (one rounding); recovering cp - cv
        rounds again. Two roundings of quantities of magnitude ~cp, and cp/R ~ 3.5 for
        air, so ~3.5 eps is the floor. NOT equality -- the dry-run proved equality fails
        for air.
EXACT   gamma == cp/cv. gamma is STORED as that quotient, so recomputing it repeats one
        operation on unchanged bits. Equality is the correct criterion and is used.
8 eps   the two sound-speed forms. DERIVED from operation counts: path 1 is 2 multiplies
        + sqrt (~3 roundings); path 2 is rho = p/(R T), then gamma*p/rho, then sqrt
        (~4 roundings). Their difference is bounded by roughly 7 eps. NOT equality -- the
        paths genuinely differ.
```

The `cp - cv = R` and `gamma = cp/cv` identities **look symmetric and are not**. That
asymmetry was found by the dry-run, corrected the audit before the gate was frozen, and is
what control N6b exists to prove.

## 2. Speed of sound

```text
a = sqrt(gamma R T)                       existing test, PASS
a == sqrt(gamma p / rho)                  NEW, 27 states, worst 0.90 eps, bound 8 eps
a > 0 and finite for every valid state    PASS, asserted in the new test
a strictly increases with T               PASS, 6 temperatures 100 … 2400 K
a rejects non-positive / non-finite T     PASS, existing test
```

Dry-run values, for the record:

```text
T = 100   a = 200.45399061611212
T = 200   a = 283.48475216111490
T = 300   a = 347.19649632704113
T = 600   a = 491.00999391412205
T = 1200  a = 694.39299265408226
T = 2400  a = 982.01998782824410      strictly increasing: yes
```

## 3. Invalid-state guards — 20 cases, every one rejected

Measured in the dry-run (existing 16) and by the new tests (4 classes on `temperature()`).

```text
entry point / input                          outcome
------------------------------------------------------------------------------------
density(p = 0)                               THREW "IdealGasEOS: pressure must be finite and > 0"
density(p < 0)                               THREW, same
density(p = NaN)                             THREW, same
density(p = +Inf)                            THREW, same
density(T = 0)                               THREW "… temperature must be finite and > 0"
density(T < 0)                               THREW, same
density(T = NaN)                             THREW, same
density(T = +Inf)                            THREW, same
IdealGasEOS(R = 0)                           THREW "… gasConstant must be finite and > 0"
IdealGasEOS(R < 0)                           THREW, same
IdealGasEOS(R = NaN)                         THREW, same
IdealGasEOS(R = +Inf)                        THREW, same
ThermodynamicProperties(cp == R)             THREW "… cp must be finite and > gasConstant (R),
                                                    so that cv = cp - R > 0"
ThermodynamicProperties(cp = 100 < R)        THREW, same
ThermodynamicProperties(cp = 0)              THREW, same
ThermodynamicProperties(cp = -1)             THREW, same
temperature(p <= 0)            NEW           THREW
temperature(rho <= 0)          NEW           THREW
temperature(p or rho non-finite) NEW         THREW
temperature -> overflowing quotient  NEW     THREW  (p = 1e308, rho = 1e-308)
```

```text
cp <= 0, cv <= 0, cp <= R      all rejected by the single cp > R check
gamma <= 1                     UNREACHABLE. R > 0 and cp > R force gamma = cp/(cp-R) > 1,
                               so no guard is added -- it would be dead code. Asserted as
                               an observable property over a cp sweep instead.
clamping                       NONE anywhere. Rejection is by exception only.
```

### What control N3 revealed about the layering

Removing `temperature()`'s **input** validation broke only
`TemperatureMessageNamesTheOffendingField` — because the **result** check
`requirePositiveFinite(T, "temperature")` independently catches every invalid case:
`p=0` gives `T=0`, `rho=0` gives `T=inf`, a NaN propagates. So for this entry point the
input validation's unique contribution is **diagnostic quality**, not rejection. Recorded
in `AMENDMENT_N6_vacuous.md` §4.

## 4. Determinism

```text
density, pressure, temperature, dDensityDPressure, dDensityDTemperature
  64 repeats, BITWISE identical                                   PASS
speedOfSound
  64 repeats, BITWISE identical (dry-run)                         PASS
evaluateDensityField over a mesh
  8 repeats, BITWISE identical per cell                           PASS
```

Bitwise is the correct criterion and needs no caveat: every path is straight-line double
arithmetic — no reduction, no threading, no iteration. Reference values:

```text
rho = 1.2250122659906946   p = 101325.00000000001   a = 340.27028004636702
```

## 5. Constant-property compatibility — existing tests, cited not duplicated

P15 already covers this, so P17.2 runs those tests rather than writing new ones:

```text
SensibleEnthalpyTest.ConstantCpGivesTheLinearForm          PASS   h = h_ref + cp(T - T_ref)
SensibleEnthalpyTest.RoundTripHoldsForConstantCpToo        PASS
SensibleEnthalpyTest.OutOfRangeEnthalpyIsRefusedNotClamped PASS   the error-not-clamp policy
TemperatureProperty.ConstantIsTheSingleCoefficientCase     PASS
whole P15 constant-property group                          13 / 13
```

```text
K3   Enthalpy.hpp and TemperatureDependentProperty.hpp UNCHANGED vs HEAD -- 0 edits
K4   e(T) remains ABSENT: 0 hits for internalEnergy across include/ and src/
     e = cv T + ref is NOT implemented. e(T) = h(T) - R T is one subtraction from P15's
     exact h(T), but adding it here would build the energy closure ahead of P17.5.
```

## 6. P17.1 integration

```text
I1  a known (p, T, R) reaches the authoritative density field with the expected value
    EosDerivedDensityReachesTheAuthoritativeFieldWithTheExpectedValue   PASS
    6-cell mesh, p and T BOTH non-uniform per cell, so a constant-state shortcut cannot
    pass; each cell checked against p/(R T) at 1e-12, and the new temperature() is used
    to recover the T that produced each density
I2  the same EOS instance -- no second gas constant. R recovered from the EOS itself via
    pressure(1,1) = R, asserted EXACTLY equal to gasConstant()                  PASS
I3  no duplicated EOS arithmetic on the production path: 0 hits for p/(R*T)-style
    expressions outside IdealGasEOS and ThermodynamicProperties                 PASS
I4  the P17.1 face-density and mass-flux path consumes that field -- unchanged and
    covered by the 20/20 CompressibleMassFluxTest suite                         PASS
```

## 7. Non-vacuity controls

```text
id    control                                          outcome
---------------------------------------------------------------------------------------
N1    temperature() returns p/rho (drop R)             DETECTED, 4 tests;
                                                       2 existing EOS tests still passed
N2    temperature() returns a constant                 DETECTED, 2 tests -- see below
N3    temperature() drops input validation             DETECTED, 1 test -- see below
N4    perturb R by 1e-6 inside temperature() only      DETECTED, 4 tests;
                                                       3 existing tests still passed
N5    speedOfSound uses gamma = 1                      DETECTED, 2 tests
N6    cv rounded instead of cp - R                     *** VACUOUS -- DID NOT FAIL ***
N6b   cv *= (1 + 1e-12), derived from T1's bound       DETECTED, 2 tests;
                                                       gamma > 1 still passed
```

All restores VERIFIED by SHA-256, 0 markers left, 50/50 at baseline afterwards.

**N6 was a genuine gate failure**, not a pass: `round(cv*1e10)/1e10` returns `cv` bit for
bit, because `cv*1e10 = 7179500000000` is already an exact integer, so the control planted
no defect at all. T1 was therefore unproven until N6b. The replacement was derived from the
bound (`delta_min = 4*eps*R/cv = 1.60 eps`; `delta = 1e-12` gives a 2816x margin), verified
in advance to preserve the T1/T2 asymmetry, and re-run with the criterion unchanged. Full
account in `AMENDMENT_N6_vacuous.md`.

**N4 is the sharpest passing control:** a 1e-6 perturbation of `R`, six orders above the
1e-12 bound and invisible to a casual reader, broke all four temperature criteria while
`MatchesKnownStandardAtmosphereState`, `PressureMatchesRhoRT` and
`PressureToDensityRoundTripRecoversDensity` all still passed — proving the bound's
tightness detects, and that the perturbation stayed confined to `temperature()`.

**N2's two survivors are a numeric coincidence** worth recording: both evaluate at
T = 288.15 and the planted constant was 288.15. The 9-state `ThreeWayClosure` test is the
discriminator; the two single-state tests cannot detect a constant-return defect.

## 8. Test counts

```text
test_ideal_gas_eos.cpp                    21 -> 30    (+9)
test_thermodynamic_properties.cpp         13 -> 18    (+5)
EOS suite (IdealGasEOSTest)               30 / 30
thermodynamics suite                      20 / 20     (ThermodynamicProperties|MachNumber
                                                       |EvaluateDensityField)
P15 constant-property group               13 / 13
compressible suite                       149 / 149    (137 + 12; the other 2 new tests are
                                                       EvaluateDensityFieldTest, which that
                                                       regex does not match)
P14 / P15 / P16 preservation         68 / 161 / 315   unchanged from baseline
```
