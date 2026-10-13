# P17.8 — solver architecture and scope, decided BEFORE implementation

Covers §2, §3, §4, §8, §12, §13, §29, §32 and §39. Written before any source existed, and frozen with
the acceptance gate.

---

## 1. What the audit found — what to reuse, and what genuinely does not exist

```text
MISSING (0 hits)        DensityBased, EulerSolver, CompressibleSolver, explicit update,
                        conserved update, finite-volume residual, local/global time stepping,
                        Runge-Kutta, shock solver
NEEDS EXTENSION         CFL -- include/cfd/solver/CFL.hpp EXISTS (31 files, 273 lines mention it)
ALREADY SATISFIED       primitive<->conserved conversion (P17.5), EOS and sound speed (P17.2),
                        the physical Euler flux and HLLC (P17.7), the reconstruction interface
                        (P17.7), input-state positivity checks (P17.7)
NOT REUSABLE            solver::TransientSolver -- see section 6
```

### The existing CFL is not the one a density-based solver needs

`cfd::solver::calculateCFL` is real and must not be duplicated blindly. Read in full, it is:

```text
  Co_P = (dt / (2 V_P)) * sum_f |massFlux_f| / rho
  * DIAGNOSTIC ONLY -- its own header says "this does not modify dt or suggest a value"
  * ADVECTIVE ONLY  -- no acoustic term, so it cannot bound a compressible explicit step
  * CONSTANT DENSITY -- it takes a single scalar `density`
  * the WRONG DIRECTION -- it reports Co given dt; a density-based solver needs dt given Co
```

So P17.8 adds a compressible CFL that differs in exactly those four ways, and the two are tied
together by a **verifiable consistency relation** rather than left as unrelated code:

```text
in the limit a -> 0 with uniform density, the compressible dt fed back through calculateCFL must
return the requested CFL. That is criterion K5 and it is what makes the new function an EXTENSION of
the old convention rather than a second, silently different one.
```

## 2. The architecture

```text
                        PrimitiveState (P17.7)                    [rho, U, p, T, a, rhoE]
                                 |  makePrimitiveState()  -- P17.2 EOS + P17.5 energy
                                 v
  ConservedField  Q = [rho, rhoU, rhoE]  (one owner, section 4)
                                 |
                                 |  reconstructFirstOrder(owner, neighbour)        P17.7
                                 v
                        ReconstructedFaceState {left, right}
                                 |
                                 |  hllcFlux(L, R, unitNormal, gamma)              P17.7
                                 v
                        FluxVector, per unit area
                                 |  integrateOverArea(flux, face.area())           P17.7
                                 v
  residual   R_P = sum_f  (+F_f A_f for the owner, -F_f A_f for the neighbour)
                                 |
                                 |  dt from the compressible CFL (this phase)
                                 v
  update     Q_P^{n+1} = Q_P^n - (dt / V_P) R_P
                                 |
                                 |  makePrimitiveStateFromConserved()  -> P17.5 recover()
                                 v
  validate   rho > 0, p > 0, T in range, finite -- REFUSE, never clamp
                                 |
                                 v
                        next step / iteration
```

```text
THE SIGN CONVENTION IS THE REPOSITORY'S, not a new one. src/compressible/CompressibleContinuity.cpp
and physics::evaluateContinuity both use "+F_f for the owner, -F_f for the neighbour", so P17.8 uses
that and section 6/7's "or the repository's equivalent convention" is satisfied by matching rather
than by inventing.
```

### Mesh conventions used

```text
Face::owner()        Index                Face::neighbor()   std::optional<Index>  (empty = boundary)
Face::areaVector()   Vector2 (= Vector3)  Face::area()       Real
Cell::volume()       Real                 Cell::faceIds()    std::vector<Index>
unit normal = areaVector / area           -- exactly the split P17.7's interface requires
```

## 3. §3 — how the solver is selected, and why SIMPLE cannot be affected

```text
DensityBasedSolver is a SEPARATE CLASS with its own entry point. It is constructed directly with its
own settings object, exactly as SIMPLE and CompressibleSIMPLE are.
```

