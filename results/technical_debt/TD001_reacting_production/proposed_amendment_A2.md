# TD-001 — PROPOSED GATE AMENDMENT A2 — **NOT FROZEN, NOT AUTHORIZED**

```text
status      PROPOSAL ONLY. No criterion in GATE.md or GATE_A1.md is changed by this file, and
            neither of those files has been touched -- their sha256 values are unchanged.
amends      GATE_A1.md criterion C4b, and GATE.md criteria C6 and C7
reason      all three bound a quantity whose error model, as frozen, is incomplete:
              C4b scales the wrong variable      (verification_A1.md section 3)
              C6, C7 sit below the noise floor   (verification_A1.md section 5)
preserved   the failures are recorded verbatim in verification_A1.md and negative_controls.md
derivation  from the governing equations and the case file's declared values, BEFORE any
            replacement number was compared against a measurement
authorized  NO. This file exists so the request can be judged against a derivation rather than
            against a preference. Nothing here may be executed until it is authorized and frozen.
```

Every replacement below is **stricter** than the criterion it replaces. None is a relaxation: C4b
gains discrimination it has lost, and C6/C7 gain lower bounds that are above the measurement noise
instead of beneath it.

---

## 1. C4b — scale the variable that actually controls the error

### 1.1 What the frozen criterion assumed, and the one step that is wrong

`GATE_A1.md` §2 writes the N per-species transport equations as `A Y_i = b_i`, notes that equal
diffusivities make every `A_i` the same matrix, sums them, and concludes:

> `sum_i b_i` is the right-hand side of the equation whose exact solution is the constant 1 ... So
> the exact discrete solution has `sum_i Y_i = 1` identically, and the computed deviation is purely
> the accumulated linear-solve error.

The summation step is right. The conclusion needs one more thing that was not stated: for the
constant 1 to be the exact discrete solution, the **convective operator must annihilate a constant**,

```text
sum_f mdot_f Y_f  =  Y * sum_f mdot_f  =  Y * I_c ,      I_c = cell c's mass imbalance
```

which requires `I_c = 0` exactly. That holds for the differential operator and for a discretely
divergence-free flux. It does **not** hold for a SIMPLE flux field converged to a finite tolerance.

With that term restored, the cell error obeys `A delta = I`, so

```text
delta_c  =  (A^-1 I)_c           and in the transient-dominated regime, a_P = rho V / dt, so
|delta|  ~  dt * |I| / (rho V)
```

The deviation is therefore controlled by **the flow solution's residual continuity error and by
`dt`** — and not at all by the species solver's tolerance. `verification_A1.md` §3 confirms both
independently: one species alone reproduces the entire deviation, and a tenfold smaller `dt` reduces
it 8.1-fold while a 430-fold tighter linear solve does not move it at all.

### 1.2 The replacement

```text
C4b-i   dt SCALING.  The criterion's PRECONDITION is checked first, from the case file, not assumed:

          rho * V_cell / dt  >=  10 * max_f |mdot_f|

        (the transient term must dominate the diagonal, or a_P does not behave as 1/dt and the
        scaling being tested is not the one derived above). For cases/reacting_plug_flow at
        dt = 0.1: rho V/dt = 0.025, max|mdot_f| = rho u A = 0.0025, ratio 10.0 -- the precondition
        holds with no margin to spare, which is why it is stated as a precondition rather than left
        implicit.

        Then, with D(dt) = max_cell |sum_i Y_i - 1| at time step dt:

          0.5  <=  [ D(dt) / D(dt/10) ] / 10  <=  2.0

        i.e. a tenfold smaller dt must reduce the deviation by between 5x and 20x.

C4b-ii  CONTINUITY SCALING, an INDEPENDENT confirmation of the same mechanism:
        tightening the FLOW's continuity_tolerance must reduce D, because the flow's imbalance is
        the source term in the error equation. Required: D falls monotonically. No constant.

C4b-iii POSITIVITY -- A1's C4c, unchanged and still tolerance-free:
          min_cell min_i Y_i >= 0

C4b-iv  REPORTED, not gated: D itself, with dt, the flow's global_mass_imbalance and continuity
        residual beside it, so the inherited error is visible rather than folded into one number.
```

### 1.3 Why the factor-of-two band, and why it is not fitted

The ideal ratio is exactly 10. The band admits a factor of two either side because `a_P` is
`rho V/dt` **plus** the convective and diffusive coefficients, so the scaling is asymptotic in
`dt -> 0` rather than exact at any finite `dt` — and the precondition above only guarantees a tenfold
separation, not an infinite one. The factor 2 is labelled **EMPIRICAL**. The quantities it is applied
to are not: both the `1/dt` scaling and the precondition are derived in §1.1.

