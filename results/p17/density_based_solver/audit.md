# P17.8 — audit of existing solver architecture

`logs/00_audit.log`, run at HEAD `3bd32f918656f471e72b5f6ac7cb6c2cbf75a274` before any code was
written. Searched roots: `include src cuda tests apps`.

---

## 1. The term sweep

```text
term                     files  lines   what it actually is
---------------------------------------------------------------------------------------------------
DensityBased                 0      0   absent
density_based                0      0   absent -- no case-format key either
EulerSolver                  0      0   absent
CompressibleSolver           0      0   absent
explicit update              0      0   absent
conserved update             0      0   absent
finite-volume residual       0      0   absent
local time stepping          0      0   absent
localTimeStep                0      0   absent
global time stepping         0      0   absent
Runge-Kutta / RungeKutta     0      0   absent -- no multi-stage integrator anywhere
shock solver                 0      0   absent
deltaTime                    0      0   absent

CFL / cfl                   31    273   REAL AND SUBSTANTIAL -- see section 2
pseudoTime                  12     53   a NAME COLLISION -- see section 3
residualNorm                 6     48   two unrelated things -- see section 4
HLLC                        11    300   P17.7's, as expected
timeStep                    50    174   mostly SIMPLE/PISO iteration naming, not a dt owner
restart / checkpoint         --     --   a FIXED incompressible payload -- see section 5
```

## 2. `cfd::solver::calculateCFL` exists, and is not what a density-based solver needs

This is the one finding that would have caused real duplication if the audit had been skipped.
`include/cfd/solver/CFL.hpp` is a complete, tested, documented CFL facility:

```text
  Co_P = (dt / (2 V_P)) * sum_over_P's_faces( |massFlux_f| / rho )
```

Read in full, it differs from P17.8's need in **four** ways, each stated in its own header:

```text
1  DIAGNOSTIC ONLY.  "this does not modify dt or suggest a value, it only reports what the Courant
   number *is* for a given (mesh, flux, dt). No adaptive time stepping here or implied by this API."
2  ADVECTIVE ONLY.  It is built from the volumetric flux |massFlux|/rho. There is no acoustic term,
   so it cannot bound an explicit compressible step, where the limiting speed is |u.n| + a.
3  CONSTANT DENSITY.  Its signature takes a single scalar `density`, not a field.
4  THE WRONG DIRECTION.  It maps dt -> Co. A density-based solver needs Co -> dt.
```

```text
SO: NEEDS EXTENSION, not ALREADY SATISFIED and not MISSING. P17.8 adds a compressible
dt-from-CFL function and ties the two together with a VERIFIABLE consistency relation rather than
leaving two unrelated conventions in the tree: in the limit a -> 0 with uniform density, feeding
P17.8's dt back through calculateCFL must return the requested CFL. That is gate criterion K5.
The existing function is NOT modified -- it is a P2 facility with its own tests.
```

## 3. `pseudoTime` is a name collision, not reusable infrastructure

All 53 hits are `CompressibleSIMPLE`'s `pseudoTimeStep`:

```text
include/cfd/compressible/CompressibleSIMPLE.hpp:38      Real pseudoTimeStep{1.0};
include/cfd/compressible/CompressibleRelaxedMomentum.hpp  "a numerical stabilization parameter,
                                                           not a physical time step"
include/cfd/compressible/CompressiblePressureCorrection.hpp  V_P/pseudoTimeStep * dRho/dp
```

That is a **relaxation coefficient inside a pressure-correction equation**, not a time integrator. It
never advances a conserved state and has no dt semantics. P17.8's local pseudo-time stepping is a
different concept that happens to share the word, and treating the hit count as existing
infrastructure would have been the mistake.

```text
(It is also the parameter recorded since P17.3 as having no case-format key -- P17.8 does not change
that and does not use it.)
```

## 4. `residualNorm` is two unrelated things, neither of them a field residual

```text
include/cfd/solver/SolverRobustness.hpp:43   struct ResidualNormalizationSettings  -- the LINEAR
                                             solver's normalisation policy (BiCGSTAB/GMRES stopping)
src/algebra/BiCGSTAB.cpp:247                 the Krylov residual of a linear system
```

Neither is a conserved-equation residual norm. P17.8's `R_rho / R_rhoU / R_rhoE` norms are new. The
*convention* to follow, though, is the repository's existing one for aggregated per-cell statistics
(volume-weighted, as in `ManufacturedFields::l2CellError` and `CFLResult::meanCFL`), and that is what
`architecture.md` §10 adopts.

## 5. Restart carries a fixed incompressible payload

```text
include/cfd/io/{RestartWriter,RestartReader,CheckpointWriter,CheckpointReader}.hpp
RestartWriter.hpp:19   "...and fields (pressure/velocity_x/velocity_y/mass_flux), each field value
                        written with the..."
```

No `rho`, no `rhoU`, no `rhoE`. Extending the format touches every existing restart test, which §44
protects. So **density-based restart is UNSUPPORTED with an explicit refusal** — permitted by §29,
which requires only that the solver refuse rather than silently produce wrong state.

