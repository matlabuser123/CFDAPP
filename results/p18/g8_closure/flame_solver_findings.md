# G8 step 4 — the premixed flame solver: findings so far

**Status: IN PROGRESS. The flame speed has NOT been measured, for methane or for anything else.
No G8 criterion has been evaluated.** This file records what the implementation has established and
the defects it found, so none of it is lost and none of it has to be rediscovered.

The G8 criterion and mechanism remain frozen and untouched at
[g8_reference.md](g8_reference.md): `35.0 <= S_L <= 37.0 cm/s`, WD (1981) Table II Set 3.
`results/p18/gates/P18_GATES.md` verified unchanged at
`921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3`.

---

## 1. What exists, and what is verified

| Piece | State |
| --- | --- |
| `include/cfd/species/PremixedFlame.hpp`, `src/species/PremixedFlame.cpp` | builds; forms a flame; conserves |
| raw-field kinetics path (`arrheniusProgressRatesFromFields`) | builds; additive, frozen path untouched |
| `tests/unit/species/test_premixed_flame.cpp` | 13 tests written; **most currently FAIL** |

**No regression from any of it**, on the unchanged pre-existing tests:

```text
CFDSpeciesTests  (excluding G8PremixedFlameTest)   339 ran   339 passed   0 failed
CFDIoTests                                         325 ran   325 passed   0 failed
CFDAppLayerTests                                    32 ran    32 passed   0 failed
```

### Established by measurement

* **No momentum solve is needed.** The face mass flux is built from the scalar `mdot` plus the
  continuity correction (F7); thermal expansion is fully present through `rho = p Mbar/(R T)`.
* **Conservation is now essentially exact** — and this is the headline number, because it was the
  symptom that located the worst defect:

  ```text
  max_cell |sum_i Y_i - 1|   0.887  ->  1.4e-11      (after F7)
  min Y_i                   -1.5e-14                 (negligible, never repaired)
  peak temperature           1800.00 K == T_ad exactly (after F6)
  ```

* **The energy equation can carry variable `lambda(T)` AND a per-cell heat release**, by composing
  the field-property assembly with the production function `assembleThermalSourceContribution`
  (RHS-only, so exact). This removed a hand-picked constant `lambda` — the largest free parameter a
  gated flame speed would otherwise have had, since `S_L ~ sqrt(lambda)`.
* **The test fixture was SELECTED BY MEASUREMENT, not guessed.** `DISABLED_ParameterWindowScan`
  sweeps `(A, Ea)` and reports, for each, the peak temperature and both zone widths in cells. It is
  retained so the choice can be re-derived rather than taken on trust.

---

## 2. Defects and dead ends found, in the order they were found

Every one was found by a measurement contradicting the design, not by inspection.

### F1 — a flame thinner than the mesh does not degrade, it EXTINGUISHES

`A = 1e7` gave `delta ~ 0.1 mm` against `0.1 mm` cells: a front one cell wide. The result was not an
inaccurate flame speed but total extinction. An unresolved front on an upwind scheme has no graceful
failure mode.

### F2 — a flame THICKER than the upstream domain also cannot exist, at any `mdot`

`A = 1e5` gave `delta ~ 4.4 mm` with only `7 mm` upstream of the anchor, so the preheat zone had
nowhere to sit and the cold inlet quenched the flame at **every** mass flux — including
`mdot ~ 1e-5`, where the inflow is `8e-6 m/s`.

### F2b — and the REACTION zone is `delta/Ze`, not `delta`

The two constraints above are not the whole window. The reaction zone is thinner than the preheat
zone by the Zeldovich number, so the mesh must resolve `delta/Ze` while the domain must fit `delta`:

```text
dx  <  delta / (4 Ze)        or the reaction zone is unresolved
delta  <  x_anchor / 5       or the preheat zone does not fit
```

With `Ze = 6.7` those nearly collide at 300 cells, which is why `Ea` had to be scanned as well as
`A`. The scan selected `A = 3e7, Ea = 1.2e5`: peak temperature exactly `1800 K`,
`delta = 2.285 mm = 45.7 cells`, reaction zone `6.84 cells`.

### F3 — the steady position-anchored eigenvalue solve is ill-conditioned BY the plateau

