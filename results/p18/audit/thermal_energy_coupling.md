# P18.0 — P15 thermal and P17 thermodynamic/energy coupling map (§19–§22)

```text
HEADLINE: every thermodynamic closure in CFDApp is SINGLE-SUBSTANCE by construction, and each one
names that assumption in exactly one place -- which is good news for P18.3, because the extension
points are few and identifiable rather than diffuse.

  IdealGasEOS            ONE Real gasConstant_       ->  rho = p / (R T), R constant
  ThermodynamicProperties ONE cp, cv = cp - R, gamma = cp/cv
  TotalEnergyState        REFUSES a temperature-dependent cp outright
  SensibleEnthalpy        h(T) for ONE substance; no species index, no formation enthalpy
  species                 carries name + diffusivity only -- no W_i, no cp_i, no h_i

NOTHING in the repository computes a gas-mixture property. The only "mixture" quantities that exist
are P6-PHYS-002/003's MULTIPHASE mixture density and viscosity, which are volume-fraction blends of
two phases -- a different closure with a different meaning, and not reusable for a gas mixture.
```

---

## 1. P15 thermal — what is reusable (§19)

| P18 need | P15 API | Status | Note |
|---|---|---|---|
| `cp(T)` | `TemperatureDependentProperty` (polynomial, range-checked, Horner) | **DIRECT REUSE** | already per-substance; P18.3 needs one instance per species |
| Enthalpy `h(T)` | `SensibleEnthalpy::enthalpy(T)` = `h_ref + ∫cp dT` | **REUSE WITH EXTENSION** | correct shape, but one substance per object. `h_i(T)` = one `SensibleEnthalpy` per species |
| Inverse `T(h)` | `SensibleEnthalpy::temperature(h)` — bisection over a monotone `h` | **REUSE WITH EXTENSION** | for a mixture, `h_mix = Σ Y_i h_i(T)` must be inverted instead; the bisection bracket logic is reusable, the integrand is not |
| Thermal conductivity `k(T)` | `TemperatureDependentProperty` via `ThermalProperties` | **DIRECT REUSE** for a per-species `k_i` | a mixture-averaged `k(Y,T)` model does not exist |
| Energy source | `assembleThermalSourceContribution(mesh, Real, rhs)` **and** a per-cell `ScalarField` overload (`EnergyEquation.hpp:257-260`) | **DIRECT REUSE** | the field overload is exactly what heat release needs — already present |
| Robin / fixed-flux BCs | `ConvectiveHeatTransfer`, `HeatFlux`, `Adiabatic`, `FixedTemperature` | **DIRECT REUSE** | thermal-side only; species has no equivalent and needs none |

```text
CAN P18 GET SPECIES OR MIXTURE ENTHALPY FROM EXISTING APIs TODAY?  NO.
  species enthalpy h_i(T)    MISSING -- SpeciesProperties has no cp, no h_ref, no W_i. A
                             SensibleEnthalpy per species would supply it, but nothing wires one.
  formation enthalpy h_f,i   MISSING -- SensibleEnthalpy's referenceEnthalpy is a SENSIBLE
                             reference offset, not a heat of formation. Using it as one would be a
                             category error: h_f enters the heat release, h_ref only shifts the
                             zero point. P18.3 must add a distinct quantity, not repurpose this.
  mixture enthalpy h_mix     MISSING -- no Sigma Y_i h_i anywhere.
```

## 2. P17 thermodynamic coupling — what assumes a single species (§20)

```text
EVERY ASSUMPTION, with the one line that encodes it:

  src/compressible/IdealGasEOS.cpp:21-22   IdealGasEOS(Real gasConstant) : gasConstant_(gasConstant)
      ONE R for the whole domain. p = rho*R*T (:66), rho = p/(R*T) (:30),
      d(rho)/dp = 1/(R T) (:38), d(rho)/dT = -p/(R T^2) (:44).
      -> P18.3 must make R composition-dependent, which touches all four.

  include/cfd/compressible/ThermodynamicProperties.hpp:44-49
      IdealGasEOS eos_; Real specificHeatPressure_, specificHeatVolume_, specificHeatRatio_;
      ONE cp, from which cv = cp - R and gamma = cp/cv are DERIVED at construction.
      -> a mixture cp(Y,T) makes cv and gamma composition- AND temperature-dependent, so they
         cannot stay as stored constants. This is the single largest P18.3 change.

  src/compressible/TotalEnergy.cpp:48   if (!cpModel.isConstant()) throw
      TotalEnergyState REFUSES a temperature-dependent cp, "because ThermodynamicProperties carries
      a single cp from which it derives cv and gamma. A variable-cp energy state would be
      thermodynamically inconsistent with the EOS block; supporting it requires a variable-gamma
      EOS."
      -> THE CODE ALREADY NAMES THE P18.3 PREREQUISITE. A reacting mixture has variable cp by
         definition, so this refusal must be lifted, and lifting it requires the variable-gamma EOS
         the message calls for. It is not a check to delete.

  src/compressible/TotalEnergy.cpp:120   T = (e - h_ref + cp*T_ref) / cv
      The closed-form temperature recovery is EXACT ONLY for a calorically perfect gas. With a
      mixture cp(Y,T) this inversion becomes a root find -- P15's SensibleEnthalpy::temperature
      bisection is the existing precedent.

  include/cfd/gpu/DeviceThermodynamics.hpp   a 9-scalar POD: R, cp, cv, gamma, T_ref, h_ref,
      T_min, T_max, eFloor
      -> the GPU side hard-codes the same single-species closure. A mixture needs per-cell
         composition on device, i.e. N_species fields, not 9 scalars. P18.8's largest item.
```

