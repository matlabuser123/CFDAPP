# GPU-DISC-001D Phase A — CPU convection audit

Written before any CUDA. The CPU implementation is the specification.

> **Status: the scope conflict recorded in section 2 was resolved by a follow-up authorization that
> permitted the momentum convection contribution as a convection-only closure task. Section 11
> audits that path. Sections 1–10 are unchanged — they are the record of what was known before that
> decision, and section 2's analysis is what prompted it.**

## 1. Inventory — three convection paths, and which are production

| path | field | schemes | production callers |
| --- | --- | --- | --- |
| `physics::assembleConvectionContribution` (`MomentumEquation.cpp:264`) | **vector** velocity, per component | **Upwind, Central, LinearUpwind, QUICK** | SIMPLE, PISO, compressible momentum, transient momentum |
| `thermal::assembleThermalConvectionContribution` (`EnergyEquation.cpp:252`, `:302`) | scalar | **Upwind only** | thermal energy; turbulence k/ε/ω (via `cp = 1.0`); species |
| `discretization::convection()` (`Convection.cpp`) | scalar | **Upwind, Central, LinearUpwind, QUICK** | **none** (grep of `src/` and `apps/` finds no caller) |

Scheme selection reaches production through `SIMPLESettings::convectionScheme`, parsed from the case
file by `parseConvectionScheme` and passed only to `assembleConvectionContribution`. The scalar
implicit assemblers take no scheme parameter at all.

## 2. The scope conflict

The brief asks for "every currently supported production scheme" — Central, LinearUpwind and QUICK
included — and simultaneously constrains:

> Do not begin boundary-condition integration, momentum assembly, pressure correction …
> Do not start Momentum Path work.

**The only production path carrying Central / LinearUpwind / QUICK is the momentum convection
assembly.** It is a function in `MomentumEquation.cpp`, it takes a `VelocityComponent` and a
`VectorField`, and its boundary branch calls `boundaryVelocity(...)` which resolves a
**`VectorBoundaryCondition`** — the 001E boundary-condition work, also explicitly deferred.

So the two instructions cannot both be satisfied. Porting the four schemes *as production uses them*
means porting momentum convection assembly plus vector boundary conditions. That is not a judgement
call I should make silently, so this phase does the part that is unambiguously in scope, proves it,
and leaves the checkbox unchecked — which the brief explicitly provides for:

> If a scheme is deliberately outside GPU-DISC-001 scope, document why and leave the TODO item
> unchecked unless the existing project scope explicitly permits partial support.

### What this phase implements and qualifies

1. **All four scheme primitives**, as device functions, differentially compared **directly against
   the production CPU functions** over a swept input space: `quickFaceValue`,
   `linearUpwindFaceValue`, `smoothnessRatio`, `vanLeerLimiter`, `upwindInternalFaceValue`,
   `upwindBoundaryFaceValue`. These are the functions `MomentumEquation.cpp` itself calls — the
   header describes them as "pure, physics-agnostic building blocks … so any physics module
   (momentum, thermal, species, …) can reuse them" — so qualifying them is qualifying the machinery
   momentum will use.
2. **The full four-scheme face-value machinery** (upwind selection, far-upstream lookup, high-order
   value, Sweby/van Leer limiting, clamp), qualified end-to-end against the scalar
   `discretization::convection()` operator, which is the only scalar API exercising it.
3. **The production scalar implicit convection assembly** — matrix diagonal, off-diagonals and RHS —
   qualified against `assembleThermalConvectionContribution` (both the constant-`cp` and
   per-cell-`cp` overloads). Upwind, because that is what the production scalar path supports.

### What it does not

The **vector/momentum** convection assembly. Consequently `* [ ] Convection schemes` stays
**unchecked**: the production path that selects Central/LinearUpwind/QUICK remains CPU-only.

## 3. Face-value machinery, exactly

### Upwind selection and flux-sign convention

`massFlux[faceId]` is **owner-oriented**: it is the flux along the stored area vector `Sf`, which
points owner → neighbour internally and outward on a boundary face.

```text
internal:  Ff >= 0  -> owner is upwind        Ff < 0  -> neighbour is upwind
boundary:  Ff >= 0  -> outflow                Ff < 0  -> inflow
```

`Ff == 0` resolves deterministically to the **owner**. The comparison is `>= 0.0`, so `-0.0`
compares equal to `0.0` and also selects the owner — reproduced exactly on the device.

