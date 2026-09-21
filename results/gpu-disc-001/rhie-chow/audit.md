# GPU-DISC-001H Phase A — CPU predicted face-flux / Rhie–Chow audit

Written before any CUDA. The CPU implementation is the specification.

## 1. Two production paths, selected by scheme

`SIMPLE.cpp:483-500`:

```text
if (faceFlux == FaceFluxScheme::RhieChow):
    gradP        = gradient(mesh, pressure, pressureBoundaries, settings.gradientScheme)
    predictorFlux = rhieChowMassFlux(mesh, velocityStar, pressure, gradP, dU, dV, dW,
                                     fluid, velocityBoundaries, relaxation.velocity)
else:
    predictorFlux = calculateMassFlux(mesh, velocityStar, fluid, velocityBoundaries)
```

`FaceFluxScheme` is `{Automatic, Linear, RhieChow}`, and `resolveFaceFluxScheme`
(`SIMPLESettings.cpp:68`) resolves `Automatic` by dimension:

```text
Automatic -> dimension == 3 ? RhieChow : Linear
```

So **both paths are production**: a default 2D solve uses **Linear**, a default 3D solve uses
**Rhie–Chow**, and a case file can force either. Both are ported.

### The recorded technical debt is exactly this default

TODO.md lists `2D Linear face-flux pressure mode` as known debt: a default 2D solve predicts its
face flux by plain linear interpolation, with **no Rhie–Chow term**, which is the classical
checkerboard-prone mode. **Reproduced, not fixed.** This phase ports the behaviour as it is; the
differential covers both schemes on 2D and 3D meshes, and the Linear path's absence of a pressure
term is a property being verified, not a defect being corrected.

## 2. `calculateMassFlux` — the Linear path and Rhie–Chow's base

`src/physics/MassFlux.cpp:15`:

```text
for faceId = 0 .. nf-1:
    faceVelocity = interpolateFace(mesh, face, velocity, velocityBoundaries)   // VECTOR overload
    massFlux[faceId] = fluid.density() * dot(faceVelocity, face.areaVector())
```

* interior face value: `((uP * dNf) + (uN * dPf)) * (1.0 / (dPf + dNf))` — the vector overload's
  multiply-by-reciprocal, **not** the scalar overload's divide;
* boundary face value: the `VectorBoundaryCondition` — Wall → 0, MovingWall/Inlet → prescribed,
  Outlet → owner value, Symmetry → normal component removed. No per-BC logic is duplicated in
  `MassFlux.cpp`;
* `dot` is `cfd::dot`, with the exact-zero z guard;
* orientation: `face.areaVector()` is owner → neighbour internally and outward on a boundary face,
  so the flux is **owner-oriented** — one canonical value per face, `+F` for the owner and `-F` for
  the neighbour by convention, never stored twice;
* density is a single scalar `fluid.density()`, applied once per face.

## 3. `rhieChowMassFlux` — the Rhie–Chow path

`src/pressure_velocity/RhieChow.cpp:53`:

```text
flux = calculateMassFlux(mesh, velocityStar, fluid, velocityBoundaries)
correction = rhieChowFaceCorrection(mesh, pressure, pressureGradient, dU, dV, dW,
                                    fluid.density(), alpha)
for f: flux[f] += correction[f]
```

### `rhieChowFaceCorrection` (`RhieChow.cpp:17`)

Boundary faces are **skipped entirely** — their correction stays exactly `0.0`.

```text
for each INTERNAL face:
    coupling = pressureCorrectionFaceCoupling(mesh, face, density, dU, dV,
                                              nonOrthogonal = FALSE, dW).coefficient
    d        = centroid(neighbour) - centroid(owner)
    gradFace = interpolateInternalFace(mesh, face, pressureGradient)      // VECTOR overload
    compactMinusInterpolated = (pressure[neighbour] - pressure[owner]) - dot(gradFace, d)
    correction[face] = -(coupling / alpha) * compactMinusInterpolated
```

`alpha` is the **velocity** under-relaxation factor (`relaxation.velocity`), validated finite and
in `(0, 1]`.

This is the compact-minus-interpolated pressure difference — the standard Rhie–Chow term written
in the repository's own form. Note `-(coupling / alpha) * (...)`: the division by alpha happens
before the multiply, and the negation is applied to the quotient.