`mdot(x_f)` is flat near the eigenvalue, so `x_f(mdot)` is vertical: a position-driven controller
inverts a flat function and needs a small gain, while extinction is an **absorbing state**.
Measured: peak temperature fell `1800 -> 1501 -> 1276 -> 1088 K` while `mdot` moved 4% per
iteration.

### F4 — a continuous gain-one drift update violates the flame's own timescale

With an unsteady march, `mdot <- mdot - rho_u * drift` has a gain of exactly one from kinematics and
no tuned constant. It still failed: the structure responds on `t_f = alpha/S_L^2 = 0.55 s` against
`dt = 1.5 ms`, so the controller ran **~360x faster than the physics it measured**. Past the
eigenvalue the drift stops meaning propagation — with `mdot` driven to `~0` the measured drift
stayed POSITIVE, tracking a dying flame's conduction profile.

Replaced by **bracket-and-bisect on the SIGN of the drift**, which needs no timescale knowledge.

### F5 — a FAIL-OPEN side decision, caught by its own symptom

The first bracketing inferred an extinguished trial's side from the accumulated recentring and
**defaulted to "downstream" when nothing had been recentred**. That read every ambiguous trial as
"`mdot` too large" and halved the mass flux 13 times without ever bracketing. Now **fails closed**:
a trial that never measured a drift is reported undecided, not assigned a side.

### F5b — and the same fail-fast mistake twice more

A trial that extinguished, and later a trial that diverged, each **aborted the entire search**. Both
are ordinary outcomes for a mass flux far from the eigenvalue — bracketing cannot widen past its
first bad guess if either is fatal. Both now name a side from the last measured window and the
search continues.

### F6 — an EXPLICIT heat release cannot carry a stiff exothermic source

The heat release was added straight to the energy RHS. One step then injects
`Delta T = S_E dt/(rho cp)`, and with `S_E = 5.5e10` and `dt = 9.4e-4` that is `2.2e5 K` in a single
step. **Measured peak temperatures of 17646 K, 38511 K and 144482 K against an adiabatic 1800 K** —
10x to 80x a bound an adiabatic flame cannot exceed at all. `dt` was ~50 chemical times.

Linearising it implicitly does not help: `dS_E/dT > 0`, so the Jacobian term SUBTRACTS from the
diagonal. The instability is the physical one of ignition.

Fixed by **operator splitting**: transport implicitly with no reaction, then the chemistry over the
same `dt` with its own adaptive sub-stepping. Bounding `dt` by `tau_c` instead would have needed of
order 38000 steps per trial.

**`ChemistryIntegrator` could NOT be reused**, and that is worth recording: it is a
**constant-density** reactor built on P18.6's `advanceClosedReactor`, which conserves sensible
energy at fixed `rho` and recovers `T` through `cv = cp - R`. A constant-pressure flame needs
`Delta T = q/(rho cp)`, and `cp/cv = gamma ~ 1.3` here — using it unmodified would have inflated
every temperature rise by about 30% and moved the very `T_ad` the frozen `cp` exists to pin.
Extending it with a constant-pressure closure is the better long-term shape.

### F7 — A UNIFORM MASS FLUX IS THE STEADY CONTINUITY EQUATION, and the march is not steady

The worst defect, and the one the conservation number found. A uniform `rho*u` solves
`d(rho u)/dx = 0`. Once the density varies in time,

```text
d(rho)/dt + d(rho u)/dx = 0   =>   (rho u)(x) = mdot_inlet - integral_0^x d(rho)/dt dx'
```

so `rho*u` **cannot** be uniform. Imposing it anyway left the conservative transient term — which
conserves `rho cp T`, not `T` — seeing a density change the flux field did not account for, and it
rescaled `T` by `rho^n/rho^{n+1}` every step.

**Measured: the transport half drove a cell to 149.2 K against a 300 K inlet.** Pure
convection-diffusion obeys a maximum principle and cannot undershoot its own boundary data, so that
number was the proof the inconsistency was real rather than a stiffness artefact — and the
divergence bracket's own message said exactly that, which is why it was added.

Fixed by marching the discrete continuity equation west to east, making the per-cell continuity
residual identically zero. The effect on conservation: `|sum Y - 1|` fell from **0.887 to 1.4e-11**.