### Internal face, explicit operator (`Convection.cpp`)

```text
upwind   = Ff >= 0 ? owner : neighbour;   downwind = the other
phiU, phiD = field[upwind], field[downwind]
farUpstream = oppositeInteriorFace(mesh, cell(upwind), face)   -> farCell, hCU = ownerNeighborDistance(farFace)

phiHighOrder:
  Upwind        phiU
  Central       interpolateInternalFace(mesh, face, field)      = ((dNf*phiP)+(dPf*phiN))/(dPf+dNf)
  LinearUpwind  phiU + dot(gradPhi[upwind], faceCentroid - upwindCentroid)
  QUICK         farUpstream ? quickFaceValue(phiC, hCU, phiU, hUf, phiD, hfD) : phiU
                  hUf = distance(upwindCentroid, faceCentroid)
                  hfD = distance(faceCentroid, downwindCentroid)

r       = farUpstream ? smoothnessRatio(phiC, phiU, phiD) : nullopt
psi     = vanLeerLimiter(r)
blended = phiU + psi*(phiHighOrder - phiU)
phiFace = clamp(blended, min(phiU,phiD), max(phiU,phiD))
```

`quickFaceValue` weights (Lagrange through the three true positions):

```text
wC = (-hUf*hfD) / (hCU*(hUf+hCU+hfD))
wU = ((hUf+hCU)*hfD) / (hCU*(hUf+hfD))
wD = (hUf*(hUf+hCU)) / ((hUf+hCU+hfD)*(hUf+hfD))
value = (wC*phiC) + (wU*phiU) + (wD*phiD)
```

`smoothnessRatio`: `local = phiD - phiU`; **exactly zero → nullopt** (no limiting question);
otherwise `(phiU - phiC) / local`.
`vanLeerLimiter`: `nullopt or r <= 0 → 0`; else `(r + |r|) / (1 + |r|)`.

### Boundary face, explicit operator

```text
Ff >= 0 : phiFace = field[owner]                                   (outflow)
Ff <  0 : phiB  = bc.boundaryValue(field[owner], distance)
          phiFace = (2.0 * phiB) - field[owner]                     (ghost mirror)
```

Boundary faces are **never** affected by `scheme` — an explicit P12-NUM-001 scope limit.

### Cell sum, explicit operator

```text
for faceId in cell.faceIds():
    cellFlux = (face.owner() == cell.id()) ? Ff : -Ff
    sum += cellFlux * phiFace
result[cell] = sum / cell.volume()       <-- DIVIDE, unlike the gradient's multiply-by-reciprocal
```

### Production scalar implicit assembly (`assembleThermalConvectionContribution`)

Note this boundary treatment is **different** from the explicit operator's: no ghost mirror.

```text
for faceId = 0 .. nf-1:                       <-- face-id order
  effectiveFlux = cp * Ff          (cp scalar, or cp[upwindCell] for the field overload)
  boundary:
     Ff >= 0 : A(owner,owner) += effectiveFlux
     Ff <  0 : rhs[owner] -= effectiveFlux * bc.boundaryValue(phi[owner], distance)
  internal:
     Ff >= 0 : A(o,o) += ef ;  A(n,o) -= ef
     Ff <  0 : A(o,n) += ef ;  A(n,n) -= ef
```

The per-cell-`cp` overload evaluates `cp` at **the upwind cell**, not face-interpolated — the
header is explicit that this differs from diffusion deliberately.

## 4. Immutable vs field-dependent

The upwind decision depends on the **sign of the mass flux**, which is an input field. So anything
downstream of "which side is upwind" must be precomputed for **both orientations**:

| immutable (mesh only) | field-dependent |
| --- | --- |
| `dPf`, `dNf` (also `hUf`/`hfD`, swapped by orientation) | `Ff` sign → upwind/downwind choice |
| far-upstream cell and `hCU`, **one per orientation** | `phiC`, `phiU`, `phiD` |
| face-centre offsets `x_f - x_P`, `x_f - x_N` | `r`, `psi`, `phiHighOrder`, `phiFace` |
| boundary owner distance and the `(a,b,kind)` encoding | `gradPhi` (from the 001B CUDA gradient) |
| cell volume, owner/neighbour, CSR connectivity | every matrix coefficient and RHS term |