**There is no dispatch change at all.** `ProjectRunner`'s existing branch
(`if (setup.compressible.has_value())` → incompressible warm start → `if (coupled)` →
`CompressibleSIMPLE`) is **not touched**, so §28's hard requirement is satisfied *structurally*: the
pressure-based default cannot change because no line that selects it is edited.

```text
CONSEQUENCE, STATED PLAINLY AND NOT HIDDEN: P17.8's solver is LIBRARY-LEVEL. It is NOT reachable from
a case file. Section 39 permits this -- "If P17.8 deliberately remains library-level until later
qualification, state that clearly based on the current roadmap/gate definition" -- and the frozen
P17_GATES.md assigns P17.8 only G1, G3, G4, G6, G9, with no reachability criterion.
It joins pseudoTimeStep (P17.3) and includeDilatationalStress (P17.4) as verified capability with no
case-format key. That is recorded as the phase's principal limitation, and NO production capability
is claimed. Adding the key belongs with P17.11's qualification, when the solver has a validated
shock case (P17.9) worth pointing a case file at.
```

## 4. §4 — conserved-variable storage and ownership

```text
struct ConservedField {            // ONE owner of the conserved state
  ScalarField density;            // rho        [kg/m^3]
  VectorField momentumDensity;    // rho U      [kg/(m^2 s)]
  ScalarField totalEnergyDensity; // rhoE       [J/m^3]
};
```

```text
allocation        sized from mesh.numberOfCells() at construction of the initial field; the solver
                  never resizes
initialization    fromPrimitive(mesh, thermodynamics, density, velocity, temperature) -- every value
                  produced by P17.5's conservedTotalEnergy, so there is no second initialisation path
old / new state   solve() takes Q by VALUE and returns the final Q, mirroring SIMPLE::solve() and
                  TransientSolver::solve(). Within a step, exactly two live copies exist: the
                  accepted state and the candidate. A rejected candidate is discarded, never merged.
temporary stages  NONE -- the integrator is single-stage explicit Euler (section 9). No RK registers.
ownership         the solver owns its working copies; the caller's field is never mutated
host/device       HOST ONLY in P17.8 -- see section 8 below
restart           UNSUPPORTED, with an explicit refusal -- see section 7
output            the solver returns its state and a history; it writes no files and touches no
                  exporter
```

**There are no unsynchronised copies**, which is the property §4 actually cares about: primitives are
never stored alongside Q. They are recomputed from Q on demand by
`makePrimitiveStateFromConserved()`, so they cannot drift out of step with the conserved state.

## 5. §8 — EULER-ONLY, stated explicitly and not dressed up

```text
P17.8 IMPLEMENTS THE INVISCID EULER EQUATIONS ONLY.
The face flux is  F_f = HLLC(Q_L, Q_R, n_f)  and nothing else.
NO viscous stress. NO heat conduction. Compressible Navier-Stokes is NOT claimed anywhere.
```

§8 permits this — *"If P17.8 initially supports Euler-only density-based flow, document that
explicitly and do not falsely claim compressible Navier–Stokes support"* — and the reasons are
concrete rather than convenience:

```text
* P17.4's dilatational stress is default-OFF and has no case-format key; P17.5's conduction needs a
  temperature field the conserved state does not carry as an independent variable.
* Adding either means a second large verification surface (viscous flux consistency, a viscous CFL
  limit, a wall treatment) inside a phase already building the whole time-integration path.
* §8's own instruction is to keep them SEPARATE from HLLC. The interface below does that, so viscous
  terms can be ADDED without touching the residual's inviscid half.
```

```text
THE SEAM IS DEFINED NOW so that adding them later is additive:
    totalFaceFlux = inviscidFlux(HLLC)  +  viscousFlux(<not implemented in P17.8>)
assembleResidual() computes only the first term and the second is absent, not stubbed with zeros
pretending to be a physical contribution. A zero stub would be indistinguishable from a wired-up
viscous term that happens to vanish -- the capability-vs-reachability trap -- so there is no stub.
```

## 6. §9, §12, §13 — time integration, and why TransientSolver is not reused

