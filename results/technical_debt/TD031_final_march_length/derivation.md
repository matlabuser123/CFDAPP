# TD-031 — DERIVATION — a self-testing convergence criterion for the final march

```text
item        TD-031   the reported state is taken before the march has converged · P1
derives     the replacement for `finalMarchRelaxationTimes = 10`, which TD-031's own measurement
            showed cannot be a default VALUE
status      DERIVATION ONLY. No gate frozen, no production change. §6 names what a gate would
            have to establish.
```

---

## 1. Why the fix cannot be a number

`logs/01` and `logs/02` measured the convergence point across a mesh pair and a cap pair:

```text
500 cells / cap  5   dt 9.42e-06   converges at relaxT 18   (three independent measurements agree)
250 cells / cap  5   dt 3.77e-05   NO convergence point in relaxT 10-26
500 cells / cap 10   dt 1.88e-05   NO convergence point in relaxT 10-26
```

**Two of three configurations never converge**, because TD-028's limit cycle is still running there.
There is no convergence point to derive a default from, and a default chosen from the one
configuration that does converge would be the same error TD-027 exists to correct — a plausible
number validated against nothing.

So the march must **test its own convergence** rather than be told how long to run.

## 2. Why the existing test cannot do it

`finalMarchTolerance = 1.0e-9` bounds `max|dT|/(T_ad-T_u)` and `max|dY_i|` per step. The header
records it as **unreachable by construction** (F13) and the suite deliberately does not assert it.
The reason is derivable, and deriving it gives the replacement.

### 2.1 The irreducible per-step change

The eigenvalue is **bracketed**, not exact. With bracket tolerance `t`, `mdot*` carries a relative
error up to

```text
eps = sqrt(1 + t) - 1        ( = 4.9988e-04 at t = 1e-3 )
```

A front at a mass flux wrong by `eps` drifts, kinematically, at `~eps * S_L`. In one step it moves
`eps * S_L * dt`. The steepest temperature gradient in the field is `(T_ad - T_u) / delta`, so the
largest per-step temperature change that displacement produces is

```text
    max|dT| / (T_ad - T_u)   >=   eps * S_L * dt / delta          <- THE FLOOR
```

**A converged state cannot do better than this**, because the residual drift is a property of the
bracket and not of the march.

### 2.2 The floor, measured on the three configurations

```text
configuration            dt s         S_L m/s     delta mm      FLOOR      vs 1e-9
500 cells / cap  5    9.4246e-06    0.0467194     1.2781     1.7221e-07      172x
250 cells / cap  5    3.7699e-05    0.0482296     1.2340     7.3653e-07      737x
500 cells / cap 10    1.8849e-05    0.0465615     1.2589     3.4849e-07      348x
```

**`1e-9` sits 172x to 737x below the floor**, so it can never be met — F13's observation, now with a
mechanism. And the floor itself **varies 4.3x across three configurations**, which is the formal
reason no single fixed number can serve: the quantity the tolerance must bound is configuration
dependent, and every term in it is known at run time.

```text
eps     from massFluxResidual, already computed at the end of bisection
S_L     mdot / rho_u, already computed
dt      the march's own step
delta   flameThickness, already computed each time the state is reported
```

## 3. The criterion

**C-conv — THE MARCH STOPS WHEN IT HAS CONVERGED, AND REFUSES WHEN IT HAS NOT.** Three clauses,
each necessary:

```text
(a) PER-STEP STEADINESS   max|dT|/(T_ad-T_u) <= kMargin * (eps * S_L * dt / delta)
                          and the same bound on max|dY_i| scaled by Y_F,u
(b) SUSTAINED             (a) holds for `settledWindowsRequired` CONSECUTIVE windows, each at
                          least one relaxation time long
(c) ON ANCHOR             the measured front stays within recentreDeadBandCells throughout
                          those windows

and, if (a)-(c) are not all met within the step budget:

(d) REFUSE                return a status that says the march did not converge, instead of
                          reporting the state the budget happened to stop at
```

### 3.1 Clause (a): the one margin, and what it is for

`kMargin` is the only constant, and it is **explicitly empirical**. It exists because §2.1's floor is
a leading-order kinematic estimate — it ignores the diffusive relaxation still present in the
profile, the recentring quantisation, and the difference between the steepest gradient and the
gradient at the front. A margin of order 10 is the usual allowance for a leading-order bound of this
kind, and it must be **fixed by measurement before the gate is frozen**, not chosen here: the test
is that a converged march meets it and a transient one does not, on all three configurations.

