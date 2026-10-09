# P18.6 — coupling order (§17, §18, §19, §30)

```text
EXPLICIT OPERATOR SPLITTING. Not implicit, not iterated, and NO CLAIM OF EITHER IS MADE --
section 18 forbids implying a coupling that is not implemented, and section 41 forbids inventing a
stiff integrator outside scope.
```

---

## 1. The frozen order

```text
  1. thermochemical state at level n:  rho^n, Y_i^n, T^n
  2. ONE evaluation of q_r at (T^n, Y_i^n, rho^n)                 P18.5   <-- the CALLER does this
  3. omega_i from THAT q_r                                        P18.4
  4. S_E from THE SAME q_r                                        P18.6
  5. advance species:  Y_i^{n+1} = Y_i^n + dt omega_i / rho       explicit Euler
  6. advance energy:   u_sens^{n+1} = u_sens^n + dt S_E
  7. recover T^{n+1} from u_sens^{n+1} AND Y_i^{n+1}              section 17
  8. the next step's q_r is evaluated at (T^{n+1}, Y_i^{n+1})     sections 18, 20
```

Frozen at `energy_accounting.md` §10 **before** the production code was written, and
`advanceClosedReactor`'s body follows it in that order.

## 2. Step 2 happens exactly once (§30)

```text
THE PROGRESS RATES ARE AN ARGUMENT, not something the stepper recomputes:

    advanceClosedReactor(mechanism, mixture, formation, const ReactionProgressRates& rates,
                         const ReactorState& state, Real dt)

so steps 3 and 4 consume the identical ReactionProgressRates object. Sharing is STRUCTURAL rather
than careful: there is no code path in which species and energy could see different rates, because
there is only one set of rates in scope.

G7's second negative control attacks exactly this, and heat_source.md section 7 measures it: a
mismatched pair differs by 150%, and the molar/mass cross-check is what detects it.
```

## 3. Step 7 uses the updated composition (§17), and N4 proves it matters

```text
Section 17: "Do not recover T_new using stale composition."

The production call passes next.composition, not state.composition. Passing the stale one would be
control N4 -- and it would be WRONG rather than merely different, because u_sens = rho cp(T,Y) T
depends on Y through cp_mix.
```

**The control needs composition-dependent `cp`, or there is nothing to detect.** With equal `cp_i`
the mixture `cp` is identical before and after, and stale `Y` gives the bitwise same answer — a
vacuous control of exactly the kind this project keeps finding. So
`N4_TemperatureRecoveryUsesTheUpdatedComposition` uses **unequal** `cp` and asserts the change:

| quantity | value |
|---|---:|
| `cp_mix` before (pure A) | **1000** J/(kg K) |
| `cp_mix` after (50 % B, `cp_B` = 2500) | **1750** J/(kg K) |
| recovered T, updated composition | the production value |
| recovered T, **stale** composition | differs by **> 10 %** |

```text
AND THE TEST ASSERTS cp_mix REALLY MOVED (EXPECT_NE on the two values, plus both exact values).
Without that assertion the control could pass while measuring nothing.
```

## 4. Step 8 uses the updated temperature (§18, §20)

```text
Section 20: "Do not continue using a stale pre-reaction temperature indefinitely." Control N3.

The loop closes because P18.5's arrheniusProgressRates takes a temperature ScalarField as an
argument, so the caller passes next.temperature at the following step. P18.6 adds no mechanism to
cache or carry a stale temperature -- there is nowhere for one to live.
```

**Measured** (`temperature_feedback.md` §1): the rate coefficient responds to the heated state and
matches `exp(-(Ea/R)(1/T1 - 1/T0))` to within 32 eps, while a deliberately stale evaluation
differs by **> 0.1 %** — far above round-off, so the control discriminates.

## 5. What the splitting costs, stated

```text
EXPLICIT EULER ON BOTH SPECIES AND ENERGY. The consequences are real and are not papered over:

(a) THE STEP IS RATE-LIMITED. dt must satisfy dt < rho Y_i / |omega_i| for every species, or the
    step would drive a partial density negative. The stepper REFUSES rather than clamping
    (section 41), and invalid_states.md section 4 measures the refusal.

(b) THE SUM-TO-ONE ERROR ACCUMULATES LINEARLY at ~0.17 eps/step, crossing P18.1's 4 eps N
    tolerance at step 72 for the measured case. tools/drift_probe.cpp establishes the growth law
    and closed_reactor.md section 4 reports it. Section 41 forbids the renormalisation that would
    mask it, so P18.1 correctly refuses the state once the invariant breaks.

(c) NO SUB-STEPPING, NO OPERATOR-SPLIT ITERATION, NO IMPLICIT TREATMENT, and the implicit Patankar
    Sp stays zero -- linearising omega in Y needs d(omega)/d(Y), which P18.6 does not implement.
    Carried forward from P18.4 and P18.5 as technical debt.

NONE OF THIS IS DESCRIBED AS COUPLED-IMPLICIT ANYWHERE. Section 18 asks for the coupling to be
named, and it is: EXPLICIT, OPERATOR-SPLIT.
```

## 6. Density in a closed constant-volume reactor

```text
rho IS UNCHANGED BY REACTION, and that is a derivation rather than an assumption: the reactor is
closed and constant-volume, and sum_i omega_i = 0 (P18.4, measured exactly 0 at
logs/06_probe.log), so the total mass in a cell does not move. next.density[cell] = rho.

THIS IS NOT A GENERAL STATEMENT. In an open or variable-volume system rho follows from continuity,
which P18.6 does not touch. The closed reactor is the configuration section 23 specifies.
```
