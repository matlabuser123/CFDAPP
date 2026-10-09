# P17 — verification gate definitions

Recorded at P17.0, 2026-09-26. **These are definitions, not results.** No gate below
has been executed. Each will be frozen, dry-run against the unchanged baseline and
proved non-vacuous under `CLAUDE.md` §5 before the item it governs is acted on.

## Rule on tolerances, applied throughout

A threshold appears here **only** where it can be derived now, from the dimensional,
round-off or discretization behaviour of the quantity it bounds, or where it is
inherited verbatim from an already-validated frozen gate. Everywhere else this file
says:

```text
TOLERANCE REQUIRES JUSTIFICATION
```

and names exactly what the derivation must come from. That is deliberate. Several P17
tolerances depend on a mesh family and a scheme order that **do not exist yet**, and
inventing a number now would be fitting a threshold to an unwritten implementation.

A criterion the baseline passes **vacuously** is not a criterion. Each gate therefore
carries its own non-vacuity requirement.

---

## G1 — baseline preservation

```text
purpose      P17 must not move P13-P16. This is the gate every other P17 item runs
             against, and the sharpest instrument the project owns.
required     the frozen P13 execution policy, verbatim:
             OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200   (Debug+GUI: 14400)
             exclusions: the same 5 patterns / 13 tests, not widened by one entry
             trees: build/qual-final, build/qual-cpuonly, build/qual-debug
measured     (a) P13-001 SIMPLE fingerprint, 10 cases / 130 lines
             (b) preservation suite counts, with the P17.2 regexes VERBATIM:
                   P14_transport   Transport|LegacyMatrix|ScalarTransport        68
                   P15_thermal     EnergyEquation|Enthalpy|ThermalPropert|...   161
                   P16_turbulence  Turbulence|EffectiveViscosity|WallFunction|
                                   WallTreatment|KEpsilon|KOmega                315
             (c) P17_CODE_FINGERPRINT before and after
reference    the P17.0 baseline: results/p17/baseline/
acceptance   (a) BITWISE IDENTICAL -- zero bits, zero lines changed. Not "close".
             (b) exact counts as above, 0 failures
             (c) unchanged across any run that does not intentionally change code
evidence     results/p17/<item>/logs/*_preservation.log + the fingerprint diff file,
             which must be EMPTY, and be preserved even when empty
failure      STOP. A moved SIMPLE fingerprint is a regression, never a tolerance
             question. Preserve the diff and report before any further P17 work.
non-vacuity  the fingerprint instrument must be shown able to FAIL: P13 proved this by
             planting a defect. Any P17 item that claims G1 must show the instrument
             detecting a deliberate perturbation, not merely reporting "identical".
note         Report BOTH fingerprints. SOURCE_FINGERPRINT_V2 includes documentation
             (ROADMAP.md, CLAUDE.md, README.md, docs/**) and will move when docs
             change -- it did exactly that during the P17.0 freeze. Claims of "no code
             changed" are made against P17_CODE_FINGERPRINT. The two hash DIFFERENT
             FILE SETS and must never be compared numerically with each other.
```

## G2 — thermodynamic closure

