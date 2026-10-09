# P17.8 — what was implemented

Covers the authorization's §4 (state ownership), §5 (conversion), §6/§7 (residual),
§9 (update), §10–§13 (CFL and stepping), §14/§15 (boundaries), §16/§17 (norms and convergence),
§18–§20 (positivity, retry, non-finite detection) and §29 (restart).

The design decisions are in `architecture.md`, frozen before any source existed. This file records
what the code actually does, and the places where the implementation had to be corrected after the
gate started measuring it.

```text
include/cfd/compressible/DensityBasedSolver.hpp    299 lines (pre-format), new
src/compressible/DensityBasedSolver.cpp            687 lines (post-format), new
```

---

## 1. §4 — conserved-variable storage and ownership

```text
struct ConservedField {
  ScalarField density;             // rho   [kg/m^3]
  VectorField momentumDensity;     // rho U [kg/(m^2 s)]
  ScalarField totalEnergyDensity;  // rhoE  [J/m^3]
};
```

```text
allocation       sized from mesh.numberOfCells() by fromPrimitive(); the solver never resizes
initialisation   fromPrimitive() -> makePrimitiveState() -> toConserved(). Every value is produced
                 by P17.2's EOS and P17.5's conservedTotalEnergy. There is no second path in.
old / new        solve() takes Q BY VALUE and returns the final state. Inside a step exactly two
                 copies live: the accepted state and the candidate. A rejected candidate is
                 discarded, never merged.
temporaries      NONE. Single-stage explicit Euler; no RK registers exist.
ownership        the solver owns its working copies; the caller's field is never mutated
host / device    HOST ONLY
restart          UNSUPPORTED, and it refuses -- section 8 below
output           the solver returns state and history; it writes no files and touches no exporter
```

**There are no unsynchronised copies**, which is the property §4 protects. Primitives are never
stored beside `Q`; they are recomputed on demand by `toPrimitive()`, so they cannot drift.
Criterion Q4 checks this structurally (0 stored primitive members, against a sanity control that
finds 4 other members — a zero from a broken pattern would otherwise read as a pass).

## 2. §5 — primitive ↔ conserved conversion

Both directions go through P17.5. **There is no conversion arithmetic in this class.**

```text
fromPrimitive()   makePrimitiveState(thermo, rho, U, T)  ->  toConserved(s)
toPrimitive()     makePrimitiveStateFromConserved(thermo, c)      = P17.5's recover()
```

Criterion Q2 is structural and was the first probe in this phase to catch itself: its original
pattern looked for `gamma - 1` and `specificGasConstant`, and its **sanity control returned 0 on
HllcFlux.hpp** — the real code writes `(gamma + 1.0)/(2.0*gamma)` and `gasConstant_`. The 0 on the
solver was therefore blind, not a pass. The corrected pattern matches gamma in any arithmetic
context plus the real identifier, and is checked against three upstream physics files that must all
be non-zero (1, 8, 3 hits) before the solver's 0 means anything.

```text
Q1, measured: rho and rhoE recover BITWISE; worst relative T error 0.000e+00, p error 0.000e+00.
```

## 3. §6, §7 — the finite-volume residual

```text
R_P = sum over P's faces of   +F_f A_f   (owner)
                              -F_f A_f   (neighbour)
F_f = HLLC(Q_L, Q_R, n_f)                one evaluation, all five components at once
```

The sign convention is **the repository's**, not a new one: `src/compressible/CompressibleContinuity.cpp`
and `physics::evaluateContinuity` both use `+F` owner / `−F` neighbour, and criterion R5 checks the
solver against that file rather than against a rule invented here.

One `hllcFlux` call site exists in the assembly loop (R3, structural: exactly 1). The **same**
`FluxVector` is added to the owner and subtracted from the neighbour, so the cancellation is exact by
construction rather than to round-off — R1 asserts that BITWISE on all five components, against an
independent re-accumulation built from P17.7's public pieces.

```text
WHAT R1 CANNOT SEE, recorded rather than glossed: a second hllcFlux evaluation with swapped states
would agree BITWISE, because P17.7 proved F(L,R,n) = -F(R,L,-n) exactly. R3's structural criterion
covers that half. The pair covers the claim; neither alone does.
```

