# Method of manufactured solutions (MMS)

P12-NUM-006. Analytical fields and forcing:
`tests/unit/discretization/ManufacturedFields.hpp` (namespace `mms`).
System studies: `tests/integration/mms/`. Error norms and pressure gauge:
`include/cfd/validation/ErrorNorms.hpp`. Study records and reports:
`include/cfd/validation/ManufacturedSolutionStudy.hpp`. Measured results:
`results/p12-num-006/summary.md`.

## The method

Choose a smooth exact solution (the *manufactured* solution) and substitute
it into the continuous governing equations. It does not satisfy them, but
the residual is a known, analytical function. Adding that residual to the
equations as a source term f (the *forcing*) makes the chosen fields their
exact solution. Solve the discretized, forced problem on a sequence of grids
and measure the error against the exact fields. It must decrease at the
order the discretization is designed for.

MMS needs no physical realism. It needs:
- fields smooth enough to show the asymptotic order;
- non-trivial enough to exercise every term (both velocity components,
  cross derivatives, non-zero boundary data);
- forcing derived **independently of the code under test**.

**The forcing is never built as `discrete_operator(exact_solution)`.**
Every forcing term in this repository is differentiated by hand from the
closed-form fields (product, chain and quotient rule) and sampled at cell
centroids. A forcing computed with the code's own discrete operators would
be satisfied exactly by the discrete solution, whatever errors those
operators contain. It would prove algebraic self-consistency, not
correctness. As an independent check of the hand derivations,
`ManufacturedFieldsTest.*` recomputes every derivative and forcing term
with 4th-order finite differences of the continuous functions. There is no
mesh, and agreement is to about 1e-7.

## Operator MMS vs system MMS

- **Operator MMS** (`test_grid_refinement.cpp`, P12-NUM-001 to 003)
  evaluates one discrete operator on the exact field (gradient, Laplacian,
  convection) and compares it with the exact derivative. That is a
  truncation-error check of one operator.
- **System MMS** (P12-NUM-006) runs a whole production solve: assembly of
  every term, boundary conditions, the linear solver, the non-linear
  (Picard / SIMPLE) iteration, and the pressure-velocity coupling. It
  compares the converged solution with the exact fields. It verifies what
  a user actually gets, including how the operators interact and the
  effect of the boundary treatment on the solution.

## The manufactured fields (unit square)

| field | closed form |
|---|---|
| streamfunction | ψ = e^{0.5x} sin(πx) · e^{−0.4y} sin(πy) / π |
| velocity | U = (∂ψ/∂y, −∂ψ/∂x): div U = 0 analytically; U·n = 0 on every wall; tangential wall velocity ≠ 0 |
| pressure | p = cos(πx) cos(πy) + xy/2: both gradient components non-zero; non-zero normal derivative on the walls |
| scalar | φ = sin(πx) sin(πy) + x²y/2 + 1/4, advected by U₀ + curl ψ, U₀ = (1, ½) (inflow left/bottom) |
| compressible | ρ = (P_ref + p)/(R T₀), ρU = curl ψ (so div(ρU) = 0), U = curl ψ / ρ |

The exponential factors break every symmetry of the square, so no
Taylor–Green-style cancellation can hide an error. Forcing:
- **Momentum:** f = ρ(U·∇)U + ∇p − μ∇²U (equal to the production
  div(ρUU) − μ∇²U form because div U = 0).
- **Scalar:** Q = ρ c_p U·∇φ − k∇²φ.
- **Compressible:** f = (m·∇)U + ∇p − μ∇²U, with m = ρU.

## Where the forcing enters (production code)

The forcing goes through generic source terms. Nothing in the production
code is specific to verification:
- `physics::assembleMomentumSourceContribution` adds V_P f_P, a prescribed
  body force per unit volume.
  - It is reached through `assembleRelaxedMomentumComponent`,
    `SIMPLE::setMomentumSource`, `assembleRelaxedCompressibleMomentumComponent`
    and `CompressibleSIMPLE::setMomentumSource`.
  - A null source leaves every assembly structurally unchanged.
- `thermal::assembleThermalSourceContribution(mesh, ScalarField, rhs)`
  adds a per-cell volumetric heat source Q_P V_P.
  - It is reached through `assembleEnergyEquation` and `ThermalSolver::solve`
    field overloads.
  - A uniform field reproduces the uniform-source overload bit for bit.

The exact Dirichlet data varies along each boundary, so every boundary face
gets its own patch (`perFaceBoundaryMesh`) with its own exact value:
- `FixedValue` for the scalar;
- `Inlet` for the velocity;
- `FixedGradient(∇p·n)` for the pressure, which gives exact Neumann data.

## Pressure gauge

