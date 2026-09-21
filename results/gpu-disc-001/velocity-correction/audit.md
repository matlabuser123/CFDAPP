# GPU-DISC-001J Phase A — CPU velocity-correction audit

Written before any CUDA. The CPU implementation is the specification.

## 1. The functions

```text
correctVelocity                      PressureCorrectionEquation.cpp:404
makeGradientBoundaries               PressureCorrectionEquation.cpp:43
requireWResponse                     PressureCorrectionEquation.cpp:132
cfd::discretization::gradient        Gradient.cpp:598        (the scheme dispatch)
  greenGaussGradient                 Gradient.cpp (already ported, GPU-DISC-001B)
  leastSquaresGradient               Gradient.cpp:361        (NOT yet on device)
    solveLeastSquaresGradient        Gradient.cpp:297        2x2 normal equations
    solveLeastSquaresGradient3D      Gradient.cpp:240        3x3 normal equations
  obliqueNeumannFace                 Gradient.cpp:190
```

**Callers:**

| caller | scheme passed | W response passed |
| --- | --- | --- |
| `SIMPLE.cpp:621` | `settings_.gradientScheme` | `dWPointer` (3D) |
| `CompressibleSIMPLE.cpp:441` | `settings_.gradientScheme` | none |
| `PISO.cpp:298`, `:330` | default (`GreenGauss`) | none |

## 2. The exact equation

```text
boundaries  = makeGradientBoundaries(mesh, pressureBoundaries)
                  FixedValue(0.0)     where the PRESSURE patch is FixedValue
                  FixedGradient(0.0)  on every other patch
gradPPrime  = cfd::discretization::gradient(mesh, pressureCorrection, boundaries, scheme)

wResponseCoefficient == nullptr:                      (the 2D branch)
    corrected[P] = Vector2{ u*[P] - (dU[P] * gradPPrime[P].x),
                            v*[P] - (dV[P] * gradPPrime[P].y) }

wResponseCoefficient != nullptr:                      (the 3D branch)
    corrected[P] = Vector3{ u*[P] - (dU[P] * gradPPrime[P].x),
                            v*[P] - (dV[P] * gradPPrime[P].y),
                            w*[P] - (dW[P] * gradPPrime[P].z) }
```

Per component: `u - (d * g)` — a rounded multiply then a rounded subtract, so the port needs
`-fmad=false` like every kernel before it.

### What is NOT in the equation

Checked explicitly because the brief asks, and because assuming any of them would be a silent
defect:

* **no density** — `correctVelocity` never sees `FluidProperties`;
* **no cell volume** — the volume already sits inside `d = V/aP` from 001G;
* **no under-relaxation** — the velocity relaxation is inside the momentum assembly; the *pressure*
  relaxation is applied by the caller to `pressureNew`, never to `p'` before this call;
* **no clipping, limiting or safeguard** of any kind;
* **no cross terms** — `dU` multiplies only `∂p'/∂x`, `dV` only `∂p'/∂y`, `dW` only `∂p'/∂z`.

## 3. Sign convention, traced

```text
p'            the pressure correction solved from the assembly gate (001I)
grad(p')      evaluated with p' boundary conditions, NOT pressure boundary conditions
d = V/aP      the momentum response coefficient (001G), positive by MomentumEquation's guarantee
correction    -(d * grad p')          SUBTRACTED from the predictor
u = u* - d ∂p'/∂x
```

A positive `∂p'/∂x` therefore *decreases* `u`. Reversing any single link — the gradient sign, the
response sign, or the subtraction — changes the result, and the differential compares the gradient,
the per-component increment and the final velocity **separately** so the reversal localises.

## 4. The 2D W contract — the sharpest trap here

`Vector2` is an alias of `Vector3` (`Vector2.hpp:12`), and `Vector3` is an aggregate with
`Real x{}; Real y{}; Real z{};`. So `Vector2{a, b}` value-initialises `z` to **exactly `+0.0`**.

In the 2D branch the corrected velocity's z is therefore **forced to zero, discarding
`predictorVelocity[id].z`** — it is not "left alone" and it is not "copied". A port that preserves
the predictor's z is wrong on any 2D case whose predictor carries a non-zero z, and invisible on
every case whose predictor already has z == 0. The differential seeds a **non-zero predictor z on
2D meshes** specifically so this is observable, and negative control **H8** copies the predictor z
instead of zeroing it.

Note also that the branch is on the **W-response pointer**, not on `mesh.dimension()`.
`requireWResponse` only rejects *null on a 3D mesh* and a size mismatch; a 2D mesh handed a
non-null W response legally takes the 3D branch. The device reproduces the pointer test, not a
dimension test.

## 5. Boundary behaviour — nothing is re-applied

Audited at every call site. After `correctVelocity` returns:

* `SIMPLE.cpp:627` checks `allFinite(velocityNew)` and later assigns `velocity = velocityNew`;
* `PISO.cpp:298/330` chain `predictorVelocity → u1 → u2` directly;
* `CompressibleSIMPLE.cpp:441` assigns `velocityNew` and proceeds to the EOS update.