### F8 — an unreachable linear tolerance, then an unstoppable one

Both fixed choices failed, in opposite directions:

* the **default `1e-12` absolute** is unreachable for the energy equation, whose RHS is `O(1e4)`:
  it asks for 16 orders of reduction. Reported as failed at a **relative** residual of `4e-9`.
* pushing it to `1e-300` so the relative criterion would govern then meant a nearly-exact system
  could never stop, and BiCGSTAB iterated into cancellation **breakdown** — at a residual of
  `3.9e-4` against an initial `2.0e-4`, both negligible.

No single dimensional constant serves a temperature field of `O(1e3)` and a mass fraction of `O(1)`,
so the bound is now scale-free: `1e-12 * ||b||`, per system, with Jacobi preconditioning.

### F9 — half-built results, and a segfault that proved it

The profile arrays were filled only on the success path, so a caller receiving `NoFlame` or
`NonFiniteState` got a result whose `massFractions` was an **empty vector**; a diagnostic that
indexed it segfaulted. Every return now goes through one finaliser. A non-converged flame is still
the evidence that has to be reported.

---

### F10 — THE CONSERVATIVE TRANSIENT TERM WAS THE CULPRIT, not the mass flux

This is the finding that unlocked the solver, and it took two attempts at the wrong thing first.

P14's conservative time term is `a_P = rho^{n+1} V/dt`, `b_P = rho^n phi^n V/dt`, which conserves
`rho*phi`. That is correct for a solver whose face fluxes satisfy the DISCRETE continuity equation.
This solver's cannot, and the mismatch then appears as a rescaling of the solved variable by
`rho^n/rho^{n+1}` on every step. Chasing the FLUX instead of the time term gave two dead ends:

1. **`d(rho u)/dx = -(rho^{n+1} - rho^n)/dt`**, the discretely exact closure. Unusable across a
   split step: the chemistry moves the density by a finite jump over `dt`, so the inferred velocity
   grows as `1/dt`. Worse, it is **scale-free** — the convective temperature change it induces,
   `(m/rho)(dT/dx) dt`, has the `dt` cancel exactly. Measured: a time-step controller cut `dt` to
   **6.6e-17** and the trials still failed at the same step. No time step can fix a feedback that
   does not depend on the time step.
2. **the thermodynamic divergence constraint**, taking `div(u)` from the heat-release and species
   sources, which IS bounded by physical rates. But it satisfies the CONTINUOUS relation, not the
   discrete identity the conservative term needs, so the leftover residual still rescaled the
   temperature: peak **4755 K** and `|sum Y - 1| = 4.26`.

The fix is to pass the SAME density at both time levels, giving `rho (phi^{n+1} - phi^n) V/dt` —
the non-conservative form, with no density ratio in it at all.

**AND IT CANNOT CHANGE THE ANSWER.** At steady state the time term vanishes identically, whichever
form it took, so the form is a property of the PATH to the eigenvalue, not of the eigenvalue. What
it buys is exact preservation of the mixture constraint: summing the species equations gives
`rho (sum_i Y^{n+1} - sum_i Y^n) V/dt` plus the transport of `sum_i Y_i`, and with `sum_i Y^n = 1`
and a uniform inlet the constant is reproduced.

Measured on the same case that had been failing:

```text
                        before F10        after F10
peak temperature        149 K / 4755 K    1800 K  (== T_ad exactly)
max |sum_i Y_i - 1|     0.887 / 4.26      4.2e-7
min Y_i                 -                 -1.4e-28
front drift             -125410 m/s       +0.90 m/s at u_in = 0.59 m/s
steps before failure    ~34               16800, no extinction, no divergence
```

### F11 — THE TWO REASSURING CHECKS WERE VACUOUS, and they were the ones that passed

Once the eigenvalue search started converging, three measurements on the same solution disagreed:

```text
peak temperature     1800.00 K == T_ad exactly        PASSED
outlet fuel          < 5e-5 (i.e. consumed)           PASSED
consumption integral S_c = 2.58 vs S_L = 4.50 cm/s    FAILED by 43%
```

