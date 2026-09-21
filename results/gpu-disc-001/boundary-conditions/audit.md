# GPU-DISC-001E Phase A — CPU boundary-condition audit

Written before any CUDA. The CPU implementation is the specification.

## 1. Inventory — eleven types, two value interfaces

`BoundaryConditionType` (`include/cfd/boundary/BoundaryCondition.hpp:15`):

| type | interface | evaluation | prescribesBoundaryValue |
| --- | --- | --- | --- |
| `FixedValue` | scalar | `value_` | **true** |
| `FixedTemperature` | scalar | `temperature_` | **true** |
| `WallOmega` | scalar | `(60 nu) / (beta1 d d)` — depends on distance, not on phi | **true** |
| `FixedGradient` | scalar | `ownerValue + (gradient_ * d)` | false |
| `HeatFlux` | scalar | `ownerValue + ((-q/k) * d)` | false |
| `Adiabatic` | scalar | `ownerValue` | false |
| `Wall` | vector | `{0,0,0}` | **true** |
| `MovingWall` | vector | `velocity_` | **true** |
| `Inlet` | vector | `velocity_` | **true** |
| `Outlet` | vector | `ownerValue` | false |
| `Symmetry` | vector | `ownerValue - (n * dot(ownerValue, n))` | false |

Two abstract interfaces:

```text
ScalarBoundaryCondition::boundaryValue(Real ownerValue, Real normalDistance) -> Real
VectorBoundaryCondition::boundaryValue(const Vector2& ownerValue, Real normalDistance,
                                       const Vector2& unitNormal) -> Vector2
```

`Adiabatic`, `FixedGradient`, `HeatFlux` and `WallOmega` additionally **throw** when
`normalDistance` is not finite and positive. `FixedValue` and `FixedTemperature` ignore the
distance entirely.

## 2. What the operators actually consume

`boundaryConditionForFace(mesh, faceId, set)` resolves a face to its patch's condition — a linear
scan over boundary faces, already recorded as a 3D performance problem
(`results/p12-mesh-005/summary.md`). Its production consumers:

```text
Gradient.cpp                3   interpolateBoundaryFace; oblique-Neumann re-evaluation
Interpolation.cpp           2   scalar and vector interpolateFace
Diffusion.cpp               1   explicit operator (not production-reachable)
Convection.cpp              1   upwindBoundaryFaceValue ghost mirror
MomentumEquation.cpp        2   boundaryVelocity; prescribesVelocity
EnergyEquation.cpp          3   thermal diffusion and convection
SpeciesEquation.cpp         2   species diffusion and convection
KEpsilonEquation.cpp        1   turbulence scalar transport
PressureCorrectionEquation.cpp 2 **type dispatch**, see below
VolumeFractionEquation.cpp  1   multiphase
```

Three distinct things are asked of a boundary condition, and a reusable device layer must supply
all three:

1. **Evaluate a value** — scalar or vector, at a face-specific distance and normal.
2. **Classify for the correction** — `prescribesBoundaryValue(type)`, which decides whether
   diffusion applies its non-orthogonal boundary correction and whether momentum treats the face
   as Dirichlet-type.
3. **Dispatch on the type itself** — `PressureCorrectionEquation.cpp:252`:

```cpp
const auto& bc = boundaryConditionForFace(mesh, faceId, pressureBoundaries);
if (bc.type() != BoundaryConditionType::FixedValue) {
  continue;  // Neumann-like: zero coupling
}
```

(3) is new. Nothing already on the device exposes the *type*; the existing encodings expose only
the evaluation form. The pressure path needs the type, so the layer must carry it.

## 3. Derived boundary constructions

Not conditions themselves, but boundary behaviour built on top of them, each already ported inside
one operator and now belonging in the shared layer:

```text
scalar face value     interpolateBoundaryFace   bc.boundaryValue(field[owner], d)
vector face value     interpolateBoundaryFace   bc.boundaryValue(u[owner], d, n)
ghost / mirror value  upwindBoundaryFaceValue   Ff >= 0 ? phiOwner : (2.0*phiB) - phiOwner
oblique re-evaluation Gradient.cpp              bc.boundaryValue(phiP, d.n) + grad_P . d_t
```