`assembleResidual()` fails closed on a boundary patch with no treatment — an unmapped patch is not
silently assumed to be a wall.

## 4. §9 — the conserved-variable update

```text
Q_P^{n+1} = Q_P^n - (dt_P / V_P) R_P        single-stage explicit forward Euler
```

`applyUpdate()` takes one `dt` per cell in **both** modes: GlobalPhysical fills every entry with
`min_P dt_P`, so the update path is identical and U2 can assert the equality instead of trusting a
branch.

```text
U1 WAS BLIND WHEN FIRST WRITTEN, and the control found it. Its mesh was createCartesian2D(3, 2, 3.0,
2.0) -- dx = dy = 1, so every cell volume was EXACTLY 1.0 and dt/V was bit-identical to dt. Control
N2 deletes the division by volume; U1 passed. It now runs on a GRADED mesh, asserts that no cell has
unit volume and that the volume spread exceeds 1.5, and adds the decisive statement: the change in Q
between two meshes scales as their actual volume ratio, within 64 eps.
```

## 5. §10–§13 — the CFL, and the two stepping modes

```text
dt_P = CFL * V_P / sum over P's faces of ( |U_f . n_f| + a_f ) * A_f
```

with the face state the arithmetic mean of the two cell primitive states (the interior state alone at
a boundary face), and `a_f` the mean sound speed. `GlobalPhysical: dt = min_P dt_P`. The last step of
a transient run is truncated so the run lands exactly on `endTime`.

**This does not replace `cfd::solver::calculateCFL`, and the audit is why.** That function is
diagnostic only, advective only, constant-density, and maps `dt -> Co`; P17.8 needs `Co -> dt` with
an acoustic term. Rather than leave two silently different conventions in the tree, K4 ties them
together:

```text
with a = 0, uniform density and a uniform grid-aligned flow, feeding P17.8's dt back through
solver::calculateCFL must return CFL/2 -- the 1/2 being that function's own documented factor that
keeps it equal to the 1D convention Co = U dt/dx.

MEASURED: P17.8 dt 2.382920340e-04 -> Co 9.531681361e-03, expected 9.531681361e-03,
          relative difference 0.000e+00   (bound 1e-12)
```

Getting that factor of 2 wrong is precisely the confusion the criterion exists to catch, and the
existing function is **not modified** — A5 verifies `CFL.hpp` by its frozen SHA-256 and the other
three files as git-clean.

```text
GlobalPhysical    the same dt for every cell. The ONLY physically meaningful transient mode.
LocalPseudoTime   a per-cell dt, for steady convergence acceleration. NOT a transient integrator:
                  cells advance different amounts of physical time. DensityBasedResult::physicalTime()
                  THROWS in this mode, so a number that means nothing cannot be read out of a
                  pseudo-time run. That is how §12's warning is enforced rather than merely written.
```

The mode is a required constructor setting; U4 checks structurally that there are 0 assignments to it
outside construction, against a sanity control showing it is read 5 times.

## 6. §14, §15 — boundary states

Three treatments, each supplying an **exterior state** to P17.7's boundary interface. No boundary
logic lives inside HLLC.

```text
InviscidWall       U_ext = U_int - 2 (U_int . n) n, with rho, p, T unchanged. rhoE is REBUILT from
                   the mirrored velocity rather than carried over: |U| is unchanged by a reflection
                   in exact arithmetic but not guaranteed to be in floating point, so recomputing it
                   keeps the state internally consistent by construction.
SupersonicInflow   the prescribed state, used as given -- every characteristic enters
SupersonicOutflow  the interior state (zero-gradient) -- every characteristic leaves
```

The wall's mirrored normal velocity makes the HLLC contact speed vanish, so mass and energy fluxes
vanish and only the pressure force remains. **This is the test that found P17.7's star-energy sign
error** (`AMENDMENT_A6_upstream_exception.md`): a wall is the one cheap configuration with `S* = 0`
while `u_n != 0`, so the erroneous term is not multiplied by zero.

