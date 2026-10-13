# TD-004 — AUDIT

```text
item        TD-004  Canonical premixed-flame capability · P1
origin      P18.7; KL 1.1
register    results/technical_debt/DEBT_REGISTER.md section 3
dependency  (register) TD-001, TD-002, TD-003
blocks      TD-005, TD-006, P18 closure
prior work  results/p18/g8_closure/flame_solver_findings.md  (F1-F17, uncredited -- DEBT_REGISTER section 6)
status      INVESTIGATION ONLY -- no production file changed, gate not frozen
```

---

## 1. The dependency question, answered first

The register lists **TD-003** (reacting-flow linear-solver robustness) as a TD-004 dependency, and
TD-003 is open. That has to be resolved before anything else, because starting a dependent item is
exactly what the campaign's failure policy forbids.

**It is already satisfied for this path, and by measurement rather than by argument.** TD-003's
substance is P18.7's finding that BiCGSTAB "broke down on a lower-bidiagonal, strongly non-normal
matrix and returned `-3.05e12` while reporting `SolverStatus::Breakdown`", with the recommendation to
"use un-restarted GMRES sized to the mesh **and check the solver status**". `PremixedFlame.cpp`
already does the stronger thing:

* it **judges the answer, not the status** — `||r||` against `||b||`, which is scale-free and does
  not get harder to meet as the guess improves (the mistake that produced two measured false
  failures, F8);
* it **fails closed** — a genuinely diverged solve has a large residual against `||b||` and is
  rejected with its numbers named; a system with `b == 0` requires a zero residual;
* it **preconditions** (Jacobi), because the Patankar `Sp` spans orders of magnitude across the front
  and unpreconditioned BiCGSTAB stagnated;
* it **reports** `worstLinearResidual` = max over every inner solve of `||r||/||b||`, so the claim is
  checkable from the result rather than taken on trust.

So TD-003's repository-wide audit remains TD-003's, and TD-004 is not blocked by it **for the flame
path**. This is recorded as a finding rather than assumed, and §4 measures `worstLinearResidual` to
confirm the solves are not the precision limit.

## 2. What already exists, and is verified

From the prior (uncredited) work, measured:

```text
max_cell |sum_i Y_i - 1|     0.887  ->  1.4e-11      conservation, after F7
min Y_i                     -1.5e-14                 negligible, never repaired
peak temperature             1800.00 K == T_ad exactly, after F6
S_L under refinement         observed order p = 1.93, Richardson 4.894 cm/s, GCI 0.21 %
```

and structurally:

* **no momentum solve is needed** — the face mass flux is the scalar `mdot` plus the continuity
  correction, with thermal expansion carried through `rho = p Mbar / (R T)` (F7);
* **variable `lambda(T)` and a per-cell heat release** both work, composing the field-property
  assembly with `assembleThermalSourceContribution` (RHS-only, so exact). This removed the largest
  free parameter a gated flame speed would otherwise have had, since `S_L ~ sqrt(lambda)`;
* **the test fixture was selected by measurement**, not guessed — `DISABLED_ParameterWindowScan`
  sweeps `(A, Ea)` and reports peak temperature and both zone widths in cells, so the choice can be
  re-derived instead of trusted.

Seventeen tests are declared (the record says 13; four were added after it was written) and **most
currently fail**. That is the starting state, not a regression.

## 3. The live defect: the method's precision, not the flame's physics

The march is stable — no extinction, no divergence, exact adiabatic temperature, conservation to
`1.4e-11`. What fails is that a trial does not **settle**:

```text
drift window 32   mdot = 0.05303   S_L = 4.50 cm/s
drift window 64   mdot = 0.05088   S_L = 4.32 cm/s
spread                             4.3 %
```

Same problem, same code; the only difference is how long each trial marched before its drift was
read. **4.3 % is the method's reproducibility, not a property of the flame.**

The diagnosis is already made and is not a solver problem: each eigenvalue trial reads the drift
after 128 steps (~3.6 flame transit times), which F11 showed is **before the profile is steady**. The
identified route is to apply the same steadiness condition INSIDE each trial that the final march
already applies to the reported state.

Two candidate contributors were separated in the prior work and both remain live:

* **grid-locking noise** — the front position is a linearly interpolated crossing, so it wobbles by
  a fraction of a cell as the front traverses the mesh. A settling bound tighter than that noise can
  never be met, whatever the physics does;
* **genuinely slow evolution** — which more steps resolve and a looser bound does not.

Separating these is TD-004's work, and the distinction matters because raising the bound until a
measurement passes is what CLAUDE.md §5 forbids.

### 3.1 Why this precision is a precondition, and for what

