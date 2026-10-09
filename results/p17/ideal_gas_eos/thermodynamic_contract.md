# P17.2 — the ideal-gas thermodynamic contract

The authoritative statement of CFDApp's single-species ideal-gas closure: what is input,
what is derived, what states are valid, and what happens when they are not.

---

## 1. Authoritative inputs

Exactly two, both from `physics.json`'s `compressible` block:

```text
gas_constant             R    J/(kg K)
specific_heat_pressure   cp   J/(kg K)
```

There is **no third input**. `cv` and `gamma` cannot be supplied, so no inconsistent
combination can be configured — the class of error §4 of the authorization warns about is
unreachable by construction rather than rejected by validation.

```text
owner        cfd::compressible::ThermodynamicProperties(R, cp)
             holds an IdealGasEOS BY VALUE, so R is single-sourced:
             ThermodynamicProperties::gasConstant() forwards to eos_.gasConstant()
```

Verified, not assumed: `ClosureHoldsTogetherAcrossGases` recovers `R` from the EOS itself
via `pressure(1,1) = 1 * R * 1` and requires it to equal `gasConstant()` **exactly**.

## 2. Derived properties

```text
cv    = cp - R            computed once in the constructor, stored
gamma = cp / cv           computed once in the constructor, stored
a(T)  = sqrt(gamma R T)   computed on demand
```

## 3. The state relation, in all three directions

```text
rho = density(p, T)        = p / (R T)
p   = pressure(rho, T)     = rho R T
T   = temperature(p, rho)  = p / (rho R)        <- P17.2's addition
```

All three are pure virtuals on `EquationOfState`, so generic compressible code inverts
the state relation **without down-casting** to a concrete EOS type. `IdealGasEOS` is
currently the only implementation.

Two analytical derivatives also exist, used by the compressible pressure-correction
equation's compressibility term:

```text
dDensityDPressure(p, T)    =  1 / (R T)
dDensityDTemperature(p, T) = -p / (R T^2)
```

### Argument-order rule

```text
pressure is the FIRST argument whenever pressure is an INPUT.

  density(pressure, temperature)     p first
  pressure(density, temperature)     p is the output
  temperature(pressure, density)     p first
```

## 4. Valid state domain — two domains, deliberately distinct

This is the distinction §7 of the authorization asks for, and conflating them would be a
real error.

```text
IdealGasEOS MATHEMATICAL domain
  p > 0, rho > 0, T > 0, R > 0, and every one of them finite.
  THAT IS THE WHOLE DOMAIN. p = rho R T holds for all positive finite arguments, so
  there is NO upper bound and NO fitted range. No arbitrary physical ceiling is
  invented; inventing one would reject valid high-pressure states.

ThermodynamicProperties domain
  cp > R, finite. That is what makes cv > 0 and gamma > 1 (see section 5).
  cp is a CONSTANT, not a correlation, so it has no temperature range.

P15 thermal::TemperatureDependentProperty CORRELATION domain
  [minimumTemperature, maximumTemperature], carried per property.
  This exists because cp(T) and k(T) ARE polynomial fits and are meaningless outside the
  interval they were fitted over. SensibleEnthalpy treats an out-of-range value as an
  ERROR, never a clamp.

  A polynomial property's fitted range is NOT the EOS's validity range, and the EOS's
  positivity requirement is NOT a material correlation limit. P17.2 keeps them separate.
```

P17.0 recorded that the EOS carries no configured *band*. This contract records **why
that is correct** for an ideal gas — and where a band would have to live if a caloric or
real-gas model were added, since `EquationOfState` currently has nowhere to put one.

## 5. Guarantees that are theorems, not checks