`hUf` and `hfD` need no separate storage: owner-upwind gives `hUf = dPf`, `hfD = dNf`; neighbour-
upwind swaps them.

## 5. How the verified CUDA gradient is reused

Only **LinearUpwind** needs a gradient, and only at the upwind cell. The CPU builds it once per
call via `gradient(mesh, field, boundaries)` (GreenGauss default) — the operator qualified in 001B.
The device path embeds a `DeviceGradientPlan`, calls `greenGaussGradientDevice` on the
device-resident field and indexes the result directly. Upwind/Central/QUICK build no gradient at
all, exactly as the CPU does.

## 6. Dimensionality, tolerances, tests

Everything is centroid/area-vector based and dimension-agnostic; 3D is covered.

Existing CPU tests: `test_convection.cpp` (primitives, step-function boundedness),
`GridRefinementTest.UpwindConvectionConvergesAtFirstOrder`, the momentum and thermal assembly
tests. Their tolerances are untouched.

For the CPU/GPU differential the target is **bitwise**, as in 001B and 001C. `clamp`, `>= 0.0`
comparisons and the `localGradient == 0.0` test are all exact predicates, so a rounding difference
would flip a branch, not just perturb a value — which is a further reason to require exact equality
rather than a tolerance.

## 7. Mapping

```text
CPU scheme/function                          -> CUDA equivalent
upwindInternalFaceValue                      -> device upwindInternalValue        (L1 + L2)
upwindBoundaryFaceValue                      -> device upwindBoundaryValue        (L1 + L2)
quickFaceValue                               -> device quickFaceValue             (L1 + L2)
linearUpwindFaceValue                        -> device linearUpwindFaceValue      (L1 + L2)
smoothnessRatio / vanLeerLimiter             -> device smoothnessRatio/vanLeer    (L1 + L2)
discretization::convection() (4 schemes)     -> convectionDevice()                (L2)
thermal::assembleThermalConvectionContribution
  (both overloads, upwind)                   -> assembleScalarConvectionDevice()  (L3)
physics::assembleConvectionContribution      -> NOT PORTED (momentum + vector BCs, out of scope)
```


---

# 11. Closure audit — `physics::assembleConvectionContribution`

Added when the momentum convection contribution was authorized as a convection-only closure. The
CPU remains the specification; nothing in `MomentumEquation.cpp` was changed.

## 11.1 The function, exactly

`src/physics/MomentumEquation.cpp:264`.

```text
velocityGradient = (scheme == LinearUpwind)
                     ? computeVelocityGradient(mesh, velocity, velocityBoundaries)
                     : nullopt                      <-- built for NO other scheme
gradPhi = componentGradient(velocityGradient, component)   // gradU / gradV / gradW

for faceId = 0 .. nf-1:                              <-- FACE-ID ORDER
  Ff = massFlux[faceId]                              // owner-oriented
  if boundary:
      Ff >= 0 : A(owner,owner) += Ff                          (outflow, implicit)
      Ff <  0 : uB   = boundaryVelocity(mesh, face, velocity, velocityBoundaries)
                phiB = selectComponent(uB, component)
                rhs[owner] -= Ff * phiB                       (inflow, known -> RHS)
      continue                                        <-- scheme NEVER affects a boundary face
  Ff >= 0 : A(o,o) += Ff ;  A(n,o) -= Ff
  Ff <  0 : A(o,n) += Ff ;  A(n,n) -= Ff              <-- implicit coefficients are ALWAYS upwind
  if scheme == Upwind: continue

  // deferred correction, from the current/lagged velocity
  ownerIsUpwind = Ff >= 0 ; upwind/downwind chosen accordingly
  phiUpwind, phiDownwind = selectComponent(velocity[.], component)
  farCell/hCU from oppositeInteriorFace(mesh, cell(upwind), face)
  phiHighOrder:
     Central       selectComponent(interpolateInternalFace(mesh, face, velocity), component)
                     <-- the VECTOR overload: ((uP*dNf)+(uN*dPf)) * (1/(dPf+dNf))
     LinearUpwind  linearUpwindFaceValue(phiUpwind, gradPhi[upwind], upwindCentroid, faceCentroid)
     QUICK         farCell ? quickFaceValue(phiC, hCU, phiUpwind, hUf, phiDownwind, hfD)
                           : phiUpwind
  r       = farCell ? smoothnessRatio(phiC, phiUpwind, phiDownwind) : nullopt
  psi     = vanLeerLimiter(r)
  blended = phiUpwind + psi*(phiHighOrder - phiUpwind)
  phiFace = clamp(blended, min(phiU,phiD), max(phiU,phiD))
  correction = Ff * (phiFace - phiUpwind)
  rhs[owner] -= correction ; rhs[neighbour] += correction
```

