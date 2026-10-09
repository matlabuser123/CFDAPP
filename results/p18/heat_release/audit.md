# P18.6 — audit of the existing energy path (§1)

```text
FROZEN BEFORE ANY PRODUCTION CHANGE. Every operation P18.6 needs is classified DIRECT REUSE,
REUSE WITH EXTENSION or MISSING, against the tree at
  HEAD 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274, P18.5 close
with the consumed files' sha256 recorded at logs/00_p18_6_start_state.log.

THE P18.0 AUDIT (results/p18/audit/thermal_energy_coupling.md) ALREADY MAPPED THIS, and its
findings are re-verified here rather than trusted: it predates P18.3, P18.4 and P18.5, any of
which could have supplied a missing piece. Two of its findings changed, and both are recorded.
```

---

## 1. Who owns energy, in each regime

| path | unknown | what it conserves | usable for a reacting mixture? |
|---|---|---|---|
| **P15 thermal** (`EnergyEquation`) | **`T`** | **`rho * cp * T`** — see §2 | **YES** |
| **P17 `TotalEnergyState`** | `rho E` | `rho(e + ½\|U\|²)`, `e = h - RT` | **NO — it refuses a temperature-dependent `cp`** |

```text
P17's TotalEnergyState CANNOT CARRY A REACTING MIXTURE, and the refusal is deliberate:

  src/compressible/TotalEnergy.cpp:48   if (!cpModel.isConstant()) throw

verified present on this tree. Its own message says ThermodynamicProperties stores ONE cp and
derives cv = cp - R and gamma = cp/cv from it, so a cp(T) model cannot be made consistent with it
at all, and lifting the refusal requires a VARIABLE-GAMMA EOS.

P18.3 FACED THIS AND DECLINED TO LIFT IT, on the grounds that it would widen a qualified P17 path
whose own G6 was then open. P18.6 DOES NOT LIFT IT EITHER. Reasons, in order:
  - §46 forbids claiming reacting shocks / detonations / supersonic combustion, which is what the
    compressible path is for;
  - a variable-gamma EOS is not in P18.6's authorized scope and would touch four qualified
    IdealGasEOS entry points;
  - the thermal path already provides everything G7 asks for.

CONSEQUENCE, STATED PLAINLY: P18.6's heat release is coupled to the P15 THERMAL energy equation
only. The compressible total-energy path remains single-species. That is a real limit on what
P18.6 delivers and it is recorded in TODO.md and ROADMAP.md, not glossed.
```

## 2. What the thermal equation actually conserves — the fact the whole phase turns on

```text
include/cfd/thermal/EnergyEquation.hpp:347, verbatim:
    "The conserved quantity is rho*cp*T (the transported quantity this equation already uses --
     see assembleThermalConvectionContribution, which scales the mass flux by cp for exactly that
     reason)"

and the transient term (:351-352):
    a_P += rho_P^{n+1} cp_P^{n+1} V_P / dt
    b_P += rho_P^n     cp_P^n     T_P^n V_P / dt

SO THE TRANSPORTED ENERGY IS  rho * cp(T,Y) * T  -- a sensible quantity whose zero is at T = 0.
It is NOT rho * integral(cp dT) from a reference temperature, and it is NOT rho*h with a formation
term. Both density AND specific heat are carried at two time levels.

THIS SINGLE FACT DECIDES §8's DESIGN GATE and §45's double-counting question. The derivation is in
energy_accounting.md; the conclusion is architectural possibility B -- the transported energy
contains NO chemical formation energy, so an explicit chemical source IS required and cannot
double-count what is not there.
```

## 3. Operation-by-operation classification

