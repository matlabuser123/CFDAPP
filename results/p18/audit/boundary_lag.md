# P18.0 — the species boundary lag, measured (§12, §13)

```text
CLASSIFICATION:  CONFIRMED DEFECT -- of convergence COST, not of the converged answer.

MEASURED, identical pure-Neumann problem, 12x8 mesh, dY/dn = 0 on every wall, pure diffusion:
    SPECIES solver   335 outer Picard iterations to converge
    THERMAL solver     2 outer Picard iterations to converge
                     ---> 167x
Both converge, and both return a field that satisfies its own boundary condition. The species
stack pays 167x the outer iterations to get there because its gradient boundary term is lagged
where thermal's is exact.

STATUS: PRE-EXISTING and already recorded. NOT fixed in P18.0 -- §12 forbids it, and §34 forbids
altering upstream behaviour. The probe is instrumentation only.
EVIDENCE: results/p18/logs/11_lag_probe.log · probe: results/p18/tools/boundary_lag_probe.cpp
```

---

## 1. This was already known, and already fixed once — in thermal

From `results/todo-archive/TODO-2026-09-17-pre-restructure.md:196-201`, describing P12-MESH-003:

> "…adiabatic/heat-flux boundaries left a converged field inconsistent with its own boundary
> values); fixed in the thermal solver (exact gradient-type boundary assembly, attained-breakdown
> rule) with a regression test that fails on the pre-fix solver (§17). …
> **Known limitation: the species solver has the same lagged-boundary pattern (not changed here,
> §21).**"

Reaffirmed at `results/p12-mesh-004/summary.md:404`: *"Species solver: the lagged-boundary pattern
from P12-MESH-003 remains."* And carried on the roadmap's own debt list (`ROADMAP.md:696`:
"species boundary lag").

So the project classified this pattern as a **defect** when it found it in thermal, fixed it there,
and knowingly left it in species. P18.0's job was to measure what remains.

## 2. The precise code difference

```text
THERMAL -- src/thermal/EnergyEquation.cpp:103-104         THE FIX
    // (boundaryValue(0, d) = g d); nothing depends on T_P.
    return {0.0, coefficient * scalarBc->boundaryValue(0.0, distance)};

  coefficient  = 0.0        -> NO diagonal entry for a gradient face
  owner value  = 0.0        -> the reconstruction cannot see the field at all
  RHS          = Df * g * d -> exactly the prescribed Neumann flux
  One assembly is already self-consistent.

SPECIES -- src/species/SpeciesEquation.cpp:33-44, 75-87   THE LAG
    const Real ownerValue = concentration[face.owner()];          // the CURRENT (lagged) iterate
    return scalarBc->boundaryValue(ownerValue, distance);          // = Y_P + g*d
    ...
    const auto terms = boundaryFaceDiffusionTerms(mesh, face, D, distance, gradYPtr, prescribed);
    builder.add(ownerId, ownerId, terms.coefficient);              // = Df, NON-ZERO
    rhs[ownerId] += terms.boundaryValueCoefficient * yB;           // carries the LAGGED Y_P

  For a FixedGradient face `prescribed` is false, so the shared helper falls through to
  `twoPointTerms(gammaFace * face.area() / distance, 0.0)` -- coefficient Df, explicitFlux 0.
  The row therefore holds  +Df*Y_P^new  on the left and  +Df*(Y_P^lagged + g*d)  on the right.

  Those two cancel to the correct flux -Df*g*d ONLY when Y_P^new == Y_P^lagged, i.e. only at outer
  convergence. Until then the boundary is anchored to the previous iterate, and the spurious +Df on
  the diagonal is what drags the fixed-point iteration out.
```

## 3. The execution sequence, as traced (§12)

```text
ProjectRunner
  └─ SIMPLE converges -> massFlux FIXED (so there is NO flow/species lag)
     └─ for each species: SpeciesSolver::solve(...)
        └─ runPicardLoop  (src/species/SpeciesSolver.cpp:70-124)
             outer = 0 .. maxIterations-1:
               1. assemble(concentration)          <-- BOUNDARY EVALUATED FROM *THIS* FIELD
                                                       (the previous outer iterate)
               2. BiCGSTAB solve -> newConcentration
               3. maxChange = max |newConcentration - concentration|
               4. concentration = newConcentration
               5. if maxChange < tolerance (1e-8) -> Converged, break
             (no relaxation, no under-relaxation factor: a pure fixed-point update)

WHICH STATE THE BOUNDARY USES:  the PREVIOUS OUTER ITERATION's field, within a single solve.
                                NOT a previous time step (there are no time steps).
                                NOT a stale cache (it is re-read every assembly).
```

## 4. The measurement (§13)

Probe: `results/p18/tools/boundary_lag_probe.cpp` — drives the existing solvers, adds no physics.
The discriminating quantity was declared in the probe's own header **before** running: outer
iterations to convergence on the same problem shape.

```text
1. SPECIES, all walls dY/dn = 0, pure diffusion, tol 1e-8
   maxOuter cap    outer used   status          maxChange
        1               1       MaxIterations   1.367e-02     <- boundary not yet consistent
        2               2       MaxIterations   1.121e-02
        5               5       MaxIterations   6.978e-03
       20              20       MaxIterations   3.353e-03     <- STILL not converged
     2000             335       Converged       9.934e-09

2. THERMAL, the identical shape (the FIXED path)
        1               1       MaxIterations   1.833e+01
        2               2       Converged       3.143e-11     <- converged on the SECOND pass
        5               2       Converged       3.143e-11
     2000               2       Converged       3.143e-11

3. SPECIES, Dirichlet inlet + Neumann walls, WITH convection (a realistic case shape)
       31 outer iterations to converge; outer change history
       3.89e-01  1.92e-01  1.18e-01  8.48e-02  5.60e-02  3.50e-02  2.10e-02  1.23e-02 ...
       -- a clean geometric decay with ratio ~0.62, the signature of a lagged fixed point
```

