# P17.5 — what was implemented

---

## 1. The delta

```text
NEW   include/cfd/compressible/TotalEnergy.hpp              the thermodynamic state + conversions
NEW   src/compressible/TotalEnergy.cpp
NEW   include/cfd/compressible/TotalEnergyEquation.hpp      the conservative equation + balance
NEW   src/compressible/TotalEnergyEquation.cpp
NEW   tests/unit/compressible/test_total_energy.cpp         34 TESTs
EDIT  src/CMakeLists.txt                                    +2 source lines, +3 comment lines
EDIT  tests/unit/compressible/CMakeLists.txt                +1 line
```

Nothing else. Verified by SHA-256 against the freeze, not by `git diff HEAD`:

```text
UNTOUCHED: 15    TOUCHED: 0
  P15   Enthalpy.cpp, EnergyEquation.{hpp,cpp}, ThermalProperties.hpp,
        TemperatureDependentProperty.hpp
  P17.2 IdealGasEOS.{hpp,cpp}, ThermodynamicProperties.{hpp,cpp}
  P17.3 CompressibleMassFlux.{hpp,cpp}
  P17.4 DilatationalStress.{hpp,cpp}, physics/MomentumEquation.{hpp,cpp}
```

`git diff --numstat HEAD` is **banned by this gate** as an "untouched" predicate: P17.4's L5 used it
and reported a file touched that the phase never opened, because nothing since P12 is committed.

The two source lines were placed in the **main** list of `src/CMakeLists.txt`, before the
`if(NOT CFDAPP_ENABLE_CUDA)` block, so the CPU-only configuration compiles them too — a file added
inside that block is built by no routine gate.

## 2. What was reused rather than duplicated

The authorization's §"Do not create duplicate" list, item by item:

```text
density models                 evaluateDensityField / IdealGasEOS (P17.2)     REUSED, 0 new
EOS logic                      IdealGasEOS::pressure, via
                               thermodynamics().equationOfState()               REUSED, 0 new
thermodynamic property models   ThermodynamicProperties for R/cp/cv/gamma       REUSED, 0 new
mass-flux paths                 the P17.3 SurfaceField is an ARGUMENT           REUSED, 0 new
momentum fields                 velocity and momentum density are arguments     REUSED, 0 new
thermal conductivity models     k is a caller argument; the conductive flux is
                                built from discretization::internalFace-
                                DiffusionTerms and thermal::boundaryDiffusion-
                                Contribution, the same helpers P15's own
                                assembler calls                                REUSED, 0 new
temperature<->energy inversion  h(T) is P15's SensibleEnthalpy verbatim. The
                                INVERSE is closed form rather than a second
                                bisection -- see section 4                     REUSED + derived
```

```text
grep -c specificHeatPressure in src/compressible/TotalEnergy.cpp   = 2
```

Both are legitimate and neither is an enthalpy evaluation: one is the dual-`cp` consistency check in
the constructor, the other is the closed-form inverse, which needs `cp` and `cv` together. Enthalpy
itself always comes from the P15 model, pinned by
`TotalEnergyContractTest.EnthalpyComesFromTheModelNotTheEosSpecificHeat`.

## 3. `TotalEnergyState` — and how it closes the dual-`cp` hazard

The class owns the EOS and the enthalpy model **together**, which makes its constructor the only
place in the codebase that can see both `cp` sources. It refuses to construct when they disagree:

```text
cp(T) supplied            -> throws. ThermodynamicProperties stores ONE cp and derives cv and gamma
                             from it, so a variable-cp state cannot be consistent with the EOS block
                             at all. Rejecting by name is the opposite of silently evaluating
                             h = cp(T)*T, which P15's own header measures as 26-103% wrong.
cp values disagree        -> throws, naming both values and the tolerance
                             (kSpecificHeatRelativeTolerance = 1e-12 relative)
consistent pair           -> constructs
```

Before this, `physics.thermal.specific_heat` and `physics.compressible.specific_heat_pressure` were
parsed independently with **0 cross-validation sites**, and in a thermal-coupled compressible case
the temperature was produced with one and consumed with the other. That is the blocker TODO.md
recorded as "decide before P17.5"; it is now a diagnostic rather than a silent inconsistency.

