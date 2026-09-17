# P12-DIFF-002 validation migration — Steps 1–3 report

**Gate:** `results/p12-diff-002/validation-migration/acceptance_gate.md`, sha256
`833ad5608a265ecc75b8df32e5a37242a68773f04155ba7f8101cf92150ba219`, frozen before any test change
(`logs/00_freeze.log`).

```text
Step 1  natural-convection F-F      RESOLVED -> F-C (2x2 factorial attribution)
Step 2  validation-migration gate   FROZEN
Step 3  obsolete instruments        BLOCKED — a PRODUCTION DEFECT surfaced
```

---

## Step 1 — natural-convection F-F resolved to F-C

Full detail in `results/p12-diff-002/investigation-f/summary.md` (Step 1 section) and
`investigation-f/logs/10`, `logs/11`. Two further **isolated** builds restore the pre-A2 wall gating
in one equation only, giving a 2×2 factorial:

| variant (10×10) | Nu_avg err | u_max err | v_max err |
| --- | --- | --- | --- |
| neither (pre-DIFF-002) | 0.048557 | 0.104420 | 0.067138 |
| **thermal reconstruction only** | 0.040417 | **0.103650** | **0.066313** |
| **momentum reconstruction only** | 0.029133 | **0.137642** | **0.130886** |
| both (authoritative) | 0.022464 | 0.137032 | 0.130255 |

The thermal wall-flux reconstruction improves Nusselt and leaves the velocity extrema untouched; the
**momentum wall shear alone** reproduces the entire degradation. θ range, buoyancy coupling, the
gradient scheme and solver convergence are excluded by the same data. With the operator independently
verified exact for linear/quadratic fields, conservation intact, and every analytically referenced
quantity improving (distorted Poiseuille dp/dx 0.6788 % → 0.207 %; Cartesian centerline 1.45455 →
1.46512 against exact 1.5), both natural-convection failures are **F-C**.

## Step 2 — gate frozen

23 tests authorized for migration (M-A 18 hand-derived constants, M-B 3 flux estimators, M-E 2
recorded baselines); 13 tests required to stay unchanged (U-C superseded Poiseuille references, U-D
MMS upper-order bands, U-F natural convection and low-Mach, U-H W8 and the GRAD-002 gradient test).
The gate records, before the work started, that leaving the U set unchanged means **Step 6's W7
cannot pass** — four U-D tests sit inside W7's own suite list.

## Step 3 — BLOCKED: `ThermalInterface.cpp`'s conjugate path is on the pre-A2 wall flux

### What was migrated, and works

`tests/support/BoundaryFluxProbe.hpp` (new) is the single shared diagnostic for "the diffusive flux
production actually assembles": it calls the production operator and assembles the flux from the
documented convention (architecture.md §4). `ThermalBoundaryConsistency`'s `boundaryHeatFlowOut` and
`internalHeatFlowOut` now use it instead of
`boundaryFaceDiffusionTerms(..., nullptr, false).coefficient * (T_P − T_b)`.

**`ThermalBoundaryConsistency.ConvergedFieldIsConsistentWithItsBoundaryValues` now PASSES** (it was
failing at worst-cell 9.83e-02 against 5.75e-10). The conservation statement is still formed and
summed in the test, independently, and its `1e-9 × throughput` tolerance is unchanged.

### The blocker: a genuine production defect, not an instrument defect

`ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo` still fails, at line 251:

```cpp
EXPECT_NEAR(result.temperature[c], single.temperature[c], 1e-12) << c;
```

— the assertion that a **single-region** conjugate solve is identical to the single-material solve.
That is two *production* paths required to agree, not a test-side estimator.

**Mechanism.** `src/thermal/ThermalInterface.cpp:60-73` assembles a boundary face as

```cpp
const Real diffusionCoefficient = conductivity * face.area() / distance;   // two-point k|S|/d
const auto boundary = boundaryDiffusionContribution(
    mesh, face, temperature, temperatureBoundaries, diffusionCoefficient);  // no
builder.add(ownerId, ownerId, boundary.diagonal);                           // boundaryValue-
rhs[ownerId] += boundary.source;                                            // Coefficient, no
continue;                                                                   // far-cell entry
```

