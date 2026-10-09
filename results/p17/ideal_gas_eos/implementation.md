# P17.2 — implementation

---

## 1. The delta, attributed

Two figures matter and must not be conflated: the **cumulative** delta versus `HEAD`,
which includes the earlier P17.2 session's `pressure(rho,T)` work, and **this session's**
addition.

```text
file                                                    vs HEAD   earlier P17.2   THIS session
------------------------------------------------------------------------------------------------
include/cfd/compressible/EquationOfState.hpp              +53          +24            +29
include/cfd/compressible/IdealGasEOS.hpp                   +8           +5             +3
src/compressible/IdealGasEOS.cpp                          +26          +13            +13
tests/unit/compressible/test_ideal_gas_eos.cpp           +265         +123           +142
tests/unit/compressible/test_thermodynamic_properties.cpp +158           —            +158
------------------------------------------------------------------------------------------------
                                                   5 files      4 files        5 files
                                                   510 ins.     165 ins.       345 ins.
                                                   0 deletions  0 deletions    0 deletions
```

The earlier figures come from `results/p17/compressible/eos/summary.md`, which records
"4 files, 165 insertions, 0 deletions".

**Zero deletions everywhere.** No existing line of production code, test code or contract
was modified or removed — verifiable directly, since `git diff --numstat` reports `-0` for
each file. Of this session's 345 insertions, **13 are production code** (the
`IdealGasEOS::temperature` body) and 32 are interface contract comment; the remaining 300
are tests.

### A file that looks changed and is not

`src/compressible/ThermodynamicProperties.cpp` carries an mtime later than the P17.2
freeze, because controls N5, N6 and N6b wrote to it and restored it. Its content is
**identical to HEAD**, proven two independent ways:

```text
git diff --numstat HEAD -- src/compressible/ThermodynamicProperties.cpp
  -> no output at all, i.e. no delta
sha256  27d74d9e535f8e716421ce24e7f955f32dc8200c5ae5a3ddfd1c9cbdb4da3a88
        == the value the controls script recorded as its baseline
```

So P17.2 changed **five** files, not six.

## 2. The one production addition

```cpp
// EquationOfState.hpp -- a fifth pure virtual
[[nodiscard]] virtual Real temperature(Real pressure, Real density) const = 0;
```

```cpp
// IdealGasEOS.cpp -- the override
Real IdealGasEOS::temperature(Real pressure, Real density) const {
  requirePositiveFinite(pressure, "pressure");
  requirePositiveFinite(density, "density");
  const Real T = pressure / (density * gasConstant_);
  requirePositiveFinite(T, "temperature");
  return T;
}
```

### Why this completes something rather than adding something

`density(p,T)` and `pressure(rho,T)` already existed. With `temperature(p,rho)`, **any one
of `{p, rho, T}` is recoverable from the other two**, so no caller ever has to write the
state relation itself.

That mattered concretely. Measured on the pre-P17.2 tree (`logs/00_dryrun.log` group D),
recovering `T` forced the caller to write `p/(rho*R)` by hand — duplicating the EOS
relation outside the EOS, which is exactly what `EquationOfState`'s own header says the
abstraction exists to prevent:

> "Do not hard-code ideal-gas calculations throughout momentum, continuity, and energy
> code."

The dry-run demonstrated the hand computation and its error: `288.14999999999992` against
a target of `288.14999999999998`, ~2 eps adrift — and that is with the caller getting the
formula *right*.

### On the abstraction, not the concrete class

The same reason the earlier P17.2 recorded for `pressure(rho,T)`: a density-based solver
recovering primitives from a conserved state must not have to down-cast to a concrete EOS
type to invert the state relation. `TemperatureIsReachableThroughTheAbstraction` holds an
`EquationOfState&` and inverts through it.

### Argument order, decided rather than stumbled into

```text
density(pressure, temperature)      p first
pressure(density, temperature)      p is the output
temperature(pressure, density)      p first     <- chosen
```

The rule that makes all three consistent: **pressure is the first argument whenever
pressure is an input.** It also reads in the order of the formula, `T = p/(rho R)`. The
alternative — `temperature(density, pressure)`, mirroring `pressure()`'s density-first
order — is equally defensible, so the choice is recorded in the header rather than left
implicit.

### The result check is not redundant

