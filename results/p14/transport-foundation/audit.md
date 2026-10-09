# P14 — Generic Transport Foundation: Phase A audit

Measured on the tree that closed P13-007. Every number below is counted, not estimated.

---

## The premise this audit overturns

The brief asks for a reusable scalar transport framework "that future CFDApp physics modules can
use". The measurement says **CFDApp already solves scalar transport four times over** — and that is
the finding, because it changes the work from *building* a framework to *unifying* four existing
ones without disturbing any of them.

```text
scalar CONVECTION assemblers already in the tree     4
scalar DIFFUSION  assemblers already in the tree     5
scalar SOURCE     assemblers already in the tree     2  (+ turbulence's Su/Sp)
a generic transport layer (src/transport)            0  -- absent
```

The nine, by name:

```text
thermal::assembleThermalConvectionContribution          (x2 overloads)
thermal::assembleThermalDiffusionContribution           (x2 overloads)
thermal::assembleRegionAwareThermalDiffusionContribution
thermal::assembleThermalSourceContribution
species::assembleSpeciesConvectionContribution
species::assembleSpeciesDiffusionContribution
species::assembleSpeciesSourceContribution
turbulence::assembleScalarDiffusionContribution
multiphase::assembleVolumeFractionConvectionContribution
```

Each is the same finite-volume operation — upwind convection on a face mass flux, orthogonal
diffusion with a face-interpolated coefficient — written separately, with its own name and
signature. Their own headers say so: `SpeciesEquation.hpp` documents itself as following "the
convention `cfd::thermal::assembleEnergyEquation` already establishes", and
`VolumeFractionEquation.hpp` calls itself an "otherwise-identical contribution assembler".

**So P14 must not add a tenth.** The rule "do not create a parallel transport framework if suitable
abstractions already exist" is the governing constraint, and the risk is the opposite of the one
the brief anticipates.

---

## Existing reusable infrastructure

```text
mesh            cfd::mesh::Mesh -- cellVolumes, faceAreaVectors, owner/neighbour,
                boundary patches, multiblock, 2D and 3D, MeshQuality, ALE motion
fields          Field<T> -> ScalarField (cells), VectorField (cells), SurfaceField (faces)
face flux       SurfaceField F_f, already carrying rho: physics::calculateMassFlux,
                rhieChowMassFlux -- the exact quantity the generic convection term needs
discretization  Convection.hpp (upwind + scheme enum/parse), Diffusion.hpp, Laplacian.hpp,
                NonOrthogonalDiffusion.hpp, Gradient.hpp (Green-Gauss + least squares),
                Interpolation.hpp, Divergence.hpp
transient       discretization::implicitEulerTimeDerivative -- ALREADY EXISTS, returns
                {diagonal, source} exactly as Phase C describes, plus an ALE variant
                on a moving control volume
sources         Patankar Su + Sp*phi linearisation ALREADY EXISTS in
                turbulence::KEpsilonEquation: "a_P -= Sp_P * V_P", Sp <= 0 required
boundaries      cfd::boundary::BoundaryCondition -> ScalarBoundaryCondition base class
                already exists, with FixedValue, FixedGradient, FixedTemperature,
                HeatFlux, WallOmega, Symmetry, Inlet, Outlet; BoundaryConditionSet
linear algebra  SparseMatrixBuilder -> SparseMatrix (CSR), Vector, LinearSystem,
                CG / BiCGSTAB / GMRES, preconditioners, LinearSolverFactory with
                documented CPU fallback
GPU             18 device kernels: CSR SpMV, momentum assembly/convection, diffusion,
                face flux, gradients, boundary conditions; DeviceCsrMatrix,
                persistent residency (GPU-PIPE-001)
conventions     a_P phi_P + sum a_N phi_N = b_P, established identically by all nine
                assemblers above; CPU is the numerical reference; bitwise equivalence
                is the project standard, not tolerance-based agreement
```

## Missing infrastructure

```text
1. No generic assembler.      Nine module-specific ones; nothing composes the terms once.
2. No source-term API.        Su/Sp exists ONLY inside turbulence; thermal and species each
                              have their own separate source assembler; nothing generic.
3. Transient takes a SCALAR density.
                              implicitEulerTimeDerivative(mesh, phiOld, Real density, dt).
                              Phase E requires rho_P per cell. The ALE variant has the same
                              limitation.
4. No GPU scalar transport.   0 of 18 device kernels touch scalar transport
                              (all are momentum / pressure / flux / gradient / BC).
                              Phase I is genuinely new work, not a port.
5. No old-time state for a generic field.
                              VolumeFractionEquation is the only transport equation using the
                              time derivative at all; there is no reusable phi^n storage.
6. No transport-level conservation check.
                              Conservation is verified per physics module, not generically.
```

