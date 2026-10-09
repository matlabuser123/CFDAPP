# P18.0 — species inventory

What CFDApp actually has today, traced from source rather than inferred. Sections map to the P18.0
authorization: §5 inventory, §7 transported quantity, §8 convection, §9 diffusion, §10 sources,
§11 boundaries, §14 multiple species, §15 bounds, §16 conservation.

**Headline: the existing species capability is a complete, production-reachable, passive
single-scalar transport stack — and it is NOT built on P14's generic transport.** There are already
two scalar-transport stacks in this repository, and the species one is the older and weaker.

---

## 1. Every species-related file (§5)

```text
PRODUCTION
  include/cfd/species/SpeciesProperties.hpp   src/species/SpeciesProperties.cpp
  include/cfd/species/SpeciesEquation.hpp     src/species/SpeciesEquation.cpp
  include/cfd/species/SpeciesSolver.hpp       src/species/SpeciesSolver.cpp

REACHED THROUGH (not species-owned, but on the species path)
  include/cfd/io/case/PhysicsConfig.hpp       src/io/case/PhysicsConfigParser.cpp   parse
  src/io/case/BoundaryConfigParser.cpp                                              parse BCs
  include/cfd/io/SimulationSetup.hpp          src/io/CaseBuilder.cpp                build
  include/cfd/app/ProjectRunner.hpp           src/app/ProjectRunner.cpp             dispatch
  include/cfd/io/CSVWriter.hpp                                                      export

CASES
  cases/species_diffusion/           full case: case/geometry/mesh/physics/solver/boundaries.json
  cases/heated_species_diffusion/    full case, species + thermal together

TESTS -- 78 TEST macros across 11 files, 92 registered ctest rows
  tests/unit/species/test_species_equation.cpp            21
  tests/unit/species/test_species_solver.cpp               8
  tests/unit/species/test_species_properties.cpp           6
  tests/unit/species/test_species_non_orthogonal.cpp       2
  tests/unit/io/test_species_case.cpp                     14
  tests/integration/species/test_species_advection_diffusion.cpp   4
  tests/integration/species/test_species_conservation.cpp          4
  tests/integration/species/test_species_diffusion.cpp             4
  tests/integration/species/test_species_solver_coupling.cpp       3
  tests/integration/case/test_species_production_case.cpp          7
  tests/integration/case/test_heated_species_diffusion_production_case.cpp  5

NOT SPECIES, despite the name overlap -- checked so the inventory is not inflated:
  include/cfd/transport/ScalarTransport.hpp   P14's GENERIC scalar transport. Species does NOT
                                              use it: 0 references from src/species or
                                              include/cfd/species. See p14_reuse.md.
  cuda/kernels/DeviceScalarTransportKernel.cu P14/P16's GPU scalar transport. Species does NOT
  include/cfd/gpu/DeviceScalarTransport.hpp   reach it: 0 GPU references from the species path.
```

## 2. The transported quantity, and the equation actually assembled (§7)

Traced from `src/species/SpeciesEquation.cpp` and stated by the header itself:

```text
ASSEMBLED:     div(rho U Y) - div(rho D grad Y) = S

  transported variable   Y          -- the concentration/mass-fraction-like scalar ITSELF,
                                       not rho*Y. Convection uses massFlux with COEFFICIENT 1
                                       (SpeciesEquation.hpp: "Y is the transported quantity
                                       directly -- unlike EnergyEquation's convection, which
                                       scales massFlux by specificHeat").
  diffusion coefficient  rho * D    -- density-weighted, so the convection and diffusion terms
                                       are dimensionally consistent with each other.
  rho                    from cfd::physics::FluidProperties::density(), a single CONSTANT.
```

