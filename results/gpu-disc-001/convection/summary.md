# GPU-DISC-001D — CUDA convection schemes

**Result: PASS. The operator gate is CLOSED.**

This phase ran in two parts. Part 1 qualified the scalar convection machinery and found that the
only production path selecting Central / LinearUpwind / QUICK was the momentum convection
contribution, which was out of scope at the time; the item was left unchecked. A follow-up
authorization permitted that specific path as a convection-only closure task, and part 2 delivered
it.

```text
PART 1  scalar operator qualification        1684 cases   0 failures   bitwise
PART 2  production momentum-convection       10352 cases  0 failures   bitwise
        qualification
TOTAL                                        12036 cases  0 failures

full regression   1998/1998 passed, 0 failed, 400.6 s
                  `ninja: no work to do` both BEFORE and AFTER ctest
                  source fingerprints recorded in regression_freshness.log
```

Part 2 assembles the convection term and **nothing else** — no diffusion, transient, source,
relaxation or pressure-gradient term. It is not momentum assembly, and no Momentum Path item is
checked.

```text
differential        1684 cases   0 failures   bitwise (memcmp)
  L1 primitives     21,023 scalar comparisons vs the production CPU functions
  L2 operators      4 schemes x 5 flux patterns x 4 fields x 7 meshes, forward and reversed
  L3 assembly       matrix diagonal, off-diagonal and RHS vs the production scalar assembler
non-vacuity         PASS
scheme coverage     upwind 280, central 280, linear_upwind 280, quick 420 operator cases
flux reversal       560/560 reversed pairs changed the result -- direction tests are not vacuous
negative controls   6 of 7 detected; the 7th is a provably NULL mutation (§4)
compute-sanitizer   4/4 clean, non-vacuous
device residency    0 D2H per operator evaluation
```

## 0. Part 2 — production momentum convection (the closure)

`differential/momentum_differential.log`

