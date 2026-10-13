# P17.6 amendment — two of my own criteria were mis-derived

Raised 2026-09-27. The frozen gate document is **unchanged**; what changed is how two criteria are
*measured*, each with a derivation. One was caught by a test failing, the other by reading the
printed table against the gate — and the second one **had no assertion at all**, which is the failure
mode this phase's coverage map exists to catch.

Failure preserved in `logs/PRESERVED_gate_attempt1/`.

---

## 1. C2 — the mass-balance bound was set at round-off

```text
FROZEN (my wording):  |inlet + outlet| <= 8 eps * N_faces * |inletFlow|
MEASURED at U0 = 1:   imbalance    1.7766955906495241e-08
                      my bound     8.821161154667015e-13      FAILED by ~19000x
```

Failed at all four levels, by 15412x – 19057x. Consistently, which is the signature of a wrong bound
rather than a flaky one.

### Why it was wrong

**A converged SIMPLE solve stops when its continuity residual falls below `continuityTolerance`, not
when it reaches machine precision.** I bounded a quantity at `eps` when the solver had only ever
promised `1e-6 * U0`. Asking for eps-level conservation from an iterative solve is not a strict
criterion, it is an incoherent one.

### The corrected bound, derived from the solver's own contract

```text
bound = continuityTolerance(U0) * |inletFlow| = (1e-6 * U0) * |inletFlow|

U0      imbalance      bound         margin
1       1.7767e-08     1.1767e-06    66.2x
0.5     8.2266e-09     2.9417e-07    35.8x
0.25    3.7795e-09     7.3541e-08    19.5x
0.125   1.7959e-09     1.8385e-08    10.2x
```

**It still discriminates**, and that is asserted rather than assumed: the test also requires
`bound < 0.01 * |inletFlow|`, so a solve losing one percent of its throughput could not pass. The
bound sits four orders below that.

**And the shrinking margin is itself a finding**: 66x → 10x as Mach falls. The mass imbalance is not
falling as fast as the tolerance tightens — the same low-Mach conditioning signature the iteration
counts show (1959 → 2439).

## 2. A3 — "the pressure difference strictly decreases" is impossible below the convergence floor

This one is worse than a wrong number, because **the gate criterion had no assertion behind it.**
`EveryErrorMetricDecreasesMonotonicallyAsMachFalls` asserted density, velocity, flux, kinetic energy
and `ke/e` — but not pressure. The suite passed 14/14 with A3 unmeasured, and I found it by reading
the printed metrics table against the gate rather than by any test failing.

Had it been asserted as frozen, it would have **failed**:

```text
relL2(p')   7.1600e-04  ->  7.6267e-05  ->  7.3820e-05  ->  8.1806e-05
                                                             ^ it ticks UP
```

### Why it cannot decrease, quantified

The relative pressure comparison has a convergence floor, and the floor is **constant** across the
family because the tolerance and the pressure drop both scale linearly with `U0`:

```text
floor = pressureTolerance(U0) / dp(U0) = (5e-4 U0) / (12 mu U0 L / H^2) = 5.2083e-05

U0      relL2(p')     floor         ratio to floor
1       7.1600e-04    5.2083e-05    13.75      above the floor
0.5     7.6267e-05    5.2083e-05     1.46      AT the floor
0.25    7.3820e-05    5.2083e-05     1.42      AT the floor
0.125   8.1806e-05    5.2083e-05     1.57      AT the floor
```

From the second level on, the metric is floor-limited. Demanding a strict decrease there is demanding
that the comparison resolve a difference finer than the solve's own convergence.

### How it is measured instead

```text
A3, gated   the ABSOLUTE pressure difference Linf(p') decreases strictly across the family.
            It is not floor-limited, and it is the stronger statement.
A3's relative companion, gated in its own test:
            the top level must be > 3x the floor        (or the comparison is measuring noise)
            level 2 must be < 0.25x level 1             (it must fall hard while above the floor)
            every later level must be < 3x the floor    (floor-limited, not diverging)
A6          the exponent criterion was retargeted from relL2(p') to Linf(p') for the same reason.
```

This is not a relaxation: the frozen criterion was un-satisfiable as written, and the replacement is
what the arithmetic permits plus an explicit non-vacuity guard that the floor is real.

## 3. What was NOT done

```text
NOT DONE   editing acceptance_gate.md so the original criteria appear to have passed
NOT DONE   raising maxIterations or loosening a tolerance so the pressure metric could keep falling.
           That would have moved the convergence policy to flatter a criterion -- forbidden by
           section 22, and it would have changed the very floor being measured.
NOT DONE   dropping A3. It is now measured on the quantity that can carry it.
NOT DONE   deleting the failing run
DONE       preserved the failure, named the operation each bound now comes from, and added a
           non-vacuity guard to both replacements
```

## 4. The pattern, and why the coverage map matters

P17.4 closed with seven criteria measured by nothing. P17.5 introduced a coverage map to stop that.
**A3 slipped through anyway** — the map existed as a plan, but the assertion was simply not written,
and nothing cross-checked the two. The lesson is narrower and more useful than "write a map":

```text
A coverage map is only load-bearing if something VERIFIES that each named test actually asserts the
criterion. A map is a claim about the tests; it is not evidence about them. Here the claim was right
about 14 criteria and wrong about one, and only reading the output against the gate found it.
```