`S_c/S_L = 0.573` implies about 43% of the fuel leaves unburned, which would put the burnt gas near
1155 K — flatly inconsistent with a peak of 1800 K and with an outlet fuel of zero. Two measurements
that contradict each other mean at least one is wrong.

**There was no contradiction. Two of the three were measuring the INITIAL CONDITION.** The seeded
profile is a tanh blend between the unburnt and the burnt states, so where `blend -> 1` downstream
it gives

```text
temperature = T_u + (T_ad - T_u) * 1 = T_ad        exactly
composition = Y_u + (burnt - Y_u) * 1 = burnt[0]   which is exactly 0.0
```

before any chemistry has run. So "the peak reaches T_ad" and "the fuel is consumed" are satisfied by
the seed, and measure nothing about the flame. `S_c` was the only honest number of the three, and it
was right: the reported state had been marched only 128 steps, about 3.6 flame transit times, and
was nowhere near steady.

**The worst part is which ones passed.** The two vacuous checks were the reassuring ones; the single
real check was the one that looked like a defect. This is the failure mode recorded in the project
notes as "a criterion the baseline passes vacuously is un-dry-run" — neither check had ever been
dry-run against a state the flame had not touched.

Three corrections:

1. **the reported state is now marched to a STEADY state**, not merely until the drift settles.
   Those are different conditions: the front can stop moving while the profile behind it is still
   filling in. `steadyStateReached` is reported rather than assumed.
2. **the vacuous checks are replaced by conservation balances**, which are integral identities of a
   steady flame and cannot be satisfied by the seed:

   ```text
   fuel    mdot (Y_out - Y_u)    = integral omega_F dx + inlet diffusive influx
   energy  mdot cp (T_out - T_u) = integral S_E dx     + inlet conductive influx
   ```

   each normalised by its own through-flux, so a small residual means the discrete solution
   conserves rather than that the quantities happen to be small. The `T_peak <= T_ad` bound is
   KEPT, because that one is not vacuous: nothing in the seed prevents the march from overshooting
   it, and overshoot is exactly what the explicit heat release used to do.
3. **a test proves the vacuity by measurement** rather than by argument. It marches one step, shows
   both old checks pass on the raw seed, and shows the new balances FAIL on it. Two-sided: it stops
   the old checks returning as evidence, and it fails loudly if the balances ever become
   satisfiable by the seed too.

### F12 — the "two independent balances" are ONE measurement written twice

The fuel and energy balances were added as two independent conservation checks. They are not. On the
reported state they came back:

```text
fuel balance   = 0.41766725
energy balance = 0.41766720
```

Identical to seven significant figures, which is not two checks agreeing but one check duplicated.
The reason is algebraic and specific to this mechanism: for a single global reaction with constant
`cp`, `integral S_E dx` is exactly proportional to `integral omega_F dx` through the reaction
enthalpy, and `mdot cp (T_ad - T_u)` is exactly `mdot Y_u |dHr|/(nu_F M_F)` **by the definition of
T_ad**. So both residuals reduce to the same ratio and the energy balance carries no information the
fuel balance does not.

It is kept, because it stops being redundant the moment `cp` varies with temperature or the
mechanism has more than one reaction — but it must not be COUNTED as independent evidence here.
Claiming two conservation checks where one exists would overstate the verification.

### F13 — a steadiness tolerance that was unreachable by construction

`finalMarchTolerance = 1e-9` on the per-step state change was never attainable, and the arithmetic
says so without needing a run. The eigenvalue is bracketed, not solved exactly, so a residual front
drift of order `2.5e-5 m/s` survives. A front creeping at that speed changes a cell's temperature by

```text
dT per step / (T_ad - T_u)  ~  (drift * dt) / delta
                            =  (2.5e-5 * 4.19e-4) / 6.7e-4
                            ~  1.6e-5
```

which is four orders of magnitude ABOVE the 1e-9 bound. So `steadyStateReached` could only ever
report false, and it did — after 40000 steps, which is 16.8 s of simulated time and about 1120 flame
transit times. The state was as steady as a bracketed eigenvalue permits; the criterion was wrong,
not the solver.

