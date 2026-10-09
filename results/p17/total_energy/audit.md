# P17.5 — audit of existing energy infrastructure

Measured on the unchanged tree, `HEAD 3bd32f918656`, before any total-energy code existed.
Everything below is from `logs/00_audit.log` (`tools/audit.sh`, read-only on production source).

---

## 1. There is no total-energy state, and no compressible energy equation

```text
symbol                  hits across include/ + src/
------------------------------------------------------
rhoE                    0
totalEnergy             0
internalEnergy          0
specificEnergy          0
totalEnthalpy           0
eTotal                  0
hTotal                  0
pressureWork            0
viscousWork             0
viscousDissipation      0
```

And inside the compressible module specifically:

```text
rhoE              0
totalEnergy       0
EnergyEquation    0
SensibleEnthalpy  0
```

`CompressibleSIMPLE.hpp` says so in its own words:

```text
line 144   "never updated by this solver itself (no energy-equation"
line 146   "compressible-energy formulation" is explicitly out of scope for
```

**Temperature is an input to the compressible solver, not a solved field.** It is either
spatially uniform (isothermal) or an already-converged field handed in from the P15 thermal
block. So P17.5 is genuinely MISSING work, not an extension of something partly there.

## 2. What P15 does provide, and is to be reused rather than duplicated

```text
EnergyEquation                          15 files   the thermal (temperature) equation
SensibleEnthalpy                         3 files   h(T) and its exact inverse T(h)
TemperatureDependentProperty             8 files   cp(T), k(T) polynomials
ThermalProperties                       18 files   constant k, cp
TurbulentConductivity                    5 files   k_t
assembleThermalConvectionContribution    7 files
assembleThermalDiffusionContribution     4 files
assembleThermalTransientContribution     2 files
```

`SensibleEnthalpy` is exactly the thermodynamic machinery this phase needs:

```text
h(T) = h_ref + integral_{T_ref}^{T} cp(theta) dtheta      CLOSED FORM, exact term by term
                                                          for a polynomial cp, no quadrature
T(h)                                                      bisection, unique because cp > 0 is
                                                          enforced, tolerance 1e-12 K
out of range                                              an ERROR, never a clamp
```

The authorities to reuse, all present:

```text
IdealGasEOS                       P17.2   p, rho, T closure
ThermodynamicProperties           P17.2   R, cp, cv = cp - R, gamma = cp/cv, speed of sound
SensibleEnthalpy                  P15     h(T), T(h)
ThermalProperties                 P15     k, cp
TemperatureDependentProperty      P15     cp(T), k(T)
calculateCompressibleMassFlux     P17.3   the authoritative m_dot_f
assembleDilatationalStressContribution  P17.4  the -(2/3) mu grad(div U) stress
```

## 3. TWO PRE-EXISTING DEFECTS this audit found, neither of them P17.5's to fix

### 3a. The production energy equation transports `cp*T`, not `h(T)`

`Enthalpy.hpp` was written specifically to say that this is wrong, in its own header:

```text
line 16    h    = cp * T                                              wrong when cp varies
```

with the reason spelled out: for `cp(T) = b0 + b1 T` the true integral carries
`b1/2 (T^2 - T_ref^2)` while the naive product carries `b1 T^2` — a factor of two in the
quadratic term, so *"a solver that used the product would conserve something, just not energy."*

But `assembleThermalConvectionContribution` still scales the mass flux by `cp` and transports
`cp*T`, and `ThermalSolver.cpp` calls it with a `cp(T)` **field**
(`evaluatePropertyField(mesh, temperature, specificHeatModel)`, line 320/450). So the production
path evaluates cp at the upwind cell and multiplies — the form its own sibling header calls wrong.

### 3b. `SensibleEnthalpy` is unreachable from a case file

```text
SensibleEnthalpy consumers
  src/io            0     <- no case key builds one
  src/app           0     <- no runner constructs one
  src/thermal       1     <- its own implementation
  tests             2
```

The correct machinery exists, is unit-tested (13 TESTs), and **no production path uses it.** This
is the capability-vs-reachability pattern: a feature no case file can reach is not shipped
capability.

```text
CONSEQUENCE FOR P17.5, and it is a real constraint rather than an excuse:
  building rhoE on SensibleEnthalpy means building on a LIBRARY-ONLY path. That is the same
  status pseudoTimeStep and includeDilatationalStress already have, and it must be stated as
  such rather than described as "coupled to the solver".
  Repairing 3a -- switching the production thermal convection from cp*T to h(T) -- would change
  every variable-cp thermal result in the qualified baseline. That is NOT P17.5's authorized
  scope and is recorded as follow-on work.
```

## 4. The dual-`cp` hazard, measured

Two independent case keys, parsed separately:

```text
physics.thermal.specific_heat           -> a TemperatureDependentProperty, cp(T), used by P15
physics.compressible.specific_heat_pressure -> one Real, constant cp, used by P17.2
```

