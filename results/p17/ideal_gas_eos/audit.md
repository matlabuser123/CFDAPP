# P17.2 — Ideal-Gas EOS: audit and implementation decision

Written 2026-09-26, **before any code was changed**, from `results/p17/baseline/`,
`results/p17/audit/`, `results/p17/gates/P17_GATES.md` and
`results/p17/variable_density/`.

---

## 0. This item was previously closed, and this authorization is broader

An earlier P17.2 ran and passed: `results/p17/compressible/eos/` holds its frozen
acceptance gate, summary, logs and four non-vacuity controls. It added
`pressure(rho, T)` to the `EquationOfState` abstraction (4 files, 165 insertions) and
recorded **PASS**.

That evidence is **preserved unchanged**. This authorization asks for materially more
than it covered, so P17.2 is reopened against the wider scope rather than re-run:

```text
asked for here                                   covered by the earlier P17.2?
--------------------------------------------------------------------------------
(p,T) -> rho, (rho,T) -> p                       YES
T = p / (rho R)  temperature recovery            NO -- the function does not exist
a = sqrt(gamma p / rho) equivalent form          NO -- 0 hits anywhere
explicit valid temperature / pressure ranges     NO -- P17.0 flagged the absence
gamma > 1 guard                                  NOT EXPLICIT (see section 3)
determinism of density()                         NO -- only pressure() has one
constant-property compatibility                  NOT VERIFIED as such
P17.1 density-path integration test               NO
GPU EOS audit                                    NO
```

## 1. What exists, traced

```text
EquationOfState (abstraction)          4 pure virtuals:
  density(pressure, temperature)              rho = p/(R T)
  dDensityDPressure(p, T)                     analytical
  dDensityDTemperature(p, T)                  analytical
  pressure(density, temperature)              p = rho R T     (earlier P17.2)
  temperature(...)                            ABSENT

IdealGasEOS final : EquationOfState     the only implementation
  explicit IdealGasEOS(Real gasConstant)      validates R finite and > 0
  gasConstant()

ThermodynamicProperties(R, cp)          owns the EOS by value (eos_)
  gasConstant()          -> eos_.gasConstant()      R is single-sourced
  specificHeatPressure() -> cp   as supplied
  specificHeatVolume()   -> cv = cp - R    DERIVED, never stored independently
  specificHeatRatio()    -> gamma = cp/cv  DERIVED
  equationOfState()      -> const EquationOfState&
  density(p, T)          -> delegates to eos_
  speedOfSound(T)        -> sqrt(gamma R T), validates T finite and > 0
  machNumber(speed, a)   free function

evaluateDensityField(mesh, p, T, thermo)  per-cell, wraps EOS failures with the CELL INDEX
```

### Production integration

```text
case/config      physics.json "compressible" { gas_constant, specific_heat_pressure, … }
  -> parser      PhysicsConfigParser
  -> model       CompressiblePhysicsConfig -> SimulationSetup.compressible
  -> EOS         ThermodynamicProperties(R, cp) constructed once, holds IdealGasEOS
  -> density     ProjectRunner.cpp:799  evaluateDensityField   (initial / post-hoc)
                 CompressibleSIMPLE.cpp:455-456  thermodynamics_.density(p_abs, T)
                                                 every outer iteration
  -> solver      CompressibleSIMPLE, and the P17.1 face-density / mass-flux path
```

Verdict: **production integrated, single implementation, not duplicated.**

```text
production integrated       YES
test-only                   no
partially integrated        only pressure(rho,T): present on the abstraction, still
                            has NO caller -- expected, it was added for the energy
                            equation and Riemann flux to come
duplicated                  NO -- see section 5 for the GPU check
```

## 2. GPU EOS audit — no duplication, measured

```text
symbol            hits in cuda/ and include/cfd/gpu/
-----------------------------------------------------
gasConstant                0
R * T                      0
speedOfSound               0
idealGas                   0
p / (R                     0
gamma                     35   <- ALL are `gammaFace`, the generic DIFFUSION coefficient
                                  interpolation in DeviceDiffusionKernel.cu. A different
                                  quantity that happens to share the symbol; not the
                                  heat-capacity ratio.
specificHeat               9   <- ALL are the P15 THERMAL convection kernel's cp scaling
                                  (DeviceConvectionKernel.cu), not compressible EOS
                                  arithmetic.
```

**There is no ideal-gas arithmetic on the GPU at all.** The EOS is evaluated on the host
only. Nothing to keep consistent, and no CPU/GPU EOS equivalence test is possible or
needed — recorded so P17.10 does not inherit a phantom obligation.

