# GPU-DISC-001F Phase A — CPU momentum assembly audit

Written before any CUDA. The CPU implementation is the specification.

## 1. Three production entry points, one term set

| entry point | solver | file | viscosity | extra term | dims |
| --- | --- | --- | --- | --- | --- |
| `assembleRelaxedMomentumComponent` | **SIMPLE** | `RelaxedMomentum.cpp:30` | effective-viscosity **field** | implicit under-relaxation | 2D + 3D |
| `assembleTransientMomentumComponent` | PISO | `TransientMomentum.cpp:40`, `:99` | constant **or** field | implicit-Euler transient | **2D only** (`requireTwoDimensional`) |
| compressible relaxed momentum | compressible SIMPLE | `CompressibleRelaxedMomentum.cpp:59` | constant `dynamicViscosity` | implicit under-relaxation | 2D |

All three are the same three contributions plus their own fourth term:

```text
momentum assembly
  = diffusion            physics::assembleDiffusionContribution
  + convection           physics::assembleConvectionContribution
  + pressure source      physics::assemblePressureSourceContribution
  [+ buoyancy]           physics::assembleBuoyancySourceContribution      (SIMPLE, optional)
  [+ momentum source]    physics::assembleMomentumSourceContribution      (SIMPLE, optional)
  + relaxation           pressure_velocity::applyImplicitUnderRelaxation  (SIMPLE, compressible)
  | + transient          pressure_velocity::applyTransientTerm            (PISO)
```

### Scope decision

This phase implements **`assembleRelaxedMomentumComponent`**, the SIMPLE path: it is the one
GPU-PIPE-001's residency work needs, the only one supporting 3D, and the only one using the
effective-viscosity field overload. The other two are audited here and **not ported**:

* PISO's transient entry point is a *different function*, is 2D-only, and adds
  `implicitEulerTimeDerivative` + `applyTransientTerm` — a term this phase does not implement.
* The compressible entry point uses the **constant**-viscosity diffusion overload, which takes
  `muFace = dynamicViscosity` directly on every face instead of interpolating a field. That is a
  genuinely different expression, not a special case of the field overload, so passing a uniform
  field would **not** reproduce it bitwise.

Both are recorded as remaining work rather than implied to be covered.

## 2. The SIMPLE assembly, exactly

`RelaxedMomentum.cpp:30`:

```text
builder(n, n); rhs(n, 0.0)
assembleDiffusionContribution(mesh, effectiveViscosity, velocity, velocityBoundaries, component,
                              builder, rhs, applyNonOrthogonalCorrection, gradientScheme,
                              nonOrthogonalCorrectionVelocity)
assembleConvectionContribution(mesh, massFlux, velocity, velocityBoundaries, component,
                               builder, rhs, convectionScheme)
assemblePressureSourceContribution(mesh, pressure, pressureBoundaries, component, rhs,
                                   gradientScheme)
[assembleBuoyancySourceContribution]      if buoyancy
[assembleMomentumSourceContribution]      if momentumSource
applyImplicitUnderRelaxation(builder, rhs, previousComponentVector, alpha)
matrix = builder.build();  diagonal[row] = matrix.diagonal(row)
throw NumericalError if !matrix.allFinite() || !rhs.allFinite()
```

**Accumulation order is part of the answer.** `SparseMatrixBuilder::build()` stable-sorts by
`(row, column)` and sums repeated entries in insertion order, so for every matrix entry the order
is: all **diffusion** contributions (face-id order), then all **convection** contributions (face-id
order), then the single **relaxation** term on the diagonal. The RHS accumulates in the same
sequence, with pressure/buoyancy/source between convection and relaxation.

### 2a. Diffusion contribution (`MomentumEquation.cpp:196`, field overload)

