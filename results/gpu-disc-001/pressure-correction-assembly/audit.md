# GPU-DISC-001I Phase A — CPU pressure-correction assembly audit

Written before any CUDA. The CPU implementation is the specification.

## 1. The functions

```text
assemblePressureCorrection            PressureCorrectionEquation.cpp:341   (incompressible wrapper)
assembleGeometricPressureCorrection   PressureCorrectionEquation.cpp:181   (the shared body)
pressureCorrectionFaceCoupling        PressureCorrectionEquation.cpp:146
coupling                              PressureCorrectionEquation.cpp:74
evaluateContinuity                    ContinuityEquation.cpp:14
makeGradientBoundaries                PressureCorrectionEquation.cpp:43
MeshGeometry::decomposeAreaVector     MeshGeometry.cpp:144
```

`assemblePressureCorrection` is a thin wrapper: it validates, builds a **uniform** `faceDensity`
(`SurfaceField(nf, density)` — "bit-identical to the pre-P12-NUM-003 single `density` multiply"),
passes `additionalDiagonal = nullptr`, and delegates. The compressible equation calls the same body
with a per-face density and a compressibility diagonal; that is a different entry point and is out
of scope here.

**Callers:** `SIMPLE.cpp:532`, `:573`; `PISO.cpp:276`, `:309`.

## 2. The assembled system

The header states it directly:

```text
sum_f s_f [ coefficient_f (p'_P - p'_N) ] = -R*_P - sum_f s_f explicitFaceFlux_f
                                             s_f = +1 owner, -1 neighbour
```

### 2a. Face loop (face-id order)

```text
for faceId = 0 .. nf-1:
  BOUNDARY face:
      if bc.type() != FixedValue:  continue        <-- Neumann-like: ZERO coupling, no entry at all
      terms = pressureCorrectionFaceCoupling(mesh, face, faceDensity[f], dU, dV,
                                             options.nonOrthogonal, dW)
      faceCoefficient[f] = terms.coefficient
      if gradPrevious: explicitFaceFlux[f] = -dot(terms.nonOrthogonal, gradPrevious[owner])
      if (!pinReferenceCell || owner != referenceCell):  A(o,o) += terms.coefficient
  INTERNAL face:
      terms = pressureCorrectionFaceCoupling(...)
      D = terms.coefficient ;  faceCoefficient[f] = D
      if gradPrevious: explicitFaceFlux[f] =
            -dot(terms.nonOrthogonal, interpolateInternalFace(mesh, face, gradPrevious))
      if (!pinReferenceCell || owner    != referenceCell):  A(o,o) += D ;  A(o,n) -= D
      if (!pinReferenceCell || neighbour != referenceCell):  A(n,n) += D ;  A(n,o) -= D
```

Note the **asymmetry**: the owner's and neighbour's contributions are guarded *independently*, so
pinning the reference cell suppresses only that cell's own row — the other row still receives its
pair. This is not "skip the face".

### 2b. Coupling coefficient

```text
pressureCorrectionFaceCoupling:
  boundary face:  d = faceCentroid - ownerCentroid,  responses = OWNER values (no interpolation)
  internal face:  d = neighbourCentroid - ownerCentroid
      if isAxisAligned(sf) && exactlyParallel(d, sf):
            dFace = interpolateInternalFace(response)        // SCALAR overload (divide)
            -> coupling(face, d, rho, dFace, dFace, dFace, nonOrthogonal)
      else: -> coupling(face, d, rho, interp(dU), interp(dV), dW ? interp(dW) : 0.0, nonOrthogonal)

coupling(face, d, rho, du, dv, dw, nonOrthogonal):
  distance = |d|
  if isAxisAligned(sf) && exactlyParallel(d, sf):
        dComponent = (sf.x != 0) ? du : ((sf.y != 0) ? dv : dw)
        return { rho * face.area() * dComponent / distance , {0,0} }
  responseVector = (sf.z == 0) ? {du*sx, dv*sy} : {du*sx, dv*sy, dw*sz}
  if nonOrthogonal:
        decomposition = decomposeAreaVector(d, responseVector)
        if decomposition.valid:
              return { rho * |decomposition.orthogonal| / distance ,
                       decomposition.nonOrthogonal * rho }
  return { rho * |responseVector| / distance , {0,0} }
```

`decomposeAreaVector(d, sf)` (`MeshGeometry.cpp:144`) — note its `sf` here is the **response
vector**, which is field-dependent, so this decomposition cannot be precomputed:

```text
wellPosed = |d| > 0 && |sf| > 0 && dot(d,sf) > 1e-6*|d|*|sf|
if !wellPosed:                      -> invalid
if cross(d, sf) == Vector3{}:       -> { sf, {0,0}, valid }        (EXACT short-circuit)
orthogonal    = d * (dot(sf,sf) / dot(d,sf))
nonOrthogonal = sf - orthogonal
```

`checked()` throws `NumericalError` on a non-finite coupling.

### 2c. RHS — the continuity imbalance

```text
continuity = evaluateContinuity(mesh, predictorMassFlux)      ContinuityEquation.cpp:14
    cellImbalance[P] = sum over P's faces of  (face.owner() == P ? +flux : -flux)
                                              // cell.faceIds() order
rhs[P] = -cellImbalance[P]
```

### 2d. Explicit non-orthogonal term (corrector passes only)

Built only when `options.nonOrthogonal && options.previousPressureCorrection != nullptr`:

```text
gradPrevious = gradient(mesh, *previousPressureCorrection,
                        makeGradientBoundaries(mesh, pressureBoundaries), options.gradientScheme)
   makeGradientBoundaries: FixedValue(0.0) where the pressure patch is FixedValue,
                           FixedGradient(0.0) everywhere else

for faceId: rhs[owner] -= explicitFaceFlux[f]
            if internal: rhs[neighbour] += explicitFaceFlux[f]
```

### 2e. Reference pressure / null space

```text
hasOpenBoundary  = any boundary patch whose pressure condition is FixedValue
pinReferenceCell = !hasOpenBoundary

if pinReferenceCell:
      A(referenceCell, referenceCell) += 1.0
      rhs[referenceCell] = 0.0
```

Two things matter and are easy to get wrong:

1. The pin is **conditional**. A Dirichlet pressure patch anywhere already removes the null space
   physically; pinning as well would over-constrain an already well-posed system. So the device
   must reproduce the *decision*, not just the pinning.
2. When pinned, the reference row is never written by the face loop at all (the guards above), so
   `A(ref,ref)` ends up **exactly 1.0** and `rhs[ref]` exactly `0.0` — not "1.0 added to whatever
   accumulated". `SparseMatrixBuilder` only ever sums, so a row cannot be overwritten after the
   fact; the CPU achieves the identity row by *never adding to it*.

## 3. Sign convention, traced end to end

The brief asks for this explicitly. For an **interior** face with owner `P`, neighbour `N` and
owner-oriented predicted flux `F*_f`:

```text
predicted flux        F*_f        (owner-oriented: along Sf, which points P -> N)
continuity imbalance  R*_P += +F*_f        R*_N += -F*_f
pressure RHS          rhs[P] = -R*_P       rhs[N] = -R*_N
                      so a face carrying flux OUT of P lowers rhs[P] and raises rhs[N]
matrix                A(P,P) += +D_f   A(P,N) -= D_f
                      A(N,N) += +D_f   A(N,P) -= D_f
```

The diagonal is positive and the off-diagonal negative — an M-matrix sign pattern. A reversal
anywhere in this chain changes the assembled system, and the differential compares the diagonal,
every off-diagonal and the RHS separately, so it localises which link flipped.

## 4. Dimensional and scope notes

* 2D vs 3D differ only through `dW` (`requireWResponse`: a 3D mesh must supply it) and through
  `sf.z == 0.0` selecting the two-component response vector.
* `additionalDiagonal` is `nullptr` for the incompressible entry point; the compressible one is a
  separate function and out of scope.
* Non-orthogonal/skewed **meshes** are covered by the mesh set. The `options.nonOrthogonal`
  **flag** is a separate axis and is also implemented, including the explicit term.

## 5. Reuse

| piece | already qualified |
| --- | --- |
| predicted face flux | 001H |
| response coefficients | 001G |
| momentum assembly (for realistic inputs) | 001F |
| scalar gradient of p' | 001B |
| device mesh, face geometry, BC layer | 001D/001E |
| the axis-aligned/parallel flags, `d`, `|d|` | 001H's face-flux plan |

New: the continuity gather, the coupling on device (both branches, plus `decomposeAreaVector`), the
CSR assembly with the conditional reference pin, and the explicit-term RHS pass.

## 6. Tolerance

Target **bitwise**. Accumulation order is again the whole difficulty: entries accumulate in
face-id order, and the reference pin is appended last. No existing tolerance is touched.