`requirePositiveFinite(T, "temperature")` catches a case the input checks cannot: `p` and
`rho` can each be finite and strictly positive while `p/(rho R)` **overflows to infinity**.
`TemperatureRejectsAnOverflowingQuotient` exercises it with `p = 1e308, rho = 1e-308`,
mirroring the existing `PressureRejectsAnOverflowingProduct` for the product direction.

## 3. What was deliberately NOT changed

```text
the R + cp configuration convention
  cv and gamma stay DERIVED. There is no input for them, so no inconsistent combination
  can be configured. Public configuration semantics are untouched.

a gamma > 1 or cv > 0 constructor guard
  NOT added, because it would be UNREACHABLE. R > 0 is enforced by IdealGasEOS and
  cp > R by ThermodynamicProperties; together they force cv = cp - R > 0 and
  gamma = cp/(cp-R) > 1. Asserted as observable properties over a sweep instead
  (GammaExceedsOneForEveryAdmissibleCp), which is the testable form of the same claim.

a validity BAND on the EOS
  NOT added. For an ideal gas, positivity IS the physical domain: p = rho R T holds for
  all positive finite arguments. An invented ceiling would reject valid high-pressure
  states. Recorded as a decision in thermodynamic_contract.md section 4, distinct from
  P15's TemperatureDependentProperty correlation range, which exists because cp(T) and
  k(T) are polynomial FITS.

e(T) or T(e)
  NOT added. e(T) = h(T) - R T is one subtraction from P15's exact h(T), but adding it
  here would build the energy closure ahead of its authorization. Gate criterion K4
  asserts internalEnergy stays at 0 hits. P17.5's work.

P15 reference-state conventions
  Enthalpy.hpp and TemperatureDependentProperty.hpp are UNTOUCHED -- 0 changes vs HEAD,
  checked as gate criterion K3. P15's own tests
  (ConstantCpGivesTheLinearForm, RoundTripHoldsForConstantCpToo,
  OutOfRangeEnthalpyIsRefusedNotClamped, ConstantIsTheSingleCoefficientCase) already
  cover constant-property compatibility, so P17.2 CITES and RUNS them rather than
  duplicating them.

any GPU EOS path
  None exists. Measured: 0 hits for gasConstant, speedOfSound, idealGas across cuda/ and
  include/cfd/gpu/. Nothing to keep consistent.
```

## 4. Tests added

```text
test_ideal_gas_eos.cpp                    21 -> 30   (+9)
  TemperatureMatchesPOverRhoR                          C3
  TemperatureToPressureRoundTripRecoversPressure       C6
  ThreeWayClosureHoldsAcrossGasesAndStateRanges        C1-C7, 9 states, 3 directions,
                                                       3 round trips each
  TemperatureIsReachableThroughTheAbstraction          C8
  TemperatureRejectsNonPositivePressureOrDensity       V1, V2
  TemperatureRejectsNonFinitePressureOrDensity         V1, V2
  TemperatureRejectsAnOverflowingQuotient              V3
  TemperatureMessageNamesTheOffendingField             V4
  EveryDirectionIsBitwiseDeterministic                 D1, 64 repeats, all 5 entry points

test_thermodynamic_properties.cpp         13 -> 18   (+5)
  ClosureHoldsTogetherAcrossGases                      T1-T5 on ONE object, 3 gases
  GammaExceedsOneForEveryAdmissibleCp                  T3, T4 over a cp sweep
  SoundSpeedFromTemperatureMatchesSoundSpeed-          A2, 27 states
    FromPressureAndDensity
  EosDerivedDensityReachesTheAuthoritativeField-       I1, I2, and the inverse direction
    WithTheExpectedValue                               recovering T, on a non-uniform field
  RepeatedEvaluationIsBitwiseIdentical                 D2, whole-field, 8 repeats
```

`ClosureHoldsTogetherAcrossGases` is the one that answers the gate's insistence that G2
"should not be marked passed merely because individual formulas have unit tests": it
asserts `cv > 0`, `gamma > 1`, `gamma == cp/cv` and `cp - cv ~= R` **on the same object**,
and recovers `R` from the EOS itself (`pressure(1,1) = R`, exactly) to prove R is
single-sourced.

## 5. Formatting

```text
clang-format-18 on all five changed files    CLEAN (gate G9, exit 0)
deletions after formatting                   still 0 on all five
```

The 22 pre-existing tree-wide clang-format failures found during P17.1 are **not** touched:
none is a P17.2 file, and `results/p13/final-qualification/` still contains zero mentions
of clang-format, so P13's own "full regression PASS" never ran that component. Recorded as
standing debt, not fixed inside P17.2.