**CPU reference:** `cfd::physics::assembleConvectionContribution`, plus
`cfd::discretization::computeVelocityGradient` (LinearUpwind's dependency, compared directly).

```text
cases                10352      failures 0      bitwise (memcmp)
components           U 4480 | V 4480 | W 1280 (3D meshes only)
schemes              upwind 2560 | central 2560 | linear_upwind 2560 | quick 2560
vector BC forms      constant 21 | identity 14 | symmetry 9  (plans reaching each)
velocity gradient    112 cases, differing = 0
reversed-flux pairs  5120, of which 4320 changed the result
pattern mismatches   0
max absolute error   0      max relative error 0      tolerance: none used (bitwise)
```

Meshes: `cartesian2d 12`, `cartesian2d 24`, `graded2d 12`, `distorted q16`, `sheared 0.35`,
`cartesian3d 5`, `warped 3d 4`. Velocity fields: constant, linear, nonuniform (`sin·cos`), and a
**step**, so the TVD limiter is genuinely active. Fluxes: positive, negative, mixed-sign, a pattern
containing exact `+0.0` and `-0.0`, and a geometric swirl — each forward and reversed.

Boundary sets cover all five production vector conditions: all-Wall; MovingWall+Inlet;
Outlet+Symmetry; and an **all-five** set putting Wall, MovingWall, Inlet, Outlet and Symmetry on one
mesh at once.

Compared: sparsity pattern, matrix diagonal, off-diagonal coefficients, RHS, and the velocity
gradient. All exactly equal.

### Minimal vector-BC device support

`include/cfd/gpu/VectorBoundaryEncoding.hpp` — three forms, each verified bitwise against the
condition itself over nine probe vectors:

```text
constant   value = c                    Wall, MovingWall, Inlet
identity   value = u                    Outlet
symmetry   value = u - (n * dot(u, n))  Symmetry
```

The symmetry form is reproduced as the same guarded dot plus component-wise multiply-subtract, not
as a 3×3 matrix product — that rounds differently. A condition matching none of the three makes the
plan reject the mesh with a reason rather than approximate it.

This is deliberately the **minimum** the convection contribution needs. It is **not** general CUDA
boundary-condition support: `* [ ] Boundary conditions` stays unchecked, and this encoder is
written to be reused there.

### The gradient dependency was not the 001B gradient

LinearUpwind needs `computeVelocityGradient` → `greenGaussVelocityGradient`, a **different
operator** from the scalar gradient qualified in 001B: no P12-GRAD-002 boundary fit, vector
boundary conditions, and `gradW` only on a 3D mesh. It was therefore ported as well
(`computeVelocityGradientDevice`) and is compared directly — 112 cases, bitwise — rather than only
through LinearUpwind. `audit.md` §11.3 tabulates the differences.

### A real defect found by initcheck

The first sanitizer run reported **2972 initcheck errors**: on a 2D mesh the device allocated the
`gradW` buffers, no kernel wrote them, and the harness downloaded them. The fix went into the
**device code, not the harness** — `VelocityGradientField` leaves `gradW` *empty* in 2D, and the
device now mirrors that contract exactly. Re-run: 0 errors on all four tools.

## 1. Part 1 scope note — retained for the record

`audit.md` §1 inventories three convection paths:

| path | field | schemes | production callers |
| --- | --- | --- | --- |
| `physics::assembleConvectionContribution` | **vector**, per component | **all four** | SIMPLE, PISO, compressible, transient momentum |
| `thermal::assembleThermalConvectionContribution` (×2) | scalar | **Upwind only** | thermal, turbulence k/ε/ω, species |
| `discretization::convection()` | scalar | all four | **none** |

The brief asks for "every currently supported production scheme" **and** forbids momentum assembly
and boundary-condition integration. Those cannot both hold: the only production path that selects
between the four schemes is a function in `MomentumEquation.cpp` that takes a `VelocityComponent`
and resolves a `VectorBoundaryCondition`.

So this phase implemented and qualified everything that is unambiguously in scope, and left the
item unchecked — the brief's own provision:

> If a scheme is deliberately outside GPU-DISC-001 scope, document why and leave the TODO item
> unchecked unless the existing project scope explicitly permits partial support.

**Part 1 implemented and qualified**

* all four scheme primitives, compared directly against the production CPU functions;
* the complete four-scheme face-value machinery (upwind selection, far-upstream lookup, high-order
  value, Sweby/van Leer limiting, clamp, ghost boundary value, cell sum);
* the production **scalar** implicit convection assembly — diagonal, off-diagonals, RHS.

**Not implemented in part 1 — delivered in part 2 (§0)**

* `physics::assembleConvectionContribution` — the vector/momentum assembly.

## 2. Differential — the gate

`differential/differential.log`

### L1 — primitives against the production CPU functions

The functions `MomentumEquation.cpp` itself calls, compared 1:1 over a swept input space:

```text
quickFaceValue         10,000 comparisons   0 differing
linearUpwindFaceValue  10,000 comparisons   0 differing
smoothnessRatio         1,000 comparisons   0 differing   (100 exercise the nullopt branch)
vanLeerLimiter             23 comparisons   0 differing   (includes NaN/nullopt, +0.0, -0.0)
```

`std::optional<Real>` is represented on the device as a NaN sentinel. That needed care: `r <= 0.0`
is **false** for NaN, so collapsing the CPU's `has_value()` test into the sign test would let a
"no far-upstream cell" case through as a full high-order blend instead of degrading to upwind. The
device tests the sentinel explicitly first, and the 100 nullopt cases confirm the branch is live.

### L2 — the four schemes end to end

Meshes: `cartesian2d 16`, `cartesian2d 32`, `graded2d 16`, `distorted q16`, `sheared 0.35`,
`cartesian3d 6`, `warped 3d 5`. Fields: constant, linear, **step** (exercises the limiter), and a
manufactured `sin·cos` with a z ramp. Boundary sets: Dirichlet and mixed.

Flux patterns, all owner-oriented, each run **forward and reversed**:

```text
positive   uniform +0.7        negative   uniform -0.7
mixed      sign varies by face id
zeros      exact +0.0 AND -0.0 on selected faces, mixed signs elsewhere
swirl      u . Sf for a rotating velocity -- sign follows geometry, not indexing
```

Every case: `differing = 0`, `maxAbs = 0`, `maxRel = 0`, over results whose scale reaches ~2×10³.

### L3 — the production scalar assembly

Against `thermal::assembleThermalConvectionContribution`, **both** overloads: constant `cp` and
per-cell `cp` field. The per-cell overload takes `cp` at the **upwind** cell rather than
face-interpolating it — deliberately unlike diffusion — and that is reproduced.

Matrix diagonal, off-diagonals and RHS all `differing = 0`. Pattern handling differs from diffusion
here and the harness reflects it: convection genuinely can produce an exactly-zero coefficient (a
zero-flux face), which `SparseMatrixBuilder::build()` drops. So the comparison is
**CPU-entries-subset-of-GPU-entries**, with every extra GPU entry required to be exactly `0.0`.
0 pattern mismatches across all cases.

## 3. Flux-direction coverage

The brief asks for explicit reversal tests so an incorrect owner/neighbour choice cannot pass
accidentally. 560 forward/reversed pairs were run, and the harness **gates on the reversal actually
changing the answer**: `reversal actually changed result = 560`. A suite where reversal were a
no-op would fail rather than pass quietly.

`-0.0` is covered explicitly. The production test is `Ff >= 0.0`, which is **true** for `-0.0`, so a
negative zero selects the owner. Writing `> 0.0` or testing a sign bit would change the stencil.

## 4. Negative controls — 6 detected, 1 provably null

`negative-control/` — each: inject → build → check → restore → rebuild → sha256 → freshness →
re-pass.

| control | mutation | outcome |
| --- | --- | --- |
| NC2 | cell-sum flux sign reversed | detected, 1260/1684 fail, maxAbs 4.7e3 |
| NC3 | Central interpolation weights swapped | detected, 100/1684 fail |
| NC4 | assembly off-diagonal `A(n,o)` sign flipped | detected, 336/1684 fail, 480 matrix entries differ |
| NC5 | assembly inflow RHS term sign flipped | detected, 336/1684 fail, maxAbs 6.3e3 |
| NC6 | `-fmad=false` removed from the convection kernel | detected at L1, quickFaceValue 1317/10000 differ |
| NC7 | upwind/downwind swapped on every internal face | detected, 945/1684 fail |
| **NC1** | `Ff >= 0.0` → `Ff > 0.0` (zero flux picks the neighbour) | **NOT detected — and correctly so** |

### NC1 is a null mutation, not a gap

This was run first and came back undetected. The honest reading is not "the suite is weak" but
"the mutation changes nothing observable", and that is provable rather than assumed:

When `Ff` is exactly `±0.0`, the upwind choice selects a different `phiFace` — but every use of
that value is multiplied by the flux:

* operator: the contribution is `cellFlux * phiFace` with `cellFlux = ±0.0`, so it is `±0.0` for
  any finite `phiFace`, and adding `±0.0` to the running sum does not change it;
* assembly: `effectiveFlux = cp * (±0.0) = ±0.0`, so the mutation moves a `±0.0` between the
  diagonal and an off-diagonal (or into the RHS). Adding `±0.0` to an entry leaves it unchanged,
  and the CPU builder drops exactly-zero entries anyway.

So the `>=` vs `>` distinction at exactly-zero flux is **unobservable in the result**. NC1's
evidence is preserved as a recorded not-detected control with this reasoning, and NC7 was added as
the real owner/neighbour control — it swaps the upwind side for *every* face, including the
nonzero-flux ones, and is detected in 945 of 1684 cases.

The practical consequence, stated plainly: **the differential does not verify the `>=`/`>` boundary
of the upwind predicate**, because no observable depends on it. The code matches the CPU by
transcription, not by test.

## 4b. Part 2 negative controls — 6 detected, 1 provably null

`negative-control/` — each: inject → build → check → restore → rebuild → sha256 → freshness →
re-pass. All against the 10352-case momentum differential.

| control | mutation | outcome |
| --- | --- | --- |
| M1 | Central face value divides instead of multiplying by the reciprocal (the scalar/vector overload confusion) | detected, 544 cases, **maxAbs 1.6e-16** |
| M2 | Symmetry boundary keeps the normal component in x | detected, 904 cases |
| M3 | Outlet (identity) boundary returns the stored constant in x | detected, 1872 cases |
| M4 | deferred-correction RHS applied with the same sign to both rows | detected, 3840 cases |
| M5 | velocity-gradient cell sum divides instead of multiplying by 1/V | detected, 446 cases, maxAbs 1.78e-15 |
| M7 | the 3D skew correction transports w with grad(u) instead of grad(w) | detected, 96 cases |
| **M6** | 2D skew-corrected face value carries the interpolated z instead of `0.0` | **NOT detected — provably null** |

M1 is the one to note: confusing the scalar and vector `interpolateInternalFace` overloads — the
same class of defect NC1 caught in diffusion — shows up here at **1.6e-16**. No tolerance-based
comparison would see it.

**M6 is null, not a gap.** `greenGaussSumKernel` reads the face-velocity z component only inside
its `threeDimensional` branch, so on a 2D mesh that value is written and never read. The CPU's
exact `0.0` and any other value give identical results. Verified by reading the code path, not
assumed. M7 was added in its place to prove the 3D skew path is genuinely covered, and it is.

This is the second provably-null mutation in this phase (the first being NC1's `>=` vs `>`). Both
are recorded rather than replaced with a manufactured assertion, and both mark behaviour that is
matched by transcription rather than by test.

## 5. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck, racecheck: **0 errors** each, over
`--quick` (distorted q16 + warped 3d 5, 484 cases). Every run's output was checked to contain
`CONVECTION EQUIVALENCE: PASS`, so none was vacuous.

`-fmad=false` is scoped to exactly three translation units — the gradient, diffusion and convection
kernels — verified in the generated `build.ninja`.

## 6. How the verified CUDA gradient is reused

Only **LinearUpwind** needs a gradient, and only at the upwind cell. The device path mirrors that
exactly: `convectionDevice` calls `greenGaussGradientDevice` **only** when the scheme is
LinearUpwind, on the device-resident field, and indexes the result directly. Upwind, Central and
QUICK allocate and compute no gradient at all, as on the CPU. `DeviceConvectionPlan` embeds a
`DeviceGradientPlan` rather than rebuilding geometry.

## 7. Flux-sign convention (as reproduced)

```text
massFlux[faceId] is owner-oriented: along Sf, which points owner -> neighbour internally
                                    and outward on a boundary face.
internal:  Ff >= 0 -> owner upwind          Ff < 0 -> neighbour upwind
boundary:  Ff >= 0 -> outflow               Ff < 0 -> inflow
cell sum:  cellFlux = (face.owner() == cell) ? Ff : -Ff
result  :  sum / cell.volume()      (a DIVIDE -- the gradient's is a multiply by the reciprocal)
```

Boundary faces always use the ghost-mirror upwind value `(2.0 * phiB) - phiOwner` on inflow and
`phiOwner` on outflow, **regardless of scheme** — P12-NUM-001's explicit scope limit, reproduced.

## 8. Environment

```text
GPU     NVIDIA RTX 5000 Ada Generation Laptop GPU, 15352 MiB, compute capability 8.9
driver  580.97 | CUDA nvcc 12.9.86, architectures 80;89
host    g++ 11.4.0 (Ubuntu 22.04, WSL2), CMake 3.22.1
build   Release, -O3 -DNDEBUG, CFDAPP_ENABLE_CUDA=ON; the three operator kernels also -fmad=false
```

## 9. Files

```text
part 1 (scalar)
  include/cfd/gpu/DeviceConvection.hpp              plan, system, entry points
  cuda/kernels/DeviceConvectionPlan.cpp             host plan builder
  cuda/kernels/DeviceConvectionKernel.cu            kernels (-fmad=false)

part 2 (production momentum convection)
  include/cfd/gpu/VectorBoundaryEncoding.hpp        minimal vector-BC encoding, reusable by 001E
  include/cfd/gpu/DeviceMomentumConvection.hpp      plan, velocity gradient, entry points
  cuda/kernels/DeviceMomentumConvectionPlan.cpp     host plan builder
  cuda/kernels/DeviceMomentumConvectionKernel.cu    kernels (-fmad=false)

cuda/CMakeLists.txt                                 sources + the no-FMA property (4 kernels)
```

No CPU production file was modified by either part.

Evidence: `audit.md`, `differential/`, `negative-control/`, `cuda-diagnostics/`, `regression.log`,
`tools/`.

## 10. What is NOT claimed

* **No momentum assembly.** Part 2 assembles the convection contribution only. Diffusion,
  transient terms, momentum sources, relaxation and pressure-gradient forcing are untouched, and
  every Momentum Path TODO item stays unchecked.
* **Not general CUDA boundary-condition support.** Only the three vector forms the convection
  contribution needs are implemented. `* [ ] Boundary conditions` stays unchecked.
* Momentum response coefficients, Rhie–Chow, pressure correction and GPU-PIPE-001 residency: not
  started.
* The explicit `discretization::convection()` operator is used as the scalar harness that exercises
  all four schemes; it has no production caller of its own.
* No performance claim. Correctness was the gate.
* Two behaviours are matched by transcription rather than by test, both provably unobservable: the
  `>=`/`>` upwind predicate at exactly zero flux (§4), and the 2D skew-corrected face velocity's z
  component (§4b).