## 4. Temperature recovery — closed form, not a second root find

```text
e(T) = h_ref + cp (T - T_ref) - R T = (h_ref - cp T_ref) + cv T
  =>  T = (e - h_ref + cp T_ref) / cv          EXACT, no iteration
```

P15's bisection is **not duplicated**. It remains the authority for the forward direction, and
`TotalEnergyStateTest.InternalEnergyInverseAgreesWithTheForwardModel` checks the closed-form inverse
against it at five temperatures under both references rather than trusting either alone. The range
check is also P15's, reached by asking the enthalpy model for `h(T)` — so an out-of-range recovered
temperature is an **error**, never a clamp.

## 5. Invalid states — rejected, with the floor derived

```text
rho <= 0 or non-finite            InvalidArgumentError
rhoE or momentum non-finite       InvalidArgumentError
recovered e below the floor       NumericalError, naming e, the floor, ke and eTotal
recovered T out of range          Error (P15's own range check)
non-finite or <= 0 conductivity   InvalidArgumentError
dt <= 0 or non-finite             InvalidArgumentError
field-level failure               names the offending CELL, e.g. "cell 2: ..."
```

The internal-energy floor is **derived, not zero**:

```text
eFloor = specificInternalEnergy(cp model's minimumTemperature)
       = h_ref - cp T_ref  at T = 0
```

Under the zero reference that is `0`, so a negative `e` is rejected. Under `T_ref = 298.15` it is
`-299640.75`, because `e`'s zero point is inherited from `h`'s and a negative `e` is then
**physically legitimate**. Testing `e > 0` would have been wrong for any shifted reference — the
floor has to come from the reference state, not from the number zero.

```text
NO energy floor and NO clamp is introduced anywhere. A clamped state corresponds to an energy the
solver does not have and would let a diverging solve look converged -- the same policy P15's
enthalpy inversion already enforces.
```

## 6. The conservative equation

```text
totalEnergyConvectiveDivergence   sum_f m_dot_f hTotal_f, upwinded, ONE evaluation per internal
                                  face added to the owner and subtracted from the neighbour
totalEnergyConductiveDivergence   sum_f q_f.n A, from P15's per-face helpers
totalEnergyAccumulation           (rhoE_new - rhoE_old) V / dt, states kept separate
bodyForcePowerField               rho (f.U) V
evaluateTotalEnergyBalance        every term reported separately, plus imbalance, scale and bound
advanceTotalEnergyExplicit        one conservative explicit step
viscousWorkLimitation             the machine-readable shortfall
```

Conservation is structural, not numerical: an internal face's two contributions are **the same
double with opposite signs**, so they cancel exactly in a global sum for any flux distribution. No
divergence-free assumption is needed anywhere.

## 7. What is NOT implemented, and is not claimed

```text
NOT DONE       an implicitly coupled rhoE solve inside CompressibleSIMPLE. hTotal is a nonlinear
               function of the unknown rhoE through T, so an implicit form needs a Newton
               linearisation -- solver architecture that belongs with P17.8. What exists is the
               conservative flux form plus an explicit conservative advance, which is enough to
               march the closed-domain, uniform-heating and source verifications.
NOT DONE       case-file reachability. rhoE has no case key and no export field, and P15's
               SensibleEnthalpy -- which this is built on -- is itself unreachable from a case, so
               wiring P17.5 requires wiring P15's enthalpy model first.
NOT DONE       k(T) conduction. A variable-k flux needs a face-interpolated k, which is new
               discretisation; left out rather than guessed at.
NOT DONE       cp(T). Rejected at construction with a named error and a stated reason.
NOT DONE       the full viscous-work term. P17.4 gives no stress tensor; see formulation.md 6.
NOT DONE       repairing P15's cp*T transport, or reconciling the two conduction boundary
               treatments. Both would move the qualified baseline; recorded as follow-on work.
NOT DONE       any GPU path. There is no GPU compressible solver to attach one to.
NOT DONE       anything in P17.6-P17.10. No HLLC, no Riemann flux, no density-based solver.
```
