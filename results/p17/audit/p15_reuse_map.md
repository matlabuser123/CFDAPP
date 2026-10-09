# P17.0 — P15 thermal / energy reuse map

What P15 actually provides, read from the implementation. The authorization asks for
one distinction above all others, and it is the organising principle of this document:

```text
THERMODYNAMIC PROPERTY SUPPORT        P15 has this, and it is good
COMPRESSIBLE TOTAL-ENERGY CONSERVATION P15 does NOT have this
```

P15 supplies the former without implementing the latter. Neither fact is allowed to
stand in for the other.

---

## 1. What P15 provides

```text
include/cfd/thermal/
  EnergyEquation.hpp              steady + transient TEMPERATURE transport
  Enthalpy.hpp                    SensibleEnthalpy: h(T) and T(h)
  TemperatureDependentProperty.hpp polynomial property with a validity range
  ThermalProperties.hpp           constant k, cp, thermalDiffusivity(rho)
  ThermalSolver.hpp               the outer thermal solve
  ThermalWallFunction.hpp         wall treatment
  TurbulentConductivity.hpp       k_t from the turbulence model
  ThermalInterface / ThermalRegion / ThermalRegionMap   conjugate regions
```

### SensibleEnthalpy — the most valuable piece for P17

```text
h(T)   h_ref + integral_{T_ref}^{T} cp(theta) dtheta, in CLOSED FORM:
       for cp(T) = sum_i b_i T^i,  h - h_ref = sum_i b_i/(i+1) (T^{i+1} - T_ref^{i+1})
       exact term by term -- no quadrature, so no quadrature error
T(h)   by BISECTION on [Tmin, Tmax]. h is strictly increasing wherever cp > 0, which
       the property model guarantees by refusing a non-positive polynomial over its
       range, so the inverse exists, is unique, and bisection cannot fail to converge.
       Bisection over Newton deliberately: no derivative needed, cannot overshoot
       outside the validity range, and the iteration count is bounded.
bound  the absolute tolerance in kelvin is EXPOSED, "so a test can assert the round
       trip against the documented bound rather than a number it discovered"
```

And the design decision P17.5 should inherit verbatim:

> An enthalpy outside `[h(Tmin), h(Tmax)]` is an **ERROR, not a clamp**. Clamping
> would return a temperature the energy does not correspond to and let a diverging
> solve look converged.

That is exactly the failure mode a compressible energy equation must avoid, and P15
already decided it correctly.

### Why Enthalpy.hpp exists at all — a trap P17.5 will meet again

The pre-existing energy equation transports `cp*T`, scaling the mass flux by cp. For
**constant** cp that equals transporting enthalpy. For cp(T) it does not:

```text
h(T) = h_ref + integral cp(theta) dtheta      correct
h    = cp * T                                 wrong when cp varies
```

With `cp(T) = b0 + b1 T` the true integral carries `b1/2 (T^2 - T_ref^2)` and the
naive product carries `b1 T^2` — a factor of two in the quadratic term. In the
header's own words, a solver using the product "would conserve something, just not
energy."

**This trap recurs one level up in P17.5.** P14 diffuses the transported variable
itself, but conduction is driven by `grad(T)`, not `grad(e_t)`. Those are different
operators unless cv is constant. See `p14_reuse_map.md` §3 item 2.

### Transient temperature — present

```text
assembleThermalTransientContribution(mesh, ThermalTimeState{transient, dt, ...})
  a_P += rho_P^{n+1} cp_P^{n+1} V_P / dt
  b_P += rho_P^n     cp_P^n     T_P^n V_P / dt
```

Conservative in the same two-density sense as P14's, and a steady solve
(`transient = false`) is bitwise unchanged by its presence.

### Other reusable pieces

```text
TemperatureDependentProperty   polynomial, with minimumTemperature/maximumTemperature,
                               evaluate(T), evaluateField(...), isConstant();
                               implements physics::TemperatureProperty, so k(T) and
                               mu(T) already flow through one abstraction
Robin BC                       boundary/ConvectiveHeatTransfer.hpp (h, T_inf)
HeatFlux, FixedTemperature     existing scalar BC types
TurbulentConductivity          k_t, already wired to the turbulence models
ThermalRegion / Interface      conjugate heat transfer across regions
```

## 2. Does P15 provide what P17.5 needs?

```text
quantity  P15 provides?   how
-------------------------------------------------------------------------------
cp        YES             TemperatureDependentProperty, polynomial cp(T)
                          AND compressible::ThermodynamicProperties, constant cp
k         YES             TemperatureDependentProperty, polynomial k(T); plus
                          TurbulentConductivity for k_t
h(T)      YES             SensibleEnthalpy::enthalpy, exact closed form
T(h)      YES             SensibleEnthalpy::temperature, bisection, error-not-clamp
R         YES             compressible::ThermodynamicProperties::gasConstant
cv        PARTIAL         only as the CONSTANT cp - R, from ThermodynamicProperties.
                          No cv(T) anywhere.
gamma     PARTIAL         only as the CONSTANT cp/cv. No gamma(T).
e(T)      NO              0 hits for internalEnergy. Must be added.
T(e)      NO              Must be added.
```