```text
EXPECTED boundary state      dY/dn = 0 exactly, from one assembly (what thermal achieves)
STATE ACTUALLY USED          Y_P from the previous outer iterate, so dY/dn is only approached
ITERATION AT WHICH USED      every assembly, iterations 1..335
MEASURED DIFFERENCE          335 vs 2 outer iterations  ->  167x on pure Neumann
                             31 outer iterations on the mixed convective case
```

## 5. Why this is a defect and not merely "an expected lagged scheme"

§13 requires proving that the lag violates intended semantics before calling it a defect. Three
independent reasons, none of which relies on my opinion:

```text
1. THE PROJECT ALREADY RULED ON IT. The identical pattern in thermal was called a defect and fixed,
   with a regression test that fails on the pre-fix solver. Species was left as a recorded "known
   limitation", not as a design choice. The intended semantics are therefore the FIXED ones.

2. AN EXACT ASSEMBLY EXISTS AND IS USED ELSEWHERE IN THE SAME REPOSITORY, five lines long. This is
   not a trade-off between accuracy and cost -- the exact treatment is cheaper AND more accurate.
   There is no scheme-design argument for the lagged form.

3. IT CAN RETURN A NON-CONVERGED FIELD. At any cap below 335 the solver returns
   status = MaxIterations with a field whose boundary condition is not satisfied -- measured above
   at caps 1, 2, 5 and 20. The default cap is 2000, which leaves margin on this 12x8 mesh, but the
   iteration count grows with mesh refinement and the margin is not analysed anywhere. That is a
   latent correctness risk, not just a slow path.
```

```text
WHAT THE DEFECT IS NOT, stated so it is not overstated:
  * it does NOT return a wrong answer when it converges. Both solvers reach a field satisfying its
    own BC; the species BC residual is 0.000e+00 at convergence in every case measured.
  * it is NOT a flow/species lag. massFlux is fixed before species runs.
  * it is NOT a stale-cache bug. The field is re-read on every assembly.
  The defect is that convergence to the boundary condition is ITERATIVE where it should be DIRECT.
```

## 6. One flaw in the probe itself, recorded rather than trimmed

```text
The first version of the probe applied the gradient residual check to EVERY boundary face,
including the Dirichlet inlet in case 3. It reported a BC residual of 1.920e+01 there -- an
artefact of the metric, not of the solver, since a Dirichlet face has no prescribed gradient to
satisfy. Restricted to FixedGradient faces, case 3's residual is 0.000e+00.

Also worth stating: the "BC residual" column is 0 by construction whenever the face value is
reconstructed FROM the owner, because then the reconstruction satisfies the gradient identically.
It measures self-consistency of the RETURNED field, not whether the assembled flux used a stale
value. That is why the discriminating number is the OUTER ITERATION COUNT, and why the probe's
header says so before presenting any figure.
```

## 7. Recommendation for a later, authorized phase

```text
The fix is to give the species gradient-face branch the same treatment thermal already has:
coefficient 0 and an exact Df*g*d right-hand side. It is a small, local change to
SpeciesEquation.cpp with an obvious regression test (outer iterations must drop to ~2 on the
pure-Neumann probe above, and the pre-fix solver must fail that test).

BUT IT IS NOT P18.0's, and arguably not P18's at all: it is P12-era debt in the species module,
independent of multi-species or chemistry.
```

### 7.1 And migrating to P14 would NOT fix it — checked, not assumed

```text
The obvious P18 plan is to migrate species onto P14's richer ScalarTransport (see p14_reuse.md).
That would NOT inherit the thermal fix, because P14 HAS THE SAME LAG. All three assemblies, side
by side:

  thermal/EnergyEquation.cpp:104     return {0.0, coefficient * boundaryValue(0.0, distance)};
                                     coefficient 0, owner value 0     -> EXACT
  species/SpeciesEquation.cpp:80     yB   = boundaryValue(concentration[owner], distance);
                                     + non-zero diagonal at :86       -> LAGGED
  transport/ScalarTransport.cpp:129  phiB = boundaryValue(phi[ownerId], distance);
                                     + non-zero diagonal at :132      -> LAGGED

P14 routes through the same shared boundaryFaceDiffusionTerms with the same
prescribesBoundaryValue(bc.type()) flag, so a gradient face falls through to
twoPointTerms(gamma*A/d, 0.0) there too -- coefficient Df, and the lagged owner value on the RHS.
The two files are structurally identical on this point.

TWO OF THE THREE SCALAR ASSEMBLIES IN THIS REPOSITORY CARRY THE LAG. Only thermal was fixed.

A further note, because it changes who would notice: P14's ScalarTransport is ASSEMBLY ONLY -- there
is no ScalarTransport solver with a Picard loop. Its production consumer is P16 turbulence
(KEpsilonEquation.cpp's solveRelaxedScalarTransport), which performs a RELAXED SINGLE SOLVE per
outer SIMPLE iteration rather than iterating to boundary self-consistency. So in the turbulence
path the lag is absorbed into SIMPLE's own outer loop and never shows up as an iteration count --
which is very likely why it was never noticed there.

IMPLICATION FOR THE P18 PLAN: whichever stack P18.2 builds on, the gradient-face treatment must be
made exact DELIBERATELY. It cannot be inherited. This is recorded as gate G4's negative control.
```