```text
purpose      the state relations must be mutually consistent, and must stay so once
             energy is added
required     unit tests on EquationOfState / IdealGasEOS / ThermodynamicProperties,
             plus whatever P17.5 adds for e(T) and T(e)
measured     p = rho R T            both round trips
             cp - cv = R
             gamma = cp / cv
             a = sqrt(gamma R T)
             e = h - R T            once e(T) exists
             T(e(T)) = T            round trip, once T(e) exists
reference    exact algebra, computed independently of CFDApp
acceptance   p = rho R T                 relative error <= 1e-12       DERIVED
             round trips (p,T) and (rho,T) <= 1e-12                    DERIVED
             cp - cv = R, gamma = cp/cv   EXACT -- holds BY CONSTRUCTION, since
                                          ThermodynamicProperties derives cv and never
                                          stores an independent value
             a = sqrt(gamma R T)          existing 7 tests pass unchanged
             T(e(T)) = T                  <= the inversion's own DOCUMENTED tolerance
                                          in kelvin, asserted against that published
                                          bound and never against a number the test
                                          discovered -- the convention P15's
                                          SensibleEnthalpy already established
derivation   1e-12: p/(R T) then rho R T is two roundings of a double, so exact
  of 1e-12   bitwise recovery is not guaranteed for arbitrary values. 1e-12 is ~4500x
             the double epsilon and four orders TIGHTER than the 1e-9 the existing
             EosConsistencyErrorIsAtFloatingPointTolerance low-Mach gate uses. Carried
             over from P17.2, where it was derived before any result was seen and
             proved load-bearing by control N4 (a 1e-6 perturbation of R was caught).
evidence     results/p17/<item>/logs/*_closure.log
failure      STOP. A broken state relation invalidates every downstream energy and
             flux result.
non-vacuity  at minimum P17.2's four controls, extended to e/T: drop a term, return a
             constant, remove the validation, and perturb a constant by ~1e-6 to prove
             the BOUND detects rather than the plumbing
new clause   e(T) is monotone only if cv = cp - R > 0. The property model currently
REQUIRED     guarantees only cp > 0. This admissibility condition must be CHECKED, not
             assumed -- it is what makes the T(e) bisection well-posed at all.
```

## G3 — variable-density conservation

```text
purpose      mass must be conserved discretely with a spatially varying density, and
             the constant-density limit must be preserved exactly
required     the existing compressible suite, plus a closed-domain case and a
             through-flow case
measured     global mass imbalance |sum over boundary faces of mDot|
             per-cell continuity imbalance, RMS and max
             constant-density reduction
reference    zero, to round-off, scaled by the problem's own flux magnitude
acceptance   global imbalance <= a COMPUTED bound of the form
               C * rho_avg * U_mean * A_ref * eps_scale
             i.e. the dimensional floor, scaled with the flux magnitude -- never an
             absolute constant. The existing GlobalMassImbalanceIsSmall already uses a
             computed bound rather than a literal, and that pattern is inherited.
             constant-density reduction: BITWISE identical to the incompressible path
             where the incompressible path is defined.       DERIVED, exact
             C: TOLERANCE REQUIRES JUSTIFICATION -- must be derived from the accumulated
             round-off of the face-flux summation over the actual cell count, not fitted
evidence     results/p17/<item>/logs/*_mass.log + per-cell imbalance data
failure      STOP. A mass-conservation failure makes every later energy and shock
             result meaningless.
non-vacuity  a dyadic control that must give EXACTLY 0 (exactly representable
             geometry and density), plus a corrupted operator -- a sign flip on one
             face flux, or a x2 coefficient -- that MUST break the bound
```

## G4 — total-energy conservation

```text
purpose      the new physics of P17. The conserved quantity must actually be conserved.
required     a closed adiabatic domain (no flux, no work, no source: total energy must
             be constant), and a through-flow case with known inflow/outflow enthalpy
measured     E_total = sum over cells of rho e_t V
             its drift over time / over outer iterations
             the discrete balance residual per cell
             the inflow-minus-outflow total-enthalpy budget
reference    closed adiabatic domain: E_total is EXACTLY constant analytically.
             through flow: the boundary total-enthalpy budget, computed independently.
acceptance   closed adiabatic: relative drift of E_total
               TOLERANCE REQUIRES JUSTIFICATION -- must be derived from the
               accumulated round-off of the per-cell summation over the actual cell
               count and the number of steps, i.e. an O(sqrt(N) * n_steps * eps)
               argument, and must SCALE with both. It must not be a fixed literal, and
               it must not be chosen after seeing the drift.
             through flow: budget closure
               TOLERANCE REQUIRES JUSTIFICATION -- derive from the boundary quadrature
               error of the actual discretization, not from the observed residual.
             temperature recovery: T(e) round trip within the inversion's DOCUMENTED
             tolerance.                                        DERIVED (inherited)
evidence     results/p17/<item>/logs/*_energy.log + the E_total time series as data
failure      STOP. Do not proceed to P17.7.
non-vacuity  REQUIRED, and this is the gate most at risk of passing vacuously:
             * the closed adiabatic case must be shown to DETECT a deliberately
               unbalanced pressure-work term -- otherwise "E_total constant" is
               satisfied by an energy equation that transports nothing;
             * a run with the energy equation DISABLED must fail the gate. If it
               passes, the gate is measuring the initial condition.
             * the dual-cp hazard (implementation_gaps.md B3) must be closed first:
               with two disagreeing cp values the budget can close on the wrong
               thermodynamics and look correct.
```

