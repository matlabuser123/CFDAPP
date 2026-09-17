# P12-DIFF-002 Amendment A2 — activation architecture audit

Written **before** any production change (A2 §1). Traces exactly how `non_orthogonal_corrections`
reaches the Dirichlet wall-flux scheme selection, and separates *scheme selection* from *number of
correction iterations*.

## 1. Control/data flow — BEFORE A2

```text
solver.json  "non_orthogonal_corrections": N        (optional int, DEFAULT 0, must be >= 0)
  src/io/case/SolverConfigParser.cpp:291
    -> SolverConfig::nonOrthogonalCorrections            (Index, include/cfd/io/case/SolverConfig.hpp:64)
  src/io/CaseBuilder.cpp:314
    -> SIMPLESettings::nonOrthogonalCorrections          (Index, SIMPLESettings.hpp:96)
       |
       +--(a) SCHEME SELECTION --------------------------------------------------
       |    src/pressure_velocity/SIMPLESettings.cpp:43
       |      nonOrthogonalOptions(settings)
       |        = NonOrthogonalCorrectionOptions{ enabled = (N > 0), gradientScheme }
       |    -> each assembler: if (!enabled) the correction gradient is not computed,
       |       so the gradient POINTER passed downstream is nullptr
       |    -> boundaryFaceDiffusionTerms(..., gradPhi, prescribedValue)
       |         src/discretization/NonOrthogonalDiffusion.cpp:67
       |           if (gradPhi != nullptr && prescribedValue) -> DIFF-002 reconstruction
       |           else                                      -> two-point secant
       |
       +--(b) NUMBER OF ITERATIONS ----------------------------------------------
            src/pressure_velocity/SIMPLE.cpp:439   if (N > 1) extra momentum passes
            src/pressure_velocity/SIMPLE.cpp:567   for (pass = 1; pass < N; ++pass)
            src/pressure_velocity/SIMPLE.cpp:625   (N > 1) ? &explicitFaceFlux : nullptr
            src/compressible/CompressibleSIMPLE.cpp:388, 445  the same, compressible path
```

**The defect.** One boolean, `gradPhi != nullptr`, currently conflates three independent questions:

1. is a cell-gradient field available at all,
2. should the **internal**-face non-orthogonal correction be applied,
3. should the **Dirichlet boundary** face use the DIFF-002 second-order wall flux.

(3) is a property of the *spatial discretization of a boundary normal derivative* and is meaningful on
a perfectly orthogonal mesh — P12-DIFF-001 measured the two-point error there as exactly `0.5 h`.
Tying it to (2), an iterative non-orthogonality treatment, is the inconsistency A2 corrects.

### Per-caller gate, before A2

| caller | gradient built when | boundary call gate |
| --- | --- | --- |
| `MomentumEquation.cpp:101` (const μ) | `applyNonOrthogonalCorrection` | `gradPhi != nullptr && prescribesVelocity(...)` (:118) |
| `MomentumEquation.cpp:206` (μ field) | `applyNonOrthogonalCorrection` | `gradPhi != nullptr && prescribesVelocity(...)` (:222) |
| `EnergyEquation.cpp:109` (const k) | `correctionGradient()` returns `nullopt` unless `options.enabled` (:53) | `gradTPtr != nullptr && prescribesTemperature(...)` (:120) |
| `EnergyEquation.cpp` (k field) | same helper | `gradTPtr != nullptr && prescribesTemperature(...)` (:206) |
| `SpeciesEquation.cpp:78` | same pattern | `gradYPtr`-derived `prescribed` |
| `KEpsilonEquation.cpp:117` | same pattern | `gradPhiPtr != nullptr && prescribesBoundaryValue(...)` (:118) |
| `ThermalInterface.cpp` | conjugate path — deliberately never corrected | n/a |
| **`Diffusion.cpp` (explicit scalar operator)** | `applyNonOrthogonalCorrection` | **NOT GATED** — see §2 |

## 2. Decisive audit finding: the explicit operator was never gated

`src/discretization/Diffusion.cpp`'s `ownerOrientedFlux` computes

```cpp
const Real baseFlux = uncorrectedBoundaryFlux(mesh, face, field, diffusivity, scalarBc);  // :195
if (gradPhi == nullptr || !prescribesBoundaryValue(scalarBc->type())) {
  return baseFlux;                                                                        // :196-198
}
```

and `uncorrectedBoundaryFlux` (:61-171) **unconditionally** fits the higher-order one-sided boundary
stencil — the 4-point cubic where a 4th point exists, else the 3-point quadratic whose coefficients
`a, b, c` are algebraically identical to DIFF-002's `cB, −cP, cF` (proved to ≤ 1e-14 in W4). Only the
*non-orthogonal* `S_nonorth · grad(φ)` term is gated by `applyNonOrthogonalCorrection`.

So the explicit scalar operator has used a **second-order Dirichlet wall flux since P0, regardless of
`non_orthogonal_corrections`**. The implicit assembly path is the one that is inconsistent, and A2
makes it match a convention that has been in the codebase, unconditional, from the beginning. This is
not a new policy; it is the removal of an accidental divergence between the two paths.

## 3. Control/data flow — AFTER A2 (the intended smallest change)

```text
solver.json  "non_orthogonal_corrections": N        (unchanged: same key, same default 0, kept)
  -> SIMPLESettings::nonOrthogonalCorrections
       |
       +--(a) SCHEME SELECTION: Dirichlet wall flux -- NO LONGER DEPENDS ON N
       |    every affected assembler always builds the correction gradient and always passes it
       |    to boundaryFaceDiffusionTerms, so the wall-flux branch is chosen by GEOMETRY alone:
       |        valid inward stencil   -> DIFF-002 reconstruction
       |        no valid inward stencil -> historical two-point fallback
       |    (boundaryFaceDiffusionTerms itself is UNCHANGED)
       |
       +--(b) INTERNAL-face non-orthogonal correction: still gated by (N > 0)
       |    internalFaceDiffusionTerms receives nullptr when N == 0, exactly as before
       |
       +--(c) NUMBER OF ITERATIONS: still N, untouched
            SIMPLE.cpp / CompressibleSIMPLE.cpp pass loops unchanged
```

`NonOrthogonalCorrectionOptions::enabled` keeps its meaning — "apply the iterative/deferred
non-orthogonal correction" — and continues to drive (b) and (c). The setting is **not deleted** and no
unrelated solver semantics are redefined.

**Cost, stated plainly.** Because the boundary reconstruction needs a cell gradient for its tangential
transfer term, the gradient is now computed for every case, including `N = 0` cases that previously
computed none. That is a real new cost on those cases and is exactly what the revalidated W5 guard
(A2 §9) measures. It is not avoidable by skipping the transfer term: the offsets are exactly zero only
on an orthogonal face, and deciding "orthogonal" would require the floating-point geometry predicate
that W3b-A1 and the GRAD-002 exact-predicate defect both rule out.

## 4. What must NOT change

- `boundaryFaceDiffusionTerms` — already correct; its branch is `stencil.valid`, which is topological.
- The fallback: no valid inward stencil still yields the historical two-point terms, bitwise (W3b-A1).
- Neumann/flux-prescribing faces — still never reconstructed (`prescribesBoundaryValue`).
- `Diffusion.cpp` — already unconditional; no change needed, and none made.
- The `non_orthogonal_corrections` key, its default of 0, its validation, and its iteration semantics.
