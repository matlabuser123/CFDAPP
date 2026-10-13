# TD-049 — C3 MOVED here from TD-003, by user decision. The ΣY closure of the species RHS assembly.

```text
STATUS:      **CARVE-OUT, not a measurement.** No production change, no test change, no threshold
             change. The `1e-8` stands untouched and `test_premixed_flame.cpp:471` still fails.
DECISION:    the user's, 2026-10-12 — option (b) of verification_model_refuted.md section 5:
             "move C3 to the species RHS assembly, where the measurement says it belongs."
             Option (a) -- re-deriving what closure an ITERATE can be held to -- was NOT chosen
             and is recorded in section 6 as the alternative that remains available.
WHY IT MOVED: measured, not suspected. The linear solve contributes **~1 %** -- an exact Thomas
             solve of the SAME assembled systems still fails the gate by **230x** -- so no
             composition-write site that produces the deviation is the linear solver.
             (`../TD003_linear_solver/verification_instrumented_residuals.md`)
EVIDENCE:    all of it stays in `../TD003_linear_solver/`. CLAUDE.md section 4 and section 10:
             evidence is immutable and corrected FORWARD. Nothing was moved on disk, renamed or
             rewritten. This file is the pointer, and sections 3 and 4 below are the inheritance,
             so the next owner re-measures nothing and re-attempts nothing.
```

---

## 1. SCOPE OF TD-049, STATED NARROWLY

**The quantity.** `d = max_c |Sigma_i Y_i - 1|`, reported as
`PremixedFlame::Result::maxCompositionSumDeviation`, measured at
`src/species/PremixedFlame.cpp:1616-1629` over the final march's field.

**The question.** Why does `d` reach `~1e-6` and park there? The transient explains PERSISTENCE;
the chemistry and the initial condition are EXCLUDED; **no mechanism accounts for the MAGNITUDE.**

**The owner site.** The species RHS assembly — `src/species/SpeciesTransport.cpp:303-348`, whose
assembler order is frozen (diffusion -> closure correction -> convection -> source -> transient
last, `:303-306`), with the transient branch at `:348`. Attribution by isolation charged **98.79 %**
of the RHS residual to the transient assembler, with the decomposition CLOSING (sum of parts /
total = 1.0263), so **one assembler is responsible — not the accumulation order.**

**The threshold.** `tests/unit/species/test_premixed_flame.cpp:471`,
`EXPECT_LT(flame.maxCompositionSumDeviation, 1.0e-8)`. Carried here OPEN and UNRESOLVED (section 5).

**Not in scope, and left with TD-003:** C1 (repository-wide solver-status audit) and C2 (a solver
demonstrated adequate on the plug-flow matrix class). Those are linear-solver questions and the
measurement leaves them there.

## 2. WHAT THIS ITEM MUST NOT DO

```text
MUST NOT  weaken, round, rescale or "migrate" the 1e-8 to obtain a PASS     (CLAUDE.md 1.4, 5.3)
MUST NOT  delete or rewrite any TD-003 evidence, including the two documents
          already marked INVALID / WITHDRAWN in place                       (CLAUDE.md 4, 10)
MUST NOT  re-attempt any of the seven refuted hypotheses in section 4 without first
          naming what is NEW in the attempt
MUST NOT  change production or tests without its own authorization          (CLAUDE.md 2)
```

## 3. INHERITED — ESTABLISHED, DO NOT RE-MEASURE