## G5 — low-Mach limiting behaviour

```text
purpose      the compressible path must reduce to the incompressible one as Ma -> 0,
             and P17 must not regress what already passes
required     tests/integration/compressible/test_low_mach_regression.cpp, unchanged
             tests/integration/mms/test_mms_compressible.cpp, unchanged
             plus a NEW energy criterion once P17.5 exists
measured     machMax, deltaRhoOverRho, maxEosError, maxRelativeMassFluxDifference,
             globalMassImbalance, MMS observed order, and (new) energy conservation
reference    the existing frozen thresholds, and the incompressible SIMPLE solution
acceptance   machMax < 0.1 AND > 1.25 * inletMach          INHERITED VERBATIM
             deltaRhoOverRho < 1e-2                        INHERITED VERBATIM
             maxEosError < 1e-9                            INHERITED VERBATIM
             maxRelativeMassFluxDifference < 0.15          INHERITED VERBATIM
             globalMassImbalance <= its computed bound      INHERITED VERBATIM
             MMS order 1.0 in band [0.55, 1.3], L2, u and v INHERITED VERBATIM
             all of the above across the nx family          INHERITED VERBATIM
             NEW energy criterion: TOLERANCE REQUIRES JUSTIFICATION -- derive from
             G4's own floor, applied at low Mach where the kinetic contribution to
             e_t is O(Ma^2) and therefore near the round-off floor. That smallness is
             a TRAP: at Ma = 1e-3 the kinetic term is ~1e-6 of the internal energy, so
             a criterion on e_t can be satisfied while the kinetic part is entirely
             wrong. The criterion must bound the KINETIC CONTRIBUTION SEPARATELY,
             relative to its own magnitude.
evidence     results/p17/<item>/logs/*_lowmach.log
failure      STOP. Any of the six inherited thresholds moving is a regression.
non-vacuity  already partly built in: the "> 1.25 * inletMach" clause stops the first
             criterion passing on a flow that never accelerates. The new energy
             criterion needs its own control -- see the trap above.
FORBIDDEN    none of the six inherited thresholds may be weakened, rescaled or
             reinterpreted for any P17 reason.
```

## G6 — Riemann / HLLC flux correctness

```text
purpose      the flux must be right before any shock result built on it means anything
required     unit tests on the flux in isolation, before it is wired to a solver
measured     (a) CONSISTENCY: for identical left and right states, HLLC must return
                 the exact physical flux F(Q)
             (b) SUPERSONIC LIMITS: S_L >= 0 must return exactly F_L; S_R <= 0 must
                 return exactly F_R
             (c) CONSERVATION: F(Q_L,Q_R) = -F(Q_R,Q_L) under reversal
             (d) ROTATIONAL / DIRECTIONAL consistency: rotating the state and the face
                 normal together must rotate the flux identically
             (e) POSITIVITY: rho and p remain strictly positive for admissible input
             (f) an exact Riemann problem, against an INDEPENDENT exact solver
reference    (a)-(e) exact algebra. (f) an exact Riemann solution computed OUTSIDE
             CFDApp -- exact-rational or arbitrary-precision Python with no CFDApp
             code, as CLAUDE.md section 6 requires. Adopting CFDApp output as the
             reference is forbidden.
acceptance   (a) EXACT for a uniform state, or round-off with a stated bound
             (b) EXACTLY F_L / F_R -- a selection test, so equality is exact  DERIVED
             (c) EXACT antisymmetry to round-off                             DERIVED
             (d) a rotation by a DYADIC angle (0, 90, 180, 270 degrees) must give
                 EXACTLY the rotated flux -- bit-for-bit, since those rotations are
                 exactly representable                                      DERIVED
             (e) strictly positive -- a boolean, no tolerance                DERIVED
             (f) TOLERANCE REQUIRES JUSTIFICATION -- derive from the wave-speed
                 estimator's own truncation behaviour, not from the observed error
evidence     results/p17/<item>/logs/*_hllc.log + the independent reference script
             preserved under results/p17/<item>/tools/
failure      STOP. Do not wire the flux to a solver.
non-vacuity  (b) and (d) are the non-vacuity backbone: a flux that ignored the wave
             speeds entirely would still pass (a). Additionally: a deliberately
             dropped contact-wave term must break (f) while still passing (a), which
             is exactly what distinguishes HLLC from HLL.
prerequisite NONE of this exists today -- 0 hits for HLLC/HLL/Rusanov/conserved-state.
             The 14 "Roe" and 107 "Euler" hits are false positives (see
             implementation_gaps.md Q15, Q16) and must not be mistaken for a
             foundation.
```

