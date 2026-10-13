# P17.1 — equation-consumer matrix

Does each equation consumer genuinely consume the **local, spatially varying** density?
Traced to code, then verified by test. A row is only `YES` if both hold.

---

## The matrix

```text
consumer     variable rho verified?   how rho enters                        evidence
-------------------------------------------------------------------------------------------
Continuity   YES                      via massFlux, which is rho_f-weighted RowVaryingDensity-
                                      per face                              ConservesMassAndScales-
                                                                            FluxPerRow  (new)
Momentum     YES                      densityOld/densityNew per cell in the CompressibleMomentumTest
                                      transient-storage term; massFlux in   (7), CompressibleRelaxed-
                                      the convective term                   Momentum (5), MMS
Energy       NO -- CONSTANT-DENSITY    via the INCOMPRESSIBLE massFlux,     ProjectRunner.cpp:531-537
             BY DESIGN                 built from FluidProperties'          then :544
                                       scalar density
Mass flux    YES                      rho_f per face, recomputed per call   InternalFaceUsesArithmetic-
                                                                            MeanDensity, FaceDensityIs-
                                                                            RecomputedPerCallNotCached
                                                                            (new), RowVarying… (new)
Boundaries   YES                      EOS at each boundary face's own       BoundaryDensityMatches-
                                      interpolated p_abs and T             DirectEosEvaluation,
                                                                            OutletBoundaryUsesEos-
                                                                            EvaluatedPressureNot-
                                                                            OwnerDensity
```

**Four of five rows are YES. The energy row is `NO`, and that is a design fact, not a
P17.1 failure** — see §3.

---

## 1. Continuity

```text
path        CompressibleSIMPLE.cpp:478  physics::evaluateContinuity(mesh, fluxNew)
            -> cellImbalance, globalNetFlux, totalAbsoluteImbalance
how rho     indirectly but completely: the evaluator sums face fluxes, and every face
enters      flux already carries its own rho_f. There is no separate density argument
            and there does not need to be.
verified    RowVaryingDensityConservesMassAndScalesFluxPerRow, which asserts BOTH:
              (a) per-cell imbalance is EXACTLY 0 with a non-uniform density
              (b) each row's x-normal flux equals that row's OWN rho * U * A
            (b) is the discriminating half. Conservation alone cannot detect a face
            density collapsed to one constant -- the fluxes would still cancel -- which
            control N4 confirms by breaking (b) while (a) survives.
NOT claimed the TRANSIENT form. evaluateCompressibleContinuity has no production caller
            (P17.0 finding, unchanged by P17.1). Only the steady constraint is in the
            production path.
```

## 2. Momentum

```text
path        CompressibleSIMPLE.cpp:265
            assembleRelaxedCompressibleMomentumComponent(..., densityOld, densityNew, ...)
how rho     TWO ways, both per cell:
enters        transient storage  rho_new u_new and rho_old u_old kept SEPARATE --
                                 (rho_new u_new - rho_old u_old) V/dt
              convection         physics::assembleConvectionContribution fed the
                                 compressible massFlux, so flux-based and telescoping
verified    CompressibleMomentumTest (7) including
            ConstantDensityMatchesIncompressibleFormula, which proves the two-density
            form degenerates exactly to implicitEulerTimeDerivative at constant density;
            CompressibleRelaxedMomentum (5); and the system MMS, whose manufactured
            state has rho in [0.95, 1.08] with div(rho U) = 0 exactly while div U != 0
            -- so a constant-density implementation cannot pass it.
NOT claimed the VISCOUS term. It is physics::assembleDiffusionContribution, the
            incompressible Laplacian mu lap U; the dilatational term
            -(2/3) mu grad(div U) is absent. That is a P17.4 item, recorded by P17.0 and
            explicitly out of P17.1 scope. It is a gap in the momentum EQUATION, not in
            its use of variable density.
```

## 3. Energy — `NO`, and why it stays `NO`

```text
ProjectRunner.cpp:531-537   massFlux = physics::calculateMassFlux(mesh, result.velocity,
                                                                 setup.fluid, ...)
                            setup.fluid is FluidProperties -- a SCALAR CONSTANT density
ProjectRunner.cpp:544       the THERMAL solve consumes *massFlux
ProjectRunner.cpp:756       the COMPRESSIBLE block runs AFTERWARDS with its own
                            variable-density compressibleMassFlux
```

So the production energy path advects with a **constant** density. Species (:677) and
multiphase (:717) consume the same constant-density flux.

```text
why not fixed in P17.1   Making energy consume the compressible flux IS two-way energy
                         coupling, which is P17.5. Changing it here would be
                         unauthorized scope and a silent physics change -- and the
                         authorization is explicit: an intentionally constant-density
                         path is to be DOCUMENTED, not quietly altered.
is it a defect?          No. It is the deliberate one-way coupling design, recorded at
                         SimulationSetup.hpp:59 and in the ROADMAP as "no two-way
                         energy coupling".
what P17.5 inherits      P15's library layer is ALREADY variable-density capable:
                         ThermalTimeState carries densityOld and densityNew as
                         ScalarField* for the conservative rho cp dT/dt term. That
                         transient path simply has no production caller yet;
                         ThermalSolver runs steady, where ThermalSolver.hpp:201-207
                         records that rho enters only through the mass flux.
                         So P17.5 extends a foundation rather than rewriting one.
reported as              CONSTANT-DENSITY BY DESIGN. Not a passed row, and the P17.1
                         checkbox for "energy" is marked [~] partial, not [x].
```

## 4. Mass flux

```text
path        calculateCompressibleMassFlux -> evaluateCompressibleFaceDensity
formula     m_dot_f = rho_f * dot(U_f, Sf),  Sf = face.areaVector() carrying normal AND
            area, so exactly rho_f (U_f . n_f) A_f
freshness   the face field is rebuilt from the current cell field on EVERY call; nothing
            caches it
verified    InternalFaceUsesArithmeticMeanDensity          exact mean, dyadic
            FaceDensityIsRecomputedPerCallNotCached  (new) staleness
            ReversingVelocityReversesMassFluxExactly (new) exact IEEE negation
            RowVarying…                              (new) per-row magnitude
            ValidUniformDensityIsUnaffectedByTheNew-
              Validation                             (new) the dry-run reference,
                                                           internal flux exactly 2
```

## 5. Boundaries

Covered in detail in `boundary_density.md`. Summary: boundary density is EOS-derived at
each face's own interpolated `(p_abs, T)`, so the face state and face density are
consistent by construction; a non-physical state is rejected by the EOS rather than
returned; and a wall carries exactly zero flux for any density, which is why the wall
test checks the invariance rather than a density effect.

## 6. What P17.1 added to this matrix

```text
before P17.1   the density precondition was an ACCIDENT of the callers: every field
               reaching the mass flux happened to be EOS-evaluated, so positive and
               finite, but nothing in the contract said so and nothing checked it. A
               negative cell density produced face rho_f = 0 and a face silently
               carrying no mass; a zero density produced a plausible non-zero flux.
               Both finite, so the downstream allFinite() sweep could not see either.
after P17.1    the precondition is stated in the header, enforced in the one validator
               both public entry points share, and covered by four negative tests plus
               five non-vacuity controls. The valid path is unchanged, asserted against
               the dry-run reference measured before the change.
```