Component selection is `velocityComponentValue`: x for U, y for V, z for W. `gradW` exists only on
a 3D mesh, so W is only meaningful there.

## 11.2 Vector boundary conditions

`boundaryVelocity` resolves a `VectorBoundaryCondition` and evaluates
`boundaryValue(ownerValue, distance, unitNormal)`. Five implementations, three distinct forms:

```text
Wall        -> {0,0,0}                      constant
MovingWall  -> velocity_                    constant
Inlet       -> velocity_                    constant
Outlet      -> ownerValue                   identity
Symmetry    -> u - (n * dot(u, n))          normal-component removal
```

Encoded on the device as (kind, constant, unit normal) and verified bitwise against the condition
itself over nine probe vectors. The symmetry form is reproduced as the same guarded dot plus
component-wise multiply-subtract — **not** as a 3×3 matrix product, which rounds differently.

## 11.3 The gradient dependency is NOT the 001B gradient

LinearUpwind needs `discretization::computeVelocityGradient`, which routes to
`greenGaussVelocityGradient` (`VectorGradient.cpp:51`). That is a **different operator** from the
scalar `greenGaussGradient` qualified in 001B:

| | scalar `greenGaussGradient` (001B) | `greenGaussVelocityGradient` |
| --- | --- | --- |
| boundary treatment | P12-GRAD-002 boundary-consistent quadratic fit, with claims | plain `interpolateFace`, vector BC |
| boundary conditions | scalar | **vector** |
| cell sum | `sum * (1.0 / V)` | `sum * (1.0 / V)` |
| skewness sweeps | yes (4) | yes (4), each component with its own gradient |
| gradW | n/a | only on a 3D mesh; **empty** in 2D |

So it had to be ported too, and is compared directly in the differential rather than only through
LinearUpwind.

```text
faceVelocity[f] = interpolateFace(mesh, face, velocity, boundaries)   // vector overload
skewedFaces     = internal faces with a non-zero skew vector
result          = greenGaussSum(mesh, faceVelocity)
repeat kGreenGaussSkewCorrectionSweeps times, only if skewedFaces is non-empty:
    for skewed f: faceVelocity[f] = interpolateInternalFaceSkewCorrected(
                                        mesh, face, velocity, gradU, gradV, 3D ? &gradW : nullptr)
    result = greenGaussSum(mesh, faceVelocity)
```

`greenGaussSum` accumulates over `cell.faceIds()` order with `sfCell = owner==cell ? Sf : Sf*-1.0`,
and divides by volume as a multiply by the reciprocal.

**A 2D subtlety that is easy to lose:** `interpolateInternalFaceSkewCorrected`'s vector overload
returns `Vector2{x, y}` in 2D, whose z is exactly `0.0` — not the interpolated z. The device
reproduces that. It happens to be unobservable (§11.5), but it is reproduced anyway.

## 11.4 Dimensional differences

* `gradW` is filled only on a 3D mesh; on a 2D mesh `VelocityGradientField::gradW` has size 0. The
  device mirrors this by leaving the gradW buffers **empty** in 2D rather than allocating them —
  allocating left them uninitialised, which `compute-sanitizer --tool initcheck` caught on the
  harness's download (2972 errors). Fixed in the device code, not in the harness.
* Component W is only meaningful on a 3D mesh, so the differential exercises it only there.

## 11.5 Two provably unobservable behaviours

Recorded because they bound what any test can claim:

1. `Ff >= 0` vs `Ff > 0` at exactly zero flux — the selected value is always multiplied by the
   flux (§ summary.md §4). Carried over from the scalar phase, unchanged.
2. The 2D skew-corrected face velocity's **z** component. `greenGaussSumKernel` reads `fz` only
   inside its `threeDimensional` branch, so in 2D the value is written and never read. Both the
   CPU's exact `0.0` and any other value produce identical results.

Both are matched by transcription, not by test, and both are recorded as such rather than covered
by a manufactured assertion.
