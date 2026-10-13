# P17.2 — Ideal-Gas EOS: summary

**P17.2 STATUS: COMPLETE.** 2026-09-26.

Evidence root: `results/p17/ideal_gas_eos/`. Gate frozen before any edit,
sha256 `1a0aefe24b7e41c2c302dfc25f67bf91faf6af9622dffb98a52be2d4dfb25c12`.

The earlier P17.2 evidence at `results/p17/compressible/eos/` is **preserved unchanged**;
this item reopened P17.2 against a broader authorization. See `audit.md` §0.

---

## 1. What P17.2 turned out to be

Twelve of thirteen requirements were already satisfied. **The production change is 13
lines** — one function completing the state closure.

```text
requirement                                class               outcome
------------------------------------------------------------------------------------
Audit existing IdealGasEOS                 ALREADY SATISFIED   audit.md
(p,T) -> rho                               ALREADY SATISFIED   verified, 1.02 eps
(rho,T) -> p                               ALREADY SATISFIED   verified, 0.96 eps
(p,rho) -> T                               MISSING             IMPLEMENTED  <- the code
cp - cv = R                                ALREADY SATISFIED   verified, 0.89 eps
gamma = cp/cv                              ALREADY SATISFIED   verified, EXACT
a = sqrt(gamma R T)                        ALREADY SATISFIED   verified
a = sqrt(gamma p/rho) equivalence          MISSING             test added, 0.90 eps
valid temperature / pressure ranges        NEEDS EXTENSION     documented
invalid-state guards                       MOSTLY SATISFIED    extended to temperature()
EOS deterministic behaviour                PARTIAL             extended, bitwise
constant-property compatibility            NEEDS VERIFICATION  verified via P15's own tests
EOS regression tests                       NEEDS EXTENSION     14 added
GPU EOS status                             ALREADY ANSWERED    none exists
```

## 2. EOS architecture

```text
Authoritative EOS class      cfd::compressible::IdealGasEOS, the only implementation of
                             the EquationOfState abstraction
Authoritative R source       physics.json compressible.gas_constant -> IdealGasEOS.
                             ThermodynamicProperties holds the EOS by value and forwards
                             gasConstant(), so R is SINGLE-SOURCED. Verified by recovering
                             R from the EOS itself: pressure(1,1) == gasConstant(), exactly.
cp source                    physics.json compressible.specific_heat_pressure, stored
cv source                    DERIVED, cp - R, never stored independently
gamma source                 DERIVED, cp/cv
speed of sound               ThermodynamicProperties::speedOfSound(T) = sqrt(gamma R T);
                             the equivalent sqrt(gamma p/rho) now verified to agree
validity policy              p > 0, rho > 0, T > 0, R > 0, all finite. NO upper bound and
                             NO fitted range -- for an ideal gas positivity IS the physical
                             domain. Kept DISTINCT from P15's polynomial correlation range.
failure policy               throw InvalidArgumentError naming the offending field; inputs
                             AND result checked on every entry point. NEVER clamped.
```

## 3. Verified identities — worst case over 27 states

```text
p - rho*R*T                          2.2737367544323206e-16    1.02 eps   bound 1e-12
temperature(p,rho) - T      NEW      1.9727023724035406e-16    0.89 eps   bound 1e-12
(cp - cv) - R                        5.6843418860808015e-14    0.89 eps   bound 4 eps
gamma - cp/cv                        0                         EXACT
a - sqrt(gamma p/rho)       NEW      2.0051667127585680e-16    0.90 eps   bound 8 eps
round trip (p,T)->rho->p             2.2737367544323206e-16    1.02 eps   bound 1e-12
round trip (rho,T)->p->rho           2.1263572769214529e-16    0.96 eps   bound 1e-12
round trip (p,rho)->T->p    NEW      1.4361623714154308e-16    0.65 eps   bound 1e-12
```

3 gases x 3 pressures x 3 temperatures. Every residual is about **one double-eps** — the
arithmetic floor. Nothing sits near its bound.

## 4. Validity and negative tests