The replacement is not a looser version of the same thing. Steadiness is **reported** rather than
gated, and the gate is the conservation balance instead — which is the quantity that actually
matters, is derived from an integral identity rather than a chosen constant, and cannot be satisfied
by the seed (F11). A balance that closes is itself the evidence that the state is steady enough to
be reported, because a transient state does not close a steady balance.

### F14 — RECENTRING IS NOT CONSERVATIVE, and it was running on every step

The fuel-balance residual of F11 turned out to be a structural leak, not a transient, and the
measurement that settled it was the march length:

```text
march  1000 steps:  fuel balance = 0.4180   recentrings = 1067   dT residual = 0.260
march  4000 steps:  fuel balance = 0.4172   recentrings = 4067   dT residual = 0.345
march 16000 steps:  fuel balance = 0.4177   recentrings = 16067  dT residual = 0.350
```

Two things fall out of that table. The residual is **pinned at 0.4177 across a 16x range of march
length**, which is the signature of a steady leak and not of a state still filling in. And
`recentrings == steps + 67`: the profile was being translated **every single step**.

The leak is an asymmetry in the translation. Recentring shifts the profile by a whole number of
cells and discards whatever sat in the cells pushed off the end, and the two directions are not
equivalent:

```text
shift +1   discards cell 0, which holds FRESH FUEL         -> loses rho Y_u dx of fuel
shift -1   refills cell 0 with Y_u, which it already held  -> loses nothing
```

With `llround` and no dead band, a front sitting half a cell off the anchor is snapped back, lands
half a cell the other side, and oscillates `+1, -1, +1, -1` — losing one cell of fuel roughly every
two steps. Predicted loss from that mechanism is about 0.25 cells per step; the rate implied by the
measured residual is 0.118 cells per step. Same order, same direction.

**One cause, two symptoms.** The same oscillation also made the state look permanently unsteady: a
front jittering a cell per step changes a near-front cell by a large fraction of the full
temperature rise, which is why `dT residual` sat at 0.35 and `steadyStateReached` was false after
1120 flame transit times (F13).

The fix is a dead band: the front is left alone while it stays within a few cells of the anchor, so
at the eigenvalue — where it barely drifts — recentring becomes rare instead of constant. The dead
band cannot bias the eigenvalue, because it only decides WHEN the frame is translated and the
accumulated displacement is tracked exactly either way. A dead band below one cell is refused
outright, with the reason stated, so this cannot be reintroduced by a careless setting.

### F15 — WITHDRAWN AND WRONG. The preheat scale is set by the MASS FLUX, not by alpha_burnt

**This finding was incorrect and the "fix" built on it made the solver worse. It is kept, corrected
in place, because acting on it is part of the record.** The corrected version is F16.

What F15 claimed: that the preheat thickness is `alpha_burnt/S_L ~ 4.7 mm`, that the domain had
been sized against the wrong scale, and that the seed and the trial length should both be derived
from `alpha_burnt`.

Why it is wrong: the preheat thickness follows from the convection-diffusion balance that defines
the structure, `mdot cp dT/dx = d/dx(lambda dT/dx)`, which gives

```text
delta_preheat = lambda / (rho_u cp S_L) = alpha_unburnt / S_L
```

The MASS FLUX sets the scale, so the unburnt density enters, not the burnt one. At
`S_L = 7.5 cm/s` that is `0.47 mm`, not `4.7 mm` — a factor of ten, which is the expansion ratio
squared and not the expansion ratio.

The solver's own instrument already agreed with the correct value and was ignored:
`flameThickness` reported `0.67 mm` at `S_L = 4.5 cm/s`, against `lambda/(rho_u cp S_L) = 0.79 mm`.

**What was actually misread.** The "preheat zone spanning 10 mm to 15 mm" in the profile dump is
the exponential TAIL, not the structure: upstream of the front the solution decays as
`exp(x/delta)`, so at 10 mm the temperature was 306.9 K — half a percent of the 1500 K rise. A
0.2-0.5% tail was mistaken for the flame.

**What the wrong fix did.** Seeding at `alpha_burnt/S` laid down a ramp 3.6x too wide and set the
trial length from the same inflated scale. The measured result:

```text
                      before the F15 "fix"   after
fuel balance          0.418                  0.846
S_c / S_L             0.582                  0.154
front structure       graded                 a ONE-CELL jump, 300 -> 1280 -> 1800 K
```