Incompressible pressure is defined only up to a constant. SIMPLE fixes its
own gauge with a reference cell (p′ = 0 there). The comparison therefore
shifts **both** the numerical and the exact pressure to zero volume-weighted
mean over all cells before forming the error:
- `computeGaugeInvariantErrorNorms`;
- `removeVolumeWeightedMean`.

Adding any constant to either field leaves the error unchanged
(`MMSErrorNormsTest.PressureGaugeRemoval`).

For CompressibleSIMPLE in a closed box, the absolute pressure level is
physical: it sets the density through the EOS, and so the total mass. The
solver keeps its reference cell at its initial pressure. The study
therefore supplies the level as one scalar datum: a uniform initial
pressure equal to the exact value at the reference cell. It then compares
the pressure directly, and also modulo gauge.

## Error norms (one implementation: `ErrorNorms.hpp`)

Volume-weighted over the selected cells:

| norm | definition |
|---|---|
| L1 | Σ\|e\|V / ΣV |
| L2 | √(Σe²V / ΣV) |
| L∞ | max\|e\| |

- **Vectors:** each component and the pointwise magnitude of the error
  vector.
- **Face quantities** (face-flux errors): area-weighted, same definitions.
- **Masks:** `boundaryAdjacentCells(mesh, layers)` and `invertMask` split
  interior from boundary-ring error. That distinguishes the operator's
  interior order from a global order limited by the boundary treatment.
- **Rejection:** norms of non-finite fields are refused.

## Observed order

Orders come from the shared P12-NUM-005 analysis. `computeMMSOrder` applies
`analyzeGridConvergence` to each consecutive triplet of error values:
- The error E(h) is the "solution", with a grid-converged limit of 0.
- The report therefore also gives the extrapolated error, which must be
  ≈ 0 for a consistent scheme.
- The asymptotic-range check uses the scheme's formal order.
- The per-refinement reduction factors E_coarse/E_fine are reported as
  plain ratios.

There is no second order calculator. The expected order comes from the
exercised schemes:
- first-order upwind convection: 1;
- central or linear-upwind convection with second-order diffusion and
  gradients: 2.

Gates use the scheme's order class with the measured pre-asymptotic spread,
justified at each call site. They never demand exactly 2.

## Solve validity

An error, and so an order, is only ever taken from a valid solve:
- SIMPLE: `assessSimpleSolve` (status `Converged`, finite fields, global
  mass imbalance ≤ 1e-10);
- `ThermalStatus::Converged`;
- CompressibleSIMPLE `Converged`;
- the momentum Picard loop converged.

`computeMMSOrder` refuses a rejected level.

## Running

```bash
# unit level (fields, forcing, sources, norms, reports) -- seconds
build/debug/tests/unit/validation/CFDValidationUnitTests
# system level, default suite (run from the repository root: reports go to
# results/validation/mms/)
build/debug/tests/integration/mms/CFDMMSValidationTests
# the 16/32/64 SIMPLE study (~25 min Debug)
build/debug/tests/integration/mms/CFDMMSValidationTests \
  --gtest_also_run_disabled_tests --gtest_filter=SIMPLEMMS.DISABLED_FineGridStudy
```

Each study writes `results/validation/mms/<study>.json` (deterministic
except the separate `runtime` block) and `<study>.md`, and prints the
Markdown tables to the test log.

## Limitations

- **Scalar transport:** production scalar transport (thermal, species) has
  upwind convection only, so the scalar system MMS is first order. The
  P12-NUM-001 higher-order schemes are verified through the momentum
  equations.
- **SIMPLE iteration count** grows with refinement: implicit momentum
  under-relaxation makes smooth modes converge with a factor of
  1 − O(h²). The default-suite SIMPLE studies therefore use 8/16/32, where
  upwind is still pre-asymptotic. The 16/32/64 study is run explicitly.
- **Pressure gradient at the boundary:** the Green-Gauss reconstruction
  from Neumann data is first order in L∞ in the boundary ring. The global
  L2 order of the pressure-gradient term is about 1.5. The interior is
  second order, and the solution orders are unaffected.
- **Compressible scope:**
  - The CompressibleSIMPLE MMS covers an isothermal ideal gas: EOS-coupled
    continuity, compressible momentum and EOS consistency.
  - It does not cover the energy equation, which is out of scope, with
    temperature a fixed input.
  - Nor does it cover the μ/3 ∇(∇·U) stress term, which is not implemented;
    the forcing manufactures the implemented μ∇²U.
  - Nor higher-order compressible convection, which is upwind-only, nor
    high Mach numbers.
- **Mesh coverage:** meshes are 2D quadrilaterals, Cartesian or distorted
  (`DistortedMesh.hpp`). Exact boundary data needs one patch per boundary
  face.