## Required architectural changes

```text
NEW   include/cfd/transport/ + src/transport/   -- the one generic layer
        ScalarTransportEquation   assemble(mesh, phi, rho, F_f, Gamma, sources, bcs, time)
        ScalarSource              {Su, Sp} per cell, composable
        TransportTimeState        phi^n, dt, scheme (Steady | Euler)
EXTEND discretization::implicitEulerTimeDerivative -- add a ScalarField-density overload.
        ADDITIVE: the existing Real-density signature stays, so no call site changes.
REUSE  Convection.hpp / Diffusion.hpp / NonOrthogonalDiffusion.hpp / Gradient.hpp verbatim.
        The generic assembler composes them; it does not reimplement them.
NEW    cuda/kernels/DeviceScalarTransport*.{cu,cpp}, following the existing
        DeviceMomentumAssemblyPlan pattern.
DO NOT migrate thermal / species / turbulence / multiphase onto the new layer in P14.
        That is P15-P19's work, and doing it here would touch the SIMPLE path.
```

## Files likely to change

```text
ADDED     include/cfd/transport/ScalarTransportEquation.hpp   the generic equation
          include/cfd/transport/ScalarSource.hpp              Su + Sp*phi
          include/cfd/transport/TransportTimeState.hpp        phi^n, dt, scheme
          src/transport/*.cpp
          cuda/kernels/DeviceScalarTransport{Kernel.cu,Plan.cpp}
          tests/unit/transport/*                              operator + sign tests
          tests/integration/transport/*                        the 5 verification cases
          docs/developer_guide/transport.md
MODIFIED  include/cfd/discretization/TimeDerivative.hpp       + ScalarField-density overload
          src/discretization/TimeDerivative.cpp
          src/CMakeLists.txt, tests/CMakeLists.txt, cuda/CMakeLists.txt
UNTOUCHED src/pressure_velocity/, src/thermal/, src/species/, src/turbulence/,
          src/multiphase/  -- deliberately. P14 adds a layer; it migrates nothing.
```

## Main numerical risks

```text
R1  A SECOND CONVENTION.  The highest risk by far. Nine assemblers already establish one
    sign and indexing convention; a generic layer that differs anywhere becomes a second
    truth in a codebase whose standard is bitwise equivalence.
    Mitigation: the generic assembler must reproduce an existing module's matrix BITWISE
    for the same inputs. That is a gate criterion, not a hope.
R2  Face interpolation of a variable Gamma. Harmonic vs linear at the face changes the
    answer for a discontinuous coefficient. CFDApp already has a choice made in
    thermal (region-aware interfaces); the generic layer must adopt it, not invent one.
R3  Sp sign. Patankar requires Sp <= 0 for diagonal dominance. KEpsilonEquation enforces
    this by CALLER contract, not in the assembler. A generic API used by less careful
    future callers needs the check where it cannot be skipped.
R4  Transient with variable rho. rho_P V_P/dt is trivially wrong if rho^n and rho^{n+1}
    are confused. Conservative form needs (rho phi)^n, not rho^{n+1} phi^n.
R5  Non-orthogonal correction is a deferred/explicit term; adding it to a transient
    equation without re-deriving the RHS silently changes the steady answer.
```

## Compatibility risks

```text
C1  SIMPLE must not change. Bitwise. The 10-case fingerprint from P13-001 is the instrument.
C2  The TimeDerivative overload must be purely additive -- every existing call site keeps
    the Real-density signature and its exact coefficients.
C3  GPU residency (GPU-PIPE-001) must not regress: a new scalar path must not force
    downloads in the momentum loop.
C4  Case format: a generic transport equation has no case-file representation yet, and
    P14 should not invent one before a physics module needs it.
C5  Build configurations: CUDA-OFF must compile the new GPU-adjacent code paths
    (the CPU-only blind spot recorded in memory).
C6  Clean-build warnings are currently 33, all -Wmissing-field-initializers, all
    pre-existing. New code must not add a different kind.
```