The band is far tighter than what it must exclude. A dt-independent defect gives a ratio of exactly
**1**, which is 10x outside the band's lower edge. So the criterion still separates the two cases
decisively, which is precisely what A1's version stopped doing.

### 1.4 Why this is STRICTER than the frozen C4b, not looser

The frozen C4b is failed by **both** the correct implementation (`D = 5.7e-08`) and A1-N1, the
Patankar-split control whose closure is `3.0e-01` — five million times worse. A criterion that both
fail is uninformative. The replacement is passed by the correct implementation and failed by A1-N1,
whose steady composition equation contains no `dt` at all and whose deviation is therefore exactly
dt-independent. The dry-run in §4 measures this rather than asserting it.

## 2. C6 — a predicted composition change, not merely a nonzero one

### 2.1 Why `> 1e-12` measures nothing on its own

`GATE.md` C6 requires `max_cell abs(omega_i) > 0` **and** `max_cell abs(Y - Y0) > 1e-12`. Control N2
(`pre_exponential = 0`, no chemistry at all) gives `max abs(dY) = 5.146406e-08`, which passes the
second clause by four orders of magnitude. It is the §1.1 transport error, not chemistry. C6 survives
only because its first clause fails on N2.

### 2.2 The replacement, derived from the case file alone

For a first-order reaction over one explicit step, with nothing taken from the run's output:

```text
k(T0)      = A * T0^b * exp(-Ea / (R T0))
dY_pred    = k(T0) * Y_fuel,0 * dt

for cases/reacting_plug_flow:
  k    = 5.0 * exp(-30000 / (8.314 * 1000))  =  0.135448 1/s
  dY   = 0.135448 * 0.1 * 0.1                =  1.354480e-03
```

```text
C6a   max_cell abs(omega_i) > 0 for at least one species          (unchanged)
C6b   0.5 * dY_pred  <=  max_cell abs(Y_i - Y_i^0)  <=  2.0 * dY_pred
```

Lower bound `6.77e-04`, which is **13 000x** the N2 noise floor of `5.15e-08`. The band is the same
empirical factor of two, for the same reason: the step is first-order with truncation error
`O(dt/tau_c) = 1.35%`, so a factor of two is loose by design — C6 asks whether chemistry ran, and
accuracy is P18.4–P18.6's already-closed gate.

## 3. C7 — a predicted temperature rise, and a sign test that is defined

### 3.1 Why `> 1e-9 K` and "sign matching standardReactionEnthalpy" both fail on N3

Control N3 (every `formation_enthalpy = 0`) gives `max abs(dT) = 5.688378e-05 K`, four orders above
the frozen `1e-9 K` threshold, so the magnitude clause **passes on the control**. And with every
formation enthalpy zeroed the reaction enthalpy is exactly zero, so "the sign matching
`standardReactionEnthalpy`" has no sign to match: the clause is undefined, not violated. **N3 does not
fail C7, and gate §5 item 2 is unsatisfied.**

The `5.69e-05 K` is the §1.1 error in the energy equation. Its distribution proves it is not a
response: `999.999943 .. 1000.000056 K`, symmetric about `T0`, with some cells **cooler**. A heat
release cannot cool a cell.

### 3.2 The replacement, derived from the case file alone

```text
dT_pred  =  dY_pred * |h_f,product - h_f,reactant| / cp
         =  1.354480e-03 * 2.0e6 / 1000     =  2.708960 K
```

```text
C7a   0.5 * dT_pred  <=  max_cell abs(T - T0)  <=  2.0 * dT_pred
C7b   SIGN, and it is now defined for every mechanism:
        exothermic   <=>  max_cell S_E > 0   (S_E = -sum_i h_f,i omega_i, which is the EXPORTED
                                              heat-release field, so the test needs no new quantity)
        for an exothermic mechanism:   min_cell (T - T0)  >=  0     -- no cell cooled
        for an endothermic mechanism:  max_cell (T - T0)  <=  0     -- no cell warmed
        for a thermally neutral one (max_cell |S_E| = 0): the mechanism is not admissible as a
        C7 instrument, and the case is rejected rather than passed vacuously.
C7c   REPORTED, not gated: dT, dT_pred and their ratio.
```

C7a's lower bound is `1.354 K`, which is **23 800x** the N3 noise floor. C7b's sign test is what N3
fails outright: N3's `min_cell (T - T0) = -5.7e-05 < 0`, and its `max |S_E| = 0` makes it
inadmissible as a C7 instrument in the first place — which is the correct verdict for a mechanism
that releases no heat.

### 3.3 Why this is STRICTER

The frozen C7 is satisfied by `1e-9 K` of anything. The replacement demands the temperature rise the
declared thermochemistry predicts, to within a factor of two, in the direction that thermochemistry
implies. A heat sink, a missing coupling and a double-counted source are all excluded; the frozen
version excluded only the first, and only when the magnitude happened to be large.