```text
solver::TransientSolver EXISTS and its shape is right: an injected step solver, a history, a
pre-step CFL guard, rejection with onStepRejected(). But its state type is
    struct TransientState { VectorField velocity; ScalarField pressure; };
which CANNOT hold [rho, rhoU, rhoE]. It is a P2 class used by PISO, and widening it would change a
shared type for every existing caller.
DECISION: P17.8 owns its own loop and MIRRORS TransientSolver's conventions -- a status enum, a
per-step history record, "the final state is the last ACCEPTED state", and a CFL guard -- without
modifying it. The similarity is deliberate so the two read alike; the duplication is forced by the
state type, and it is recorded here rather than presented as a fresh design.
```

### The update

```text
Q_P^{n+1} = Q_P^n - (dt_P / V_P) * R_P          R_P = sum_f  +/- F_f A_f
single-stage explicit forward Euler. Section 9: "Do not introduce a complex high-order time
integrator unless necessary. The first requirement is a robust, verified baseline solver."
```

### Two explicit modes, no hidden switching (§13)

```text
TimeSteppingMode::GlobalPhysical    dt = min over cells of dt_P; the SAME dt for every cell.
                                    The only PHYSICALLY MEANINGFUL transient mode.
TimeSteppingMode::LocalPseudoTime   dt_P per cell, for steady-state convergence acceleration.
                                    NOT a transient integrator: cells advance different amounts of
                                    physical time, so the intermediate states are not a time history
                                    of anything. The solver REFUSES to report a physical `time` in
                                    this mode (it reports the iteration count only), which is how
                                    section 12's "must not be presented as a physically correct
                                    transient integrator" is enforced rather than merely stated.
The mode is a required constructor setting. There is no automatic switching anywhere.
```

## 7. §29 — restart: UNSUPPORTED, and it refuses

```text
The existing framework (io/RestartWriter.hpp, CheckpointWriter.hpp) serialises a FIXED payload:
    pressure, velocity_x, velocity_y, mass_flux
It carries no rho, no rhoU and no rhoE, and it is the format the existing restart tests assert
against. Widening it is an IO-format change affecting every existing caller -- outside P17.8.
DECISION: density-based restart = UNSUPPORTED.
Section 29 permits exactly this, on one condition: the solver must "refuse an invalid restart request
explicitly rather than silently producing incorrect state". So there is an explicit
    DensityBasedSolver::restartSupported() -> false
    restartFrom(...)  -> throws, naming the missing conserved fields
and a test asserts the refusal. Silence would have been the failure mode; a throw is the requirement.
```

## 8. §32, §33 — GPU: NOT IMPLEMENTED in P17.8

```text
The frozen P17_GATES.md gate matrix assigns P17.8:  G1  G3  G4  G6  G9.
G8 is NOT in that row. Sections 32 and 33 are both conditional -- "where available", "If the GPU
density-based solver is implemented in P17.8".
DECISION: no GPU density-based solver. Reported NOT IMPLEMENTED, never PASS.
What IS already true, from P17.7: the HLLC flux itself is bitwise identical on CPU and GPU (17/17
rows, difference exactly 0), because both compile one CFD_HLLC_HOST_DEVICE source. So the flux half of
a future GPU solver is already qualified; what is missing is the residual assembly, the CFL reduction
and the update, all of which need device-resident fields. That is P17.10's ground.
```

## 9. §14, §15 — boundary states: the minimum the verification needs

§14 says *"Do not implement every advanced compressible BC unless required"*, so P17.8 implements
three, each as an **exterior state** handed to P17.7's boundary interface — no boundary logic inside
HLLC:

```text
InviscidWall        the exterior state MIRRORS the normal velocity and keeps everything else:
                      U_ext = U_int - 2 (U_int . n) n,  rho/p/T unchanged
                    so the contact speed is exactly zero and the flux is pure pressure. Section 15's
                    requirement (mass flux = 0, energy flux = 0, pressure force retained) becomes a
                    BITWISE claim, derived the same way P17.7's stationary contact was.
Supersonic inflow   the exterior state is the PRESCRIBED state; every characteristic enters, so HLLC's
                    own branch selection returns F_R (or F_L) with no extrapolation needed.
Supersonic outflow  the exterior state is the INTERIOR state (zero-gradient extrapolation); every
                    characteristic leaves.
```