| P18.6 operation | existing API | status |
|---|---|---|
| `cp_i(T)` | P18.3 `SpeciesThermodynamics::specificHeatPressure` | **DIRECT REUSE** |
| `cp_mix(T,Y)` | P18.3 `MixtureThermodynamics::specificHeatPressure` | **DIRECT REUSE** |
| `R_mix(Y)`, `M_mix(Y)` | P18.3 | **DIRECT REUSE** |
| sensible `h_i(T)` | P18.3 `SpeciesThermodynamics::enthalpy`, = P15's integrated `SensibleEnthalpy` | **DIRECT REUSE** |
| sensible `h_mix(T,Y)` | P18.3 `MixtureThermodynamics::enthalpy` = `Σ Y_i h_i(T)` | **DIRECT REUSE** |
| `e_mix = h_mix - R_mix T` | P18.3 `MixtureThermodynamics::internalEnergy` | **DIRECT REUSE** |
| `cv_mix`, `gamma_mix`, `rho(p,T)` | P18.3 `thermodynamicProperties()` → the **existing P17 type** | **DIRECT REUSE** |
| `q_r` progress rates | P18.5 `arrheniusProgressRates` | **DIRECT REUSE** |
| `omega_i` | P18.4 `reactionSpeciesSources` | **DIRECT REUSE** |
| net `nu_i`, `Σ M_i nu_i = 0` | P18.4 `Reaction` | **DIRECT REUSE** |
| **per-cell volumetric heat source into the energy equation** | P15 `assembleThermalSourceContribution(mesh, ScalarField, rhs)` and `assembleEnergyEquation(..., const ScalarField& volumetricHeatSource, ...)` | **DIRECT REUSE — already present, nothing to extend** |
| transient thermal term | P15 `assembleThermalTransientContribution` / `ThermalTimeState` | **DIRECT REUSE** |
| **`h_f,i` formation enthalpy** | — | **MISSING** |
| **`Delta H_r(T)` reaction enthalpy** | — | **MISSING** (depends on `h_f,i`) |
| **`S_E` reaction energy source** | — | **MISSING** |
| mixture `T` from a given energy | P15 `SensibleEnthalpy::temperature(h)` inverts for ONE substance | **REUSE WITH EXTENSION** — the bracket/bisection logic is reusable, the integrand is not |

```text
THE PER-CELL HEAT SOURCE ALREADY EXISTING IS THE REASON §1's "do not add another energy equation"
costs nothing. P18.6 produces a ScalarField of W/m^3 and hands it to an assembler that has been in
the tree since P12-NUM-006. No new equation, no new assembler, no second energy path.
```

## 4. The one genuinely missing quantity, and why it cannot be faked

```text
h_f,i IS MISSING, and SensibleEnthalpy::referenceEnthalpy() IS NOT IT.

  include/cfd/thermal/Enthalpy.hpp:73   Real referenceEnthalpy() const
  verified present, and it is a SENSIBLE OFFSET: h(T) = h_ref + integral_{T_ref}^{T} cp dtheta.

USING IT AS A HEAT OF FORMATION WOULD BE A CATEGORY ERROR. h_f enters the heat release; h_ref only
shifts the zero point of a single substance's sensible scale. The P18.0 audit said exactly this,
and G7 MAKES IT A NEGATIVE CONTROL BY NAME:

    "h_f,i replaced by the SENSIBLE reference enthalpy must FAIL. This control matters because
     those are different quantities that the existing API could easily conflate
     (audit/thermal_energy_coupling.md section 1)"

So the gate anticipated this specific confusion two phases ago. P18.6 adds h_f,i as a DISTINCT
quantity on the species thermodynamics -- §4 of the authorization authorizes "the smallest correct
species thermochemistry extension required" -- and does not repurpose referenceEnthalpy.
```

## 5. What changed since the P18.0 audit

```text
P18.0 said                                      NOW
---------------------------------------------   --------------------------------------------------
"species enthalpy h_i(T) MISSING"               SUPPLIED by P18.3 SpeciesThermodynamics::enthalpy
"mixture enthalpy h_mix MISSING"                SUPPLIED by P18.3 MixtureThermodynamics::enthalpy
"formation enthalpy h_f,i MISSING"              STILL MISSING -- P18.3 added M_i and a sensible
                                                SensibleEnthalpy per species, and no h_f. Verified
                                                by reading the class: molecularWeight, gasConstant,
                                                specificHeat{Pressure,Volume}, specificHeatRatio,
                                                enthalpy, enthalpyModel -- no formation term.
"TotalEnergyState refuses variable cp"          STILL REFUSES. P18.3 documented the extension
                                                point without lifting it; P18.6 does not lift it.

SO P18.6'S MISSING SET IS EXACTLY ONE NEW THERMOCHEMICAL QUANTITY plus the two derived quantities
that need it. Everything else in the chain is reuse.
```

## 6. What P18.6 must not create

```text
NO second thermodynamics          P18.3 is the only mixture-property implementation
NO second species state           P18.1 MassFractionState remains authoritative
NO second chemistry evaluation    ONE q_r per step, shared by P18.4's species sources and
                                  P18.6's energy source (§30). Evaluating kinetics twice could
                                  give two different numbers for the same step.
NO second enthalpy implementation h_i(T) comes from P15 through P18.3, never h_i = cp_i * T
                                  (P15's own Enthalpy.hpp measures that shortcut as 26-103% wrong)
NO second energy equation         P15's assembler takes the per-cell source already
NO conservation formula duplicate Sigma M_i nu_i = 0 and Sigma omega_i = 0 stay P18.4's
```