and its own comment reads "**Same boundary treatment as the single-material assembly**". P12-DIFF-002
A2 made that untrue: it changed `EnergyEquation.cpp` to use the DIFF-002 three-point Dirichlet wall
flux **unconditionally**, but left this path on the hard-coded two-point form. A2's own audit table
listed `ThermalInterface.cpp` as "unchanged — deliberately never corrected", which was right for the
*non-orthogonal internal* correction and wrong for the *Dirichlet wall scheme* A2 made unconditional.

**Measured** (`investigation-f/logs/12_conjugate_divergence.log`), single region, shipped default
`non_orthogonal_corrections = 0`:

| mesh | rows differing | max \|Δ diagonal\| | max \|Δ rhs\| | solved max \|ΔT\| |
| --- | --- | --- | --- | --- |
| Cartesian 2×1 | 2 of 2 | 2.500000e+00 | 5.333333e+02 | **3.023810e+00** |
| Cartesian 10×10 | 36 of 100 | 5.000000e+00 | 1.133333e+03 | **6.606599e-01** |
| Cartesian 20×20 | 76 of 400 | 5.000000e+00 | 1.133333e+03 | **6.566056e-01** |
| Cartesian 3D 8×8×8 | 296 of 512 | 9.375000e-01 | 2.250000e+02 | **1.430871e+00** |

The differing rows are exactly the boundary-adjacent ones, and the divergence is O(1) physical
temperature, not round-off. A conjugate (multi-material) conduction case therefore still carries the
first-order half-cell wall-flux error that DIFF-002 exists to remove, and disagrees with the
single-material path it is documented to match.

**Classification: F-A — genuine DIFF-002 production defect.** The failing test is correct and must
not be amended.

### Smallest proposed production correction — NOT implemented

In `src/thermal/ThermalInterface.cpp`'s boundary branch, apply the same six-line pattern A2 applied to
the other five call sites:

1. build the correction gradient once per assembly (as `EnergyEquation.cpp` now does
   unconditionally) and obtain
   `terms = boundaryFaceDiffusionTerms(mesh, face, conductivity, distance, &gradT, prescribesTemperature(...))`;
2. pass `terms.coefficient` as the diagonal and `terms.boundaryValueCoefficient` as
   `boundaryDiffusionContribution`'s optional second coefficient;
3. add `rhs[ownerId] += terms.explicitFlux;`
4. add the far-cell entry when it applies:
   `if (terms.farCellCoefficient != 0.0 && boundary.diagonal != 0.0) builder.add(ownerId, terms.farCell, -terms.farCellCoefficient);`
5. correct the stale comment.

No new mechanism is required (the far cell is a face neighbour of the owner, so the sparsity pattern
is unchanged — verified in W4). The conjugate *interface* faces between different materials are a
separate question and are **not** part of this proposal: they need an interface-temperature
reconstruction, which A2's audit already recorded as out of scope.

**This correction was not made.** Production numerical files are byte-identical to
`a4/production_freeze.txt`.

## Files changed by Steps 1–3

```text
tests/support/BoundaryFluxProbe.hpp                       new — the one shared flux diagnostic
tests/unit/thermal/test_thermal_boundary_consistency.cpp  migrated to it (1 test now passes)
tests/unit/thermal/CMakeLists.txt                         include path for the probe
tests/integration/species/CMakeLists.txt                  include path for the probe
results/p12-diff-002/validation-migration/                gate, freeze log, this report
results/p12-diff-002/investigation-f/                     Step 1 factorial + the conjugate probe
```

No production numerical file, no threshold, no benchmark reference, no golden output, no case, and
none of the 13 **U** tests was modified. The remaining 20 authorized migrations (M-A 18, M-B 2 of 3,
M-E 2) were **not** started: the phase stops at the first failed gate.
