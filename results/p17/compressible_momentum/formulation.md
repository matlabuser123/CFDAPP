# P17.4 — the discrete momentum formulation

The exact discrete equation assembled, term by term, with the authoritative owner of each
quantity. Consolidates what the authorization suggested as `formulation.md`,
`transient_term.md`, `convective_flux.md`, `pressure_gradient.md`, `viscous_stress.md` and
`boundary_fluxes.md`.

---

## 1. What is assembled, in order

`assembleRelaxedCompressibleMomentumComponent`, once per velocity component per outer
iteration, from `CompressibleSIMPLE.cpp:265`. The order is part of the numerical result —
`SparseMatrixBuilder` accumulates and floating-point addition is not associative.

```text
1  assembleDiffusionContribution        mu lap(U_c)                 matrix + rhs
2  assembleConvectionContribution       the P17.3 massFlux          matrix + rhs
3  assemblePressureSourceContribution   rhs += -V_P grad(p)_c
4  assembleMomentumSourceContribution   rhs += V_P (rho f)_c        [optional, null default]
5  assembleDilatationalStressContribution  rhs += -(2/3) mu grad(div U)_c V_P
                                                                    [P17.4, DEFAULT OFF]
6  compressibleMomentumTimeDerivative   diag += rho_new V/dt
                                        rhs  += rho_old V/dt u_old
7  applyImplicitUnderRelaxation(alpha)
8  build; allFinite() on matrix and rhs, else NumericalError
```

`requireTwoDimensional` at entry — **2D only**, and every P17.4 claim is a 2D claim.

## 2. Transient — `d(rho U)/dt`, not `rho dU/dt`

```text
diagonal[P] = rho_new[P] V_P / dt          multiplies the unknown u_new
source[P]   = rho_old[P] V_P / dt u_old[P] known, to the RHS
```

i.e. `(rho_new u_new - rho_old u_old) V_P / dt` — the two densities kept **separate**. The
header records the trap: *"Do not simply use `rho_current*(u_new-u_old)*V/dt`… it is not
[consistent], for a genuinely changing density."*

Measured, with rho doubling from 1 to 2, `u_old = 3`, `dt = 0.25`, `V = 1`:

```text
diagonal  8    == rho_new V/dt              correct
source    12   == rho_old V/dt u_old        correct
the WRONG rho_new-weighted form would give   24   -- a factor of 2 apart
```

`TransientTermDistinguishesDRhoUDtFromRhoDUDt` asserts the correct values **and** that they
differ from the wrong form by more than 1.0, so the test cannot pass if the forms were
confused. `ConstantDensityMatchesIncompressibleFormula` (pre-existing) proves exact
degeneration to `implicitEulerTimeDerivative` at constant density.

**Used as pseudo-transient stabilisation, not time accuracy.** `pseudoTimeStep` is "a
numerical stabilization parameter, not a physical time step"
(`CompressibleSIMPLE.hpp:27`), and `densityOld == densityNew` at the call site. The *form*
is conservative; the *use* is dual-time relaxation. No transient compressible solver exists.

## 3. Convection — `div(rho U (x) U)` through the P17.3 mass flux

```text
assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component, ...)
```

The `massFlux` is **the** P17.3 authoritative field, `m_dot_f = rho_f (U_f . n_f) A_f`,
**passed in as an argument** — not recomputed. `CompressibleSIMPLE` computes one `massFlux`
and hands the same object to the momentum assembly and to the continuity evaluation, so no
second reconstruction can drift.

```text
UPWIND ONLY, first order. No higher-order scheme exists on this path, and the MMS order
gate is 1.0 in band [0.55, 1.30] accordingly.
```

## 4. Pressure — `-grad(p)`, cell gradient, scaled by volume

```text
gradP = discretization::gradient(mesh, pressure, pressureBoundaries, scheme)
rhs[P] += -V_P * gradP_component[P]
```

Measured against an analytical linear field `p = 100 + 7x`:

```text
deep-interior cells, worst relative error vs -V * dp/dx     0      EXACT
```

**Deep interior only, and why:** a boundary cell under zero-gradient gets a one-sided
Green-Gauss gradient, measured as `-2.333` against `-V·C = -7`. That is the BC's behaviour,
not an error in the pressure term, so the criterion is stated where the analytical value
applies. Attempt 1 of this gate had no deep-interior cells at all and the guard caught it —
see §8.

```text
This is the PHYSICAL pressure force, applied ONCE. It is distinct from the
pressure-correction machinery (assembleCompressiblePressureCorrection), which solves for p'
and corrects the velocity in a separate step. No double counting: momentum applies
-grad(p) V once per assembly; the correction is a different equation.
```

## 5. Viscous — the Laplacian, plus the dilatational term P17.4 added