```text
invalid T (0, <0, NaN, +Inf)          all 4 REJECTED
invalid p (0, <0, NaN, +Inf)          all 4 REJECTED
invalid rho (0, <0, NaN, +Inf)        all REJECTED, via the new temperature() guards
invalid R (0, <0, NaN, +Inf)          all 4 REJECTED at construction
non-finite inputs                     REJECTED on every entry point
inconsistent cp/cv                    UNREACHABLE by configuration: cv and gamma cannot be
                                      supplied, and cp <= R is rejected at construction
gamma <= 1                            UNREACHABLE: R > 0 and cp > R force gamma > 1. No
                                      guard added -- it would be dead code. Asserted as an
                                      observable property over a cp sweep instead.
overflowing quotient          NEW     REJECTED (p = 1e308, rho = 1e-308)
clamping                              NONE anywhere
```

## 5. Compatibility

```text
P17.1 variable-density integration   VERIFIED. A known (p,T,R) reaches the authoritative
                                     density field at the expected value on a mesh with p
                                     AND T non-uniform per cell; the new temperature()
                                     recovers the T that produced each density. R proven
                                     single-sourced. 0 duplicated p/(R*T) arithmetic on the
                                     production path.
P15 thermodynamic compatibility      VERIFIED via P15's OWN tests, not duplicated:
                                     ConstantCpGivesTheLinearForm, RoundTripHoldsFor-
                                     ConstantCpToo, OutOfRangeEnthalpyIsRefusedNotClamped,
                                     ConstantIsTheSingleCoefficientCase -- 13/13.
                                     Enthalpy.hpp and TemperatureDependentProperty.hpp
                                     UNCHANGED. Reference-state conventions untouched.
                                     e(T) remains ABSENT (0 hits), as P17.5's work.
constant-property compatibility      VERIFIED. R, cp, cv, gamma are constants; the
                                     constant-cp case reduces exactly to
                                     h = h_ref + cp (T - T_ref).
determinism                          BITWISE. All five EOS entry points over 64 repeats;
                                     whole-field evaluateDensityField over 8 repeats.
```

## 6. Tests and gates

```text
focused EOS tests          30 / 30    (21 + 9 new)
thermodynamic tests        20 / 20    (13 + 5 new)
P15 constant-property      13 / 13    existing, cited
compressible suite        149 / 149   (137 + 12; the other 2 new tests are
                                       EvaluateDensityFieldTest, outside that regex)
CPU regression           2319 / 2319  0 failed   PREDICTED 2319
CUDA regression          2519 / 2519  0 failed   PREDICTED 2519
G1  baseline preservation  PASS -- P13-001 SIMPLE fingerprint BITWISE IDENTICAL, empty
                           diff; P14 68/68, P15 161/161, P16 315/315
G2  thermodynamic closure  PASS -- and verified as ONE coherent state, not four separate
                           formula tests: ClosureHoldsTogetherAcrossGases asserts cv > 0,
                           gamma > 1, gamma == cp/cv and cp - cv ~= R on the SAME object,
                           for three gases
G10 full regression        PASS on both mandated configurations; Debug+GUI NOT RUN
```

Every registry and execution count landed on a number frozen **before** the run:

```text
Release+CUDA   2581 registered - 13 excluded - 49 disabled = 2519 executed    exact
CPU-only       2379 registered - 13 excluded - 47 disabled = 2319 executed    exact
```

## 7. Controls — and one genuine gate failure

```text
N1   temperature() returns p/rho                DETECTED  4 tests; 2 existing passed
N2   temperature() returns a constant           DETECTED  2 tests
N3   temperature() drops input validation       DETECTED  1 test
N4   perturb R by 1e-6 inside temperature()     DETECTED  4 tests; 3 existing passed
N5   speedOfSound with gamma = 1                DETECTED  2 tests
N6   cv rounded instead of cp - R               *** VACUOUS -- DID NOT FAIL ***
N6b  cv *= (1 + 1e-12), derived from T1's bound DETECTED  2 tests; gamma > 1 passed
```

**N6 was a real gate failure.** `round(cv*1e10)/1e10` returns `cv` bit for bit, because
`cv*1e10 = 7179500000000` is already an exact integer — the control planted **no defect at
all**, so criterion T1 was unproven. Preserved, diagnosed to the exact arithmetic, and
replaced with a perturbation **derived from T1's own bound**
(`delta_min = 4*eps*R/cv = 1.60 eps`; `delta = 1e-12` gives a 2816x margin), verified in
advance to preserve the T1/T2 asymmetry, then re-run with the criterion untouched. Full
account: `AMENDMENT_N6_vacuous.md`.

