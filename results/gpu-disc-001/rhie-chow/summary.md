# GPU-DISC-001H — CUDA predicted face flux / Rhie–Chow

**Result: PASS.** Both production face-flux paths are **bitwise identical** to the CPU —
2,341,800 face values across 2,628 cases, with orientation and conservation invariants verified
exactly and a checkerboard case that fails if the Rhie–Chow term is removed.

```text
direct cases           2592   (Linear, correction-only and Rhie-Chow, each per case)
integrated chain        24    (CPU assembly->response->flux vs CUDA assembly->response->flux)
invariants               6    orientation + conservation
checkerboard             6
values compared    2,341,800
bitwise-identical  2,341,800  (100%)
max absolute error         0
max relative error         0
negative controls      8/8 detected, each restored and re-passed
compute-sanitizer      4/4 clean, non-vacuous
GPU-DISC gates         8/8 green
full regression        1998/1998 passed, 0 failed, 406.5 s
                       `ninja: no work to do` both BEFORE and AFTER ctest
```

## 1. Both paths are production, and both are ported

`resolveFaceFluxScheme` (`SIMPLESettings.cpp:68`) resolves `Automatic` by dimension:

```text
Automatic -> dimension == 3 ? RhieChow : Linear
```

so a default **2D** solve uses `calculateMassFlux` (no pressure term) and a default **3D** solve
uses `rhieChowMassFlux`. A case file can force either. Both are implemented and compared.

### The recorded technical debt is that 2D default

TODO.md lists `2D Linear face-flux pressure mode`. That is exactly this: a default 2D solve
predicts its face flux by plain linear interpolation, with no Rhie–Chow term — the classical
checkerboard-prone mode. **Reproduced, not fixed.** The Linear path's absence of a pressure term is
a property being verified here, not a defect being corrected, and no scope was broadened.

## 2. The formula, from the source

```text
calculateMassFlux                                       MassFlux.cpp:15
  faceVelocity = interpolateFace(mesh, face, velocity, velocityBoundaries)   // VECTOR overload
  massFlux[f]  = density * dot(faceVelocity, face.areaVector())

rhieChowFaceCorrection  (INTERNAL faces only; boundary stays exactly 0.0)    RhieChow.cpp:17
  coupling = pressureCorrectionFaceCoupling(..., nonOrthogonal = FALSE).coefficient
  d        = centroid(neighbour) - centroid(owner)
  gradFace = interpolateInternalFace(mesh, face, pressureGradient)           // VECTOR overload
  correction[f] = -(coupling / alpha) * ((p[n] - p[o]) - dot(gradFace, d))

coupling                                          PressureCorrectionEquation.cpp:74, :146
  if isAxisAligned(sf) && exactlyParallel(d, sf):
        dFace = interpolateInternalFace(response)                            // SCALAR overload
        -> density * face.area() * dFace / |d|
  else: responseVector = (sf.z == 0) ? {du*sx, dv*sy} : {du*sx, dv*sy, dw*sz}
        -> density * |responseVector| / |d|

rhieChowMassFlux = calculateMassFlux + correction                            RhieChow.cpp:53
```

`alpha` is the **velocity** under-relaxation factor. Both geometric predicates are **exact**
(`!= 0.0`, `cross(...) == Vector3{}`), so the host decides them once and stores a flag plus an axis
index — the device never branches on a float.

**Both `interpolateInternalFace` overloads appear in the same function** and must not be confused:
the face velocity and face pressure gradient use the **vector** form (multiply by the reciprocal),
the response coefficients the **scalar** form (divide). Negative control F8 is exactly that
confusion, detected at **1.01e-28**.

## 3. Differential

`differential/differential.log`

Meshes: `cartesian2d 8`, `cartesian2d 16`, `graded2d 10`, `distorted q16`, `cartesian3d 4`,
`warped 3d 3` — four with axis-aligned coupling faces, two with only the general branch, so both
coupling paths are exercised.

Per mesh: 4 velocities (zero, uniform, nonuniform, reversed) × 3 pressures (uniform, linear,
nonuniform) × 2 response fields (uniform, nonuniform) × 3 densities (1.0, 1.2, 998.2) × 3
relaxation factors (1.0, 0.7, 0.3) × 2 boundary sets (all-Wall, all-five). Each case compares
**three** fields: the Linear flux, the correction alone, and the combined Rhie–Chow flux.