```text
THE SOLVE IS NOT IT
  an exact Thomas solve of the same systems leaves 2.301527e-06 -- 99.97 % of the deviation,
    still 230x the gate, with ZERO solver error
  worstLinearResidual 9.999987e-10; all 221 049 solves cleared converged(); the fallback never
    fired; :625's 1e-11 relative tolerance never binds because :686's absolute tolerance is what
    converges

THE TRANSIENT CARRIES IT
  the transient assembler produces 98.79 % of the RHS residual r (2.376432e-11 of 2.405621e-11),
    charged by ISOLATION with the decomposition CLOSING (sum of parts / total = 1.0263)
  closure correction 3.85 % - diffusion 6.8e-21 - convection and the zero source EXACTLY zero
  r^trans = rho V/dt * d_old, because :1157 passes the SAME density at both time levels -- so the
    transient CARRIES the deviation rather than creating it. This is the correct non-conservative
    rho dY/dt form and is PROBABLY NOT A DEFECT.

NOTHING INJECTS IT
  the seeded profile closes to 1.110223e-16 (0.5 ulp) -- the initial condition is NOT the seed
  the chemistry half CANNOT seed: sum_i dY_i = 0 EXACTLY, by exact negation at two species
  inlet, recentring fill {0.05, 1.0-0.05} and burnt composition all sum to 1 EXACTLY
  yet d is already 4.633173e-09 by the end of STEP 1 -- a 4e+07 jump inside one step

THE OPERATOR IS FINE
  ||A||_inf * ||A^-1||_inf = 33 -- WELL-CONDITIONED. ||A^-1|| is large only because the
    coefficients are small: the assembly is in FLUX units, ||A||_inf = 2.73e-04
  the per-species matrices are BIT-IDENTICAL at Le = 1, so sum_i(A y_i) = A(sum_i y_i) EXACTLY
  the assembled RHS closes to 3.162982e-11 = 3.5x the round-off floor eps*||b||
```

## 4. INHERITED — REFUTED. SEVEN. DO NOT RE-ATTEMPT.

```text
1  tolerance-limited solve       spread 4.8x, not constant; non-monotonic in tolerance
2  round-off accumulation        steps x2.89 while the deviation FELL x0.57
3  the Patankar source sum       the transport source is an EXPLICIT ZERO (PremixedFlame.cpp:1172)
4  kappa x residual              kappa MEASURED 26.28, not the assumed 1000
5  round-off x near-singular op  condition number is 33; r is 5.6e+08 x eps*||b||
                                 -- and this one had already been COMMITTED as a finding
6  feedback via the closure      causal control: removing the term left r at 2.73e-11 vs 3.16e-11
                                 and S_L BIT-IDENTICAL (rawSum ~ 0 at Le = 1)
7  the recursion d = M d + s     injection s spans 8.305241e-09 .. 1.684183e-07, a 20.28x SPREAD
                                 where the model requires it CONSTANT, with its upper end within
                                 5x of the entire deviation it was meant to explain
```

**Two further things died alongside them**, and are recorded so they are not revived:

* **AMENDMENT A1** (`T*eps*||A^-1||*(N_s||b||+||A||)`, `T = 24`) failed its own pre-registered
  dry-run by **6.2e+06**. `T` was NOT adjusted. The error was SCALE: flux units.
* **Option B's premise** — that the `9.54994e-08` minimum showed `1e-8` to be within reach — was my
  own counter-argument and is REFUTED. That minimum came from a **125-cell** state whose
  consumption integral is off by **51 %**: a worse solution on a coarser mesh, not a
  better-conditioned flame.

### 4.1 THE METHOD WARNING, which is the most transferable thing here

**Three of the seven (1, 5, 7) failed because the instrument measured something DERIVED from its own
subject**, and a fourth (6) ran its causal control on a term contributing 3.85 %. An eighth
instrument on this same fixture is more likely to produce an eighth plausible story than an answer.
Whatever is tried next should be able to fail for a reason that is not about `d` itself.

## 5. THE THRESHOLD QUESTION, CARRIED HERE OPEN

```text
the 1e-8 is in NO frozen gate      P18_GATES.md has no such bound; the test file carrying it was
                                   untracked until 1a70f0a
it was never DERIVED               every neighbouring bound on this quantity class is RELATIVE --
                                   G2(a) 4*eps*N = 1.78e-15, G3 8*eps*N_cells = 8.88e-13, and the
                                   repository's own section-30 form 8*eps*N*max|J| at
                                   include/cfd/species/SpeciesTransport.hpp:140-153, whose comment
                                   states outright that "an absolute tolerance on a dimensional
                                   flux is meaningless across meshes and diffusivities"
it has NEVER been met              0 of 81 recorded measurements, range 9.54994e-08 .. 4.00526e-06
the same constant passes elsewhere applied to minMassFraction it clears by 529x, while failing
                                   here by 88x
```