```text
NOT IMPLEMENTED, and NOT claimed: subsonic inlet/outlet with characteristic (Riemann-invariant)
treatment, and far-field. A subsonic open boundary needs incoming-characteristic reconstruction to be
well-posed, which is its own verification surface. The canonical cases in section 21-24 are built to
need only the three above: uniform flow and the advected contact are PERIODIC or supersonic, and the
wall case is closed.
```

## 10. §16, §17 — residual norms and convergence

```text
FIVE norms, one per conserved equation:  R_rho, R_rhoU, R_rhoV, R_rhoW, R_rhoE
NORM       volume-weighted L2, matching the convention every other aggregated per-cell statistic in
           this codebase uses (ManufacturedFields::l2CellError, solver::CFLResult::meanCFL):
               ||R||_2 = sqrt( sum_P (R_P / V_P)^2 V_P / sum_P V_P )
           R_P/V_P is the per-unit-volume rate of change, so the norm is a physical rate and does NOT
           scale with mesh size -- which is section 16's explicit requirement ("Do not use
           unnormalized residuals whose magnitude depends only on mesh size").
NORMALISED by the FIRST iteration's norm, reported as both absolute and relative. The reference scale
           is captured once, at iteration 0, and never re-captured -- re-capturing it mid-run is how a
           stagnating solve is made to look convergent.
```

```text
CONVERGENCE requires MORE THAN ONE SIGNAL (section 17):
  (a) every one of the five relative residual norms below its tolerance, AND
  (b) the global mass imbalance below its own bound, AND
  (c) the maximum state change |Q^{n+1} - Q^n| / |Q^n| below its threshold
and the status distinguishes Converged / MaxIterations / Stagnated / InvalidState / NonFiniteState.
For GlobalPhysical mode, reaching the end time is TimeComplete -- a DIFFERENT status from Converged,
because section 17 warns against applying steady stopping criteria to a transient run. Steady
criteria are evaluated ONLY in LocalPseudoTime mode.
```

## 11. §18, §19 — positivity of the UPDATED state, and the retry policy

P17.7 guaranteed admissible *input* and *star* states. P17.8 must protect the *updated cell state*,
which is a different claim.

```text
AFTER EVERY CANDIDATE UPDATE, for every cell:  rho > 0, finite Q, and a successful
makePrimitiveStateFromConserved() -- which is P17.5's recover(), so p > 0, T in range and a valid
internal energy are checked BY THE AUTHORITATIVE MODEL rather than by a second opinion here.

THE POLICY IS DETERMINISTIC RETRY, NOT CLIPPING:
    attempt the step with dt
      -> candidate invalid?  ->  dt <- dt * kRetryReductionFactor (0.5), retry
      -> up to kMaxRetries (5) times
      -> still invalid  ->  return InvalidState with the offending CELL ID and its values.
NOTHING IS EVER CLAMPED. Section 18 permits clipping only if its conservation impact is documented;
the impact is that clipping ADDS mass/energy that the solver did not compute, which would silently
break the G3/G4 conservation criteria this same phase is asserting. So it is not done, and that is
the reason rather than a preference.
Retries and rejections are COUNTED and reported, so a solve that only succeeded by shrinking dt five
times cannot look like a clean one.
```

## 12. §20 — non-finite detection

```text
Checked, with the CELL ID and the iteration recorded in the thrown/returned diagnostic:
  rho, each momentum component, rhoE, p, T, every residual component, and dt itself.
A non-finite dt or residual fails the step BEFORE it is applied, so corruption cannot propagate into
Q. Detection is per-cell and reports the FIRST offending cell in deterministic id order.
```

## 13. Files P17.8 will add

```text
include/cfd/compressible/DensityBasedSolver.hpp   the state, settings, residual, CFL, the solver
src/compressible/DensityBasedSolver.cpp
tests/unit/compressible/test_density_based_solver.cpp
tests/integration/compressible/test_density_based_canonical.cpp   uniform flow, oblique uniform
                                                                  flow, advected contact, wall,
                                                                  conservation, determinism
PRODUCTION FILES MODIFIED: 0 expected. No dispatch change, no core-type change, no SIMPLE change.
```
