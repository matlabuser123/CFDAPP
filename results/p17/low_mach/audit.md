# P17.6 — audit of existing low-Mach validation

Measured on the unchanged tree, `HEAD 3bd32f918656`. Everything below is from `logs/00_audit.log`
(`tools/audit.sh`, read-only on production source).

---

## 1. THE CENTRAL FINDING: the existing low-Mach test runs no compressible solve

`tests/integration/compressible/test_low_mach_regression.cpp`, 338 lines, 7 TESTs:

```text
MachNumberStaysWellBelowPointOne
DensityVariationIsSmallRelativeToReferenceDensity
EosConsistencyErrorIsAtFloatingPointTolerance
CompressibleMassFluxApproachesTheIncompressibleLimit
GlobalMassImbalanceIsSmall
GridRefinementKeepsAllMetricsSmall
RepeatedRunIsDeterministic
```

```text
CompressibleSIMPLE mentions in that file : 0
incompressible SIMPLE solve calls        : 1
```

What it actually does, from its own lines:

```text
161   const VectorField& velocity = flow.velocity;          <- the INCOMPRESSIBLE solution
177   density[i] = thermo.density(pressureAbsolute[i], kTemperature);
193   machMax = std::max(machMax, machNumber(speed, soundSpeed));
```

So the density, the Mach number and the "compressible" mass flux are all evaluated **on the
incompressible solution**. There is no compressible solve anywhere in the file.

```text
CONSEQUENCE, and it decides this whole phase:
  "the compressible solution approaches the incompressible one" is TRUE BY CONSTRUCTION here --
  they are the SAME velocity field. The test cannot answer P17.6's question because nothing in it
  ever solves the compressible system.

  This is precisely the degenerate case section 20 of the authorization forbids: "A compressible
  case silently falling back to incompressible SIMPLE is not valid low-Mach evidence." The
  existing test is not a fallback by accident -- it never had a compressible path to fall back
  from -- but the effect on the evidence is identical.
```

**What the existing test IS good for, and why it is kept unchanged.** It is a sound *post-hoc
thermodynamic consistency* check: given an incompressible solution, is the EOS-derived density
self-consistent, is the implied Mach number small, is the implied density variation small, does the
implied compressible flux stay close to the incompressible one, and does the global mass imbalance
stay inside its derived bound. Those are real properties and G5 requires this file **unchanged**, so
it is preserved verbatim and re-run, not replaced. It is simply not evidence of solver convergence to
the incompressible limit.

## 2. Where the compressible solver IS genuinely driven

```text
tests/solver/compressible_simple/test_compressible_simple.cpp    9 solve() calls
tests/integration/mms/test_mms_compressible.cpp                  drives it via MMSCases
tests/integration/case/test_compressible_coupled_production_case.cpp  via ProjectRunner
```

So `CompressibleSIMPLE` works and is exercised — including a determinism test that already asserts
bitwise-identical velocity, pressure and iteration counts across repeated solves. That harness is the
pattern P17.6 reuses; nothing new has to be invented to drive the solver.

```text
CompressibleSIMPLEResult carries everything this phase needs:
  velocity, pressure (gauge), density, massFlux
  status, iterations, pressureCorrectionPasses
  finalUResidual, finalVResidual, finalPressureResidual, finalContinuityResidual
  globalMassImbalance
  uResidualHistory, vResidualHistory, ...
```

## 3. The frozen G5 criterion — thresholds are INHERITED VERBATIM

```text
required     tests/integration/compressible/test_low_mach_regression.cpp, unchanged
             tests/integration/mms/test_mms_compressible.cpp, unchanged
             plus a NEW energy criterion once P17.5 exists
acceptance   machMax < 0.1 AND > 1.25 * inletMach          INHERITED VERBATIM
             deltaRhoOverRho < 1e-2                        INHERITED VERBATIM
             maxEosError < 1e-9                            INHERITED VERBATIM
             maxRelativeMassFluxDifference < 0.15          INHERITED VERBATIM
             globalMassImbalance <= its computed bound      INHERITED VERBATIM
             MMS order 1.0 in band [0.55, 1.3], L2, u and v INHERITED VERBATIM
             all of the above across the nx family          INHERITED VERBATIM
             NEW energy criterion: TOLERANCE REQUIRES JUSTIFICATION
```

Note what the frozen family is: **`nx` — a GRID refinement family (16, 32, 64), not a Mach family.**
The existing test varies the mesh at one flow speed. P17.6's own requirement is the opposite axis:
*"Do not merely demonstrate one low-Mach case. Demonstrate the trend."* That trend does not exist
anywhere in the repository today.

## 4. The P17.5 scope question, settled — no energy wiring is needed