```text
CLASSIFICATION, each item traced not assumed:

  steady / transient        STEADY ONLY. SpeciesEquation.hpp states it outright: "There is
                            deliberately no transient (d(rho Y)/dt) term here -- standalone steady
                            spatial assembly only". There is no dt, no phiOld, no time state
                            anywhere in the species headers.
  constant / variable rho   CONSTANT DENSITY. rho is FluidProperties::density(), one Real for the
                            whole domain. The header derives the assembled form from that
                            assumption explicitly.
  conservative              CONSERVATIVE IN FORM for constant rho -- the assembled operator is the
                            conservative equation's left-hand side multiplied through by the
                            constant rho. It is NOT the variable-density conservative form.
  single / multiple         ONE SPECIES PER SOLVE. SpeciesProperties holds one name + one
                            diffusivity. Multiple species are supported by CALLING solve() once
                            per species -- see §5 below.
  passive / reactive        PASSIVE, NON-REACTING. ProjectRunner: "this codebase's species are
                            passive/non-reacting -- no cross-species coupling term exists to
                            solve". There is no reaction term, no omega_i, no stoichiometry.
```

## 3. Convection (§8)

```text
API            assembleSpeciesConvectionContribution(mesh, massFlux, concentration, bcs,
                                                     builder, rhs)
mass flux      THE AUTHORITATIVE ONE. It takes a SurfaceField massFlux computed by
               physics::calculateMassFlux (rho * U.Sf) and passed in from ProjectRunner as the
               converged SIMPLE result. NO SEPARATE FLUX IS COMPUTED.
               -> NO DUPLICATED FLUX LOGIC. This is the one thing the species stack does share
                  with the rest of the solver, and it is the right thing.
scheme         FIRST-ORDER UPWIND ONLY. "Upwind-face-mass-flux convection contribution,
               coefficient 1". No higher-order or blended scheme is exposed; the P14 stack's
               scheme options are not reachable from here.
boundedness    not enforced by the scheme; upwind is monotone but no limiter or bound is applied.
CPU            yes.
GPU            NONE.
```

## 4. Diffusion (§9)

```text
MODEL ACTUALLY IMPLEMENTED:

    J = -rho * D * grad(Y)        with D a SINGLE CONSTANT Real per species

  SpeciesProperties carries exactly two members: a name and one `Real diffusivity_`.
  The assembler takes `Real diffusionCoefficient` = fluid.density() * species.diffusivity().

NOT PRESENT -- and this is stated so the capability is not over-read:
  species-dependent diffusivity across a mixture   NO
  mixture-averaged diffusivity                     NO
  a Schmidt-number model                           NO  (0 matches for Schmidt anywhere)
  temperature- or composition-dependent D          NO
  per-cell / face-interpolated D                   NO  (P14 HAS this; species does not use it)

Each species does carry its OWN D, so different species can diffuse at different rates -- but each
D is a scalar constant, so this is "several independent constant-D tracers", not multi-species
diffusion.

boundary diffusion   handled through the shared cfd::discretization::boundaryFaceDiffusionTerms,
                     including P12-DIFF-002's second-order one-sided reconstruction for prescribed
                     (Dirichlet) faces and P12-NUM-003's non-orthogonal correction (default off).
```

## 5. Source terms (§10)

```text
API        assembleSpeciesSourceContribution(mesh, Real volumetricSource, rhs)
form       rhs[cell] += S * V_cell       -- a UNIFORM scalar over the whole domain
explicit   YES, and only this
implicit   NO. There is no Sp term, no linearisation, no a_P modification.
per-cell   NO. One Real for the entire domain.
reaction   NO.

SUITABILITY FOR A FUTURE omega_i: NOT SUITABLE AS-IS. A reaction rate is per-cell and generally
stiff, so it needs both a per-cell Su and an implicit Sp to be stable. P14's ScalarSourceFields
already provides exactly that pair (Su and Sp fields, with `a_P -= Sp_P * V_P`), which is the
strongest single argument for migrating species onto P14 rather than extending this function.
```

## 6. Boundaries (§11)

