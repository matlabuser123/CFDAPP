# GPU-DISC-001J — CUDA cell-velocity correction

**Result: PASS.** The production velocity correction assembles **bitwise identically** on CUDA —
1,021,332 values across 894 cases — and the second production gradient operator it can forward to,
the weighted least-squares gradient, is ported and qualified with it.

```text
direct cases             780   (3 pressure BC sets x 8 p' fields x 3 response fields
                                x 2 gradient schemes x 6 meshes, + the 2D-with-W-response branch)
reused-buffer cases       36   144 successive calls through ONE set of output buffers
isolated LS gradient      18   144 field cases
invariant cases           36
controlled p' cases       12   one real solved p' fed to BOTH correction paths
full chain cases          12   CPU assembly->solve->correct vs CUDA assembly->GPU solve->correct
values compared    1,021,332
bitwise-identical  1,021,332  (100%)
max absolute error         0
max relative error         0

observable controls       11
detected observable    11/11
documented null            4
source restoration     15/15 sha256 match, 15/15 re-passed
compute-sanitizer        4/4 clean, non-vacuous
GPU-DISC gates         11/11 green
full regression    1998/1998 passed, 0 failed, 1284.1 s (106 disabled of 2043 registered)
                   `ninja: no work to do` BEFORE and AFTER ctest; library sha256 identical
```

Face-flux correction was **not** started.

## 1. The exact CPU correction path

`correctVelocity` (`PressureCorrectionEquation.cpp:404`). Callers: `SIMPLE.cpp:621`,
`CompressibleSIMPLE.cpp:441` (both forwarding `settings_.gradientScheme`), `PISO.cpp:298`/`:330`
(default Green–Gauss).

```text
boundaries  = makeGradientBoundaries(mesh, pressureBoundaries)      (:43)
                  FixedValue(0.0)    where the PRESSURE patch is FixedValue
                  FixedGradient(0.0) on every other patch
gradPPrime  = cfd::discretization::gradient(mesh, p', boundaries, scheme)

wResponse == nullptr:   corrected = Vector2{ u* - (dU * dp'/dx),  v* - (dV * dp'/dy) }
wResponse != nullptr:   corrected = Vector3{ u* - (dU * dp'/dx),  v* - (dV * dp'/dy),
                                             w* - (dW * dp'/dz) }
```

Per component `u - (d * g)`: one rounded product then one rounded subtract, so the kernel is built
with `-fmad=false` like every kernel before it.