From `R > 0` (enforced in `IdealGasEOS`'s constructor) and `cp > R` (enforced in
`ThermodynamicProperties`'s constructor):

```text
cv = cp - R > 0                            because cp > R
gamma = cp/(cp - R) > 1  <=>  R > 0        which is enforced
```

So **`cv > 0` and `gamma > 1` hold for every constructible object.** No separate guard is
added, because such a guard would be unreachable code. They are instead asserted as
*observable* properties over a sweep of admissible `(R, cp)` —
`GammaExceedsOneForEveryAdmissibleCp`.

### And one identity that is NOT exact — measured

```text
gamma == cp/cv        EXACT. gamma is STORED as that quotient, so recomputing it repeats
                      one operation on unchanged bits. Gated with ==.
cp - cv  ~=  R        NOT exact. cv is stored as cp - R (one rounding); recovering
                      cp - cv rounds again, and the loss cannot be undone.
                      Measured, air: (cp-cv)-R = -5.6843418860808015e-14
                                              = 0.89 eps relative to R
                      Helium and water vapour happen to be exactly 0.
                      Gated at 4 eps relative, DERIVED from two roundings of quantities
                      of magnitude ~cp (cp/R ~ 3.5 for air, so ~3.5 eps is the floor).
```

The two identities look symmetric and are not. A gate demanding equality of both would
have failed on air for a reason that is not a defect. This was caught by the dry-run and
corrected the audit before the gate was frozen.

## 6. Failure semantics

```text
mechanism    throw cfd::InvalidArgumentError
message      names the offending field, e.g.
             "IdealGasEOS: temperature must be finite and > 0"
             evaluateDensityField wraps failures with the offending CELL INDEX
NEVER        clamp, floor, substitute, or return a sentinel
```

Every entry point validates its **inputs** and its **result**:

```text
density(p,T)        validates p, T, and the resulting rho
pressure(rho,T)     validates rho, T, and the resulting p  (catches product OVERFLOW)
temperature(p,rho)  validates p, rho, and the resulting T  (catches quotient OVERFLOW)
speedOfSound(T)     validates T
IdealGasEOS(R)      validates R
ThermodynamicProperties(R, cp)   validates cp finite and > R
```

The result check is not redundant: `p` and `rho` can each be finite and positive while
`p/(rho R)` overflows to infinity — `TemperatureRejectsAnOverflowingQuotient` exercises
exactly that, mirroring the existing `PressureRejectsAnOverflowingProduct`.

### The three validation layers, kept separate

```text
CONFIGURATION / INPUT VALIDATION   the constructors and the per-call input checks above.
                                   Rejects physically impossible configuration outright.
RUNTIME EOS STATE VALIDATION       the per-call result checks. A state that became
                                   invalid during a solve is rejected where it is
                                   produced, not where it is eventually noticed.
INTERNAL NUMERICAL PROTECTION      CompressibleSIMPLE catches the EOS's
                                   InvalidArgumentError and reports NonFiniteState with a
                                   diagnostic detail string, plus an allFinite() sweep.
                                   NOTE its measured limit (P17.1): allFinite() cannot
                                   see a FINITE-but-invalid value, which is why input
                                   validation is the load-bearing layer.
PHYSICAL CLIPPING                  DOES NOT EXIST, deliberately. A clamped state yields
                                   results no thermodynamic state corresponds to and lets
                                   a diverging solve look plausible -- the reasoning
                                   thermal/Enthalpy.hpp already records. A positivity
                                   LIMITER on a conserved state belongs to P17.8.
```

## 7. Reference-state assumptions

```text
IdealGasEOS            NONE. p = rho R T has no reference state.
pressure convention    SIMPLE solves GAUGE pressure; the EOS consumes and produces
                       ABSOLUTE pressure; `referencePressure` is the single documented
                       bridge (CompressibleSIMPLE.hpp, PhysicsConfig.hpp,
                       ProjectRunner's pressureAbsolute). P17.2 does not change this.
P15 enthalpy           h(T) = h_ref + integral_{T_ref}^{T} cp dtheta, with h_ref and
                       T_ref owned by SensibleEnthalpy. For CONSTANT cp this reduces
                       exactly to h = h_ref + cp (T - T_ref), which P15 already tests
                       (SensibleEnthalpyTest.ConstantCpGivesTheLinearForm).
                       P17.2 does NOT touch this convention.
e(T)                   DOES NOT EXIST. 0 hits for internalEnergy, verified as gate
                       criterion K4. e = cv T + ref is NOT implemented, and P17.2 does
                       not add it: e(T) = h(T) - R T is one subtraction from P15's exact
                       h(T), but introducing it here would build the energy closure ahead
                       of its authorization (P17.5).
```

## 8. Where the closure is evaluated

```text
CPU    ThermodynamicProperties / IdealGasEOS, host-side only.
       ProjectRunner.cpp:799    evaluateDensityField   (initial / post-hoc)
       CompressibleSIMPLE.cpp:455-456  per outer iteration
       CompressibleMassFlux     boundary face densities
GPU    NOTHING. Measured: gasConstant 0, speedOfSound 0, idealGas 0 hits across cuda/
       and include/cfd/gpu/. The 35 `gamma` hits are `gammaFace`, the generic DIFFUSION
       coefficient; the 9 `specificHeat` hits are P15's thermal convection kernel.
       There is no duplicated EOS arithmetic to keep consistent, and no CPU/GPU EOS
       equivalence test is possible or needed.
DUPLICATION   NONE on the production path: 0 hits for p/(R*T)-style arithmetic outside
              IdealGasEOS and ThermodynamicProperties (gate criterion I3).
```
