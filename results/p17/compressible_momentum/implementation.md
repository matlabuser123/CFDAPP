# P17.4 — what was implemented

The exact production change, with the boundary between new numerics and reused, already
qualified machinery drawn explicitly.

---

## 1. The delta, measured against the PRE-P17.4 sources

Not against `HEAD`. `HEAD` is `3bd32f91` of 2026-09-21 and **286 files are uncommitted**
against it, so a `git diff HEAD` charges this phase for P13-001, thermal, P16 and GPU work it
never touched. The pre-P17.4 text of the two editable files was reconstructed by
reverse-applying the recorded edits and then **proven against the SHA-256 the freeze recorded
before any P17.4 code existed** (`tools/reconstruct_pre_p17_4.py`); a reconstruction that does
not hash to the frozen value is discarded rather than used.

```text
file                                                      P17.4     vs HEAD      note
-------------------------------------------------------------------------------------------
include/cfd/compressible/DilatationalStress.hpp           +72 new   untracked    new file
src/compressible/DilatationalStress.cpp                   +65 new   untracked    new file
include/cfd/compressible/CompressibleRelaxedMomentum.hpp  +19 -1    +19 -1       agrees
src/compressible/CompressibleRelaxedMomentum.cpp          +11 -1    +11 -1       agrees
tests/unit/compressible/test_compressible_momentum.cpp    +376 -0   +376 -0      agrees
src/CMakeLists.txt                                        +1 -0     +32 -0       ONE line is mine
-------------------------------------------------------------------------------------------
src/physics/MomentumEquation.{cpp,hpp}                    0         +14-1/+13-2  UNTOUCHED,
                                                                                 by SHA-256
```

Reconstruction proof — the recovered pre-P17.4 files hash to the frozen values exactly:

```text
5d97510fbac1f38c2dd0b5051a3cdccd22b3d960e9b3187e9a14444cc22a0cc5  CompressibleRelaxedMomentum.hpp
f44c8da4175f228f2b67bc225728e621d00534b2acc3f34cb028bccccbf57a4e  CompressibleRelaxedMomentum.cpp
```

The last two rows are the ones the vs-HEAD figure got wrong, and they are wrong the same way:
earlier uncommitted phases had also edited those files. See `AMENDMENT_L5_instrument.md`.

## 2. The one new operator

`assembleDilatationalStressContribution` — the `-(2/3) mu grad(div U)` part of the Newtonian
compressible stress under the Stokes hypothesis. It is a **composition of two existing,
already qualified operators**, not a new discretisation:

```cpp
const ScalarField divergenceU = discretization::divergence(mesh, velocity, velocityBoundaries);
const BoundaryConditionSet divergenceBoundaries = zeroGradientForDivergence(mesh);
const VectorField gradientOfDivergence =
    discretization::gradient(mesh, divergenceU, divergenceBoundaries, scheme);

constexpr Real kTwoThirds = 2.0 / 3.0;
for (const auto& cell : mesh.cells()) {
  const Real gradComponent =
      physics::velocityComponentValue(gradientOfDivergence[cell.id()], component);
  rhs[cell.id()] += -kTwoThirds * dynamicViscosity * gradComponent * cell.volume();
}
```

It writes to the **RHS only** — an explicit source. It does not alter the matrix or the
diagonal, and therefore does not disturb the relaxation or the pressure-correction coupling,
both of which read the diagonal.

Preconditions, all throwing `InvalidArgumentError`: `velocity.size()` and `rhs.size()` against
the cell count, and `dynamicViscosity` finite and `>= 0`. **Zero viscosity is admissible, not
an error** — an inviscid call must return a zero contribution rather than throw, which
`ScalesLinearlyWithViscosityAndVanishesAtZeroViscosity` pins.

The velocity boundary conditions handed to `divergence` are **the same set the mass flux
uses**, so the face velocities entering `div U` are the ones the rest of the solver sees. No
second velocity reconstruction is introduced.