### The two genuinely missing pieces, and how close P15 gets

```text
e(T)   For an ideal gas  e = h - p/rho = h(T) - R T.
       So e(T) is one subtraction away from P15's exact h(T), and it inherits the
       closed form for free. No quadrature, no new integration.
T(e)   e(T) is strictly increasing wherever cv = cp - R > 0. The property model
       already guarantees cp > 0 over its range, but it does NOT guarantee
       cp - R > 0 -- that is a NEW admissibility condition P17.5 must check, and it
       is exactly the kind of clause a gate should carry rather than assume.
       Given it, the same bisection structure inverts e as it inverts h:
       monotone, bracketed on [Tmin, Tmax], error-not-clamp, documented tolerance.
```

Verdict: **REUSE WITH ADAPTATION.** Not direct — `SensibleEnthalpy::temperature`
inverts h, not e, so it cannot simply be called. But the algorithm, the validity-range
discipline, the error-not-clamp decision and the exposed-tolerance convention should
all be reproduced rather than reinvented, and the closed-form `h(T)` is reused
outright.

## 3. What P15 does NOT provide — stated plainly

```text
a conservative total-energy equation           NO. EnergyEquation transports
                                               TEMPERATURE, steady or transient.
rho e_t as a conserved variable                NO. 0 hits for rhoE / totalEnergy.
kinetic energy in the energy balance           NO. 0.5|U|^2 appears nowhere.
pressure work div(pU)                          NO. 0 hits.
viscous work / dissipation div(tau.U)          NO. 0 hits.
two-way energy <-> flow coupling               NO. Temperature is a read-only input
                                               to the compressible path
                                               (SimulationSetup.hpp:59).
a device-side energy equation                  NO. DeviceTurbulence and
                                               DeviceScalarTransport exist; there is
                                               no DeviceThermal or DeviceEnergy.
```

Per the authorization: **a thermal temperature equation must not be classified as a
compressible total-energy equation.** It is not, here.

## 4. A latent inconsistency P17.5 must resolve — FINDING

Two independent homes for cp can be configured in one case file, and nothing
reconciles them.

```text
compressible block   CompressiblePhysicsConfig::specificHeatPressure   a single Real
thermal block        ThermalPhysicsConfig::specificHeatModel           optional
                     TemperaturePolynomialConfig -- i.e. cp(T)
```

`compressible.thermal_coupled = true` *requires* the thermal block to be present
(`PhysicsConfigParser`'s cross-check). So a case may legitimately declare:

```text
"compressible": { "thermal_coupled": true, "specific_heat_pressure": 1005.0 }
"thermal":      { "specific_heat": { polynomial cp(T) } }
```

The EOS then derives `cv`, `gamma` and `a = sqrt(gamma R T)` from the **constant**
1005.0, while the temperature field it consumes was produced with a **varying** cp(T).
Nothing detects the disagreement.

```text
currently active?   NO -- no case in cases/ or tests/data/cases sets both. Checked.
reachable?          YES, by construction, with no new code.
severity for P17    HIGH for P17.5. A total-energy equation needs ONE authoritative
                    cp/cv/gamma. Choosing silently would make an energy-conservation
                    gate measure the wrong thing while appearing to pass.
```

Recorded as a finding for the P17.5 authorization to settle. Options are: make the
thermal model authoritative and derive the compressible constants from it; forbid the
combination in the parser; or extend `ThermodynamicProperties` to accept a cp(T)
model. **P17.0 does not choose** — it is not investigation-only scope to change a
parser or a property class.

## 5. Summary of verdicts

```text
P17 need                                   verdict                      source
-----------------------------------------------------------------------------------
k(T) for conduction -div(q)                SAFE DIRECT REUSE            TemperatureDependentProperty
cp(T)                                      SAFE DIRECT REUSE            TemperatureDependentProperty
h(T) exact closed form                     SAFE DIRECT REUSE            SensibleEnthalpy
T(h) inversion                             SAFE DIRECT REUSE            SensibleEnthalpy
e(T), T(e)                                 REUSE WITH ADAPTATION        derive from h(T);
                                                                        replicate the bisection
                                                                        discipline
conservative transient assembly            SAFE DIRECT REUSE            assembleThermalTransient-
                                                                        Contribution / P14
energy boundary conditions                 REUSE WITH ADAPTATION        polymorphic scalar BCs
                                                                        accept new types
R, cv, gamma, a                            SAFE DIRECT REUSE (constant) ThermodynamicProperties
                                           but see §4 before relying on it
total-energy conservation                  NOT PROVIDED                 must be built
two-way energy coupling                    NOT PROVIDED                 must be built
GPU energy                                 NOT PROVIDED                 out of P17 scope
```