```text
BEFORE P17.4   div(tau) = mu lap(U)  only
               measured: grad(div, dilatation, bulkViscosity, twoThirds, Stokes, divU
                         ALL 0 hits across include/ and src/
AFTER P17.4    the same Laplacian, plus, when enabled:
               rhs[P] += -(2/3) mu grad(div U)_component[P] V_P
```

For a Newtonian fluid under the Stokes hypothesis,

```text
tau = mu [grad(U) + grad(U)^T] - (2/3) mu (div U) I
```

The Laplacian covers the first part; `assembleDilatationalStressContribution` supplies
precisely the `-(2/3) mu grad(div U)` term and nothing else.

### Composed from two existing tested operators

```text
divU     = discretization::divergence(mesh, velocity, velocityBoundaries)
gradDivU = discretization::gradient(mesh, divU, <zero-gradient>, scheme)
```

No new discretisation is invented. The velocity BCs are the **same** ones the mass flux
uses, so the face velocities entering the divergence are those the rest of the solver sees.

### Verified against an analytical field

`U = (x^2/2, 0)` gives `div U = x` and `grad(div U) = (1, 0)` exactly:

```text
worst |div U - x|, INTERIOR cells           0        the divergence operator is EXACT there
worst |div U - x|, boundary-adjacent cells  10       the zero-gradient BC, see below
grad(div U).x at deep-interior cells        exactly 1
the contribution                            -(2/3) mu * 1 * V = -0.26666666666666666
                                            (mu = 0.1, V = 4 on the post-N2-fix mesh)
```

**CORRECTION.** An earlier draft of this section reported `worst |div U - x| = 0` without
qualification and §5 below said `div U` was "exact EVERYWHERE". That was **wrong**: the figure
had been measured over *interior* cells only, and the claim generalised it. Re-measured over
every cell (`logs/10_verify_values.log`), the boundary-adjacent layer is off by up to **10** on
this mesh, because the zero-gradient velocity BC sets the boundary face value to the owner
cell's and so halves the one-sided difference there. The corrected statement is below and the
overstated one is withdrawn. Nothing about the operator changes — only the accuracy of the
claim made about it.

```text
mu-scaling at a deep-interior cell (cell 22, centroid (5, 5)), measured:
  mu 0.00  ->  0                      expected -0
  mu 0.05  ->  -0.13333333333333333   expected -0.13333333333333333
  mu 0.10  ->  -0.26666666666666666   expected -0.26666666666666666
  mu 0.20  ->  -0.53333333333333333   expected -0.53333333333333333
```

### The boundary limitation — real, measured, and recorded

There is **no physical boundary condition for the derived scalar `div U`**, so zero-gradient
is used. Consequence, measured:

```text
div U            exact in the INTERIOR; WRONG in the boundary-adjacent layer, worst 10 on
                 the 10x10 over 20x20 mesh. An earlier draft of this row said "exact
                 EVERYWHERE" -- withdrawn; it contradicted the paragraph directly below it.
grad(div U)      exact only TWO OR MORE CELLS from a boundary
                 worst |grad(div U).x - 1|    all cells      2.5
                                              one cell in    2.5
                                              two cells in   0    <- the deep interior
```

The artificial BC makes `div U` wrong in the boundary-adjacent layer, and a Green-Gauss
gradient of a wrong neighbour pollutes one cell further in. **So the term's accuracy is
asserted in the deep interior only.** Asserting exactness globally would fail a correct
implementation — the same shape of honesty P17.3 needed for `rho(x)U(x) = const`.

### Default OFF, and why that is load-bearing

```text
bool includeDilatationalStress = false
```

```text
1. The compressible system MMS manufactures its forcing as
   "f = (m.grad)U + grad p - mu lap U" and names "mu/3 grad(div U) stress term" in its own
   not_covered field. Enabling the term unconditionally would make a PASSING, VALIDATED
   study fail for a reason that is not a defect.
2. CLAUDE.md §6 -- prefer a dedicated path; prove existing results unchanged. Default-off
   makes that provable by construction.
3. Precedent: nonOrthogonalCorrections{0} and robustness both default to "exactly the
   previous solver".
```

Control **N6** proves this is not decorative: making the default `true` disturbs the MMS and
low-Mach suites.

**Reachability, stated honestly:** the option is reachable from the library but **not yet
from a case file**, the same status `pseudoTimeStep` already has. Wiring it to the case
format is recorded as follow-on work, not claimed. A feature no case file can reach is not
shipped capability, and this evidence does not pretend otherwise.

## 6. Body force — `rho f`, and the weighting is correct by construction

```text
assembleMomentumSourceContribution(mesh, sourcePerUnitVolume, component, rhs)
  rhs[P] += cell.volume() * source_component[P]
```

The parameter is literally named `sourcePerUnitVolume`: the API takes a **force density**, so
a caller supplying `rho*f` gets the correct `rho f` weighting. Measured, `g = -9.81`:

```text
rho 1.0  ->  rhs -9.8100000000000005      == V rho g
rho 2.0  ->  rhs -19.620000000000001      exactly 2x
rho 4.0  ->  rhs -39.240000000000002      exactly 4x
```

```text
NO compressible body-force PHYSICS exists: bodyForce, gravity, buoyancy -> 0 hits on the
compressible path. momentumSource is used only by the MMS via setMomentumSource. P17.4
verifies the API contract and records the absence; §8 of the authorization says not to
expand buoyancy or gravity architecture.
```

## 7. Boundary momentum

The assemblers are polymorphic — no per-BC special case in the momentum path.

```text
BC            convective momentum      pressure force    viscous force
-----------------------------------------------------------------------------
Inlet         rho_f (U_in.n) A U_in     yes               yes
Outlet        rho_f (U_own.n) A U_own   yes               yes
Wall          ZERO (U_f.n = 0)          YES, transmitted  YES, shear transmitted
MovingWall    zero normal component     yes               yes, tangential
Symmetry      zero normal component     yes               zero normal
```

**An impermeable wall carries zero normal mass flux but still transmits pressure and viscous
shear** — the property §15 asks about. It follows from the structure: the convective term is
`m_dot_f * U_f` and `m_dot_f = 0` there, while the pressure and viscous terms are assembled
from the cell/face state independently of the mass flux.

### How much of that table is actually MEASURED — audited, and two rows are not

The table above was written from the structure of the assemblers. Auditing it against the tests
that exist gives a weaker and more honest picture:

```text
criterion                                      status
-------------------------------------------------------------------------------------------
B1  wall carries zero normal mass flux         MEASURED. tests/unit/physics/
                                               test_momentum_assembly.cpp asserts
                                               massFlux[faceId] == 0 on "top" and "bottom"
                                               to 1e-12. A pre-existing test, not P17.4's.
B1  wall still transmits pressure/viscous      STRUCTURAL ONLY. Argued from the assembler
                                               layout above; no test isolates it.
B2  inlet/outlet convective signs              PARTLY MEASURED. The same file checks that
                                               inlet and outlet mass flows balance globally;
                                               the per-face momentum SIGN is not asserted.
B3  Symmetry contributes zero normal flux       MEASURED, by a test this phase added after
                                               the audit found that NO momentum test used the
                                               Symmetry BC at all:
                                               SymmetryCarriesZeroNormalMomentumFlux.
```

**B3 was going to be reported `NOT VERIFIED`, and then was implemented instead.** The audit
first found it uncovered — Symmetry has its own unit test but nothing exercised it through a
flux or an assembly — and the initial judgement was that the property really belongs to the
**mass flux**, which P17.3 owns, so it sat outside P17.4's authorized scope. What settled it
the other way is §12 of the frozen gate: *"P17.4 closes only when F, D, C, M, L, B, V and G all
pass."* Leaving B3 unmeasured would have meant reporting P17.4 as NOT CLOSED. Measuring an
already-shipped BC through the momentum path is a smaller step than that, and it does not
implement anything new.

The test makes the **composite** claim, which is the only non-trivial form of it:

```text
Symmetry::boundaryValue returns U_P - n (U_P . n), so U_f . n = 0 exactly, so the solver's own
mass flux rho_f (U_f . n) A at that face must be EXACTLY zero.
  (1) calculateCompressibleMassFlux on a mesh with Symmetry on "top" gives exactly 0 there
  (2) therefore zeroing those entries leaves the assembled momentum system BITWISE identical
Asserting only (1) would re-test the BC. Asserting only (2) would be a tautology. Two
non-vacuity guards keep it honest: the mesh must HAVE symmetry faces, and the flux must exceed
1.0 somewhere, so "zero at the top" is evidence rather than an artefact of a dead field.
```

```text
NOT PRESENT in the codebase: mass-flow inlet and periodic BCs do not exist at all.
```

## 8. Continuity/momentum consistency — the §14 matrix

```text
quantity            continuity                     momentum                    consistent?
-------------------------------------------------------------------------------------------
cell density        P17.1 field, EOS(p_abs,T)      densityOld/densityNew,      YES, same field
                                                    the same field
face density        evaluateCompressibleFaceDensity reached only via massFlux   YES, one source
face velocity       interpolateFace(vel, BCs)      interpolateFace inside the  YES, same helper
                                                    convection assembler
mass flux           calculateCompressibleMassFlux  THE SAME SurfaceField,      YES, passed in
                                                    passed as an argument
boundary mass flux  EOS at the face's own p, T     the same entries            YES
corrected flux      correctFaceMassFlux after the  the next outer iteration    YES, by
                    pressure correction            re-assembles from it        construction
```

All six consistent. **Structural, not numerical:** `CompressibleSIMPLE` computes one
`massFlux` and passes it to both, so there is no second reconstruction that could drift. No
mismatch was found, so nothing had to be fixed or deferred.