## 6. `solver::TransientSolver` — the right shape, the wrong state type

```text
include/cfd/solver/TransientSolver.hpp
  struct TransientState { VectorField velocity; ScalarField pressure; };     <-- INCOMPRESSIBLE
  class TransientStepSolver { virtual TransientStepResult solveTimeStep(prev, dt) const = 0; };
  class TransientSolver { TransientResult solve(TransientState, TimeController) const; };
  + a pre-step CFL guard (cflFailAbove), history records, onStepRejected(), and
    "the last *accepted* state -- for any non-Completed status this is the state the failing step was
     computed *from*, never that step's own (discarded) output"
```

Its conventions are exactly right and P17.8 mirrors them. Its **state type cannot hold
`[rho, rhoU, rhoE]`**, and it is a P2 class used by PISO, so widening it would change a shared type
for every existing caller. P17.8 therefore owns its own loop and does not modify it —
`architecture.md` §6 records that as forced duplication rather than a fresh design.

## 7. Dispatch and the case format

```text
src/app/ProjectRunner.cpp:756   if (setup.compressible.has_value()) { ... incompressible warm start
                                 ... if (coupled) -> CompressibleSIMPLE }
CompressibleSetup { enabled, referencePressure, temperature, thermalCoupled, coupled }
existing keys: compressible.enabled, compressible.coupled, compressible.thermal_coupled,
               compressible.gas, compressible.reference*, compressible.specific*, compressible.temperature
NO compressible.method key exists.
```

P17.8 **adds no dispatch and no key**, so §28's hard requirement ("default configuration unchanged")
holds structurally — no line that selects a solver is edited. The consequence, stated plainly in
`architecture.md` §3, is that the solver is library-level and **not reachable from a case file**.

## 8. Mesh and field conventions P17.8 must follow

```text
Face::owner() -> Index          Face::neighbor() -> std::optional<Index>   (empty == boundary)
Face::areaVector() -> Vector2   Face::area() -> Real      Face::isBoundary() -> bool
Cell::volume() -> Real          Cell::faceIds() -> const std::vector<Index>&
unit normal = areaVector / area -- exactly the split P17.7's hllcFlux() interface requires

SIGN CONVENTION, from src/compressible/CompressibleContinuity.cpp and physics::evaluateContinuity:
    "+F_f for the owner, -F_f for the neighbour"
P17.8 matches it rather than inventing one; §6/§7 permit "the repository's equivalent convention".
```

## 9. What P17.7 hands over, ready to consume

```text
ConservedState, PrimitiveState, FluxVector          the types
makePrimitiveState(), makePrimitiveStateFromConserved()   the ONLY thermodynamic paths
toConserved()                                       primitive -> Q
eulerFlux(), hllcFlux(), hllcFluxChecked()          the flux, with validation
integrateOverArea()                                 per-unit-area flux -> face flux
reconstructFirstOrder(), reconstructFirstOrderBoundary()   the reconstruction seam
validateState(), validateUnitNormal(), describeValidity()  the input positivity checks
```

## 10. Requirement classification (§1)

```text
requirement                              classification    note
---------------------------------------------------------------------------------------------------
Define solver architecture               MISSING
Keep pressure-based SIMPLE intact        ALREADY SATISFIED  no dispatch change is made at all
Implement conserved-variable update      MISSING
Couple HLLC flux to FV residual          MISSING            HLLC itself is ALREADY SATISFIED (P17.7)
Add CFL-based time-step control          NEEDS EXTENSION    section 2
Add local / global time stepping         MISSING            pseudoTime is a collision, section 3
Add primitive <-> conserved conversion   ALREADY SATISFIED  P17.5, reused verbatim, not reimplemented
Add residual norms                       MISSING            section 4
Add convergence criteria                 MISSING
Add positivity protection                NEEDS EXTENSION    P17.7 covers input and star states; the
                                                            UPDATED cell state is what is missing
Add non-finite-state detection           NEEDS EXTENSION    P17.5/P17.7 validate states; the solver
                                                            needs per-cell, per-iteration reporting
Add restart compatibility where supported NOT SUPPORTABLE   section 5 -- explicit refusal instead
Preserve deterministic execution         MISSING            no control flow exists yet to be
                                                            deterministic about
---------------------------------------------------------------------------------------------------
ALREADY SATISFIED 3 · NEEDS EXTENSION 4 · MISSING 5 · NOT SUPPORTABLE 1
```

## 11. One probe of mine that misreported

```text
Section 1h asked for mesh accessors by name and printed 0 for several, because I grepped
include/cfd/mesh/ for 'owner(' and 'faceArea' -- the real accessors are Face::owner() (which matched)
and Face::areaVector() (which my 'faceArea' pattern did not). The real API was read from Face.hpp and
Cell.hpp directly, in section 8.
No consequence, but it is the same fail-open shape logged in P17.7's audit.md section 6 -- a grep for
the wrong identifier printing a zero that reads as absence. Recorded rather than fixed silently.
```