## G7 — shock validation

```text
purpose      quantitative evidence that shocks are captured correctly. Visual
             similarity is explicitly not sufficient.
required     Sod shock tube:  left rho=1.0, p=1.0, u=0 ; right rho=0.125, p=0.1, u=0
             contact discontinuity; supersonic advection; normal shock;
             compression / expansion wave
measured     shock POSITION at a fixed time
             density ratio, pressure ratio, velocity across each wave
             recovery of all THREE structures: rarefaction, contact, shock
             global conservation of mass, momentum and total energy
reference    the exact Riemann solution, computed independently of CFDApp (the same
             reference G6(f) requires). Normal shock: the Rankine-Hugoniot relations,
             evaluated independently.
acceptance   ALL TOLERANCES REQUIRE JUSTIFICATION. Each must be derived from the
             first-order scheme's own expected smearing width on the actual mesh --
             i.e. proportional to the cell size and the number of cells the wave
             crosses -- and must SCALE with mesh refinement. A fixed literal would
             either pass a wrong answer on a coarse mesh or fail a right one on a fine
             mesh. Specifically:
               shock position   scale with dx; a position error below one cell width
                                is not measurable and must not be claimed
               jump ratios      derive from the smearing width, and evaluate AWAY from
                                the smeared region, at a stated distance
               conservation     inherits G3's and G4's floors, not a new number
             The three structures must be identified by MEASUREMENT (locating each
             wave and comparing its speed and jump to the exact solution), never by
             eye and never from a plot.
evidence     results/p17/<item>/logs/*_shock.log, the profile data, the independent
             reference script, and a refinement study showing the errors decreasing at
             the expected rate
failure      STOP and preserve. A shock result that does not converge toward the exact
             solution under refinement is a defect, not a tolerance question.
non-vacuity  a first-order upwind scheme WITHOUT the Riemann flux must FAIL this gate.
             If it passes, the gate is not measuring shock capturing.
             A second, discriminating case is required beyond Sod: Sod alone can be
             passed by an over-diffusive scheme that smears everything to roughly the
             right place.
constraint   CompressibleSIMPLE is 2D-only (requireTwoDimensional). Sod and a normal
             shock are 1D and can run on a 2D mesh, so this does not block -- but
             every claim must say 2D.
```

## G8 — CPU/GPU numerical equivalence

```text
purpose      if a GPU compressible path is ever claimed, prove it matches CPU
required     equivalent numerical SOLVES, not kernel microbenchmarks
measured     density, momentum, pressure, temperature, total energy, face flux,
             residual histories, conservation, iteration/time-step control
reference    the CPU path. CPU numerics remain the reference (CLAUDE.md section 6).
acceptance   TOLERANCE REQUIRES JUSTIFICATION -- derive from the actual reduction-order
             difference between the CPU and GPU implementations. Where the operation
             order is identical, BITWISE is the correct requirement and anything looser
             is a concession that must be argued for, not assumed.
evidence     would live under results/p17/<item>/gpu_equivalence/
failure      no GPU performance claim without equivalent numerical solves.
STATUS       **VACUOUS TODAY.** There is no GPU compressible implementation: exactly
             one GPU-layer file mentions "compressible" and only to say it ports the
             INCOMPRESSIBLE assembly. This gate therefore cannot be dry-run, and per
             CLAUDE.md section 5.2 a criterion the baseline cannot exercise is an
             INSUFFICIENT dry-run.
             Recorded as DEFERRED, not as PASS. "Nothing to compare, therefore
             equivalent" is exactly the vacuous pass this project forbids.
             GPU compressible work is outside the authorized P17 scope.
```

