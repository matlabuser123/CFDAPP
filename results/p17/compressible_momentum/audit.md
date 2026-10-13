# P17.4 — Compressible Momentum: audit and implementation decision

Written 2026-09-27, **before any code was changed**, from `results/p17/baseline/`,
`results/p17/audit/`, `results/p17/gates/P17_GATES.md`, and the verified P17.1–P17.3
evidence.

---

## 1. The production momentum assembly, traced

`assembleRelaxedCompressibleMomentumComponent` — called from `CompressibleSIMPLE.cpp:265`,
once per velocity component per outer iteration. Assembled in this exact order (order
matters: `SparseMatrixBuilder` accumulates and floating-point addition is not associative):

```text
1  assembleDiffusionContribution(mesh, mu, velocity, velocityBCs, component,
                                 builder, rhs, nonOrthogonal.enabled, gradientScheme)
2  assembleConvectionContribution(mesh, massFlux, velocity, velocityBCs, component,
                                  builder, rhs)
3  assemblePressureSourceContribution(mesh, pressure, pressureBCs, component, rhs,
                                      pressureGradientScheme)
4  assembleMomentumSourceContribution(mesh, *momentumSource, component, rhs)   [optional]
5  compressibleMomentumTimeDerivative(mesh, previousComponentValue,
                                      densityOld, densityNew, pseudoTimeStep)
      -> builder.add(i, i, diagonal[i]);  rhs[i] += source[i]
6  applyImplicitUnderRelaxation(builder, rhs, previousComponentVector, alpha)
7  build; allFinite() on the matrix and rhs, else NumericalError
```

`requireTwoDimensional` at entry — **2D only**.

`assembleCompressibleMomentumComponent` (the non-relaxed sibling) has **no production
caller**: it calls `.build()` internally so it cannot sit inside a relaxed outer loop. Its
own header records this. Library capability, not production capability.

## 2. Term by term — what is actually implemented

```text
term                  implementation                                    verdict
-------------------------------------------------------------------------------------------
d(rho U)/dt           compressibleMomentumTimeDerivative:               CORRECT, and
                        diagonal[P] = rho_new[P] V_P / dt               conservative
                        source[P]   = rho_old[P] V_P / dt * u_old[P]
                      i.e. (rho_new u_new - rho_old u_old) V/dt, the
                      two densities kept SEPARATE
div(rho U (x) U)      assembleConvectionContribution fed the P17.3      CORRECT in form,
                      authoritative massFlux -> flux-based, telescoping  UPWIND ONLY
-grad(p)              assemblePressureSourceContribution:               CORRECT
                        rhs += -V_P * gradP_component,
                        gradP = discretization::gradient(p, BCs, scheme)
div(tau)              assembleDiffusionContribution -- the              INCOMPLETE
                      INCOMPRESSIBLE Laplacian mu lap U                  see section 4
rho f                 assembleMomentumSourceContribution:               API CORRECT,
                        rhs += V_P * source_component,                   NO PHYSICS
                      and the parameter is named sourcePerUnitVolume,
                      so it is a FORCE DENSITY -- the caller supplies
                      rho*f. No gravity/buoyancy exists on the
                      compressible path at all.
```

### The transient term is the correct conservative form

The header records the trap it avoids, verbatim: *"Do not simply use
`rho_current*(u_new-u_old)*V/dt` without verifying whether that is consistent — it is not,
for a genuinely changing density; the correct conservative discretization keeps `rho_old`
with `u_old` and `rho_new` with `u_new` separately."* And
`ConstantDensityMatchesIncompressibleFormula` proves it degenerates exactly to
`implicitEulerTimeDerivative` at constant density.

**But it is used as pseudo-transient stabilisation, not time accuracy.**
`CompressibleSIMPLE.hpp:27` states `pseudoTimeStep` is "a numerical stabilization
parameter, not a physical time step", and at the call site `densityOld == densityNew`. So
the *form* is conservative and correct; the *use* is dual-time relaxation.

### Body force: the API is right, the physics is absent