The `gamma` count is recorded deliberately: a bare census would report 35 hits and
suggest the heat-capacity ratio is already on the device. It is not, in the same way
P17.0's `Roe` and `Euler` counts were false positives.

## 3. Property consistency is guaranteed by construction — derived, not assumed

`ThermodynamicProperties`' constructor:

```cpp
if (!std::isfinite(specificHeatPressure_) || !(specificHeatPressure_ > gasConstant)) {
  throw InvalidArgumentError("… cp must be finite and > gasConstant (R), so that cv = cp - R > 0");
}
specificHeatVolume_ = specificHeatPressure_ - gasConstant;
specificHeatRatio_  = specificHeatPressure_ / specificHeatVolume_;
```

From `R > 0` (enforced by `IdealGasEOS`'s constructor) and `cp > R` (enforced here):

```text
cv = cp - R  > 0                          because cp > R
cp - cv = R                               ALGEBRAICALLY identical, by construction --
                                          cv is never stored independently, so the two
                                          cannot disagree in principle
gamma = cp/cv = cp/(cp - R)
gamma > 1  <=>  cp > cp - R  <=>  R > 0   which is enforced.   THEREFORE gamma > 1
                                          ALWAYS, for every constructible object.
```

### CORRECTION, from the dry-run: `cp - cv = R` is NOT bitwise exact

An earlier draft of this document claimed `cp - cv = R` holds "identically". That is true
algebraically and **false in floating point**, and the dry-run measured it:

```text
gas            R                    cp     cv                    (cp-cv)-R
air            287.05000000000001   1005   717.95000000000005    -5.6843418860808015e-14
helium         2077.0999999999999   5193   3115.9000000000001     0
water vapour   461.5                1996   1534.5                 0
```

For air, `cv = 1005 - 287.05000000000001` rounds to `717.95000000000005`; recovering
`cp - cv` then gives `287.04999999999995`, which differs from the stored `R` by
`-5.68e-14` — **0.89 double-eps relative to R**. Helium and water vapour happen to be
exact because their values round favourably. So the identity is exact for *some* inputs
and one rounding off for others, which is precisely the situation that must not be gated
with an equality.

```text
consequence   the gate bounds (cp - cv) - R by a DERIVED relative tolerance, not by
              equality. See acceptance_gate.md group T.
gamma = cp/cv IS bitwise exact (measured 0 for all three gases), because gamma is STORED
              as that quotient -- recomputing it repeats the same operation on the same
              bits. Equality is the correct criterion there, and is used.
```

Recorded rather than quietly adjusted: the two identities look symmetric and are not, and
a gate that demanded equality of both would have failed on air for a reason that is not a
defect.

So **`gamma > 1` needs no separate guard — it is a theorem, not a check.** Adding an
explicit `gamma > 1` test on the constructor would be unreachable code. What P17.2 adds
instead is a *test* asserting the property holds across a range of admissible `(R, cp)`,
which is the observable form of the same claim.

This is the architecture §4 asks about, and it already exists:

```text
AUTHORITATIVE   R (gas_constant), cp (specific_heat_pressure)   -- the two case-file inputs
DERIVED         cv = cp - R,  gamma = cp/cv
IMPOSSIBLE      supplying cv or gamma independently. There is no such input, so no
                inconsistent combination can be configured.
```

No change to public configuration semantics is needed or made.

## 4. Classification of every P17.2 requirement

```text
requirement                                   class               action
-------------------------------------------------------------------------------------
Audit existing IdealGasEOS                    ALREADY SATISFIED   this document
Verify p-rho-T consistency
  (p,T) -> rho                                ALREADY SATISFIED   verify
  (rho,T) -> p                                ALREADY SATISFIED   verify
  (p,rho) -> T                                MISSING             IMPLEMENT  <- the code
cp - cv = R                                   ALREADY SATISFIED   verify + derivation
gamma = cp / cv                               ALREADY SATISFIED   verify + derivation
a = sqrt(gamma R T)                           ALREADY SATISFIED   verify
a = sqrt(gamma p / rho) equivalence           MISSING             test only
Define valid temperature / pressure ranges    NEEDS EXTENSION     document (section 6)
Add invalid-state guards                      MOSTLY SATISFIED    extend to temperature()
Verify EOS deterministic behaviour            PARTIAL             density() has no
                                                                  determinism test
Verify constant-property compatibility        NEEDS VERIFICATION  test
Add EOS regression tests                      NEEDS EXTENSION     the new suite
P17.1 density integration                     MISSING             integration test
GPU EOS status                                ALREADY ANSWERED    none exists (section 2)
```

**One function is missing. Everything else is verification, testing or documentation.**

## 5. The one implementation: `temperature(pressure, density)`

```text
T = p / (rho R)
```

Placed on the **abstraction**, not the concrete class, for exactly the reason the earlier
P17.2 recorded for `pressure(rho, T)`: generic compressible code — a density-based solver
recovering primitives from a conserved state — must not have to down-cast to a concrete
EOS type to invert the state relation.

### Argument order, decided deliberately

```text
density(Real pressure, Real temperature)      pressure FIRST
pressure(Real density, Real temperature)      pressure is the OUTPUT
temperature(Real pressure, Real density)      pressure FIRST     <- chosen
```

The rule that makes all three consistent: **pressure is the first argument whenever
pressure is an input.** It also matches how the formula reads, `T = p/(rho R)`. Recorded
because the alternative (`temperature(density, pressure)`, mirroring `pressure`'s
density-first order) is equally defensible and a future reader deserves to know a choice
was made rather than stumbled into.

### Validity: the same contract as its two siblings

`p` and `rho` finite and `> 0` on input; the resulting `T` finite and `> 0`. Throws
`InvalidArgumentError` naming the offending field. Identical style, order and messages to
`density()` and `pressure()` — a caller who has learned what one rejects does not have to
learn a second set of rules.

## 6. Validity domain — two domains, not one

This is §7's requirement and the distinction matters.

```text
IdealGasEOS MATHEMATICAL domain
  T > 0, p > 0, rho > 0, R > 0, all finite
  That is the whole domain. p = rho R T has no upper bound and no fitted range: for an
  ideal gas, positivity IS the physical domain. No arbitrary ceiling is invented.

ThermodynamicProperties CORRELATION domain
  There is none. cp is a single constant, not a correlation, so it has no fitted
  temperature range.

P15 thermal::TemperatureDependentProperty CORRELATION domain
  [minimumTemperature, maximumTemperature], carried per property, because cp(T) and k(T)
  ARE polynomial fits and are meaningless outside the range they were fitted over.
  SensibleEnthalpy treats an out-of-range value as an ERROR, never a clamp.
```

**These must not be conflated.** A polynomial property's fitted range is not the EOS's
validity range, and the EOS's positivity requirement is not a material correlation limit.
P17.0 recorded that the EOS has no configured *band*; this audit records *why that is
correct* for an ideal gas, and where a band would have to live if a caloric or real-gas
model were ever added — the `EquationOfState` abstraction currently has nowhere to put one.

## 7. Constant-property compatibility, and P15 reference states

```text
R, cp                 constants by construction; cv, gamma derived constants
h(T)                  P15 SensibleEnthalpy: h = h_ref + integral_{T_ref}^{T} cp dtheta.
                      For CONSTANT cp this reduces exactly to h = h_ref + cp (T - T_ref).
e(T)                  DOES NOT EXIST -- 0 hits for internalEnergy (P17.0 finding,
                      unchanged). e = cv T + ref is NOT implemented anywhere.
```

P17.2 **does not add `e(T)`**, and does not touch P15's reference-state convention. Both
are P17.5's business: `e(T) = h(T) - R T` is one subtraction from P15's exact `h(T)`, but
introducing it here would be building the energy closure ahead of its authorization.
Recorded as the inheritance P17.5 gets, not as a P17.2 gap.

## 8. Plan, in gate order

```text
1. dry-run the gate against the UNCHANGED tree; record reference values at %.17g and
   confirm temperature() is absent so the criterion is non-vacuous
2. freeze acceptance_gate.md with thresholds derived or inherited, never fitted
3. implement temperature(pressure, density) -- smallest correct change
4. non-vacuity controls; each must fail for its intended reason; restore by sha256
5. focused tests: identities together as one state, three round trips, both sound-speed
   forms, invalid states, determinism, constant-property, P17.1 integration
6. G1 preservation incl. the P13-001 SIMPLE fingerprint bitwise
7. regression: Release+CUDA and CPU-only against the P17.1 baseline (2505 / 2305)
8. evidence + TODO.md
```

## 9. Explicitly NOT in P17.2

```text
e(T), T(e), total energy                  -> P17.5
HLLC, wave speeds, conserved state        -> P17.7
density-based solver, CFL control         -> P17.8
the dilatational viscous stress term      -> P17.4
transient compressible continuity         -> P17.3
a GPU EOS path                            -> none exists; outside P17 scope
changing public config semantics          -> unnecessary; the R+cp convention stands
reformatting the 22 pre-existing clang-format failures  -> recorded debt (P17.1 finding)
```