## G9 — deterministic repeatability

```text
purpose      every P17 result must be reproducible bit for bit
required     repeated runs of the same configuration in the same build
measured     all solution fields, all residual histories, all derived scalars,
             iteration counts, and the reported status
reference    the first run
acceptance   BITWISE IDENTICAL -- EXPECT_EQ, not EXPECT_NEAR. Exact, no tolerance.
             This follows the existing RepeatedRunIsDeterministic precedent, which
             already asserts EXPECT_EQ on density and the derived scalars.
evidence     results/p17/<item>/logs/*_determinism.log with both runs recorded
failure      STOP. Non-determinism invalidates every other gate, because no other
             measurement can then be reproduced.
non-vacuity  the check must be shown able to fail -- e.g. against a deliberately
             order-dependent reduction. A determinism test that has never seen a
             nondeterministic input is unproven.
scope note   P17 must not REDUCE determinism where it is currently guaranteed. Any new
             path that cannot be deterministic must say so explicitly rather than
             quietly widening the tolerance.
```

## G10 — full regression

```text
purpose      the final qualification gate. P17 is not complete until this passes.
required     all three qualified configurations, frozen policy verbatim:
               Release+CUDA  build/qual-final     --timeout 7200
               Debug+GUI     build/qual-debug     --timeout 14400 (AMENDMENT_G10)
               CPU-only      build/qual-cpuonly   --timeout 7200
             plus sanitizers at CI settings and clang-format-18 clean
measured     exact counts per configuration, reported SEPARATELY. Never an aggregate.
             The P13-001 SIMPLE fingerprint. P14/P15/P16 preservation. The full
             compressible canonical suite. The shock suite. Determinism. Sanitizer
             coverage. Both fingerprints.
reference    the P17.0 baseline in results/p17/baseline/, plus each item's own gate
acceptance   0 failures in every configuration, except failures the gate NAMES in
             advance. Exact counts. SIMPLE fingerprint bitwise identical. Every
             per-item gate G1-G9 passed and evidenced.
             Test binaries must be demonstrably rebuilt against the exact source:
             CLAUDE.md section 8's authoritative sequence, with a FAIL-CLOSED staleness
             check. The harness must derive its binary set from ctest's own registry,
             not from find(1) -- on this /mnt/c drvfs mount every file reports as
             executable, and two P17.0 harness attempts failed closed on exactly that.
             See results/p17/baseline/logs/PRESERVED_invalid_*/.
evidence     results/p17/final_qualification/ with per-configuration logs, the
             fingerprint records, and the known-limitations document
failure      P17 is BLOCKED, not complete. Preserve every failing run.
non-vacuity  a stale-binary run must be refused, not reported. A harness that rebuilds
             only MISSING binaries is invalid (CLAUDE.md section 8).
```

---

## Gate-to-item map

```text
item    gates that must pass
------------------------------------------------------------
P17.1   G1  G3          G9
P17.2   G1  G2          G9         (CLOSED -- see results/p17/compressible/eos/)
P17.3   G1  G3          G9
P17.4   G1  G3          G9         + the dilatational-stress addition
P17.5   G1  G2  G4      G9
P17.6   G1  G3  G4  G5  G9
P17.7   G1  G2  G6      G9
P17.8   G1  G3  G4  G6  G9
P17.9   G1  G3  G4  G6  G7  G9
P17.10  G1  G8          G9         DEFERRED -- G8 is vacuous today
P17.11  G1 .. G10, all
```

## Standing prohibitions

```text
no threshold is weakened to obtain a PASS
no exclusion is added or widened
no test is disabled, skipped or special-cased; no iteration limit is inflated
no failed run is deleted -- it is marked INVALID AS AUTHORITATIVE in place
CFDApp output is never adopted as a reference value
a criterion the baseline passes vacuously is not a criterion
every gate is frozen and dry-run BEFORE the item it governs is implemented
stop at the first genuine failure; classifying a failure does not waive it
```