**What has changed versus `1e-9`:** the tolerance is now *reachable by construction* and *adapts to
the configuration*, so it is a real criterion rather than a flag that is always false.

### 3.2 Clause (b): why one window is not enough

A limit cycle has turning points. At a turning point the per-step change passes through a minimum,
so clause (a) alone could be satisfied momentarily by an oscillating state — the exact failure mode
TD-028 produces. Requiring the bound to hold across **consecutive** windows, each a full relaxation
time, makes that far harder to fake: an oscillation would have to be stationary for two relaxation
times, which for a cycle of period 14 means a quarter of its period at the extreme.

This is deliberately the **same structure the trials already use** —
`driftSettleTolerance` / `settledWindowsRequired = 2` — rather than a new mechanism. One pattern,
already in the solver, applied to the march as well as to the search.

### 3.3 Clause (c): why the front position has to be in it

A settled per-step change at a **displaced** front is exactly what `logs/02` shows at relaxT 10: the
front sits 34.7 cells off the anchor and the state is changing slowly. Without (c), a march could
declare convergence on a state whose front has left the anchor, which is the condition the whole
anchoring mechanism exists to prevent. **(c) is only checkable because TD-005's C4d made
`flamePosition` a measurement** — before that it was the anchor echoed back, and this clause would
have been another identity.

### 3.4 Clause (d) is the one that matters most

Today the march runs to `finalMarchSteps`, sets `steadyStateReached = false`, and reports whatever
state it reached. Nothing asserts the flag — the suite explicitly cannot, because the tolerance is
unreachable. **So a non-converged state is reported as a result, silently.**

That single behaviour is why this campaign spent twelve probe runs on it:

```text
TD-028   the limit cycle ran undetected because the march never claimed to have converged and
         nothing required it to
TD-031   the default reports a transient, and no test could see that it was one
TD-004   C2 bounded the ratio of two arbitrary samples of that non-converged state
TD-030   the 8.47e-02 residual was masked by whichever phase the march stopped in
```

**A march that refuses rather than reports would have surfaced all four at the first run.** Clause
(d) costs nothing to implement and is the highest-value part of this derivation.

## 4. What this criterion would have caught, as a check on it

```text
cap 20, 500 cells   the limit cycle: (b) fails -- no two consecutive windows are steady
cap  5, 250 cells   the cycle persists: (b) fails
cap 10, 500 cells   the cycle persists: (b) fails
cap  5, 500 cells   converges at relaxT 18: (a)(b)(c) all met, so the march stops there and the
                    reported state is the converged one rather than the relaxT-10 transient
relaxT 10 at cap 5  (c) fails -- the front is 34.7 cells off the anchor
```

**Three of five configurations would REFUSE**, which is the correct answer for them and is the
opposite of what the solver does today. That is a severe outcome and it should be stated plainly: on
this derivation, the premixed-flame solver currently produces a defensible converged state at **one**
of the five configurations measured.

## 5. What this does NOT fix

```text
TD-028   the cycle itself. Refusing to report it is not removing it. A refusing march makes two
         of three configurations produce NO result, which is honest and not progress.
TD-030   the 8.47e-02 converged residual. It survives at the converged state, so clause (a)-(c)
         being met does not make the balance admissible.
TD-029   the non-self-similar mesh family.
```

## 6. What a gate for this would have to establish

```text
1. kMargin FIXED BY MEASUREMENT on all three configurations -- a converged march meets it, a
   transient one does not. It must NOT be fitted to make the one converging configuration pass.
2. clause (d) exercised: a configuration that does not converge must REFUSE, and the refusal must
   be distinguishable from the existing NoFlame and MaxIterations statuses.
3. the three measured configurations' verdicts predicted BEFORE the run (§4 above is that
   prediction, recorded now).
4. no silent change elsewhere: the trials use the same settling machinery, so a change there
   would move the eigenvalue. S_L must be bitwise unchanged at the one converging configuration.
5. the blast radius: a refusing march changes what the suite's tests receive. Several currently
   assert on states that this criterion would refuse to produce, so their verdicts WILL move and
   each movement must be named -- the accounting criterion TD-027's A1 had to add.
```

**Point 4 is the trap worth naming.** `settledWindowsRequired` and `driftSettleTolerance` are shared
with the eigenvalue search. Reusing them in the march is the right design, but any change to their
*meaning* would perturb the bracketing and therefore `S_L` — so the march must use the same
machinery with its own parameters, not alter theirs.
