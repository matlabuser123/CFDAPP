# P18.5 — parameter validation (§6, §7, §30, §31)

```text
NOTHING IS CLAMPED. Every invalid parameter is REFUSED at construction with a message naming the
parameter and its value. A clamp would turn a typo into a plausible wrong answer, which is the
failure mode this phase is most exposed to.
```

---

## 1. What is refused, and what is not

| parameter | refused | allowed | why |
|---|---|---|---|
| `A` | `< 0`, non-finite | `0`, any finite `>= 0` | `A = 0` is a meaningful "off" reaction and gives `k = 0` exactly |
| `n` | non-finite | **any finite, including negative** | `T^n` is well defined for `T > 0` and any finite `n`; negative `n` is ordinary |
| `Ea` | `< 0`, non-finite | `0`, any finite `>= 0` | `Ea = 0` is the legitimate reduced case `k = A T^n` |
| `T` | `<= 0`, non-finite | any finite `> 0` | checked **before** any division or `exp()` |

```text
Ea < 0 IS REFUSED, AND THE REFUSAL IS A SCOPE DECISION RATHER THAN A PHYSICS CLAIM. Negative
activation energies are physical for some elementary steps. Nothing in the frozen gate judges
them, so accepting them would ship an untested path; the error message says exactly this rather
than claiming they are unphysical.

AND Ea >= 0 IS LOAD-BEARING FOR AMENDMENT A1: it guarantees exp(-Ea/(R_u T)) <= 1 for T > 0, which
is why multiplying a finite A T^n by it can never overflow. A1's branch predicate depends on that,
so relaxing Ea >= 0 later would require revisiting A1 -- recorded here because the coupling is not
obvious from either site.
```

`n < 0` being allowed is not a detail: `ANegativeExponentCanMakeTheRateNonMonotone` exists because
a negative exponent creates a genuine interior maximum in `k(T)`, and a monotonicity assertion
would have been wrong. See `temperature_sensitivity.md` §2.

## 2. Asserted by

`RejectsInvalidParameters` — `A = -1`, `A = NaN`, `A = inf`, `n = NaN`, `n = inf`, `Ea = -1`,
`Ea = NaN`, `Ea = inf`, each `InvalidArgumentError`. `n = -3.5` and `Ea = 0` and `A = 0` are
asserted to **construct successfully**, so the validation is shown not to over-reject.

## 3. Overflow and underflow (§31)

```text
AN OVERFLOW IS REFUSED BY NAME, not returned as inf. NumericalError carries T, A T^n, A, n and Ea,
so the diagnosis does not require re-running under a debugger.

AN UNDERFLOW TO EXACTLY 0 IS ACCEPTED AND DOCUMENTED. A rate that small is physically negligible;
refusing it would make an unreachable reaction an error rather than a non-event. The asymmetry is
deliberate: inf propagates into every downstream quantity, 0 does not.
```

Measured (`OverflowIsRefusedAndUnderflowIsAccepted`):

| case | parameters | behaviour |
|---|---|---|
| overflow | `A = 1e300, n = 20, Ea = 0` at `T = 1e6` | `NumericalError` |
| underflow | `A = 1e-300, n = 0, Ea = 1e6` | returns exactly `0.0` |

The A1 fallback path is reached in a *third* regime — `A T^n` overflows while `k` itself is
representable — and that case returns a finite correct value rather than throwing:

```text
A = 1e300, n = 40, Ea = 3e6, T = 1000:
    A T^n = inf        (so the direct branch is not taken)
    k     = 1.9913135012679248e+263   finite
```

This is `NV2_TheLogSpaceFallbackIsStillReachedAndCorrect`, and it is what proves A1 did not turn
the original formulation's robustness into dead code.

## 4. Units (§5, §33)

One system throughout — **mol, never kmol**:

```text
    Ea  [J/mol]       paired with R_u [J/(mol K)] REUSED FROM P18.3, so Ea/(R_u T) is
                      dimensionless by construction rather than by convention
    T   [K]           C_i [mol/m^3]        q [mol/(m^3 s)]
    A   carries k's units times K^-n, and k's units DEPEND ON REACTION ORDER:
                      [k] = (m^3/mol)^(m-1) / s   for total reactant order m
```

```text
A's DIMENSION CANNOT BE VALIDATED IN CODE and no attempt is made to. It depends on the total
reactant order of the reaction the parameters are bound to, so there is no single dimensional form
to check. units.md records this, and it is a real limitation: a user supplying A in the units of a
different reaction order gets a plausible wrong rate and nothing detects it.

WHAT IS CHECKED INSTEAD is the one unit error that CAN be detected -- the R_u basis. Control N4
measures the mol/kmol confusion at 3.33e6x, and the test asserts the production path uses P18.3's
constant rather than a local copy.
```