```text
bodyForce 0   gravity 0   buoyancy 0   "rho * f" 0        hits in the compressible module
```

`assembleMomentumSourceContribution` takes `sourcePerUnitVolume` and multiplies by
`cell.volume()`. That is exactly the shape `rho f` needs — a force **density**. So the API
weights correctly by construction and the caller supplies `rho*f`. What does not exist is
any production body force on the compressible path; `momentumSource` is used only by the
MMS test via `setMomentumSource`.

P17.4 therefore verifies the **API contract** (that the RHS scales linearly with a supplied
`rho*f`) and records that no compressible body-force physics exists. Adding gravity or
buoyancy to the compressible path is beyond P17.4's scope and §8 says not to expand it.

## 3. GPU status — measured

```text
files matching compressibleMomentum or CompressibleRelaxed in cuda/ or include/cfd/gpu/   0
```

**No GPU compressible momentum path exists.** Gate **G8 is vacuous** for P17.4, exactly as
it was for P17.3. Recorded as `NOT PRESENT`, never as PASS.

## 4. The one real gap — the dilatational stress term

For a Newtonian fluid under the Stokes hypothesis,

```text
tau = mu [grad(U) + grad(U)^T] - (2/3) mu (div U) I
```

What is implemented is `physics::assembleDiffusionContribution`, the **incompressible**
Laplacian `mu lap U`. The dilatational part is absent, measured repeatedly:

```text
grad(div   0 hits     dilatation  0 hits     bulkViscosity 0 hits
twoThirds  0 hits     Stokes      0 hits     divU          0 hits
```

and `tests/integration/mms/test_mms_compressible.cpp` names it in its own machine-readable
`not_covered` field:

```text
"energy equation, mu/3 grad(div U) stress term, higher-order convection, high Mach"
```

For incompressible flow `div U = 0` and the term vanishes, so this was harmless through
P16. **Compressible flow is exactly the regime where `div U != 0`.**

### Why this is implementable without new numerics

Both operators already exist and are tested:

```text
discretization::divergence(mesh, velocity, velocityBCs)  -> ScalarField div(U) per cell
    div(U)_P = (1/V_P) * sum_f (U_f . Sf)
discretization::gradient(mesh, scalarField, BCs, scheme) -> VectorField
```

So `-(2/3) mu grad(div U)` is a **composition of two tested operators**, added as a source
term `rhs += -(2/3) mu * gradDivU_component * V_P`. No new discretisation is invented.

The one genuinely new decision is the boundary condition for the derived scalar `div U`.
There is no physical BC for dilatation, so **zero-gradient** is used: the boundary face
takes its owner cell's value. That is the standard treatment for a derived quantity with no
physical boundary condition, and it is stated in the gate rather than buried.

## 5. The decision: implement it, DEFAULT OFF

```text
NEW      a separate operator, assembleDilatationalStressContribution
WIRED    into assembleRelaxedCompressibleMomentumComponent behind an option that
         DEFAULTS TO DISABLED
```

### Why default-off rather than unconditional

Three reasons, in order of weight:

```text
1. THE MMS TEST WOULD FAIL, CORRECTLY. Its forcing is hand-derived for `mu lap U` only --
   "forcing: analytical f = (m.grad)U + grad p - mu lap U". Adding the term unconditionally
   changes the equation the MMS is manufactured against, so a PASSING, VALIDATED test would
   start failing for a reason that is not a defect. Fixing that means re-deriving and
   re-verifying the manufactured forcing, which is a separate piece of work with its own
   evidence burden.
2. CLAUDE.md section 6: "Prefer a dedicated new path over conditionals bolted onto shared
   code. When shared code changes, prove existing results are unchanged (bitwise where
   claimed)." Default-off makes "unchanged" provable by construction rather than by
   argument -- the P13-001 SIMPLE fingerprint and all 2531 tests stay bitwise identical.
3. ESTABLISHED PRECEDENT. nonOrthogonalCorrections{0} defaults to "exactly the
   pre-P12-NUM-003 solver"; robustness defaults to "every feature off -- exactly the
   pre-P12-NUM-004 solver". A default-off numerical option is this codebase's idiom for
   adding physics without disturbing a qualified baseline.
```