```text
cross-validation sites: 0     nothing anywhere requires the two to agree
```

How they meet at runtime, in `ProjectRunner.cpp`:

```text
770   temperatureField[i] = thermalResult->temperature[i]   <- T from the P15 solve, which
                                                              used thermal.specific_heat
800   evaluateDensityField(..., temperatureField, c.thermodynamics)
833   c.thermodynamics.speedOfSound(temperatureField[i])    <- consumed via
                                                              compressible.specific_heat_pressure
```

So in a **thermal-coupled compressible case** the temperature is produced with one `cp` and
consumed with another, and a case supplying `thermal.specific_heat = 1005` alongside
`compressible.specific_heat_pressure = 1200` runs silently.

This is the blocker TODO.md records as *"decide before P17.5"*, and it has to be settled first
because `rhoE = rho (e + ke)` is defined through `cp`: an ambiguous `cp` makes `rhoE` ambiguous.
The decision is in `thermodynamic_contract.md` §1.

## 5. GPU: there is nothing to be equivalent to

```text
symbol                    hits in cuda/ + include/cfd/gpu/
----------------------------------------------------------
compressible              1    <- a COMMENT in DevicePressureCorrection.hpp reading
                                  "the body behind the incompressible", i.e. a false positive
IdealGas                  0
EquationOfState           0
ThermodynamicProperties   0
Enthalpy                  0
rhoE                      0
totalEnergy               0
machNumber                0
```

There is **no GPU compressible path of any kind** — the same structural finding P17.4's G8 made
for momentum, and what TODO.md already records as *"P17.10 is vacuous — no GPU compressible path
exists"*.

The nearest reusable GPU machinery is the generic scalar-transport family
(`DeviceScalarTransportKernel.cu`, `DeviceConvectionKernel.cu`, `DeviceDiffusionKernel.cu`),
and `DeviceScalarTransport.hpp` states that `assembleScalarConvectionDevice` **ports
`thermal::assembleThermalConvectionContribution`** — i.e. the GPU already carries the `cp*T` form
of defect 3a.

```text
So a GPU total-energy path would require, in order: a GPU EOS, a GPU compressible mass flux, a
GPU compressible momentum assembly, and a GPU compressible solver to drive them. None exists.
That is examined here rather than asserted, and §27 of the authorization is answered on the
evidence: not deferred by preference, blocked by absence.
```

## 6. Requirement classification

```text
requirement                                   status              basis
---------------------------------------------------------------------------------------------
Introduce total-energy state                  MISSING             rhoE 0 hits
Define internal energy                        MISSING             internalEnergy 0 hits
Define kinetic energy                         MISSING             the 2 kineticEnergy hits are
                                                                  turbulent k, not 0.5|U|^2
Define total energy                           MISSING             totalEnergy 0 hits
Define total enthalpy                         MISSING             totalEnthalpy 0 hits
Conservative total-energy equation            MISSING             no energy equation on the
                                                                  compressible path at all
Couple energy to EOS                          NEEDS EXTENSION     IdealGasEOS exists (P17.2);
                                                                  nothing connects it to energy
Couple pressure work                          MISSING             pressureWork 0 hits
Couple thermal conduction                     NEEDS EXTENSION     P15 conduction + k(T) exist and
                                                                  are qualified; not wired to a
                                                                  total-energy equation
Couple viscous work                           MISSING             viscousWork 0 hits; and P17.4
                                                                  left the tensor stress
                                                                  deliberately incomplete
Temperature recovery from conserved vars      NEEDS EXTENSION     SensibleEnthalpy::temperature(h)
                                                                  is exact but inverts h, not e,
                                                                  and is library-only
Validate thermodynamic closure                NEEDS EXTENSION     P17.2 verified p = rho R T and
                                                                  cp - cv = R; nothing ties
                                                                  h/e/rhoE into one state
Add energy conservation tests                 MISSING
Add CPU/GPU equivalence                       BLOCKED BY ABSENCE  no GPU compressible path
---------------------------------------------------------------------------------------------
MISSING 8   NEEDS EXTENSION 4   BLOCKED 1   ALREADY SATISFIED 0
```

Nothing is ALREADY SATISFIED. P15's conduction is the closest, and it is conduction *for a
temperature equation*, which is not the same term in a total-energy equation.

## 7. Baselines P17.5 must preserve

```text
tests/unit/thermal/test_enthalpy_properties.cpp             13 TESTs
tests/unit/compressible/test_thermodynamic_properties.cpp   18 TESTs
tests/unit/compressible/test_ideal_gas_eos.cpp              30 TESTs
Release+CUDA  2608 registered / 2546 executed   (the P17.4 close)
CPU-only      2406 registered / 2346 executed
compressible suite 176 · P14 68 · P15 161 · P16 315
P13-001 SIMPLE fingerprint: BITWISE IDENTICAL, empty diff
```