G8's band is `35.0-37.0 cm/s`, i.e. **±2.8 %** about 36. A method whose own reproducibility is 4.3 %
cannot resolve a ±2.8 % band near its edges — a result landing at 36.5 would carry an uncertainty
wider than the band judging it, and a PASS would be an artefact of which drift window was used.

Stated precisely, because overstating it would be its own error:

* a result landing **far** outside the band (50 cm/s, say) is decisive as a FAIL even at 4.3 %;
* a result landing **inside** the band, or just outside, is **not** decisive either way — and that is
  the case in which G8 would otherwise be declared closed.

So the precision is a precondition for claiming a PASS and for reporting a near-miss FAIL as a real
one. **Deriving TD-004's precision requirement from the band is legitimate**: the band is frozen,
cited and public (`g8_reference.md`, Hermanns 2007, DOI 10.6100/IR630126). The integrity crux is not
seeing the **answer**, which no synthetic-fuel test does.

## 4. MEASURED — and the prior record cannot serve as TD-004's baseline

`logs/01_flame_state.log`. The measurement was taken to check `worstLinearResidual`; it found
something more consequential.

### 4.1 Three recorded numbers do not reproduce

```text
quantity                      record says        measured now      test gate     verdict
max_cell |sum_i Y_i - 1|      1.4e-11 (§2)       6.29103e-07       < 1.0e-8      FAIL
fuel balance residual         0.0222  (F16)      0.0432796         < 0.02        FAIL
energy balance residual       --                 0.0432805         < 0.02        FAIL
```

`CLAUDE.md` §8: *"A 'known' failure stays known only while it reproduces its recorded values exactly.
Any new or changed failure is a regression."* These do not reproduce, so they must be attributed
before anything is built on them.

**TD-001 and TD-002 are cleared, by inspection of the dependency surface rather than by assumption:**

* `PremixedFlame.cpp` includes `SpeciesTransport.hpp` and `EnergyEquation.hpp`; the header adds
  `ArrheniusKinetics.hpp`, `HeatRelease.hpp`, `MixtureThermodynamics.hpp`, `ReactionMechanism.hpp`.
  **None of those was changed by TD-001.**
* `HeatRelease.hpp` *was* changed by TD-002 — but only by a doc block and a **defaulted** parameter
  on `advanceClosedReactor`, and the flame **never calls it** (the three mentions are comments). It
  calls `reactionHeatRelease`, which TD-002 did not touch. TD-002's C8 separately proved
  `advanceClosedReactor` with no carry is bitwise the pre-change function over 140 swept states.
* `ChemistryIntegrator` is mentioned only in comments; not included, not called.
* `src/species/PremixedFlame.cpp` and its header were last modified **2026-10-03T16:43**, before
  TD-001 began.

**So the mismatch is internal to the prior work's own record.** §2's "Established by measurement"
block was measured after F7 and never re-measured after F10, F14, F15 and F16 changed the solver and
F17 re-gated the tests at 20:00 — one minute before the record was written. It presents early numbers
as current state.

**Consequence for this item:** the record's figures are **not** a usable baseline. CLAUDE.md §3.4
requires the baseline to be reproduced on the unchanged tree and recorded, and `logs/01` is now that
baseline. Nothing in TD-004 may be compared against the record's numbers.

### 4.2 The fuel and energy balances are ONE measurement, not two — F12 recurring

```text
fuel balance   = 0.0432796
energy balance = 0.0432805      agreeing to FOUR significant figures
```

