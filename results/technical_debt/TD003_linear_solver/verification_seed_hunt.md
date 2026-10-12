# TD-003 C3 — the SEED is not the initial condition: it appears inside step 1. A model now fits every measurement, and is NOT established.

```text
STATUS:      **ONE SOLID RESULT, ONE INSTRUMENT LIMITATION, ONE UNCONFIRMED MODEL.**
             SOLID      the seeded profile's closure is `1.110223e-16` = 0.5 ulp, machine
                        precision. **The initial condition is NOT the seed.** By the end of
                        step 1 the deviation is `4.633173e-09` -- a 4e+07 jump inside one step.
             LIMITATION the chemistry and transport samples track each other to ~7 digits, so
                        they DO NOT separate the halves as the instrument was designed to. §3.
             MODEL      `d_new = gain * d_old + A^-1 r_solve` -- a first-order recursion driven
                        by the per-step SOLVE error and carried by the transient -- fits every
                        measurement in this item. It is NOT established. §4.
SCOPE:       one instrumented run on a copy outside the repository, read-only. No production
             change, no test change; the 1e-8 stands untouched and the test still fails.
INSTRUMENT:  logs/09_seed_hunt.log. The measurement sits INSIDE the `seedProfile` lambda, so it
             covers all four call sites and adds no call -- an earlier draft ADDED a
             `seedProfile()` call, which would have perturbed the run it measures. A presence
             gate asserts `seedProfile()` still appears exactly 4 times, as production does.
COMMIT:      carried as evidence.
```

---

## 1. THE SEED IS NOT THE INITIAL PROFILE

```text
seeded profile closure            1.110223e-16      = 0.5 ulp
seedProfile() calls               18
first transport half > 1e-15      step 1
first transport half > 1e-12      step 1
first transport half > 1e-09      step 1
```

`1.110223e-16` is exactly the floor the convex blend can achieve -- consistent with the earlier
finding that the seed is a blend of two compositions each summing to exactly 1. **So the initial
condition contributes nothing, and the deviation is created during the march.**

## 2. THE FIRST 18 STEPS

```text
step |  after chemistry  |  after transport
   1 |     4.633173e-09 |     4.633173e-09
   2 |     2.528672e-08 |     2.528672e-08
   3 |     3.040163e-08 |     3.040163e-08
   5 |     3.507872e-08 |     3.507872e-08
   8 |     6.531137e-08 |     6.531137e-08
  10 |     6.403045e-08 |     6.403045e-08
  13 |     5.638432e-08 |     5.638432e-08
  14 |     5.406854e-08 |     5.406855e-08     <- the only step where the columns differ
  18 |     9.111393e-08 |     9.111393e-08
```

It rises, then wanders between `5.4e-08` and `9.1e-08` rather than growing monotonically -- the
shape of something approaching a fixed point, not an accumulation.

## 3. THE INSTRUMENT DID NOT DO ITS MAIN JOB

It was built to separate the halves, and it does not: the two columns agree to ~7 digits at every
step but one. Both samples land effectively after the same operations within a step, so the pair
cannot say which half raised `d`.

```text
WHAT IT STILL ESTABLISHES   the seed is not the initial profile (§1), and d is already 4.6e-09 by
                            the end of step 1.
WHAT IT CANNOT SAY          which half of step 1 produced it.
AND A SECOND LIMITATION     the first 40 samples come from the FIRST TRIAL, not the final march,
                            so this history belongs to a different state than the gated one.
```

## 4. A MODEL THAT FITS EVERYTHING — and is NOT established

Combining this run with the attribution and the decomposition:

```text
chemistry half   sum_i dY_i = 0 EXACTLY. Two species, omega_P = -omega_F exactly, so
                 step*omega_P/rho is the exact negation of step*omega_F/rho. It cannot seed.
transport        r^trans = rho V/dt * d_old  (attributed, 98.79 % of r) -- it CARRIES d.
the solve        contributes 2.464686e-08 per step (measured in the decomposition).
step 1           d_old ~ 1.1e-16, so r^trans ~ 0, and the only remaining source is the SOLVE.
                 Measured d after step 1 = 4.633173e-09, the same order as 2.46e-08.
```

which gives

```text
d_new = gain * d_old + A^-1 r_solve
```

a first-order recursion **driven by the per-step solve error and carried by the transient**. With
a stable `gain` it settles at `A^-1 r_solve / (1 - gain)`; reaching the observed `~1e-06` from a
`~2.4e-08` injection needs `gain ~ 0.975`.

### 4.1 Why it is attractive — which is exactly why it must not be believed yet

It retro-explains the three observations that defeated earlier hypotheses:

```text
no dependence on step count   a FIXED POINT, not an accumulation (steps x2.89 -> deviation x0.57)
variation with state          gain and injection both vary with anchor, relaxT, dt
parked at ~1e-6 everywhere    the fixed point is ~40x the per-step injection
```

**My previous six hypotheses were also consistent with everything known at the time.** Three of
them died precisely because they were plausible. So this is recorded as a model, not a finding.

## 5. WHAT IS NOT ESTABLISHED

```text
NOT established  the GAIN. The only figure available, `||A^-1||_inf * rho V/dt = 1.2391`, mixes a
                 GLOBAL max-row-sum with a LOCAL coefficient -- the same cross-scope error that
                 wrecked the "round-off floor" claim. A real gain is the spectral radius of
                 (rho V/dt) A^-1 and has not been computed. A gain of 1.2391 would imply GROWTH,
                 which the data contradicts, so the figure is not merely imprecise but wrong in
                 kind.
NOT established  the fixed point. §4's `gain ~ 0.975` is BACK-SOLVED from the observed 1e-6. That
                 is fitting, not predicting, and §5.3's spirit forbids treating it as derived.
                 The honest test predicts the fixed point from an independently measured gain and
                 injection, then checks it against 8.822866e-07 WITHOUT adjusting either.
NOT established  which half of step 1 seeds d. §3.
NOT established  that the solve error is the only injection. It is the only one left after
                 chemistry (exact) and the seed (1.1e-16) are excluded, but "what remains" is an
                 argument from an enumeration I may not have completed.
NOT measured     any of this on the final march or the production reacting path. §3.
UNCHANGED        the 1e-8 threshold, AMENDMENT A1's withdrawal, TD-003's §12 stop, and the
                 position that the transient is correct rather than defective.
```