A section 5.3 amendment is therefore **DUE and NOT AUTHORIZED**, and it would **REPLACE AN UNDERIVED
CONSTANT**, not correct a stale one. Note for whoever writes it: the first attempt (A1) failed its
own dry-run, so the next must dry-run against the measured `||A||_inf = 2.73e-04` rather than an
assumed scale.

## 6. THE ALTERNATIVE NOT CHOSEN, recorded because it is still available

Option (a) was to **re-derive what `Sigma Y` closure an ITERATE can be held to**. The repository
already draws that distinction and has not acted on it: `MassFractionState` enforces
`4*eps*N_species` *at step boundaries, where it must hold*, while `ProjectRunner.hpp` states that an
iterate "legitimately violates sum(Y_i) = 1" and that such a composition "cannot meet that bound —
its closure is the linear solve's." That last clause is **itself the refuted hypothesis #1**, now
measured at ~1 %, so option (a) would have to re-derive the clause rather than cite it.

## 7. A DEFECT FOUND WHILE WRITING THIS, and NOT fixed here

> **RESOLVED FORWARD 2026-10-12, after this document was committed.** The user authorized the
> comment fix and it is done: see `verification_comment_fix.md`. Five lines replaced by five lines,
> the line count preserved so that none of the 60 citations below line 619 shifted, and the
> compiled object is **BIT-IDENTICAL** at `-O3 -DNDEBUG` with a determinism control and a
> perturbation control. The production hash is now
> `19db4b6b3b5279055899d1051763b6f5ae85926aa1f4fe2d4d0c5038b405fe9d`, so the `fa815c97…` stated
> below was correct when written and is now superseded. The text below is left exactly as
> committed, per CLAUDE.md §4 and §10.

`src/species/PremixedFlame.cpp:615-619` carries a production comment asserting the refuted cause:

> "...the per-species systems must be solved tightly for sum_i Y_i to stay at 1 ... that
> cancellation only holds to the tolerance each one is solved to."

The measurement says the solve contributes **~1 %** and an exact solve still fails by **230x**, so
this comment attributes the deviation to the wrong term at the exact site TD-049 now owns. It is
**left in place**: this carve-out is register structure only, `PremixedFlame.cpp` is unchanged at
`fa815c97216584485e1c94842669fbf71976b9fb06863096868972d88448e3a2`, and a production edit needs its
own authorization (CLAUDE.md section 2). Recorded here as TD-049's first concrete task.

## 8. WHAT CHANGED IN THE REGISTER, AND WHAT DID NOT

```text
TD-003   loses C3. Keeps C1 (SATISFIED, escalation gap carved out as TD-047) and C2 (AUDITED, the
         candidate fix identified and NOT applied). Stays OPEN on C2.
         CITATION CORRECTED WHILE WRITING THIS: the register records the bare BiCGSTAB at
         ProjectRunner.cpp:816. It is now :828 -- `const cfd::algebra::BiCGSTAB
         reactingLinearSolver{reactingLinearSettings};` -- because TD-047's fix inserted
         statusWithReactingOutcome() and its call site above it. The SUBSTANCE is unchanged and
         re-verified here: `LinearSolverFallback` appears in SIMPLE.cpp and CompressibleSIMPLE.cpp
         and in NO line of src/app/ProjectRunner.cpp, so the reacting path still has no escalation.
TD-049   NEW. Owns C3, its evidence by reference, and its threshold question.
blocking the register's own words were that C3 "is why this item blocks TD-004's completion RATHER
         THAN MERELY PRECEDING IT". Applying that sentence, the COMPLETION-block on TD-004 moves to
         TD-049; TD-003 continues to precede TD-004, TD-005 and TD-006 on C2 grounds.
         **NOT established, and flagged for the user:** whether C2's unwired fallback
         INDEPENDENTLY blocks TD-004's completion. The measurement says the fallback never fired on
         any measured configuration, but TD-004's gate was never evaluated against a solver change.
TD-006   NOT claimed as blocked by TD-049. G8's criterion is the flame-speed band 35.0-37.0 cm/s;
         Sigma Y is not one of its criteria, so no such block is asserted.
on disk  NOTHING MOVED. No file in TD003_linear_solver/ was renamed, edited or deleted.
```