```text
velocityGradient = computeVelocityGradient(mesh, correctionVelocity ? *correctionVelocity : velocity,
                                           velocityBoundaries, correctionGradientScheme)
                                           <-- ALWAYS built (P12-DIFF-002 A2)
gradPhi        = componentGradient(velocityGradient, component)
internalGradPhi = applyNonOrthogonalCorrection ? gradPhi : nullptr

for faceId = 0 .. nf-1:
  boundary:
     distance = distance(ownerCentroid, faceCentroid)
     muFace   = effectiveViscosity[owner]              <-- owner value, NOT interpolated
     terms    = boundaryFaceDiffusionTerms(mesh, face, muFace, distance, gradPhi,
                                            prescribesVelocity(...))
     phiB     = selectComponent(boundaryVelocity(mesh, face, velocity, velocityBoundaries), component)
     rhs[o]  += terms.explicitFlux
     A(o,o)  += terms.coefficient
     rhs[o]  += terms.boundaryValueCoefficient * phiB
     if terms.farCellCoefficient != 0: A(o, terms.farCell) += -terms.farCellCoefficient
  internal:
     dPN    = ownerNeighborDistance(face)
     muFace = interpolateInternalFace(mesh, face, effectiveViscosity)   <-- SCALAR overload
     terms  = internalFaceDiffusionTerms(mesh, face, muFace, dPN, internalGradPhi)
     if internalGradPhi: rhs[o] += terms.explicitFlux ; rhs[n] -= terms.explicitFlux
     A(o,o) += c ; A(o,n) -= c ; A(n,n) += c ; A(n,o) -= c
```

This is the **same face-term machinery 001C qualified**, with two substitutions: the boundary value
comes from a **vector** condition via `boundaryVelocity` instead of a scalar one, and `gradPhi` is
a component of the **velocity** gradient instead of the scalar gradient.

### 2b. Convection contribution

`physics::assembleConvectionContribution` — **already ported and qualified in 001D**
(10,352 bitwise cases, all four schemes, U/V/W, all five vector BCs). Reused unchanged.

### 2c. Pressure source (`MomentumEquation.cpp:415`)

```text
gradP = gradient(mesh, pressure, pressureBoundaries, scheme)      <-- the SCALAR 001B gradient
for cell: rhs[cell] += -cell.volume() * selectComponent(gradP[cell], component)
```

Note `-cell.volume() * gradComponent`: the negation is applied to the volume, then multiplied.

### 2d. Under-relaxation (`UnderRelaxation.cpp:13`)

```text
if alpha not finite, <= 0, or > 1 -> throw
if alpha == 1.0 -> RETURN (exact no-op, not "multiply by one")
unrelaxed = builder.build()
factor = (1.0 / alpha) - 1.0
for i: extra = unrelaxed.diagonal(i) * factor
       builder.add(i, i, extra)
       rhs[i] += extra * previousValue[i]
```

Three things are load-bearing:

1. `alpha == 1.0` returns **before** building anything — so no extra triplet is inserted at all.
   Computing `extra = diag * 0.0` and adding it would append `+0.0` to every diagonal, which is
   *usually* invisible but is not the same code path.
2. `factor = (1.0 / alpha) - 1.0` — a reciprocal then a subtraction. Rewriting it as
   `(1 - alpha) / alpha` is algebraically identical and **not** bitwise identical.
3. `extra` is derived from the diagonal of the **unrelaxed** matrix, i.e. after diffusion and
   convection have both been summed, and is appended last.

## 3. What is reused vs what is new

| term | verified CUDA primitive available? |
| --- | --- |
| convection contribution | **yes** — `assembleMomentumConvectionDevice` (001D), used unchanged |
| velocity gradient (diffusion's `gradPhi`) | **yes** — `computeVelocityGradientDevice` (001D) |
| pressure gradient | **yes** — `greenGaussGradientDevice` (001B) |
| vector boundary values | **yes** — `DeviceBoundaryConditions` / `VectorBoundaryEncoding` (001D/E) |
| diffusion face terms | **yes, but** the device code lives inside `DeviceDiffusionKernel.cu`'s anonymous namespace. It is **extracted** into a shared header so the momentum path calls the same implementation rather than a copy. 001C's 528-case gate is re-run to prove the extraction changed nothing. |
| diffusion geometry (decompose, inward stencil, cP/cF/cB) | rebuilt in the momentum plan by calling the **same production MeshGeometry functions**; not a second implementation, the same host calls |
| pressure-source term | new, three lines |
| under-relaxation | new, four lines |
| assembly glue (gather, ordering) | new |

## 4. Dimensional behaviour

`assembleRelaxedMomentumComponent` supports 2D and 3D. `componentGradient` returns `gradW` only on
a 3D mesh, so the W component is meaningful only there — the same constraint 001D already handles.

## 5. Tolerances

Target is **bitwise**, as in 001B–001E. Every operation is the same arithmetic in the same order;
the only new floating-point is the pressure source and the relaxation, both transcribed. No
existing tolerance is touched.
