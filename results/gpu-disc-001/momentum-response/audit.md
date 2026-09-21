# GPU-DISC-001G Phase A — CPU momentum response-coefficient audit

Written before any CUDA. The CPU implementation is the specification.

## 1. The function

`cfd::pressure_velocity::computeMomentumResponseCoefficient`
(`src/pressure_velocity/PressureCorrectionEquation.cpp:103`), declared at
`include/cfd/pressure_velocity/PressureCorrectionEquation.hpp:60`.

```cpp
ScalarField computeMomentumResponseCoefficient(const Mesh& mesh, const Vector& momentumDiagonal) {
  if (momentumDiagonal.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "computeMomentumResponseCoefficient: momentumDiagonal size does not match mesh cell count");
  }
  ScalarField d(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    d[cell.id()] = cell.volume() / momentumDiagonal[cell.id()];
  }
  return d;
}
```

That is the whole implementation. There is nothing else.

## 2. Mathematical definition

```text
d_P = V_P / aP_P
```

one value per cell, where `V_P` is `cell.volume()` and `aP_P` is the corresponding entry of a
`MomentumAssembly::diagonal`. The project's own header states it in the same form:

> `d_P = V_P / aP`, the momentum-response coefficient from TODO.md section 13/18 — how much a
> cell's velocity component responds to a unit pressure-correction gradient.

So this **is** the textbook `V/aP` form, confirmed from the source rather than assumed.

### What it does NOT depend on

Checked against the brief's list, each answered from the code:

| candidate dependence | present? |
| --- | --- |
| cell volume | **yes** — the numerator |
| momentum diagonal | **yes** — the denominator |
| density | **no** — `rho` appears nowhere in the function or its signature |
| under-relaxation | **not explicitly** — but see §3 |
| velocity component | **not in the function** — see §4 |
| dimensionality (2D/3D) | **no** — `cell.volume()` is the mesh's own volume either way |
| face/boundary data | **no** — it is a pure per-cell expression, no face loop, no BC lookup |
| clipping / safeguards | **none** — see §5 |

## 3. Relaxation dependence is real but indirect

`computeMomentumResponseCoefficient` has no `alpha` argument, yet the value it produces **does**
change with the relaxation factor, because the diagonal it is handed is the **relaxed** one:

```text
SIMPLE.cpp:474   dU = computeMomentumResponseCoefficient(mesh, uAssembly->diagonal)
                 where uAssembly came from assembleRelaxedMomentumComponent(..., alpha, ...)
                 and applyImplicitUnderRelaxation added  extra = aP_unrelaxed * ((1/alpha) - 1)
                 to that diagonal.
```

So `d_P = V_P / (aP_unrelaxed / alpha)` in effect. The differential therefore has to sweep alpha
through the **assembly**, not through this function — which is what the integrated chain test does.

## 4. Component dependence is in the caller, not the function

The function is component-agnostic; each caller invokes it once per component with that
component's diagonal:

```text
SIMPLE.cpp:474-477              dU, dV, and dW only when threeDimensional
PISO.cpp:270-271                dU, dV        (PISO is 2D-only)
CompressibleSIMPLE.cpp:339-341  dU, dV
```

Downstream consumers (all deferred to later phases, none touched here): `rhieChowMassFlux`,
`assemblePressureCorrection`, `correctVelocity`.

## 5. Edge behaviour — the contract is upstream, and there are no safeguards

This matters, because the brief asks what happens at zero / near-zero / negative / non-finite
input. The answer is: **the function does not check, by design**, and the header says why:

> `momentumDiagonal` is a `MomentumAssembly` diagonal (already positive, finite, and nonzero per
> `MomentumEquation.hpp`'s own guarantees).

The guarantee is enforced at assembly time, not here — `assembleRelaxedMomentumComponent`
(`RelaxedMomentum.cpp:98`) throws `NumericalError` if the assembled matrix or RHS contains a
non-finite value, and `SparseMatrix::diagonal(row)` throws if a row has no stored diagonal entry
at all.

Consequently:

| input | CPU behaviour |
| --- | --- |
| size mismatch | `InvalidArgumentError` — the only check in the function |
| `aP = 0` | `V/0` → `±inf`. Not guarded. Cannot arise from a valid assembly. |
| `aP` tiny but valid | a large finite `d`. Correct, not clipped. |
| `aP` negative | a negative `d`. Not guarded. Cannot arise from a valid assembly (diffusion contributes a positive diagonal). |
| `aP` non-finite | propagates. Prevented upstream by the `allFinite` check. |
| `V = 0` | `d = 0`. Degenerate cells are rejected by `MeshQuality` before any solve. |

**No new safeguard is added on the device.** The port reproduces the expression exactly, including
its unguarded division, and the differential tests the **valid domain** plus the documented
boundary of it (very small and very large valid diagonals). Adding a guard would be a numerical
semantics change, which this phase is not authorized to make.

## 6. Ordering and determinism

The loop runs over `mesh.cells()` and writes `d[cell.id()]`, so the result is indexed by cell id
and is order-independent — every cell is a pure function of its own two inputs. A per-cell CUDA
kernel is a faithful mapping with no accumulation-order question, unlike every earlier operator in
this sequence.

## 7. Mapping

```text
CPU                                                   -> CUDA
computeMomentumResponseCoefficient(mesh, diagonal)    -> computeMomentumResponseCoefficientDevice(
                                                           cellVolumes, diagonal, out)
  d[c] = cell.volume() / momentumDiagonal[c]             d[c] = cellVolumes[c] / diagonal[c]
```

The device consumes `DeviceMomentumSystem::diagonal` — the output of the already-qualified 001F
assembly — and `DeviceMesh::cellVolumes()`, both already device-resident, so there is no transfer.

## 8. Tolerance

A single division, same operands, same rounding mode. Target is **bitwise**. No existing tolerance
is touched.