```text
MEASURED, after the correction:
  wall mass flux    2.842171e-14   (bound 6.115e-12)
  wall energy flux  7.450581e-09   (was 2.645e+07 with the wrong sign)
  wall momentum.x   1.713575e+05   against p = 1.102272e+05 -- the pressure force is RETAINED
```

**Not implemented and not claimed:** subsonic inlet/outlet with characteristic treatment, and far
field. A subsonic open boundary needs incoming-characteristic reconstruction to be well posed, which
is its own verification surface. The canonical cases are built to need only the three above.

## 7. §16, §17 — residual norms and convergence

```text
five norms, one per conserved equation: R_rho, R_rhoU, R_rhoV, R_rhoW, R_rhoE
||R||_2 = sqrt( sum_P (R_P/V_P)^2 V_P / sum_P V_P )      volume-weighted L2 of a per-unit-volume RATE
```

`R_P/V_P` is a rate, so the norm does **not** scale with mesh size — §16's explicit requirement, and
M2 measures it: `0.000e+00` at both 8×8 and 32×32 on a uniform state. The weighting matches the
convention every other aggregated per-cell statistic in this codebase uses
(`ManufacturedFields::l2CellError`, `solver::CFLResult::meanCFL`).

The reference scale is captured **once**, at iteration 0, and never re-captured; M3 asserts it by
reconstructing the implied reference from every record and comparing it to iteration 0's (drift
≤ `8 eps`), and additionally requires the relative residual to actually move — otherwise "captured
once" would be untested because nothing changed.

```text
CONVERGENCE REQUIRES ALL THREE, in LocalPseudoTime mode only:
  (a) all five RELATIVE residual norms below tolerance, AND
  (b) the global mass imbalance below its own bound, AND
  (c) max |Q^{n+1} - Q^n| / |Q^n| below its threshold

M4, measured: all three loose -> Converged after 354 iterations. Blocking each in turn:
  residual norms  -> MaxIterations at 4000     mass imbalance -> 1548     state change -> 2231
Every signal changes the outcome, so none is decorative.
```

`GlobalPhysical` reaching `endTime` is **`TimeComplete`**, a different status from `Converged` (M5),
and the steady criteria are not evaluated in that mode at all (M6: the same state and the same
tolerances converge in pseudo-time and never report Converged in transient mode).

```text
NOTE, recorded not fixed: the DensityBasedStatus enum declares Stagnated, which nothing ever
assigns. It is unreachable today. Not a gate criterion, and removing a public enumerator is an API
change outside P17.8's scope -- reported under Known Technical Debt instead.

FIXED, and recorded because it was found the same way: solve() also computed a local `totalVolume`
that nothing read. It was removed. That is a DELIBERATE semantic change made after the clang-format
pass, so logs/20_format_verified.log does not claim "formatting only" for this file -- it asserts
the stronger and more specific thing, that the ONLY token-multiset difference is exactly the two
deleted lines and that no string literal moved. The focused suite was re-run afterwards.
```

## 8. §18–§20 — positivity, retry, non-finite detection

```text
AFTER EVERY CANDIDATE UPDATE, for every cell:
  rho > 0, Q finite, and makePrimitiveStateFromConserved() succeeds -- so p > 0, T in range and a
  valid internal energy are checked BY P17.5's AUTHORITATIVE MODEL, not by a second opinion here.

THE POLICY IS DETERMINISTIC RETRY, NOT CLIPPING:
  invalid candidate -> dt <- dt * 0.5, up to 5 times -> still invalid -> InvalidState, naming the
  offending cell id and its value. The result keeps the last ACCEPTED state, never the rejected
  candidate.
```

**Nothing is ever clamped.** §18 permits clipping only with its conservation impact documented; the
impact is that clipping adds mass or energy the solver did not compute, which would silently break
the G3/G4 conservation criteria this same phase asserts. Control N4 plants a density floor and it
breaks G1, G2 and P4 — so the choice is load-bearing, not a preference.

```text
P2/P6 measured, CFL = 50 (deliberately unstable): status TimeComplete, 5 iterations, 5 retries,
0 rejected steps, and a repeat made IDENTICAL control-flow decisions.
```

