# P17.5 amendment — `discretization::diffusion` does not deliver a prescribed zero heat flux

Raised 2026-09-27 by gate attempt 1's failure of **G2** (adiabatic closed domain).
The frozen gate is **unchanged**. The IMPLEMENTATION was wrong, not the criterion.

---

## 1. The failure, as it appeared

```text
TotalEnergyConservationTest.AdiabaticClosedDomainConservesTotalEnergy   FAILED
  initial 10068664.404938092
  final   10068664.393379444
  drift   -0.011558648198843002        bound 1.2519878615536344e-05
```

A relative drift of **1.15e-9**, which is about **5e6 eps**. Far too large to be round-off, so a
threshold adjustment would have been the wrong response. The first question was which term failed
to sum to zero, and that was measured rather than guessed.

## 2. The diagnosis — `logs/04_diagnose_g2.log`

A purpose-built probe printed the global sum of each flux term per step:

```text
                     BEFORE the fix                      AFTER the fix
sum(convOut)         3.49e-10  = 0.95 eps of worst cell  -1.16e-10 = 0.95 eps    <- always fine
sum(condOut)         5.6361    = 2.44e+15 eps            2.66e-15  = 1.9 eps     <- THE CULPRIT
domain drift, 20 st  -0.0116   = 1.15e-9 relative        1.86e-09  = 0.83 eps
```

The convective operator telescoped perfectly from the start. **Conduction leaked 5.64 W out of a
domain whose every boundary carried a prescribed zero gradient.**

## 3. The cause, in the operator's own source

`discretization::diffusion` -> `uncorrectedBoundaryFlux` (`src/discretization/Diffusion.cpp:61`)
does not use the two-point secant when an opposite interior face exists. Its own comment:

```text
// The plain two-point secant (phiB - phiP) / dPB estimates dphi/dn
// at the *midpoint* of P and the face, not at the face itself: it is
// only first-order accurate there ... Where a third point is available
// (the interior neighbor across the cell from this boundary face), fit a
// quadratic through (boundary, owner, opposite-neighbor) instead
```

So with `FixedGradient(0.0)` the *face value* is correctly `T_b = T_P`, but the *flux* is taken from
the quadratic's derivative at the face, which is **not zero for a curved profile**. The test's
temperature was `320 + 15 sin(x)` — curved. A prescribed zero gradient therefore produced a
non-zero heat flux, and the "adiabatic" box was not adiabatic.

This is not a defect in that operator. It is a deliberate accuracy trade: the quadratic recovers
second-order accuracy at a Dirichlet wall, which is what most of the solver wants. It is simply the
wrong tool for a **conservation** statement.

## 4. P15 already does the opposite, on purpose

`include/cfd/thermal/EnergyEquation.hpp`, describing gradient-type conditions:

```text
A gradient-type condition (Adiabatic, FixedGradient, HeatFlux -- every such boundaryValue() is
ownerValue + g d) gives T_b = T_P + g d, so the flow is -Df g d whatever T_P is: diagonal = 0,
source = Df * boundaryValue(0, d) = Df g d. The prescribed flux is assembled EXACTLY
(zero for Adiabatic), with no dependence on any iterate.
```

That is exactly the behaviour an energy balance needs: an adiabatic wall must carry **exactly** zero
energy, not a second-order estimate of zero.

**So the reuse target was wrong.** The authorization's §12 says to reuse P15's conductivity *and its
validated boundary handling*; the first implementation reused a different, generic operator and got
a different boundary contract with it.

## 5. The correction

`totalEnergyConductiveDivergence` is now built from the per-face helpers both paths share:

```text
internal face   Df = discretization::internalFaceDiffusionTerms(...).coefficient
                outflow_owner     += Df (T_P - T_N)
                outflow_neighbour -= the same double
                -> exactly antisymmetric, so internal faces cancel exactly in a global sum
boundary face   Df   = discretization::boundaryFaceDiffusionTerms(..., prescribedValue=false)
                {diagonal, source} = thermal::boundaryDiffusionContribution(..., Df)
                outflow = diagonal * T_P - source
                -> gradient-type: diagonal = 0, source = Df g d, so outflow = -Df g d,
                   EXACTLY zero for g = 0
```

No new conduction discretisation and no private conductivity correlation is introduced; both helpers
are the ones P15's own assembler calls.

## 6. What this adds to the gate rather than removes

Criterion **E6** originally read "conduction reuses P15's k, not a private correlation", and its test
asserted equality against `discretization::diffusion`. That assertion is now false by design, so the
test was re-aimed at the property that actually matters and that the failure exposed:

```text
NEW ASSERTION   a UNIFORM temperature must conduct EXACTLY zero through every face
                -- the exact-prescribed-flux property, pinned as behaviour
```

The criterion text is unchanged. The test now measures something stronger and more relevant than it
did before, which is the opposite of weakening it.

## 7. Carried forward as a recorded finding, not a fix

```text
FINDING   discretization::diffusion and thermal::assembleThermalDiffusionContribution disagree on
          prescribed-flux boundaries. The generic operator returns a quadratic-reconstruction
          estimate; the thermal one assembles the prescribed flux exactly. For a zero-gradient
          condition on a curved profile the difference is O(1), measured here as 5.64 W on a 7x5
          mesh -- not a subtlety.
NOT FIXED Reconciling them is not P17.5's scope: changing the generic operator would move results
          across the whole solver, and changing the thermal one would move the qualified P15
          baseline. Recorded in summary.md as follow-on work with this measurement attached.
```