### `pressureCorrectionFaceCoupling`, internal face, `nonOrthogonal = false`
(`PressureCorrectionEquation.cpp:146`)

```text
d  = centroid(neighbour) - centroid(owner)
sf = face.areaVector()
if isAxisAligned(sf) && exactlyParallel(d, sf):
      response = (sf.x != 0) ? dU : ((sf.y != 0) ? dV : dW)
      dFace    = interpolateInternalFace(mesh, face, response)   // SCALAR overload (divide)
      -> coupling(face, d, density, dFace, dFace, dFace, false)
else:
      -> coupling(face, d, density,
                  interpolateInternalFace(dU),
                  interpolateInternalFace(dV),
                  dW ? interpolateInternalFace(dW) : 0.0,
                  false)
```

### `coupling` (`PressureCorrectionEquation.cpp:74`)

```text
distance = magnitude(d)
if isAxisAligned(sf) && exactlyParallel(d, sf):
      dComponent = (sf.x != 0) ? du : ((sf.y != 0) ? dv : dw)
      return density * face.area() * dComponent / distance          // left-to-right
responseVector = (sf.z == 0.0) ? Vector2{du*sf.x, dv*sf.y}
                               : Vector3{du*sf.x, dv*sf.y, dw*sf.z}
return density * magnitude(responseVector) / distance
```

with

```text
isAxisAligned(sf)      exactly one of sf.x, sf.y, sf.z is non-zero
exactlyParallel(a, b)  cross(a, b) == Vector3{}      (an EXACT predicate)
```

The axis-aligned test is evaluated **twice** — once to choose which response field to interpolate,
once inside `coupling`. In that branch `du == dv == dw == dFace`, so the second test picks the same
value; both are reproduced rather than collapsed, because the *interpolation* differs between the
branches (one scalar interpolation versus three).

`checked()` throws `NumericalError` on a non-finite coupling (degenerate geometry). SIMPLE turns
that into `NonFiniteState`. Degenerate cells are rejected by `MeshQuality` before any solve, so the
device reproduces the arithmetic and the differential runs on valid meshes; the throw is a host-side
contract, not a per-face device branch.

## 4. Immutable vs field-dependent

| immutable (mesh only) | field-dependent |
| --- | --- |
| `isAxisAligned(sf) && exactlyParallel(d, sf)` per face | `dU`, `dV`, `dW` and their face interpolation |
| which component the axis-aligned branch selects | `pressure`, `pressureGradient`, `gradFace` |
| `d` and `distance = magnitude(d)` | `velocityStar` and the face velocity |
| `face.area()`, area vector, `sf.z == 0.0` | every flux and correction value |
| `dPf`, `dNf` | |

Both geometric predicates are **exact** (`!= 0.0`, `cross(...) == Vector3{}`), so they are decided
once on the host and stored as a flag plus an axis index — no floating-point branch on the device.

## 5. Callers

```text
SIMPLE.cpp:483-500        the only caller of rhieChowMassFlux; Linear via calculateMassFlux
PISO.cpp                  calculateMassFlux (2D)
CompressibleSIMPLE.cpp    its own compressible flux path -- NOT this function
```

`calculateMassFlux` additionally has callers throughout the solvers for the corrected flux; this
phase ports the function, not its every call site.

## 6. Reuse

| piece | already qualified |
| --- | --- |
| interior vector face velocity | 001D `faceVelocityKernel` logic |
| boundary vector velocity | 001D/001E `deviceBoundaryVelocityComponent` |
| pressure gradient | 001B `greenGaussGradientDevice` |
| `dU`, `dV`, `dW` | 001G `computeMomentumResponseCoefficientDevice` |
| device mesh, face geometry, vector BC encoding | 001D plan |

New: the flux kernel, the Rhie–Chow correction kernel, and the per-face geometric flags.

## 7. Tolerance

Target is **bitwise**. Every expression is transcribed in the CPU's operand order; the only
subtlety is that the two `interpolateInternalFace` overloads appear in the same function (vector for
`gradFace`, scalar for the response coefficients) and must not be confused — the same hazard that
control NC1 caught in diffusion.