So it drove the front into the F1 regime — thinner than the mesh — which is the one failure mode
already known to extinguish rather than degrade.

### F16 — the flame has two thickness scales, and the REACTION zone is the unresolved one

The profile dump settles what the flame actually looks like, and it is a textbook structure:

```text
x[mm]    T[K]      Y_fuel      S_E[W/m3]      rho
0-10     300       0.0500      ~1e-7          fresh gas, no reaction
11       322.5     0.0492      2.1e-6         preheat begins
13       541.3     0.0420      78
14       1104      0.0232      1.69e7         <- the reaction zone
15       1798.9    3.7e-5      2.58e6         burnout
28-40    1800      ~0          0              burnt plateau
```

Exactly ONE crossing of the anchor temperature, at 13.967 mm against an anchor of 14.0 mm — half a
cell. Density falls 1.178 -> 0.196, the full expansion ratio. The reported `flameThickness` is
0.67 mm.

But the PREHEAT ZONE spans about 5 mm, from 10 mm to 15 mm — roughly seven times the reported
thickness. Both numbers are right, and they measure different things:

The two scales are the preheat zone and the reaction zone inside it, and they differ by the
Zeldovich number rather than by the expansion ratio:

```text
delta_preheat  = alpha_unburnt / S_L        = 0.8 mm  = 12 cells at dx = 0.067 mm   RESOLVED
delta_reaction = sqrt(alpha_unburnt tau_c)  = 0.06 mm =  1 cell                     NOT RESOLVED
                 = delta_preheat / Ze,  Ze = 6.7 for Ea = 1.2e5
```

with `tau_c = 1/k(T_ad) = 1.01e-4 s`. **The reaction zone is one cell wide, and that is what the
one-cell front in the profile actually is.** The preheat zone was never the problem; the mesh
resolves it twelvefold.

This restores the rule F2b had right, with the scales named explicitly:

```text
dx        <=  delta_reaction / 4  =  delta_preheat / (4 Ze)    resolve the REACTION zone
x_anchor  >=  ~5 delta_preheat                                 leave room for the plateau
domain    >=  ~12 delta_preheat                                let the burnt gas leave cleanly
```

**Applied to this fixture, those three are satisfiable together and more cheaply than the current
setup.** With `S_L ~ 6 cm/s`, `delta_preheat = 0.59 mm` and `delta_reaction = 0.088 mm`:

```text
dx        <=  0.022 mm
domain    >=  7 mm       -> 10 mm chosen
cells      =  10 mm / 0.02 mm  =  500
x_anchor   =  0.35 * 10 mm = 3.5 mm = 5.9 delta_preheat
```

500 cells over 10 mm, against the 600 cells over 40 mm used until now: the domain was **four times
too long and the mesh four times too coarse at the same time**, which is why both the resolution and
the plateau criteria were failing at once.

**Measured effect of correcting the two scales**, all on the same case and the same code paths:

```text
                          F15 "fix"   F16 sizing
fuel balance residual     0.846       0.0222        38x
S_c / S_L  (ideal 1)      0.154       0.9756
final-march recentrings   654/1000    2/1000        327x
worst front offset        209 cells   3.0 cells     70x
status                    -           Converged
S_L                       7.46 cm/s   4.86 cm/s
max |sum_i Y_i - 1|       -           6.8e-7
```

The profile is now a resolved flame: a graded preheat from 300 K, the reaction zone peaking at
4.09 mm with `S_E = 1.7e8 W/m^3`, complete burnout to exactly `T_ad`, monotone throughout, one
anchor crossing at 3.53 mm against an anchor of 3.50 mm. The temperature rises **21 K per cell**
through the front where the unresolved version rose 365 K per cell.

The recentring storm (F14) is gone as a consequence rather than by its own fix: with the front
genuinely at the anchor, the dead band finally does what it was added to do.

**What remains is 2.2% on the fuel balance against a 2% gate** -- a near-miss, and now plausibly
discretization rather than a leak, since the reaction zone is still only about 4.4 cells wide. That
is a convergence question, so it is settled by REFINEMENT rather than by moving the tolerance: if
the residual falls with the mesh, the solver conserves and the gate should be set from the observed
convergence. If it does not fall, there is still a leak to find.

