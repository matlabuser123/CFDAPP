# GPU-DISC-001K Phase A — CPU face-flux-correction audit

Written before any CUDA. The CPU implementation is the specification.

## 1. The function

```text
correctFaceMassFlux            PressureCorrectionEquation.cpp:369
```

There is **one** production function. It is spelled `correctFaceMassFlux`, not `correctFaceFlux`.

**Callers:**

| caller | predictor flux | coefficient source | explicit term |
| --- | --- | --- | --- |
| `SIMPLE.cpp:623` | `predictorFlux` | `pAssembly->faceCoefficient` | `&pAssembly->explicitFaceFlux` **iff** `nonOrthogonalCorrections > 1`, else null |
| `CompressibleSIMPLE.cpp:443` | `predictorFlux` | `pAssembly->faceCoefficient` | same conditional |
| `PISO.cpp:300` | `predictorFlux` | `pAssemblyOne->faceCoefficient` | null |
| `PISO.cpp:331` | `f1` (never the predictor) | `pAssemblyTwo->faceCoefficient` | null |

There is **no separate compressible variant**. The compressible equation reuses this function
unchanged, which is possible precisely because the function is density-agnostic (§3).

## 2. The exact equation

```text
for faceId = 0 .. numberOfFaces()-1:
    pOwner    = pressureCorrection[face.owner()]
    pNeighbor = face.isBoundary() ? 0.0 : pressureCorrection[*face.neighbor()]
    fluxCorrection    = faceCoefficient[faceId] * (pOwner - pNeighbor)
    corrected[faceId] = predictorMassFlux[faceId] + fluxCorrection
    if explicitFaceFlux != nullptr:
        corrected[faceId] += explicitFaceFlux[faceId]
```

Two ordering details are load-bearing:

* the explicit term is a **separate `+=`**, so the result is `(F* + F') + E`, left to right — not
  `F* + (F' + E)`;
* `fluxCorrection` is one rounded product of `faceCoefficient` with one rounded difference.

Validation: the two surface fields and `explicitFaceFlux` must have `numberOfFaces()` entries,
`pressureCorrection` must have `numberOfCells()`. Nothing else is checked; there is no clipping, no
limiter and no safeguard.

## 3. Density — there is none here

`correctFaceMassFlux` never sees a density. The mass flux is corrected by `D_f * Δp'`, and `D_f`
already carries `rho_f`: `pressureCorrectionFaceCoupling` returns `rho_f * |E| / |d|` (or
`rho_f * |S_D| / |d|`). Multiplying by a density here would double-count it. That is also why the
compressible solver can call the same function with its per-face density: the density it wants is
already inside the coefficients it passes.

## 4. Sign and orientation

```text
Sf points owner -> neighbour, and F_f is owner-oriented.
F'_f = D_f * (p'_owner - p'_neighbour)          D_f >= 0 (the M-matrix coefficient, 001I §4)
F_f  = F*_f + F'_f
```

So a p' that is **higher at the owner** drives **more** flux out of the owner, along `+Sf`. This is
the same orientation the pressure-correction matrix was assembled with, and the single shared
`faceCoefficient` is what guarantees the matrix, the RHS and this update cannot disagree
(TODO.md section 30 — never `interpolate(correctedU)`).

Reversing any one link — the difference, the owner/neighbour roles, the addition, or the coefficient
— changes the result, and the differential compares the **face jump**, the **correction increment**
and the **final flux** separately so the reversal localises.

## 5. Boundary treatment

The boundary face is **not skipped**. It takes `pNeighbor = 0.0`, giving
`F'_f = D_f * (p'_owner - 0)` — the Dirichlet-pressure-patch term the assembly put in the matrix.

What makes every other boundary type a no-op is **not a branch here** but the value of `D_f`:
`assembleGeometricPressureCorrection` sets `faceCoefficient` to **exactly `0.0`** for every
non-FixedValue boundary face. So:

| pressure BC on the patch | `faceCoefficient` | effect on the face flux |
| --- | --- | --- |
| `FixedValue` (open/Dirichlet) | the real coupling `D_f` | corrected by `D_f * p'_owner` |
| `FixedGradient` | exactly `0.0` | unchanged |
| Wall / MovingWall / Inlet / Outlet / Symmetry velocity patches (which always pair with a Neumann pressure condition in this codebase) | exactly `0.0` | unchanged |