## 8. Findings beyond the checklist

```text
1. cp - cv = R IS NOT BITWISE EXACT. Measured for air: (cp-cv)-R = -5.6843e-14, 0.89 eps.
   cv is stored as cp - R (one rounding); recovering cp - cv rounds again. gamma = cp/cv
   IS exact, because gamma is stored as that quotient. The two identities look symmetric
   and are not. The dry-run caught this and CORRECTED audit.md before the gate was frozen;
   a gate demanding equality of both would have failed on air for a non-defect.

2. gamma > 1 and cv > 0 ARE THEOREMS, not checks. R > 0 and cp > R are both enforced, and
   together they force cv = cp - R > 0 and gamma = cp/(cp-R) > 1. A constructor guard would
   be unreachable code, so none was added; they are asserted as observable properties over
   a cp sweep instead.

3. NO EOS ARITHMETIC ON THE GPU. Measured: gasConstant 0, speedOfSound 0, idealGas 0 hits
   across cuda/ and include/cfd/gpu/. The 35 `gamma` hits are `gammaFace`, the generic
   DIFFUSION coefficient; the 9 `specificHeat` hits are P15's thermal convection kernel.
   So P17.10 inherits no CPU/GPU EOS equivalence obligation -- there is nothing to compare.

4. TWO SINGLE-STATE TESTS CANNOT DETECT A CONSTANT-RETURN DEFECT.
   TemperatureMatchesPOverRhoR and TemperatureToPressureRoundTripRecoversPressure both
   evaluate at T = 288.15, and control N2's planted constant was 288.15 -- a numeric
   coincidence, the same trap P17.1 found in InternalFaceUsesArithmeticMeanDensity. The
   9-state ThreeWayClosure test is the discriminator; do not delete it as redundant.

5. FOR temperature(), INPUT VALIDATION BUYS DIAGNOSTIC QUALITY, NOT REJECTION. Control N3
   showed the RESULT check alone rejects every invalid case (p=0 -> T=0; rho=0 -> T=inf;
   NaN propagates). Only the field naming degrades, which is why the message test exists.

6. THE VALIDITY DOMAIN IS TWO DOMAINS. The ideal-gas mathematical domain (positivity only,
   no upper bound) must not be conflated with P15 TemperatureDependentProperty's fitted
   [Tmin, Tmax], which exists because cp(T) and k(T) are polynomial FITS. Documented
   rather than merged, and no arbitrary ceiling invented.
```

## 9. Repository impact

```text
source files added      0
source files modified   3   (EquationOfState.hpp, IdealGasEOS.hpp, IdealGasEOS.cpp)
tests modified          2   (test_ideal_gas_eos.cpp, test_thermodynamic_properties.cpp)
tests added            14   (9 EOS + 5 thermodynamics)
evidence files         ~40  (8 documents, 9 tools, raw logs, 1 amendment)
threshold changes       NONE
```

```text
this session      5 files, 345 insertions, 0 deletions
cumulative vs HEAD 5 files, 510 insertions, 0 deletions  (includes the earlier P17.2's 165)
production code    13 lines (the IdealGasEOS::temperature body)
tracked modified   170 -> 171   untracked 107 -> 107   tracked eng 119 -> 120
binaries added     0
```

`src/compressible/ThermodynamicProperties.cpp` has a post-freeze mtime because controls N5,
N6 and N6b wrote to it and restored it. Its content is **identical to HEAD** — no git delta,
and sha256 `27d74d9e…` matches the controls' recorded baseline. Five files changed, not six.

## 10. Recommended next step

**P17.3 — compressible continuity verification.** Freeze its gate and dry-run it first.

Why P17.3: it is next in the authorized working order, and the closure P17.2 just completed
is what continuity's verification will lean on — one authoritative EOS with all three
directions, a validated density precondition from P17.1, and one canonical face mass flux.

The hazard it must design against, carried from P17.0 and still true: **the transient form
`d(rho)/dt + div(rho U) = 0` has no production caller.**
`evaluateCompressibleContinuity` is a diagnostic by its own header, reached only from tests,
while `CompressibleSIMPLE.cpp:478` calls the **steady** `physics::evaluateContinuity`. A
P17.3 gate that exercised only the diagnostic would verify a library function while
claiming to verify the solver.

**Not started.** P17.3 has no implementation authorization.