## 3. The wiring, and why it is one line behind a default

```cpp
if (includeDilatationalStress) {
  assembleDilatationalStressContribution(mesh, velocity, velocityBoundaries, dynamicViscosity,
                                         component, rhs, nonOrthogonal.gradientScheme);
}
```

Placed after the pressure and body-force sources and **before** the transient term and
relaxation, so it participates in relaxation exactly as the other sources do. The gradient
scheme comes from `nonOrthogonal.gradientScheme` rather than being hard-coded, so the
configured scheme drives this term as it already drives the pressure source.

```text
bool includeDilatationalStress = false      <- the header default
```

Default OFF, which is load-bearing rather than tidy: the compressible system MMS manufactures
its forcing for `mu lap U` only and names `mu/3 grad(div U)` in its own `not_covered` field.
The default is pinned **behaviourally**, not by grepping the header — see
`DefaultIsDisabledBehaviourally` and `AMENDMENT_N2_N6_vacuous.md` §2, where control N6 showed
that a header grep cannot detect the default changing.

## 4. What was reused unmodified, and proved so

```text
physics::assembleDiffusionContribution         mu lap(U)        UNTOUCHED, SHA-256 verified
physics::assembleConvectionContribution        the P17.3 flux   UNTOUCHED
physics::assemblePressureSourceContribution    -V grad(p)       UNTOUCHED
physics::assembleMomentumSourceContribution    V (rho f)        UNTOUCHED
compressible::compressibleMomentumTimeDerivative                UNTOUCHED
pressure_velocity::applyImplicitUnderRelaxation                 UNTOUCHED
discretization::divergence, discretization::gradient            UNTOUCHED, reused as-is
```

P17.4 added one operator and one guarded call. Every other term in the momentum equation is
the same code, byte for byte, that the P13 qualification signed off.

## 5. Build wiring

One line in `src/CMakeLists.txt`:

```cmake
compressible/DilatationalStress.cpp
```

placed in the **main source list**, before the `if(NOT CFDAPP_ENABLE_CUDA)` block, so it
compiles in both the CUDA and the CPU-only configuration. The placement is deliberate: a file
added inside that block is compiled by no routine gate, a blind spot this project has already
been bitten by once.

## 6. What was NOT implemented, stated rather than implied

```text
NOT DONE   a GPU path for compressible momentum. None exists to extend -- G8 probes cuda/ and
           include/cfd/gpu/ for compressibleMomentum, CompressibleRelaxed, dilatational and
           DilatationalStress and finds 0 hits on all four. Inventing one to tick a
           CPU/GPU-equivalence box was explicitly out of scope.
NOT DONE   case-file reachability. includeDilatationalStress is reachable from the library but
           has no case-format key, the same status pseudoTimeStep already has. A feature no
           case file can reach is not shipped capability and is not claimed as such.
NOT DONE   the full tensor viscous stress. The Laplacian plus this dilatational term is what
           exists; mu[grad U + grad U^T] is not assembled as a tensor.
NOT DONE   re-deriving the MMS forcing to include the new term. That is follow-on work with
           its own evidence burden, and it is why the default is off.
NOT DONE   compressible body-force physics. The API contract is verified; no gravity or
           buoyancy exists on the compressible path and none was added.
```

## 7. Known limitation, measured

`div U` is a derived scalar with **no physical boundary condition**, so zero-gradient is used
for its gradient. The consequence is measured, not assumed:

```text
div U         exact EVERYWHERE
grad(div U)   exact only TWO OR MORE cells from a boundary
```

The artificial BC makes `div U` wrong in the boundary-adjacent layer, and a Green-Gauss
gradient of a wrong neighbour pollutes one cell further in. The term's accuracy is therefore
asserted in the **deep interior only**. Asserting it globally would fail a correct
implementation — the same shape of honesty P17.3 needed for `rho(x)U(x) = const`.
