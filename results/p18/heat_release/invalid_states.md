# P18.6 — invalid thermochemical states (§40, §41, §42)

```text
NOTHING IS CLAMPED, FLOORED OR RENORMALISED ANYWHERE IN P18.6.

Every invalid state is REFUSED, naming the quantity, the cell and the value. A clamp would turn a
diverging solve into a converged wrong answer, and a silent renormalisation would destroy the
conservation this phase exists to prove.
```

---

## 1. What is refused

| condition | behaviour | where |
|---|---|---|
| non-finite `h_f,i` | `InvalidArgumentError`, naming the species | `FormationEnthalpies` ctor |
| `h_f` count ≠ species count | `InvalidArgumentError` | ctor and `fromMolar` |
| non-finite `Delta H_r` | `NumericalError`, naming the reaction | `standardReactionEnthalpy` |
| non-finite `q_r` | `InvalidArgumentError`, naming reaction + cell | `reactionHeatRelease` |
| non-finite `S_E` | `NumericalError`, naming the cell | `reactionHeatRelease` |
| wrong reaction or cell count | `InvalidArgumentError` | all field entry points |
| non-finite / non-positive `rho` | `InvalidArgumentError` | `energyDensity`, `temperatureFromSensibleEnergy` |
| non-finite / non-positive `T` | `InvalidArgumentError` | `energyDensity` |
| `dt` ≤ 0 or non-finite | `InvalidArgumentError` | `advanceClosedReactor` |
| non-finite updated energy | `NumericalError`, naming the cell | `advanceClosedReactor` |
| `rho Y_i + dt omega_i < 0` | **`NumericalError`** — see §4 | `advanceClosedReactor` |
| recovered `T` ≤ 0 | **`NumericalError`** — see §5 | `temperatureFromSensibleEnergy` |

`RejectsMismatchedSizesAndNonFiniteInput` and `RejectsBadInputAndAcceptsEveryPhysicalSign` cover
these; `N12_TemperatureRecoveryRefusesAnImpossibleEnergy` covers the temperature path.

## 2. What is deliberately NOT refused

```text
h_f,i OF ANY SIGN, INCLUDING ZERO. h_f = 0 is correct for an element in its reference state, and
both signs are ordinary. A positivity check here would be a DEFECT rather than a safeguard, and
the test asserts the acceptances explicitly so a future "hardening" change cannot quietly add one.
```

## 3. Temperature recovery outside the `cp` validity range

```text
THE FIRST IMPLEMENTATION BRACKETED FROM T = 1 K UPWARD and threw for any ranged cp polynomial:

    "TemperatureDependentProperty(cp_A): temperature 1.000000 is outside the validity range
     [200.000000, 3000.000000]. Extrapolating a fitted property is refused rather than silently
     performed."

TemperatureDependentProperty WAS RIGHT AND THE BRACKETING WAS WRONG. Preserved at
logs/03_four_failures_PRESERVED.log. The recovery only ever worked for a constant cp -- which was
the only case the first tests exercised, so the defect was invisible until a cp(T) test was added.

FIXED: the bracket is now the INTERSECTION of the species' own cp validity ranges, read from
mixture.species(i).enthalpyModel().specificHeatModel().minimum/maximumTemperature(). A root
outside it is reported as UN-BRACKETED, naming both bounds and what the energy density implies --
never found by extrapolating a fitted polynomial past its fit, which for a fitted cp can go
negative and makes the inversion ill-posed.
```

**Measured**: `temperatureFromSensibleEnergy` now round-trips **exactly** for both a constant `cp`
and a linear `cp(T)` (`logs/06_probe.log`: both `0.000e+00`, 0.00 eps). Non-overlapping ranges are
refused by name.

## 4. The bounded species step (§41)

```text
Section 41: if a proposed explicit step would consume more species than available, "the solver
must not silently produce negative species and then normalize them... If P18.6 still lacks a
proper chemistry time integrator, detect and refuse the invalid step. Do not invent a complex
stiff integrator outside scope."

P18.6 REFUSES, and the diagnostic is actionable:

    advanceClosedReactor: the explicit step would drive rho*Y negative for species "A" at cell 0:
    rho*Y = 1 kg/m^3, dt*omega = -1.815011283612517 kg/m^3, sum = -0.81501128361251696. dt is too
    large for this rate. P18.6 has no stiff-chemistry integrator and does not clamp or renormalise
    species.

It names the species, the cell, BOTH terms, their sum, the cause and the limitation -- so the
caller can compute a valid dt from the message alone.
```

**Measured** (`RefusesAStepThatWouldDriveSpeciesNegative`): with `rho Y_A = 0.25` and a step that
would remove 0.5, the refusal fires and the message contains the species, `cell 0` and the phrase
`does not clamp or renormalise`. **And a step small enough is accepted**, so the guard is not
simply always firing.

```text
THIS GUARD FIRED IN ANGER during P18.6's own test development -- the endothermic feedback test used
a dt that would have removed 1.8x the available A. That failure is preserved, and the test now
SIZES dt FROM THE RATE (dt = 0.1 rho Y / |omega|) rather than using a guessed constant.
```

## 5. Thermal positivity (§42)

```text
Section 42: "If the energy update would produce T <= 0 or invalid internal energy, reject the
update explicitly. Do not apply an undocumented temperature floor."

rho cp T > 0 for every positive T and positive cp, so a non-positive target sensible energy
density has NO root. It is refused, and the message says so:

    "...which is not positive; rho cp T > 0 for every valid state, so no temperature satisfies it.
     This usually means an energy update removed more energy than the state held. No temperature
     floor is applied."

THE TEST ASSERTS THE PHRASE "No temperature floor" IS PRESENT, so a reader of the failure cannot
mistake the refusal for a clamp -- and a future change that introduced a floor would break the
test rather than silently changing behaviour.
```

**This case is physically reachable, not hypothetical.** The reference records it
(`data/reference_values.txt`):

```text
    A->C complete    T_final = -2000.0000000000000 K   <-- T <= 0: PHYSICALLY IMPOSSIBLE,
                                                            must be REFUSED (section 42)

An endothermic reaction with Delta H_r = +187 500 J/mol consumes 3e6 J/kg while the sensible pool
at T_0 = 1000 K holds only 1e6 J/kg. The reference FLAGS it rather than printing it as a result,
and RefusesAnEnergyUpdateThatWouldGiveANonPositiveTemperature drives exactly that case and asserts
the refusal. A SMALL endothermic step is then shown to be accepted and to LOWER T -- so the guard
discriminates rather than rejecting all cooling.
```

## 6. Where validation is deliberately NOT duplicated

```text
ONE INVARIANT, ONE ENFORCEMENT POINT:

    composition bounds and sum-to-one   P18.1 MassFractionState       NOT re-checked in P18.6
    reaction mass balance               P18.4 Reaction                NOT re-checked
    progress-rate direction validity    P18.4 reactionSpeciesSources  NOT re-checked
    cp/enthalpy range validity          P15 TemperatureDependentProperty  NOT re-checked

THIS CREATES A VISIBLE ASYMMETRY, and it is the intended one. P18.6's MOLAR route needs only q and
Delta H_r, so it accepts a negative q; the MASS route goes through P18.4, which refuses one on an
irreversible reaction. The first version of the route-agreement test compared the two at a rate
only one of them accepts, and P18.4's guard caught it -- preserved at
logs/02_first_failure_PRESERVED.log. The TEST was wrong, not the asymmetry: validating direction in
two places would be a duplicated invariant, which is exactly what section 6 of audit.md forbids.
```