### Two defects the gate found in this area

```text
1  solve() DID NOT CATCH errors from assembleResidual.
   toPrimitive and the dt evaluation were both wrapped; the residual assembly was not. A flux that
   OVERFLOWS (as distinct from coming out NaN) is caught inside P17.7's hllcFluxChecked, which
   THROWS -- so it escaped solve() as an uncaught exception instead of a NonFiniteState with a
   location. Found by criterion P5, which enumerates the residual components as one of the
   quantities that must be DETECTED rather than propagated.
   FIXED: assembleResidual now names the face and its owner cell when P17.7's guard fires (the loop
   runs in face-id order, so the first to throw is the first offender in deterministic order), and
   solve() catches NumericalError and returns NonFiniteState. InvalidArgumentError is deliberately
   NOT caught -- an unmapped boundary patch is a configuration error and must still throw.

2  result.offendingCell was hard-coded to 0 on the recover-failure path.
   The diagnostic MESSAGE named the right cell, and criterion P3's test asserted the message, so the
   FIELD went untested and disagreed with it. FIXED with a shared helper that returns the index;
   the message now quotes the field, so the two cannot drift apart. A new test asserts the field.
```

```text
P5 coverage, each quantity corrupted IN TURN and detected naming the cell:
  rho = NaN, rho = +Inf, rhoU = NaN, rhoV = +Inf, rhoW = NaN, rhoE = NaN, rhoE = -Inf
  p / T  -- a conserved state whose kinetic energy exceeds its total energy (no valid p, T exists)
  dt     -- a non-finite face speed, refused BEFORE any update, so corruption cannot reach Q
  residual -- an extreme but ADMISSIBLE state (rho = 1e300) whose energy flux overflows to Inf;
              reported as NonFiniteState naming the iteration, the face and the owner cell
```

## 9. §29 — restart: UNSUPPORTED, and it refuses

The existing framework (`io/RestartWriter.hpp`, `CheckpointWriter.hpp`) serialises a fixed payload —
`pressure`, `velocity_x`, `velocity_y`, `mass_flux`. It carries no `rho`, no `rhoU` and no `rhoE`,
and it is the format every existing restart test asserts against. Widening it is an IO-format change
affecting every existing caller, outside P17.8.

§29 permits UNSUPPORTED on one condition: the solver must refuse explicitly rather than silently
produce wrong state.

```text
DensityBasedSolver::restartSupported()   -> false
DensityBasedSolver::throwIfRestartRequested() -> ALWAYS throws, and the message NAMES the fields the
    format cannot carry: "it has no density, no momentumDensity (rhoU) and no totalEnergyDensity
    (rhoE), so a restarted conserved state cannot be reconstructed. Refusing rather than producing
    an incorrect state."
S1/S2 assert both, including that the message contains rhoU, rhoE and NOT SUPPORTED.
```

**Restart determinism (§30) is therefore NOT TESTED and NOT CLAIMED.** There is no restart to
compare against a continuous run. Determinism of the continuous run itself is verified separately
(D1, bitwise).

## 10. What this solver is not

```text
EULER ONLY            the face flux is HLLC and nothing else. No viscous stress, no heat conduction.
                      Compressible Navier-Stokes is NOT claimed. The seam for adding them is defined
                      (totalFaceFlux = inviscid + viscous) but the second term is ABSENT, not stubbed
                      with zeros -- a zero stub is indistinguishable from a wired-up term that
                      happens to vanish. S3 checks structurally that there is no viscous term in code.
LIBRARY LEVEL         no dispatch change, no case-format key. A case file cannot select this solver.
                      That is also WHY SIMPLE is provably unaffected: no line that selects a solver
                      is edited. Stated plainly; no production capability is claimed.
HOST ONLY             no GPU density-based solver. P17.7's flux is already bitwise identical on CPU
                      and GPU, but the residual assembly, the CFL reduction and the update are not
                      ported. Reported NOT IMPLEMENTED, never PASS.
FIRST ORDER           piecewise-constant reconstruction, single-stage explicit Euler. Higher-order
                      reconstruction can be added without touching the residual or the flux.
```
