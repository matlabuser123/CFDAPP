# P18.5 — invalid-state handling (§29–§31, §38)

```text
EVERY GUARD CHECKS THE INPUT, NEVER THE OUTPUT. An invalid temperature is refused BEFORE any
division or exp(), not inferred from a NaN coming back out. Inferring it from the output is
unreliable -- exp(-Ea/(R_u * 0)) is -inf, exp of which is 0, a perfectly finite wrong answer -- and
it loses the information needed to say what went wrong.

NOTHING IS CLAMPED anywhere in P18.5.
```

---

## 1. Temperature (control N10)

| value | refused | why |
|---|---|---|
| `0` | yes | `Ea/(R_u T)` divides by zero; the Arrhenius form is undefined at absolute zero |
| `-1`, `-300` | yes | `T^n` is undefined for non-integer `n` and negative `T` |
| `NaN` | yes | checked explicitly by `isfinite`, not by comparison (`NaN < 0` is false) |
| `+inf` | yes | `isfinite` |

× **three entry points**: `rateCoefficient`, `logRateCoefficient`, `exponentialArgument`. All
three validate, so no route reaches the arithmetic unchecked.
`N10_RejectsInvalidTemperatures` covers 5 × 3 = 15 cases.

```text
NO TEMPERATURE FLOOR IS APPLIED, and the error message says so explicitly. A floor (say,
max(T, 200)) would make a solver that has gone unphysical keep running and produce a converged
wrong answer. The message states the value and the rule rather than the fix, because the fix is
the caller's.
```

## 2. The field entry point names the cell (§38)

`arrheniusProgressRates` validates each cell's temperature as it reaches it and the diagnostic
identifies **both the cell and the reaction**:

```text
arrheniusProgressRates: reaction "2H2+O2->2H2O" at cell 2: temperature is -5; must be > 0 K.
Arrhenius kinetics are undefined at or below absolute zero, and no temperature floor is applied.
```

`RejectsAnInvalidTemperatureFieldNamingTheCell` asserts the message contains `cell 2` and the
reaction name — not merely that *something* was thrown. A test asserting only the exception type
would pass for a message that says nothing useful, and the message is the whole point of the guard.

The same applies to concentrations: the diagnostic names the species, the cell and the value.

## 3. Density

`molarConcentrations` refuses a negative or non-finite density, naming the cell.

```text
THE GUARD IS ON THE DENSITY, NOT THE CONCENTRATION. C_i = rho Y_i / M_i, and a negative C_i cannot
arise from a VALID MassFractionState (Y_i >= 0, enforced by P18.1 without tolerance) and a
positive rho. Guarding C_i instead would check a consequence while leaving the cause unexamined,
and would report the wrong quantity when it fired.
```

## 4. Overflow and underflow (§31)

| case | behaviour | rationale |
|---|---|---|
| `k` would be `inf` | **`NumericalError`**, carrying `T`, `A T^n`, `A`, `n`, `Ea` | `inf` propagates into every downstream quantity |
| `k` underflows | returns **exactly `0.0`** | a rate that small is physically negligible; refusing it would make an unreachable reaction an error rather than a non-event |
| `A T^n` overflows but `k` is representable | log-space fallback returns the finite value | this is the regime the original formulation existed for |

The asymmetry between the first two is deliberate and is the reason it is recorded here: `inf`
contaminates, `0` does not.

## 5. Structural refusals at construction

| refused | asserted by |
|---|---|
| invalid `A`, `n`, `Ea` | `RejectsInvalidParameters` (8 cases) |
| an unknown reaction name | `RejectsMisnamedMissingAndDuplicateParameterSets` |
| two parameter sets for one reaction | same |
| a reaction with no parameter set / wrong count | same |
| reverse parameters on an irreversible reaction | `RejectsMismatchedDirectionParameters` |
| missing reverse parameters on a reversible one | same |
| a mismatched species or field count | `arrheniusProgressRates` / `molarConcentrations` guards |

```text
ALL OF THESE ARE CONSTRUCTION-TIME. A ReactionKinetics that exists is one whose parameters bind
one-to-one to the mechanism's reactions with the right directions -- there is no partially valid
state to carry into a solve, and no per-call revalidation to forget.
```