**No velocity boundary condition is re-applied, enforced or clipped after the correction.**
`allFinite` is a check, not a mutation. So Wall / MovingWall / Inlet / Outlet / Symmetry affect this
gate only through the *momentum* path that produced `u*` and `d` — never through the correction
operator itself. The differential still runs all five velocity BC types through the integrated
chain, because that is where they legitimately enter; inventing a post-correction BC pass would be
reproducing behaviour the CPU does not have.

The **pressure** boundary conditions do matter, but only via `makeGradientBoundaries`: a
`FixedValue` pressure patch becomes `FixedValue(0.0)` for p', everything else
`FixedGradient(0.0)`.

## 6. Pressure reference

`correctVelocity` never receives `referenceCell`. The reference enters only through the p' field it
is handed: with `pinReferenceCell` active (001I), `p'[ref] == 0`, which shifts the whole field and
so does change `grad(p')` near that cell. Since the gradient of a constant is zero, a *uniform* p'
produces exactly zero correction regardless of the reference — which is invariant I2 below.

## 7. The gradient operator — a scope finding

`correctVelocity` forwards `scheme`, and `SIMPLESettings::gradientScheme` is a **case-file option**:
`SolverConfigParser.cpp:283` accepts `"green_gauss"` and `"least_squares"`, and
`CaseBuilder.cpp:312` parses it into the setting SIMPLE passes here.

GPU-DISC-001B qualified **Green–Gauss only**. So the production velocity-correction path has a
second gradient operator that is not on the device. Per this task's own instruction — *"If the CPU
velocity-correction path uses a gradient operator that differs from the already-qualified scalar
Green–Gauss path, audit and port the exact operator rather than assuming equivalence"* — the
least-squares scalar gradient is ported here rather than assumed equivalent or excluded.

### `leastSquaresGradient`, exactly

```text
greenGaussFallback = gradient(mesh, field, boundaries)        // the WHOLE mesh, computed first

per cell P, over cell.faceIds() ORDER:
  interior face -> displacement = centroid(N) - centroid(P)
                   valueDifference = field[N] - field[P]
  boundary face -> bc = the scalar condition for that face
                   if obliqueNeumannFace(mesh, face, boundaries).applies:
                        displacement    = unitNormal * normalDistance
                        valueDifference = bc.boundaryValue(field[P], normalDistance) - field[P]
                   else:
                        distance        = |centroid(face) - centroid(P)|
                        displacement    = centroid(face) - centroid(P)
                        valueDifference = bc.boundaryValue(field[P], distance) - field[P]

solveLeastSquaresGradient(displacements, valueDifferences):
  threeDimensional = ANY displacement has d.z != 0.0        // per CELL, not per mesh
  weight = 1/|d|^2 ; a displacement with |d|^2 == 0 is SKIPPED entirely
  2x2:  Sxx,Sxy,Syy,bx,by ; det = Sxx*Syy - Sxy*Sxy ; scale = Sxx + Syy
        reject if !(scale > 0) || det < 1e-10*scale*scale
        gx = (bx*Syy - by*Sxy)/det        gy = (Sxx*by - Sxy*bx)/det
  3x3:  cofactors c11..c33 ; det = Sxx*c11 + Sxy*c12 + Sxz*c13 ; scale = Sxx+Syy+Szz
        reject if !(scale > 0) || det < 1e-10*scale*scale*scale
        gx = (c11*bx + c12*by + c13*bz)/det   (and the two symmetric rows)
  a non-finite result is also rejected (defensive backstop)

result[P] = wellConditioned ? localGradient : greenGaussFallback[P]
```

Note the asymmetric `gx`/`gy` operand order in the 2x2 solve — `(bx*Syy - by*Sxy)` but
`(Sxx*by - Sxy*bx)`. Both are transcribed literally; "tidying" either one changes the rounding.

### What is precomputable, and what is not

Every **displacement** is pure geometry — the interior ones trivially, and the oblique-Neumann ones
(`unitNormal * normalDistance`) too. So `S`, its cofactors, `det`, `scale`, the conditioning verdict
and the per-cell 2D/3D verdict are all **field-independent** and are computed once by the host plan,
in `cell.faceIds()` order, giving bitwise-identical values.

`b` is **not** precomputable: it carries `valueDifference`, which depends on the field and, at a
boundary, on `boundaryValue(field[P], d)`. It is accumulated on device, in the same order, with the
same zero-distance skip, using the 001E boundary encodings.

This is the opposite of 001I's `decomposeAreaVector`, whose second argument *was* field-dependent
and therefore could not be precomputed. The distinction is the argument's dependence, not the
function's shape.

## 8. Reuse

| piece | already qualified |
| --- | --- |
| device mesh, face/cell geometry | 001A |
| Green–Gauss scalar gradient (and the LS fallback) | 001B |
| oblique-Neumann geometry and its second affine encoding | 001B |
| scalar boundary-condition encodings | 001B / 001E |
| momentum response coefficients | 001G |
| pressure-correction assembly producing p' | 001I |

New: the least-squares scalar gradient (plan + kernel) and the correction itself.

## 9. Tolerance

Target **bitwise**, as every earlier gate. The correction is two rounded operations per component;
the least-squares solve is a fixed sequence of rounded operations per cell with no reduction across
threads. No existing tolerance is touched.