That is not two independent conservation checks. For a single-step `A -> B` with constant `cp`, the
energy balance is the fuel balance scaled by `Delta h / cp`, so they are the same equation written
twice — exactly what **F12** found and named ("the 'two independent balances' are ONE measurement
written twice"). The tests still assert both as if independent, which means the gate currently counts
one piece of information as two.

**Any gate TD-004 freezes must not do that.** An independent second check has to come from a
different conserved quantity, not from rescaling the first.

### 4.3 What is now WORKING that the record listed as open

The record's §3 open problem was that trials do not settle. They do now:

```text
every trial reports "(measured, settled)"
bisection drift:  0.993 -> 0.447 -> 0.172 -> 0.0393 -> -0.0157 -> ... -> -4.39864e-06
S_L = 4.8623 cm/s   S_c = 4.90006 cm/s   mismatch 0.78 %
T_peak = 1800 K == T_ad exactly    min Y_i = 0    delta = 1.22891 mm
bisect 10 after 4 brackets, 19264 steps
```

`S_L = 4.8623` also sits close to the record's Richardson extrapolation of `4.894 cm/s`.

**What is not steady is the FINAL march**, not the trials: `steady = 0 after 3968 final-march steps,
dT residual = 0.166188`. That is a different defect from the one the record describes, and it is where
the 4.33 % balance residual most likely comes from — a march stopped before the profile settled does
not close a balance.

### 4.4 The question this measurement was taken to answer is still open

`worstLinearResidual` is **not** printed by these three tests, so whether the inner solves limit the
precision is still unmeasured. It must be reported before the gate is frozen, because §1's clearing
of TD-003 rests on it.

## 4.5 A second defect the baseline exposed, recorded and NOT fixed here

The per-test baseline (`logs/06`) found that **G8's negative control 1 cannot run**:

```text
WithReactionsDisabledNoFlamePropagates   ->   TIMEOUT after 900 s
```

This is property 6 of §5 — *reactions off ⇒ no flame*. The test raises `maxOuterIterations` from 40
to 2000; with reactions off the seeded front washes out slowly rather than vanishing, so early trials
still locate a front, return a sided verdict, and the bracket grinds through the raised budget at up
to 20000 steps per trial. The `!sided -> NoFlame` short-circuit fires only once the front is already
gone, so **the no-flame case costs more than the flame case** — there is no early-out for a mixture
that cannot sustain a flame.

It is a defect in the eigenvalue search's TERMINATION, not in the recentring operator, so it is
outside route (a). Recorded as **TD-020** and named in `GATE.md` §5.1 as a permitted non-runner, with
the consequence stated plainly: **TD-004 closes with one of G8's two negative controls unexercised.**

A process note belongs here too. The first baseline invocation was stopped at the 30-minute
background cap and its `CFDSpeciesTests` child was **orphaned**, still consuming a core — the WSL
child-process leak this repository has on record. It was cleared before the run resumed; left alone
it would have competed with the resumed measurement and contaminated every timing in it. The
harness's own per-test `timeout 900` worked correctly; it was the outer kill that leaked.

## 5. Scope, and the order the prior work fixed

The prior work fixed an order that this audit does not propose to change, because it is the integrity
argument of the whole G8 closure:

```text
TD-004   finish the solver on the SYNTHETIC flame -- the 9 properties below
TD-005   the extraction METHOD, frozen before the answer is looked at
TD-006   freeze cp and lambda(T) for methane-air with citations, then measure ONCE vs 35.0-37.0
```

Every current test uses a **synthetic** fuel deliberately: debugging against methane would mean every
decision was taken with the gated answer visible.

The nine properties a correct eigenvalue solver must have, from the prior work's own list — no test
asserts an absolute `S_L`, because there is no published flame speed for a made-up fuel:

| # | property | what it catches |
| --- | --- | --- |
| 1 | peak `T` approaches the independent `T_ad` | a broken energy balance (caught F6) |
| 2 | `S_L` independent of the initial guess | **the degeneracy trap** (§6) |
| 3 | `S_L` plateaus in anchor position and domain length | the cold-boundary difficulty |
| 4 | consumption integral agrees with `mdot/rho_u` | discrete conservation (caught F7) |
| 5 | `S_L` converges under refinement | G8's own criterion for profiles |
| 6 | reactions off ⇒ no flame | G8 negative control 1 |
| 7 | `10x A` moves `S_L` by `> 2x` | G8 negative control 2 |
| 8 | variable `lambda(T)` changes the answer | `lambda` frozen at the inlet value |
| 9 | **answer independent of the drift window** | **the live defect — §3** |

**In scope:** properties 1-9 on the synthetic flame, with 9 as the open one; the fuel-balance
residual (F16 left it at 2.2 % against a 2 % gate — a near-miss that must be resolved, not rounded);
and whatever the §4 measurement turns up.

**Out of scope, so the item stays closable:** the extraction method's freezing (TD-005); methane,
`cp`, `lambda(T)` and the G8 measurement (TD-006); TD-003's repository-wide audit; the implicit
Jacobian (TD-019); reacting shocks, detonations, supersonic combustion, 3D combustion — excluded by
the register's own scope bound. **No parallel combustion stack**: P18.1-P18.6 are reused.

## 6. The degeneracy that must not come back

The obvious way to find `mdot` — a fixed point on the consumption integral
`S_c = -1/(rho_u Y_F,u) * integral omega_F dx` — is **degenerate**. Integrating the fuel equation
with the front detached from the inlet gives `S_c == mdot / rho_u` identically, for **any** `mdot`
admitting a steady solution. It reproduces whatever `mdot` it was given, converges instantly from any
guess, and would have looked like a converged flame speed while measuring only the initial guess.

The integral is still computed and reported for what it is good for: since the identity is exact for a
converged conservative discretisation, `S_c == mdot/rho_u` is a sharp **consistency** check. It is not
validation, and property 2 is the control that would expose the degenerate method if it returned.

Any gate TD-004 freezes must keep property 2, for this reason specifically.