P17.5's summary flagged this as the one thing to decide at P17.6's freeze: does P17.6 require the
energy equation wired into `CompressibleSIMPLE`'s outer loop? If so, that is the implicit-coupling
work P17.5 left to P17.8, and the scopes overlap.

```text
CompressibleSIMPLE.hpp:144   "never updated by this solver itself (no energy-equation"
TotalEnergy.hpp              ScalarField members: 0   -- a STATELESS converter
```

```text
DECISION: NO. Section 12 asks to verify that the total-energy formulation "remains physically
sensible in the low-Mach limit", that kinetic energy is small beside internal energy, and that
temperature recovery from rhoE/rho - 0.5|U|^2 stays stable. Every one of those is answerable by
EVALUATING P17.5's stateless converter on the CONVERGED compressible fields. No solver loop is
touched, so the P17.8 overlap does not materialise and P17.6 stays inside its authorized scope.
```

This is recorded as a decision with its reason, because the alternative reading — wiring energy into
the loop — would have silently pulled P17.8 work into this phase.

## 5. Requirement classification

```text
requirement                                    status              basis
-----------------------------------------------------------------------------------------------
Preserve existing low-Mach cases               ALREADY SATISFIED   7 TESTs, run unchanged
Compressible -> incompressible as Ma -> 0      MISSING             no compressible solve exists in
                                                                   any low-Mach test; the claim is
                                                                   true by construction today
  compare velocity                             MISSING
  compare pressure                             MISSING
  compare temperature                          MISSING             isothermal by construction; the
                                                                   EXPECTED behaviour has to be
                                                                   stated rather than measured
  compare density                              NEEDS EXTENSION     Delta rho/rho is measured, but on
                                                                   the INCOMPRESSIBLE solution
  compare mass flux                            NEEDS EXTENSION     measured as compressible-flux-on-
                                                                   incompressible-velocity vs
                                                                   incompressible flux, i.e. a
                                                                   density-only difference
  compare residual history                     MISSING
Verify conservation                            NEEDS EXTENSION     mass imbalance exists; momentum
                                                                   (P17.4) and energy (P17.5)
                                                                   diagnostics are not applied here
Verify deterministic repeatability             NEEDS EXTENSION     exists for the post-hoc metrics;
                                                                   not for a compressible solve
Ensure P17 does not regress SIMPLE             ALREADY SATISFIED   fingerprint + Poiseuille (5) +
                                                                   cavity (5) + the full regression
-----------------------------------------------------------------------------------------------
MISSING 5   NEEDS EXTENSION 3   ALREADY SATISFIED 2
```

## 6. The incompressible reference, and the settings to inherit

The existing low-Mach test's flow settings are themselves inherited, with the comment *"Same settings
tests/integration/poiseuille/test_poiseuille_validation.cpp already diagnosed and locked"*:

```text
maxIterations 3000   velocityRelaxation 0.7   pressureRelaxation 0.3
velocityTolerance 2e-5   pressureTolerance 5e-4   continuityTolerance 1e-6
momentumSolver 500 / 1e-10 / 1e-8      pressureSolver 2000 / 1e-8 / 1e-6
```

P17.6 reuses these verbatim for the incompressible reference, so the reference is the already
diagnosed one rather than a new configuration chosen by this phase.

```text
SIMPLE reference cases that must stay preserved
  tests/integration/poiseuille/test_poiseuille_validation.cpp   5 TESTs
  tests/integration/cavity/test_cavity_ghia.cpp                 5 TESTs
  the P13-001 SIMPLE fingerprint, results/p13/restart/tools/solve_fingerprint.cpp
```

## 7. Baselines P17.6 must preserve

```text
Release+CUDA 2642 registered / 2580 executed    (the P17.5 close)
CPU-only     2440 registered / 2380 executed
compressible 210 · P14 68 · P15 161 · P16 315
P13-001 SIMPLE fingerprint: BITWISE IDENTICAL, empty diff
```

## 8. What P17.6 therefore has to build

```text
a genuine Mach FAMILY: run CompressibleSIMPLE at several decreasing inlet Mach numbers and, for
each, run the matched incompressible SIMPLE reference on the same mesh, viscosity and boundary
conditions -- then compare velocity, pressure (gauge-referenced), density, mass flux and residual
behaviour, and show the differences TRENDING to zero.

plus, on the converged compressible fields:
  mass conservation      from CompressibleSIMPLEResult::globalMassImbalance
  momentum conservation  the P17.4 diagnostic
  energy conservation    the P17.5 balance -- this is G5's "NEW energy criterion"
  the P17.5 state scales internal vs kinetic energy, and temperature-recovery stability
  determinism            bitwise, on a real compressible solve
  an ANTI-FALSE-SUCCESS check that the compressible path is genuinely active (section 20)
```

The last line matters most given §1: the first thing P17.6's own harness has to prove is that it is
not repeating the mistake it just found.