`d` is `MeshGeometry::distance(ownerCentroid, faceCentroid)` in every case **except** the oblique
re-evaluation, which uses the normal distance `d . n`. So the layer must support a **second
encoding at a different distance** on the same face — not one encoding per face.

## 4. Dimensional differences

* Vector conditions are dimension-agnostic in form; `Symmetry` naturally removes nothing in z on a
  2D mesh because `n.z == 0`.
* `VelocityGradientField::gradW` exists only on a 3D mesh — a consumer-side difference, already
  handled in 001D.
* `Symmetry` validates `|n| == 1` within `constants::small` and throws otherwise; the device layer
  stores the host-computed unit normal, so that validation happens once at build time.

## 5. Mapping

```text
CPU BC type        -> CPU evaluation            -> operators            -> device representation      -> CUDA evaluator
FixedValue         -> value_                    -> all scalar operators -> scalar kind=constant       -> evaluateScalar
FixedTemperature   -> temperature_              -> thermal              -> scalar kind=constant       -> evaluateScalar
WallOmega          -> 60nu/(beta1 d^2)          -> turbulence           -> scalar kind=constant       -> evaluateScalar
FixedGradient      -> ownerValue + g*d          -> all scalar operators -> scalar kind=shift          -> evaluateScalar
HeatFlux           -> ownerValue + (-q/k)*d     -> thermal              -> scalar kind=shift          -> evaluateScalar
Adiabatic          -> ownerValue                -> thermal              -> scalar kind=shift (a=0)    -> evaluateScalar
Wall               -> {0,0,0}                   -> momentum             -> vector kind=constant       -> evaluateVector
MovingWall         -> velocity_                 -> momentum             -> vector kind=constant       -> evaluateVector
Inlet              -> velocity_                 -> momentum             -> vector kind=constant       -> evaluateVector
Outlet             -> ownerValue                -> momentum             -> vector kind=identity       -> evaluateVector
Symmetry           -> u - n*dot(u,n)            -> momentum             -> vector kind=symmetry       -> evaluateVector
(any)              -> type()                    -> pressure correction  -> typeId per face            -> boundaryType
(any)              -> prescribesBoundaryValue   -> diffusion, momentum  -> flag per face              -> prescribesValue
```

Note that the **kind** is a property of the evaluation *form*, not of the type: three scalar types
collapse to `constant` and three to `shift`. Keeping the type separately is what lets the pressure
path dispatch correctly while the evaluator stays branch-light.

## 6. Reuse of what 001B–001D already built

The two encoders already exist and are already bitwise-verified, so this phase **extends** rather
than duplicates:

```text
include/cfd/gpu/BoundaryEncoding.hpp         scalar: constant | shift | affine   (001B, shared in 001C)
include/cfd/gpu/VectorBoundaryEncoding.hpp   vector: constant | identity | symmetry (001D)
```

What is genuinely missing, and is this phase's work:

1. a per-face **type** id, for the pressure path's dispatch;
2. a per-face **prescribesBoundaryValue** flag in the shared layer rather than re-derived per
   operator;
3. one container holding scalar *and* vector encodings plus a **second scalar encoding at an
   arbitrary per-face distance**, so the gradient's oblique re-evaluation and any future operator
   can share it;
4. `__device__` evaluators exposed as a small header so every operator calls the same code rather
   than each re-implementing `a + b*phi`;
5. the **ghost/mirror** construction as a shared evaluator.

## 7. Scope limit

This phase builds and qualifies the layer. It does **not** rewire the already-qualified gradient,
diffusion, convection and momentum-convection operators onto it: those four gates are green against
their current code, and moving them is a mechanical follow-up that the integration checks protect.
The layer is verified to produce exactly the encodings those operators build, so the rewiring is
safe when it happens.

It also does not start momentum assembly or pressure-correction assembly — only the BC layer the
pressure path will need.

## 8. Tolerances

The target is **bitwise**, as in 001B–001D. `boundaryValue` is a pure function of
`(ownerValue, distance, normal)`, and the device evaluates the same expressions, so there is no
reason for a rounding difference. No existing tolerance is touched.