### F17 — a systematic vacuity sweep of all 12 active tests, after finding two by accident

Two vacuous criteria had already surfaced by chance (F11), which is reason enough to stop trusting
luck and check the rest. All 12 active tests, 53 assertions, audited against one question: **would
this still pass on a state the flame never shaped?** Two more failed it.

| test | verdict |
| --- | --- |
| 1 ItConverges… | balances non-vacuous; **velocity-expansion check WAS vacuous — deleted** |
| 1b TheObviousChecksAreVacuous… | the proof itself, two-sided by construction |
| 2 EigenvalueIndependentOfInitialGuess | strong — a degenerate method returns the guess |
| 3 EigenvaluePlateaus… | compares across anchor and domain; cannot be met by one state |
| 4 ConsumptionIntegralAgrees… | **gated on an UNREACHABLE criterion — re-gated on the balance** |
| 5 FlameSpeedConvergesUnderRefinement | differences must shrink; carries the resolution check that would have caught F1 |
| 6 WithReactionsDisabled… | exact zeros; chemistry ON converges, so not vacuous |
| 7 TenTimesThePreExponential… | ratio > 2 |
| 8 VariableConductivity… | a frozen-lambda defect gives equality |
| 9 MalformedConfigurationsAreRefused | EXPECT_THROW |
| 10 BurntCompositionConsumesFuelExactly | algebra on the helper, independent of any solve |
| 11 InsensitiveToTheDriftWindow | spread bound |

**The deleted expansion check.** `EXPECT_GT(velocity[last], 3*velocity[0])` looked like a physics
check and was not one. In this formulation the velocity is not solved: the mass flux is imposed
uniform and `velocity = mdot/rho` with `rho` from the equation of state, so the ratio is identically
`rho_u/rho_b = T_ad/T_u = 6` — which the SEED already satisfies. There is no non-vacuous version,
because thermal expansion here is a CONSEQUENCE of the equation of state and the imposed flux rather
than something the solve determines. Deleted and recorded, not kept with an exception.

**The unreachable gate.** Test 4 still asserted `steadyStateReached`, the criterion F13 showed can
only ever be false. It would have failed for a reason unrelated to the identity it exists to check.
Re-gated on the fuel balance, which is the steadiness evidence: a transient state does not close a
steady balance.

**The lesson for the gate work ahead:** both of these were introduced by me, in the same session,
while fixing other vacuity. A criterion sweep is cheap and belongs AFTER every round of changes to
the instrument, not only once at the start.

---

## 3. The open problem

The march is now STABLE — no extinction, no divergence, the exact adiabatic temperature,
conservation to 4e-7 — and the eigenvalue search halves correctly on physical drift signs. What
remains is that a trial does not **settle**: over 20000 steps, which is 0.68 s of simulated time and
about 145 flame transit times, consecutive drift windows kept disagreeing by more than the settling
bound.

Two causes are identified and are being separated:

* **grid-locking noise.** The front position is a linearly interpolated crossing, so it wobbles by a
  fraction of a cell as the front moves through the mesh. A settling bound tighter than that noise
  can never be met, whatever the physics is doing.
* **a genuinely slow evolution**, which more steps would resolve and a looser bound would not.

**Why the distinction is not waved through:** quoting a flame speed from a trial whose drift had not
settled would be quoting the transient, and raising a tolerance until a measurement passes is
exactly what CLAUDE.md section 5 forbids. The settling bound is therefore derived from the
measurement noise rather than from what makes a test green, and the drift-window insensitivity test
is what holds it honest.

### 3.1 A PRECISION REQUIREMENT THAT G8 IMPOSES, and that is not yet met

The eigenvalue search converges, but its REPRODUCIBILITY has been measured and it is not yet good
enough to decide G8. Varying only the drift window:

```text
drift window 32   mdot = 0.05303   S_L = 4.50 cm/s
drift window 64   mdot = 0.05088   S_L = 4.32 cm/s
spread                             4.3 %
```

Both are the same problem solved by the same code; the only difference is how long each trial
marched before its drift was read. So 4.3% is the method's current precision, not a property of the
flame.