The "unchanged" rows are unchanged **in value**. They are unchanged *bitwise* for every predictor
except exactly `-0.0`:

```text
F'_f = 0.0 * (p'_owner - 0.0)   ->  +0.0 when p'_owner >= 0,  -0.0 when p'_owner < 0
F_f  = F*_f + F'_f
       x + (+0.0) == x   for every finite x EXCEPT x = -0.0, which gives +0.0
       x + (-0.0) == x   for every finite x, including -0.0
```

So a predictor of `-0.0` on an uncorrected boundary face comes back as `+0.0` whenever `p'_owner` is
non-negative. The magnitude is untouched and the physics is unaffected — a wall still carries zero
flux — but the claim "bitwise unchanged" would be false, and the harness states the contract as
numerical equality with the sign-of-zero flips counted and reported separately.

The device reproduces the arithmetic rather than branching around it, so it inherits this behaviour
exactly. A branch would give an equally valid answer by a different route; reproducing the
expression is what makes the port faithful rather than merely equivalent.

*(This paragraph originally claimed bitwise invariance including `±0.0`. That was wrong, and the
harness's own boundary check caught it — see summary.md §5.)*

Physically this is right: a wall is impermeable (`F_f = 0` always) and a prescribed-velocity inlet
already has its flux fixed, so neither may be corrected.

## 6. 2D vs 3D, orthogonal vs non-orthogonal

This function has **no dimensional branch and no orthogonality branch of its own**. Both enter
entirely through its two inputs, which 001I already produces and qualifies:

* `faceCoefficient` — the axis-aligned short-circuit, the general branch and the over-relaxed
  non-orthogonal decomposition all end up in this one number;
* `explicitFaceFlux` — exactly `0.0` on every face unless a previous pass's `p'` was supplied, and
  otherwise `-rho_f * T_f . grad(p')_f`.

So the gate's dimensional and orthogonality coverage comes from driving those inputs across meshes
and options, not from branches in the kernel.

## 7. Pressure reference

`correctFaceMassFlux` never receives `referenceCell`. The reference enters only through `p'`. Note
that it **cannot** be transparent here the way it is for the velocity correction: the velocity
correction depends on `grad(p')`, which is invariant under adding a constant to `p'`, whereas this
operator depends on the **difference across a face** for interior faces (also invariant) but on the
**absolute value** `p'_owner` at a FixedValue boundary face (not invariant). That asymmetry is
correct — it is precisely the open boundary's degree of freedom — and it is why a uniform `p'`
leaves interior fluxes untouched but does change a Dirichlet boundary face's flux. The invariants in
Phase C are stated accordingly.

## 8. Continuity

`evaluateContinuity` (`ContinuityEquation.cpp:14`) gathers `cellImbalance[P] = Σ (owner==P ? +F : -F)`
over `cell.faceIds()`. The assembled system solves `A p' = -R*`, so after correction the imbalance
should fall to the level the **solve** achieved — no better. The gate therefore:

* measures the imbalance before and after, and reports the reduction;
* asserts the exactly-representable pairwise invariant instead of a threshold on the sum: every
  interior face is visited exactly twice, once as `+F` and once as `-F`, and `(+F)+(-F) == 0.0`
  exactly (the same invariant that replaced a wrong one in 001H);
* compares CPU and GPU imbalance bitwise.

No convergence requirement stronger than the CPU path's own is invented.

## 9. Reuse — and what must NOT be rebuilt

| piece | already qualified | where it comes from on device |
| --- | --- | --- |
| predicted face flux `F*` | 001H | `rhieChowMassFluxDevice` |
| `faceCoefficient` | 001I | `DevicePressureCorrectionSystem::faceCoefficient` |
| `explicitFaceFlux` | 001I | `DevicePressureCorrectionSystem::explicitFaceFlux` |
| `p'` | 001I assembly + GPU-PCORR-001 solve | device buffer |
| face owner/neighbour | 001A | `DeviceMesh` |

001I already emits both coefficient fields device-side **for this reason** — its header says so —
so this gate adds a single per-face kernel and recomputes nothing. There is no plan object: the
operator needs only the device mesh's owner/neighbour arrays.

## 10. Tolerance

Target **bitwise**. Two rounded operations per face, no reduction, no accumulation order to
preserve. No existing tolerance is touched.