## 3. P17 density coupling (§21)

```text
CURRENT, and it is exactly the single-R form:
    rho = p / (R T)                    src/compressible/IdealGasEOS.cpp:30, one constant R

FUTURE MIXTURE FORM -- RECORDED AS A P18 DEPENDENCY ONLY, NOT IMPLEMENTED HERE:
    R_mix = R_u / W_mix          with    1 / W_mix = Sigma_i ( Y_i / W_i )
    rho   = p / (R_mix T)

WHAT THAT REQUIRES THAT DOES NOT EXIST:
    molecular weight W_i per species     MISSING -- SpeciesProperties has name + diffusivity only
    the universal gas constant R_u       MISSING as a named constant
    a per-cell R_mix field               MISSING -- IdealGasEOS stores one scalar
    composition available where rho is evaluated
                                         MISSING -- the EOS has no access to species fields, and
                                         ProjectRunner's one-way ordering means species are solved
                                         AFTER the flow that would need rho

THE LAST POINT IS ARCHITECTURAL, NOT ALGEBRAIC, and it is the real dependency: composition-dependent
density makes the flow depend on species, which makes the current one-way arrow
(flow -> species -> export) insufficient. See production_path.md §3.
```

## 4. Energy / reaction coupling map (§22)

```text
WHERE A HEAT-RELEASE SOURCE WOULD ENTER -- all three already accept a per-cell source field, so no
new source plumbing is needed:

  temperature / enthalpy equation (P15)
      assembleThermalSourceContribution(mesh, const ScalarField& volumetricHeatSource, rhs)
      EnergyEquation.hpp:257-260 -- the FIELD overload already exists.
      OWNER of the production path: ThermalSolver, driven by ProjectRunner.
      -> the natural home for heat release in the INCOMPRESSIBLE/thermal path.

  total-energy equation (P17)
      TotalEnergyEquation.hpp:71  Real volumetricSource{};   // sum over cells of S_E V  [W]
      TotalEnergyEquation.hpp:126, :138 take `const ScalarField* volumetricSource`
      and the balance already carries it: accumulation + convOut + condOut - viscIn - body
      - source = imbalance.
      OWNER: the P17 total-energy path -- which is LIBRARY-ONLY and not production-reachable.
      -> heat release in a compressible reacting flow lands here, but the path itself is not
         shipped, so P18.6 inherits P17's reachability gap as well.

  density-based solver (P17.8)
      NO source term at all. Q = [rho, rhoU, rhoE] with an HLLC flux and no S.
      -> a reacting density-based solver would need a source term added; none exists.

THE COUPLING CYCLE, and where it breaks today:

    species Y_i  ----------------------------------->  EXISTS (steady, passive, production)
        |
        v  reaction rates omega_i(Y, T, p)             MISSING ENTIRELY
        |
        v  species source terms                        ASSEMBLER EXISTS (P14 Su/Sp);
        |                                              species' own is uniform-Real only
        v  heat release q = -Sigma h_f,i omega_i        MISSING (no h_f,i, no omega_i)
        |
        v  thermal / total-energy equation              SOURCE FIELD ACCEPTED by both
        |
        v  T                                            EXISTS
        |
        v  back to omega_i(T)                           THE LOOP IS NOT CLOSED: ProjectRunner runs
                                                        flow -> thermal -> species ONE WAY, and
                                                        never re-enters. There is no outer
                                                        iteration over the cycle.

MISSING INTERFACES, named without implementing any of them:
  1. a reaction-rate provider:  omega_i = f(Y, T, p)                       -- P18.4/P18.5
  2. a heat-release provider:   q = -Sigma_i h_f,i omega_i                 -- P18.6
  3. per-species thermodynamic data: W_i, cp_i(T), h_f,i                   -- P18.3
  4. a mixture closure: W_mix, R_mix, cp_mix, gamma_mix, h_mix             -- P18.3
  5. an OUTER COUPLING LOOP in ProjectRunner over species <-> energy       -- P18.6, and this is
     the one that is an architecture change rather than a new module
```

## 5. What P18.0 explicitly did NOT do here

```text
No P15 or P17 source file was read for modification, and none was modified. The TotalEnergyState
cp refusal, the single-R EOS and the 9-scalar device POD are recorded as extension points for
P18.3/P18.8 -- not as defects, and not as things to change now. §20's instruction ("Do not alter
P17 during this audit") and §34's are both honoured: the P18 start fingerprint is unchanged.
```