2D: 1728 direct cases. 3D: 864.

## 4. Orientation and conservation — and a test I had to fix

`differential/differential.log`, the `L3` lines.

The first version of this check summed the signed interior flux over every cell and required the
total to be **exactly 0.0**. It failed on correct code: summing ~200 signed doubles leaves ~1e-14
of accumulation round-off against a magnitude of ~300. Relaxing it to "small" would have been
weakening a threshold to make a test pass, so instead the **right invariant** is checked, which is
exactly representable:

* every interior face is visited **exactly twice** in the per-cell traversal — once as owner, once
  as neighbour — and boundary faces exactly zero times;
* the two contributions are `+F` and `-F`, and `(+F) + (-F)` is **exactly 0.0** for any finite `F`;
* the flux array holds exactly `numberOfFaces()` entries, so a duplicated, inconsistent
  owner/neighbour pair cannot exist by construction;
* reversing the velocity **changes** the flux — otherwise the orientation checks would be vacuous.

The global signed sum is still printed, as a diagnostic, not a pass criterion.

## 5. Checkerboard sensitivity

`differential/differential.log`, the `L4` lines.

A cell-to-cell alternating pressure (`±2000` about 101325) with a uniform velocity. Its
interpolated gradient is smooth while the **compact** face difference is large, so the Rhie–Chow
term — which is exactly compact-minus-interpolated — dominates:

```text
distorted q16   |Rhie-Chow term|max = 169.5   vs |linear flux|max = 0.1125
warped 3d 3     |Rhie-Chow term|max =  59.17  vs |linear flux|max = 0.2
graded2d 10     |Rhie-Chow term|max = 358.1   vs |linear flux|max = 0.2087
```

The test asserts the term dominates, so it cannot pass on a smooth field where a broken Rhie–Chow
would be invisible. **Negative control F1 removes the term and this test catches it** — 480 and 54
differing faces on the two quick meshes.

## 6. Negative controls — 8/8 detected

`negative-control/` — each: inject → build → check → restore → rebuild → sha256 → freshness →
re-pass.

| control | mutation | detected |
| --- | --- | --- |
| F1 | Rhie–Chow pressure term removed entirely | yes — **caught by the checkerboard test** |
| F2 | interpolated gradient term added instead of subtracted | yes |
| F3 | general branch uses the owner response instead of interpolating | yes |
| F4 | owner/neighbour interpolation weights swapped | yes |
| F5 | area-vector sign reversed | yes |
| F6 | density omitted | yes |
| F7 | boundary face uses the raw owner velocity instead of the BC | yes |
| F8 | axis-aligned response uses the **vector** interpolation form | yes — full run, **maxAbs 1.01e-28** |

F8 needed the **full** differential rather than `--quick`: both quick meshes are distorted, so the
axis-aligned branch never runs there. That is a coverage property of quick mode, not a null
mutation — the mutation is genuinely detectable, and the full run detects it in 437 cases.

No provably-null mutation was counted as detected.

## 7. Integrated chain

24 cases of

```text
CPU assembly -> CPU response -> CPU rhieChowMassFlux
CUDA assembly -> CUDA response -> CUDA rhieChowMassFlux
```

across 6 meshes × 2 convection schemes × 2 relaxation factors, all bitwise, so the whole upstream
chain composes correctly rather than only the isolated kernel.

## 8. CUDA diagnostics

memcheck, initcheck, synccheck, racecheck: **0 errors** each, each run verified to contain
`FACE FLUX EQUIVALENCE: PASS`. `-fmad=false` now covers eight kernels.

## 9. Files

```text
new
  include/cfd/gpu/DeviceFaceFlux.hpp         plan + three entry points
  cuda/kernels/DeviceFaceFluxPlan.cpp        host plan (the two exact geometric predicates)
  cuda/kernels/DeviceFaceFluxKernel.cu       kernels (-fmad=false)
modified
  cuda/CMakeLists.txt                        sources + the no-FMA property
```

No CPU production file was modified. The plan reuses the 001D convection plan for the device mesh,
face geometry and vector BC encoding; the flux consumes 001B's pressure gradient and 001G's
response coefficients.

## 10. What is NOT started

Pressure-correction assembly, velocity correction, face-flux correction, SIMPLE integration,
GPU-PIPE-001 residency.