**G8's band is 35.0–37.0 cm/s, which is ±2.8% about 36 cm/s.** A method whose own reproducibility is
4.3% cannot resolve a ±2.8% band *near its edges*: a measurement landing at, say, 36.5 would carry
an uncertainty wider than the band it is judged against, and a PASS would be an artefact of which
drift window happened to be used.

To be precise about what the precision does and does not block, since overstating it would be its
own error:

* a result landing FAR outside the band — 50 cm/s, say — is decisive as a FAIL even at 4.3%, since
  the uncertainty is nowhere near large enough to reach 37;
* a result landing INSIDE the band, or just outside it, is **not** decisive either way, and that is
  the case that matters, because it is the case in which G8 would otherwise be declared closed.

So the precision requirement is a precondition for claiming a PASS, and for reporting a near-miss
FAIL as a real one. It does not block reporting a gross failure.

**This is a precondition, and it has to be established BEFORE the methane measurement, not after.**
Measuring first and discovering afterwards that the instrument cannot resolve the criterion is how a
gate gets quietly reinterpreted. Recorded here so the order stays fixed:

```text
required   method reproducibility << 2.8 %, demonstrated on the synthetic flame
then       freeze cp and lambda(T) with citations
then       measure S_L once against 35.0-37.0 cm/s
```

The identified route to the needed precision: the drift is currently read after a trial's drift
settles, which happens at 128 steps — about 3.6 flame transit times, and demonstrably before the
profile is steady (F11). Applying the same steadiness condition INSIDE each trial that the final
march now applies to the reported state should determine the eigenvalue far more sharply, at the
cost of more steps per trial.

---

## 4. What the synthetic verification suite will gate, once it runs

No test asserts an absolute `S_L` — there is no published flame speed for a made-up fuel. They
assert the properties a correct eigenvalue solver must have:

| # | Property | Why it can fail loudly |
| --- | --- | --- |
| 1 | peak `T` approaches the independent `T_ad` | catches a broken energy balance (caught F6) |
| 2 | `S_L` independent of the initial guess | **the degeneracy trap** (section 5) |
| 3 | `S_L` plateaus in anchor position and domain | the cold-boundary difficulty |
| 4 | consumption integral agrees with `mdot/rho_u` | discrete conservation (caught F7) |
| 5 | `S_L` converges under refinement | G8's own criterion for profiles |
| 6 | reactions off => no flame | G8 negative control 1 |
| 7 | `10x A` moves `S_L` by `> 2x` | G8 negative control 2 |
| 8 | variable `lambda(T)` changes the answer | catches `lambda` frozen at the inlet value |
| 9 | answer independent of the drift window | the one method knob left |

---

## 5. The degeneracy that had to be designed around

The obvious way to find `mdot` is a fixed point on the consumption integral
`S_c = -1/(rho_u Y_F,u) integral omega_F dx`. **It is degenerate.** Integrating the fuel equation
over a domain where the front is detached from the inlet gives

```text
mdot (Y_F,outlet - Y_F,u) = [rho D dY_F/dx] + integral omega_F dx
    =>  -mdot Y_F,u = integral omega_F dx   =>   S_c == mdot / rho_u
```

identically, for ANY `mdot` admitting a steady solution. It reproduces whatever `mdot` it was given
and carries no information about the eigenvalue: such a fixed point converges instantly from any
guess and would have looked like a converged flame speed while measuring only the initial guess.

The integral is still computed and reported, for what it IS good for: since the identity is exact
for a converged conservative discretisation, `S_c == mdot/rho_u` is a sharp **consistency** check.
It is not validation. Test 2 is the control that would expose the degenerate method if it ever came
back.

---

## 6. Inputs still NOT frozen, and nothing may be measured until they are

`cp` and `lambda(T)` for methane–air are inputs to the gated flame speed and are **not yet cited or
frozen**. Every test above uses a SYNTHETIC fuel, deliberately: debugging against methane would mean
every decision was taken with the gated answer visible, which [g8_reference.md](g8_reference.md)
section 2 calls the integrity crux of the gate.

The order is: finish the solver on the synthetic flame, freeze `cp` and `lambda(T)` with citations,
then measure once.