### What default-off must NOT be allowed to become

A feature no case file can reach is not shipped capability. So the gate requires:

```text
* the term is CORRECT when enabled -- verified against an analytical grad(div U)
* enabling it CHANGES the answer -- otherwise the option is vacuous
* it VANISHES for a divergence-free field -- so it cannot corrupt the incompressible limit
* the default-off path is BITWISE identical to today
```

and it is recorded as **reachable from the library but not yet from a case file**, which is
honest and is the same status `pseudoTimeStep` already has. Wiring it to the case format is
recorded as follow-on work, not claimed.

## 6. Classification of every P17.4 requirement

```text
requirement                                class                action
---------------------------------------------------------------------------------------
Audit existing momentum implementation     ALREADY SATISFIED    this document
Verify conservative form                   PARTIAL              verify 4 terms, fix the 5th
Verify density-weighted transient term     ALREADY SATISFIED    verify, incl. the rho dU/dt
                                                                trap
Verify convective momentum flux            ALREADY SATISFIED    verify, reuses P17.3 flux
Verify pressure-gradient treatment         ALREADY SATISFIED    verify against analytic
Verify viscous stress formulation          INCOMPLETE           IMPLEMENT  <- the code
Preserve incompressible / low-Mach limit   ALREADY SATISFIED    verify, bitwise
Verify momentum conservation               NEEDS EXTENSION      integral balance tests
Add canonical tests                        NEEDS EXTENSION      the new tests
CPU/GPU equivalence                        NO GPU PATH          record accurately  [~]
```

**One requirement needs code. Eight are verification.**

## 7. Continuity/momentum consistency — the §14 matrix, pre-checked

```text
quantity            continuity                      momentum                    consistent?
-------------------------------------------------------------------------------------------
cell density        the P17.1 field, EOS(p_abs,T)    densityOld/densityNew,      YES, same field
                                                     the same field
face density        evaluateCompressibleFaceDensity  via massFlux only           YES, one source
face velocity       interpolateFace(velocity, BCs)   interpolateFace inside      YES, same helper
                                                     the convection assembler
mass flux           calculateCompressibleMassFlux    THE SAME SurfaceField,      YES, passed in
                                                     passed as an argument
boundary mass flux  EOS at the face's own p,T        the same massFlux entries   YES
corrected flux      correctFaceMassFlux after the    the NEXT outer iteration    YES, by
                    pressure correction              re-assembles from it        construction
```

No mismatch found. The invariant holds structurally: `CompressibleSIMPLE` computes one
`massFlux` and **passes** it to both the momentum assembly and the continuity evaluation, so
there is no second reconstruction that could drift.

## 8. Plan, in gate order

```text
1. dry-run against the UNCHANGED tree: measure the four correct terms against analytical
   values, prove the dilatational term is absent and quantify what it WOULD contribute,
   and measure the momentum integral balance
2. freeze acceptance_gate.md
3. implement the dilatational operator, default OFF
4. non-vacuity controls
5. focused tests: conservative form, transient, convection, pressure, viscous, boundaries,
   conservation, canonical, low-Mach
6. G1 preservation incl. the SIMPLE fingerprint bitwise
7. regression against the P17.3 baseline (2531 / 2331)
8. evidence + TODO.md
```

## 9. Explicitly NOT in P17.4

```text
re-deriving the MMS manufactured forcing to include the dilatational term  -> follow-on
wiring the dilatational option to the case format                          -> follow-on
gravity or buoyancy on the compressible path                               -> out of scope (§8)
higher-order convection (upwind is all that exists)                        -> later
lifting the 2D restriction                                                 -> later
a production caller for assembleCompressibleMomentumComponent              -> not needed
total energy (P17.5), HLLC (P17.7), density-based solver (P17.8)
modifying physics::assembleDiffusionContribution -- shared with the incompressible path
a GPU momentum path -- none exists
```