## 4. Non-vacuity — what the dry-run must show BEFORE this is frozen

`CLAUDE.md` §5.2 requires every criterion to be run against the unchanged baseline and each control
shown able to fail. For this amendment that means:

| id | control | must produce |
| --- | --- | --- |
| A2-N1 | **A1-N1**, the Patankar-split steady pass, at `dt` and `dt/10` | **C4b-i FAILS** — its composition equation has no `dt`, so the ratio must be 1, not 10 |
| A2-N2 | **N2** (`pre_exponential = 0`) | **C6b FAILS** — `5.15e-08` against a lower bound of `6.77e-04` |
| A2-N3 | **N3** (`formation_enthalpy = 0`) | **C7a AND C7b FAIL** — `5.69e-05 K` against `1.354 K`, and `min(T - T0) < 0`; and `max abs(S_E) = 0` makes it inadmissible |
| A2-N4 | the production implementation | **every replacement PASSES** |

A2-N1 is the one that matters, for the same reason A1-N1 was: it is the control whose failure proves
the criterion has regained the discrimination the frozen C4b lost.

### 4.1 Two instrument defects the dry-run exposed, corrected BEFORE this freeze

Both make the gate stricter. Neither changes a threshold or a derivation; they fix how the criteria
above are *evaluated*, which is what a pre-freeze dry-run is for.

**A zero prediction is not an excuse from the criterion.** The first version of the C6b/C7a probe
returned `N/A` when `dY_pred` or `dT_pred` came out zero — which is exactly the case for controls
A2-N2 (`pre_exponential = 0`, so `dY_pred = 0`) and A2-N3 (`formation_enthalpy = 0`, so
`dT_pred = 0`). Those two controls would have been excused from the criteria they exist to break: the
identical shape of failure to the one this whole amendment is correcting. The band
`[0.5*pred, 2*pred]` degenerates to `{0}` when `pred = 0`, so the requirement in that case is that
**nothing changed**:

```text
C6b, C7a   when the prediction is 0, the measured change must be exactly 0.
           A2-N2 measures dY = 5.146406e-08  ->  FAIL, as required.
           A2-N3 measures dT = 5.688378e-05  ->  FAIL, as required.
```

Likewise C7b's thermally-neutral branch now **fails** rather than reporting "inadmissible": §3.2's
word for that case is *rejected*, and a rejection is a failure, not a neutral verdict.

**C4b-ii is keyed to the flow's ACHIEVED pressure residual, not its declared tolerance.** The
dry-run tightened `pressure_tolerance` to `1e-10` and the flow did **not** reach it — not at 20 000
iterations and not at 400 000:

```text
declared 1e-8,  13 439 iters, converged     p_res = 9.998797e-09    D = 5.718198e-08
declared 1e-10, 20 000 iters, NOT converged p_res = 2.544617e-10    D = 1.753284e-09
declared 1e-10, 400 000 iters, NOT converged p_res (see log)        D = 6.808347e-10
```

So the declared tolerance is not the quantity that controls `D`; the residual actually achieved is,
and `D` tracks it closely — a 39-fold smaller `p_res` gave a 33-fold smaller `D` at the first step.
C4b-ii therefore reads:

```text
C4b-ii   run the same case with the flow driven to a SMALLER ACHIEVED pressure residual
         (reported in metadata.json residuals.p, not assumed from solver.json).
         Required: D falls monotonically as the achieved residual falls.
         The flow's own convergence flag is reported beside it, because a run that stopped at its
         iteration cap is a better-converged flow but not a converged one, and the evidence must
         say which it was.
```

## 5. What this amendment does NOT change

* `GATE.md` C1, C2, C3, C5, C8, C9, C10, C11, C12 — verbatim, including C10's **bitwise** requirement;
* `GATE_A1.md` C4a (the constructed state keeps `4*eps*N`) and C4c (positivity, tolerance-free), both
  carried through unchanged as C4b-iii;
* controls N1, N2, N4, N5 of `GATE.md` §3;
* the regression baselines;
* the original C4 failure in `verification.md` and the C4b failure in `verification_A1.md`, neither of
  which is reclassified.

## 6. What is NOT proposed, and why

The deviation could be driven to `eps` by renormalising the composition, by clipping it, or by
solving `N-1` species and setting the carrier to `1 - sum`. **None of these is proposed.** Each would
make C4b pass by *hiding* the inherited continuity error that §1.1 identifies, rather than by
bounding it — and the first two are the silent normalisation TD-002's authorization forbids outright.
The error is a real property of a segregated solve on a flux field that satisfies continuity only to
the flow solver's tolerance, and a gate's job here is to bound it and report it, not to conceal it.