**Not in the equation**, checked rather than assumed: no density, no cell volume, no
under-relaxation, no clipping, no cross terms (dU multiplies only ∂p'/∂x, and so on), and **no
velocity boundary condition re-applied afterwards** — every caller assigns the returned field
directly; `allFinite` is a check, not a mutation. Wall/MovingWall/Inlet/Outlet/Symmetry therefore
enter this gate only through the momentum path that produced `u*` and `d`, which is where the
integrated layer exercises all five. No BC behaviour was invented.

### The 2D W contract

`Vector2` is `Vector3` (`Vector2.hpp:12`) with `Real z{}`, so `Vector2{x, y}` value-initialises z to
**exactly +0.0**: the 2D branch **discards** the predictor's z. The differential seeds a non-zero
predictor z on every 2D mesh (528 cases) so a port that preserved it cannot hide. The branch is on
the W-response **pointer**, not `mesh.dimension()` — a 2D mesh handed a W response legally takes the
3D branch, covered by 24 dedicated cases.

### Pressure reference

`correctVelocity` never receives `referenceCell`; the reference enters only through the p' field.
Since the gradient of a constant is zero, a uniform p' produces no correction regardless of it
(invariant I2).

## 2. The least-squares gradient — a scope finding

`correctVelocity` forwards `scheme`, and `SIMPLESettings::gradientScheme` is a **case-file option**:
`SolverConfigParser.cpp:283` accepts `"green_gauss"` and `"least_squares"`, parsed at
`CaseBuilder.cpp:312`. GPU-DISC-001B qualified **Green–Gauss only**, so the production
velocity-correction path had a second gradient operator that was not on the device. Per this task's
instruction to port the exact operator rather than assume equivalence, `leastSquaresGradient`
(`Gradient.cpp:361`) is ported here and qualified in isolation (layer L2), not only through the
correction.

```text
greenGaussFallback = gradient(mesh, field, boundaries)          // the WHOLE mesh, first

per cell, over cell.faceIds() ORDER:
  interior -> d = centroid(N) - centroid(P),          dphi = field[N] - field[P]
  boundary -> oblique (P12-MESH-001) ? d = unitNormal * normalDistance
                                     : d = centroid(face) - centroid(P)
              dphi = bc.boundaryValue(field[P], thatDistance) - field[P]

threeDimensional = ANY displacement has d.z != 0.0              // per CELL, not per mesh
weight = 1/|d|^2 ; |d|^2 == 0 is SKIPPED entirely
2x2: det = Sxx*Syy - Sxy*Sxy ; reject if !(scale>0) || det < 1e-10*scale^2
     gx = (bx*Syy - by*Sxy)/det        gy = (Sxx*by - Sxy*bx)/det
3x3: cofactors c11..c33 ; det = Sxx*c11 + Sxy*c12 + Sxz*c13
     reject if !(scale>0) || det < 1e-10*scale^3 ; g = adj(S) b / det
non-finite result also rejected (defensive backstop)
result = wellConditioned ? local : greenGaussFallback
```

### What is precomputed, and why that is legitimate here

Every **displacement** is pure geometry — the oblique ones (`unitNormal * normalDistance`) included
— so `S`, its cofactors, `det`, `scale`, the conditioning verdict and the per-cell 2D/3D verdict are
field-independent and are built once by the host plan, in `cell.faceIds()` order, with the CPU's own
expressions. `b` is **not** precomputable: it carries `dphi`, which depends on the field and, at a
boundary, on `boundaryValue(phi_P, d)`. It is accumulated on device in the same order with the same
zero-distance skip, using the 001E boundary encodings. The `isfinite` backstop is field-dependent
too, so `wellConditioned` is split: the host decides the det/scale part, the device re-checks
finiteness.

This is the mirror image of 001I's `decomposeAreaVector`, whose second argument *was*
field-dependent and so could not be precomputed. The distinguishing question is the argument's
dependence, not the function's shape.

### The 2D packed representation, and the 3D one

The plan stores one set of arrays for both dimensions:

```text
2D cell:  c11 = Syy    c12 = Sxy    c22 = Sxx    (c13, c23, c33 unused)
3D cell:  c11..c33 = the six cofactors of the symmetric 3x3 S
both:     det, plus cellThreeD_ and cellConditioned_ per cell
```

A shared layout like this can conceal an indexing error even when the mathematics is right, which is
exactly what controls **H9b** (2D packing read backwards) and **H9c** (3D adjugate row using the
wrong cofactor) exist to catch. Both are detected.

Two also-ran arrangements are deliberate: `weight * d` is stored already rounded, because the CPU
evaluates `weight * dx * dphi` as `((weight * dx) * dphi)`, so multiplying the stored inner product
by `dphi` on device is bitwise what the CPU computes — and the same rounded value built `S` on the
host.

## 3. Differential

`differential/differential.log`

Meshes: `cartesian2d 8`, `cartesian2d 16`, `graded2d 10`, `distorted q16`, `cartesian3d 4`,
`warped 3d 3`. Per mesh: 3 pressure BC sets (all-Neumann, mixed, all-FixedValue) × 8 p' fields
(zero, uniform, linear x/y/z, nonuniform, negative, extreme scale ~1e305) × 3 response fields (zero,
uniform, nonuniform) × 2 gradient schemes.

The **p' gradient**, the **per-component increment** and the **final U/V/W** are compared
independently, so a reversal localises to one link of the chain rather than merely failing.

```text
2D / 3D direct                    528 / 252
green_gauss / least_squares       390 / 390
isolated LS gradient cases        144
LS oblique-Neumann entries        177
LS three-dimensional cells        273
2D cases with a non-zero predictor z   528
2D cases taking the 3D branch          24
reused-buffer successive calls         144
```

### A harness gap the control design exposed

Every layer allocated fresh output buffers, but a real caller (SIMPLE's outer loop) reuses them
across iterations — so a port that reused a **stale** p' gradient instead of recomputing it would
have agreed with the CPU everywhere and still been wrong in the solver. Found while designing
control H6, and closed by layer **L1b**, which drives four successive p' fields through one set of
buffers. H6 is detected.

### Integrated pressure path

* **L4a (controlled, bitwise):** one real solved p' — from `CPU assembly → CPU BiCGSTAB` — fed to
  **both** correction paths. 12 cases, bitwise. This is the statement about the correction operator
  itself, independent of any solver difference.
* **L4b (full chain, solver-limited):** `CPU assembly → CPU solve → CPU correct` against
  `CUDA assembly → GPU BiCGSTAB → CUDA correct`. 12 cases. Every solve converged in 19 iterations
  with **0 restarts**, so the recorded GPU BiCGSTAB restart asymmetry did not fire and nothing had
  to be classified against it. `|Δp'| ≈ 7.2e-10` against a bound of 3.05e-4 derived from the solve,
  `|Δu| ≈ 6.8e-10`. This layer is about composition; L4a is the bitwise claim.

## 4. Invariants — and three of my own claims that were wrong

`differential/differential.log`, the `L3` lines. Each invariant is now stated **only where the CPU
makes it exactly true**, with a non-vacuity counter so it cannot quietly become empty.

| id | claim | where it is claimed |
| --- | --- | --- |
| I1 | p' = 0 ⇒ gradient exactly 0, x/y untouched bitwise | unconditional |
| I2 | uniform p' ⇒ no correction | only with no FixedValue pressure patch; for Green–Gauss also only where `sweepsNeeded() == false` and the cell's area vectors close exactly — **1,151 cells** |
| I3 | p' = a·x ⇒ v and w untouched bitwise | Cartesian mesh **and** no FixedValue pressure patch — **768 cells** |
| I4 | 2D corrected z is exactly +0.0, predictor z discarded | 2D meshes, with the predictor's z proven non-zero |
| I5 | the increment's sign matches the gradient's | wherever the increment is non-zero — **4,602 checks** |
| I6 | zero response ⇒ exactly no correction | unconditional, with the gradient proven live |

Three first drafts were wrong and were corrected rather than relaxed:

1. **I2 and I3 ignored `makeGradientBoundaries`.** A FixedValue *pressure* patch becomes
   `FixedValue(0.0)` for p', so a uniform p' has a genuine jump at that boundary and `p' = 3.5x` has
   a genuine y-gradient in the boundary cells. Claiming zero there would have been asserting
   something false. Both are now gated on `fixedValuePressurePatches() == 0`.
2. **I2 also ignored the Green–Gauss sweeps.** A skew/oblique/boundary-transfer sweep reintroduces
   the previous gradient, which is only ~0, so exactness needs `sweepsNeeded() == false` as well as
   exact closure. Both are proven properties read from the plan and the mesh, not assumptions.
3. **I5 was defeatable by underflow.** Stated as `corrected < predictor`, it failed when `d·g`
   underflows to exactly zero and leaves the velocity untouched — which is correct behaviour.
   Restated as sign agreement on the non-zero increment.

## 5. Negative controls

`negative-control/` — each: inject → build → run → restore → rebuild → sha256 → re-run.
**All 15 restored to a matching sha256 and re-passed the baseline differential**, and the four
mutated sources hash identically to the driver's recorded baseline afterwards, so no mutation
remains in the tree.

### Observable controls — 11/11 detected

| control | mutation | detected |
| --- | --- | --- |
| H1 | the pressure-gradient term added instead of subtracted | yes |
| H2 | the v correction uses the x gradient | yes |
| H3 | the v correction uses the U response coefficient | yes |
| H4 | the v correction drops the response coefficient | yes |
| H5 | the u correction applied twice | yes |
| H6 | the p' gradient not recomputed — a stale gradient reused across calls | yes (needs L1b) |
| H7 | `makeGradientBoundaries` skipped, the raw pressure set used for the p' gradient — the operator-level form of correcting with p instead of p' | yes |
| H8 | the 2D branch preserves the predictor's z instead of zeroing it | yes |
| H9b | the packed 2D layout read backwards (gx takes Sxx, gy takes Syy) | yes |
| H9c | the 3D adjugate's second row uses the wrong cofactor (k13 for k12) | yes |
| H11 | an oblique-Neumann displacement uses the face centroid instead of the foot of the normal | yes |

### Documented null controls — 4, each with a proof

These are **not** counted toward the detection rate. The driver treats a null control that *is*
detected as a failure, so each claim is checked rather than asserted.

| control | mutation | why it cannot be observed |
| --- | --- | --- |
| H9 | the 2×2 solve's factors reordered **within** each product | IEEE multiplication is commutative: `Sxx*by` and `by*Sxx` are bitwise identical, and the subtraction operands are unchanged. See §6. |
| H10 | the Green–Gauss fallback for an ill-conditioned cell dropped | The conditioning branch is unreachable on any mesh `MeshGeometry` can build: every cell has faces in at least two independent directions, so the displacement set is never colinear/coplanar. Observed directly — `LS cells taking the GG fallback = 0` across all six meshes. The `isfinite` backstop is the only other route, and a p' field at ~1e305 does not overflow the weighted products on these meshes either. |
| H12 | the zero-distance displacement skip removed | No displacement on these meshes has zero length — `skippedEntries = 0` on every plan built — so the skip never fires and removing it changes nothing. |
| H13 | a 2D cell's weight includes the z term | Every displacement of a 2D cell has `dz == 0.0`, so `dz*dz` is `+0.0` and `((dx*dx)+(dy*dy)) + 0.0` is exactly `(dx*dx)+(dy*dy)`. Exactly equal, not approximately. |

## 6. The h9 finding — a corrected assumption, recorded

The original H9 was written to prove that the CPU's asymmetric spelling of the 2×2 solve

```text
gx = (bx*Syy - by*Sxy)/det        gy = (Sxx*by - Sxy*bx)/det
```

was load-bearing, by reordering `gy` to `((by*sxx) - (bx*sxy))`. It came back **UNDETECTED**.

That was correct, and the assumption behind the control was wrong: **IEEE multiplication is
commutative**, so `sxx*by` and `by*sxx` produce identical bits, and the subtraction's operand order
was unchanged. The source's asymmetric spelling is cosmetic. A comment in
`DeviceLeastSquaresGradientKernel.cu` had claimed the opposite — that "making them symmetric changes
the rounding" — and that comment has been corrected.

The response was not to delete the control but to split it:

* **H9** is retained and reclassified as a **documented null control**, as the standing proof that
  operand order within these products carries no information.
* **H9b** and **H9c** replace it with the mutations that are genuinely meaningful for this code: the
  **packed 2D layout** read backwards, and the **3D adjugate row** using the wrong cofactor. Both
  are **detected**. The shared 2D/3D storage is precisely the kind of representation that can
  conceal an indexing error while the mathematics stays correct, which is what makes these the right
  controls.

Reported effectiveness therefore uses observable controls only: **11 observable, 11 detected, 4
documented null** — not "9/10", which would have counted a formally null mutation in the
denominator.

## 7. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck: **0 errors** each; racecheck: **0 hazards**.
Each log was verified to contain `VELOCITY CORRECTION EQUIVALENCE: PASS (bitwise)`, so none is
vacuous — the velocity-correction CUDA path actually executed under every tool.

initcheck in particular covers what the 001D `gradW` incident exposed: the 2D path still writes all
three corrected components (`cellCount` zeros into z rather than leaving the buffer unwritten,
because the CPU produces a full `VectorField`), the W response buffer is absent in 2D and present in
3D, and the least-squares per-entry and per-cell arrays are fully initialised by the plan before any
kernel reads them.

## 8. Files

```text
new
  include/cfd/gpu/DeviceVelocityCorrection.hpp        plan + the correction entry point
  include/cfd/gpu/DeviceLeastSquaresGradient.hpp      plan + the LS gradient entry point
  include/cfd/gpu/ObliqueNeumann.hpp                  the shared oblique-Neumann restatement
  cuda/kernels/DeviceVelocityCorrectionPlan.cpp       makeGradientBoundaries, restated
  cuda/kernels/DeviceVelocityCorrectionKernel.cu      the correction kernel (-fmad=false)
  cuda/kernels/DeviceLeastSquaresGradientPlan.cpp     normal equations, cofactors, conditioning
  cuda/kernels/DeviceLeastSquaresGradientKernel.cu    the LS solve (-fmad=false)
modified
  cuda/kernels/DeviceGradientPlan.cpp                 uses the shared ObliqueNeumann.hpp
  cuda/CMakeLists.txt                                 sources + the no-FMA property (11 kernels)
```

No CPU production file was modified. `obliqueNeumannFaceRestated` was **moved**, not copied, out of
`DeviceGradientPlan.cpp` so there is still exactly one restatement of `Gradient.cpp`'s file-static
predicate; 001B's 132-case bitwise gradient differential was re-run immediately after the move and
stayed green, and again in the final all-gates run.

## 9. Gate chain and regression

`all_gates.log`, `regression.log`, `regression_freshness.log`

```text
libcfdcuda.a  b29380a29a61f9d2178be2c07be293d1470a2e06a8d09ff7cf36b67c7835dee5
libcfdcore.a  eae75242473f6be813fc2926f07b14c877ce28bfe787441aca47119af4e2d16f

mesh                  96 checks     PASS        momentum assembly    27744 cases  PASS (bitwise)
gradients            132 cases      PASS        momentum response      260 cases  PASS (bitwise)
diffusion            528 cases      PASS        face flux             2628 cases  PASS (bitwise)
convection          1684 cases      PASS        pressure correction   4950 cases  PASS (bitwise)
momentum convection 10352 cases     PASS        velocity correction    894 cases  PASS (bitwise)
boundary conditions   75 cases      PASS
                                                             11/11 gates green

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998        (106 disabled of 2043 registered)
  Total Test time (real) = 1284.12 s
  `ninja: no work to do` BEFORE and AFTER ctest; library and 001J source sha256 identical on both sides
```

## 10. What is NOT started

Face-flux correction, integrated SIMPLE, GPU-PIPE-001 residency. Nothing committed or pushed.