```text
NO SPECIES-SPECIFIC BC CLASSES EXIST. The generic scalar conditions are used directly:
    cfd::boundary::FixedValue      Dirichlet -- a known inlet concentration
    cfd::boundary::FixedGradient   Neumann   -- FixedGradient(0) for an impermeable wall or a
                                               "let it leave" outflow

  The assemblers never branch on BC subtype; they only call the polymorphic
  ScalarBoundaryCondition::boundaryValue(ownerValue, normalDistance).

what value is stored     the BC object holds the prescribed value or gradient; the FACE value is
                         reconstructed per assembly from the owner cell
when it is evaluated     at EVERY assembly, from the concentration field passed into that assembly
current or lagged        LAGGED within one assembly -- see boundary_lag.md, where it is measured
how it enters            Dirichlet: boundaryValueCoefficient * y_b on the RHS plus a diagonal
conservation             conductance (and, with the second-order reconstruction, one far-cell
                         entry). Gradient: the same path, with y_b reconstructed FROM the owner.
CPU/GPU parity           N/A -- there is no GPU species path at all.

NOT PRESENT: symmetry, periodic and inlet/outlet as named species BC types. "Inlet" and "outlet"
are expressed with FixedValue / FixedGradient rather than dedicated classes.
```

## 7. Multiple species (§14)

```text
SUPPORTED, by repetition rather than by a collection type.

  PhysicsConfig holds a vector of species configs; CaseBuilder turns each into a SpeciesSetup;
  ProjectRunner loops:
      for (const SpeciesSetup& s : setup.species)
          speciesSolver.solve(mesh, s.initialConcentration, *massFlux, fluid, s.properties,
                              s.concentrationBoundaries);
  each independent of the others.

  stable IDs / names     names, from the case file; used as the export field suffix
                         ("concentration_<name>")
  number of species      whatever the case declares; no fixed limit, no minimum
  field ownership        one ScalarField per species, owned by its SpeciesSetup / SpeciesRunResult
  boundary configuration per species, its own BoundaryConditionSet
  initial conditions     per species, its own initialConcentration
  serialization          exported per species; NO restart of species state (see capability matrix)
  export                 one CSV column per species

  sum(Y_i) = 1      DOES NOT EXIST ANYWHERE. 0 matches for massFraction/sumY/normalisation in
                    src/ or include/. SpeciesProperties.hpp says so deliberately: "this class does
                    not itself enforce 0 <= Y <= 1 or sum(Y_i) = 1 -- those are properties of a
                    MASS-FRACTION MIXTURE convention, not of a generic constant-diffusivity passive
                    scalar". The species are independent tracers, not a closed mixture.
```

## 8. Bounds (§15)

```text
STATUS: CHECKED ON REQUEST, NEVER ENFORCED, NEVER CLAMPED.

  cfd::species::concentrationBounds(field) -> {minimum, maximum}
  SpeciesEquation.hpp: "Reporting-only boundedness diagnostic ... Never clips or modifies the
  field -- boundedness violation is a numerical-quality signal a caller reports, not something
  silently patched over."

  guaranteed   NO -- upwind convection is monotone but nothing bounds the solution of the coupled
               system, and no positivity-preserving argument is made anywhere.
  checked      YES, but only if a caller calls concentrationBounds and acts on it.
  clamped      NO -- and deliberately so. This is the right default: clipping is not
               conservation-preserving and the header refuses to do it silently.
  0 <= Y <= 1  NOT a property of the type. Y is a generic passive scalar here.
```

## 9. Conservation (§16)

```text
THE BALANCE THE CURRENT IMPLEMENTATION CAN EXPRESS is the steady, constant-density, non-reacting
reduction of the general species balance:

    general      d/dt INT rho Y_i dV + OINT rho Y_i U.n dA + OINT J_i.n dA - INT omega_i dV = 0

    implemented                       OINT rho Y_i U.n dA + OINT J_i.n dA - INT S dV = 0
                 |________________|                                          |______|
                   NOT EXPRESSIBLE                                        uniform S only
                   (no transient term)

  term by term:
    d/dt INT rho Y dV    NOT EXPRESSIBLE -- there is no transient term in the assembly at all
    OINT rho Y U.n dA    EXPRESSIBLE -- convection uses the authoritative massFlux
    OINT J.n dA          EXPRESSIBLE -- rho*D*grad(Y) through the shared boundary diffusion terms
    INT omega_i dV       NOT EXPRESSIBLE as a reaction; only a uniform constant S
    sum over species     NOT EXPRESSIBLE -- species are independent; no mixture closure

DIAGNOSTICS THAT EXIST: tests/integration/species/test_species_conservation.cpp (4 tests) measures
a global species balance for the steady case. There is no production-facing conservation report
equivalent to the compressible solver's, and no per-boundary species flux accounting API.
```
