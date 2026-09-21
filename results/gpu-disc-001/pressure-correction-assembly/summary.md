# GPU-DISC-001I — CUDA pressure-correction assembly

**Result: PASS.** The production pressure-correction system assembles **bitwise identically** on
CUDA — 6,945,908 coefficients across 4,950 cases, with the sign chain, the conditional reference
pin and both coupling branches verified structurally rather than inferred from equality.

```text
direct cases            4860   (3 pressure BC sets x 5 fluxes x 3 response fields
                                x 2 densities x 3 reference cells x 3 option modes
                                x 6 meshes)
structural (L5)           54   reference-pin topology, opposite-row pairs, M-matrix signs
threshold sweep (L6)      18   25 decades straddling dot(d, S_D) = 0
guard-band sweep (L7)      6   response tuned to land INSIDE 0 < dot <= 1e-6|d||S_D|
integrated chain (L2)     12   CPU momentum->response->flux->pcorr vs the CUDA chain
coefficients compared  6,945,908
bitwise-identical      6,945,908  (100%)
max absolute error             0
max relative error             0
negative controls           8/8 observable detected; 2 provably null, not counted
compute-sanitizer           4/4 clean, non-vacuous
GPU-DISC gates            10/10 green
full regression       1998/1998 passed, 0 failed, 1290.8 s (45 disabled of 2043 registered)
                      `ninja: no work to do` BEFORE and AFTER ctest, source sha256
                      identical on both sides
```

Assembly only. The pressure-correction **solve** was qualified separately by GPU-PCORR-001; no
velocity correction, no face-flux correction, no SIMPLE integration was started.

## 1. What is ported

`assemblePressureCorrection` → `assembleGeometricPressureCorrection`
(`PressureCorrectionEquation.cpp:341`, `:181`), the incompressible entry point. The compressible
equation calls the same body with a per-face density and a compressibility diagonal; that is a
different entry point and was left alone.

Full formula trace in `audit.md`. The three things that decide correctness:

```text
interior face   A(o,o) += D   A(o,n) -= D   A(n,n) += D   A(n,o) -= D
boundary face   FixedValue only:  A(o,o) += D      every other type: no entry at all
RHS             rhs[P] = -cellImbalance[P],  cellImbalance[P] = sum_f (owner==P ? +F* : -F*)
```

## 2. The reference pin is subtractive, and conditional

```text
pinReferenceCell = !hasOpenBoundary      (any FixedValue pressure patch disables it)
```

When active, the CPU's face loop **never writes the reference row** — each of the four adds is
independently guarded — and only then is `1.0` added. `SparseMatrixBuilder` only ever sums, so a
row cannot be overwritten after the fact; the identity row exists because nothing was added to it.
The device reproduces this by construction: the per-row gather emits `(column == c) ? 1.0 : 0.0`
for the reference row and never visits its faces.

Critically, the guards are **per row, not per face**. Pinning suppresses one endpoint's pair; the
opposite row still receives `+D` on its diagonal and `-D` in the reference's column. L5 checks that
directly against `faceCoefficient` rather than trusting CPU/GPU equality: for every internal face
with a pinned endpoint (and exactly one shared face, so the check is exact), `A(other, ref)` must
be bitwise `-D_f`. 28 owner-is-reference faces and 22 neighbour-is-reference faces were exercised,
against 13,000 faces where neither endpoint is the reference.

`plan.pinReferenceCell()` exposes the **decision**, so the all-Neumann / one-FixedValue / mixed
boundary sets assert `true / false / false` — not merely that the numbers happen to match.

## 3. `decomposeAreaVector` runs on the device

Its second argument is the **response vector** `(d_u Sx, d_v Sy, d_w Sz)`, which depends on the
response-coefficient fields, so it cannot be precomputed. The device restates it verbatim,
including the exact `cross(d, S_D) == 0` short-circuit and the `dot(d,S_D) > 1e-6 |d| |S_D|`
well-posedness guard. Negative control **G4** feeds it the geometric area vector instead — i.e.
treats it as precomputable — and is detected.

Both branches are exercised, and coverage of the guard is explicit rather than assumed:

| layer | what it does | observed |
| --- | --- | --- |
| mesh set | 4 meshes take the axis-aligned short-circuit, 2 the general branch | 12 / 6 plans |
| L6 | sweeps the v-response over 25 decades so `dot(d, S_D)` changes **sign** | 6,765 guard rejections |
| L7 | solves `dot(d, S_D) = 0` for a chosen face, then steps off the root by 1e-9…1e-4 | **69 faces inside the band** |

L7 exists because L6 alone is not enough: crossing zero exercises *positivity*, not the `1e-6`
factor. Negative control **G8** replaces the guard with a bare `> 0.0` and is invisible without a
face inside the band. With L7 it is detected.

### A coverage criterion I had to correct, not relax

L6's first version demanded guard rejections on **every** mesh. It failed on the four Cartesian
meshes — with `differing=0`, so not an equivalence failure. The reason is structural: on an
orthogonal mesh every internal face satisfies `isAxisAligned(Sf) && exactlyParallel(d, Sf)` and
takes the short-circuit *before* `decomposeAreaVector` is ever called, so **no response field
whatsoever** can make the guard fire there. The criterion is now gated on
`decomposingFaceCount(mesh) > 0` — a proven property of the mesh, checked per mesh and printed —
and the suite-wide requirement that some face land in the band is unchanged. No threshold was
weakened; the requirement was made applicable where it is meaningful.

## 4. The sign chain, checked link by link

The brief asked for the links to be separable. The harness compares, **independently**:

```text
F*_f  ->  cellImbalance  ->  rhs = -cellImbalance  ->  matrix coefficients
          ^^^^^^^^^^^^^      ^^^^^^^^^^^^^^^^^^^^      ^^^^^^^^^^^^^^^^^^
          L3, computed       diagonal / off-diagonal / RHS reported separately
          independently      per case
```

* **L3** recomputes each cell's imbalance in the harness, straight from the flux and
  `face.owner()`, and requires `rhs[c]` to be bitwise `-imbalance`. 4,860 checks. This does not go
  through the CPU function, so the *convention* is verified, not assumed.
* **Diagonal, off-diagonal and RHS** are counted and printed separately (`diag[d=] off[d=]
  rhs[d=]`), so a reversal localises rather than merely failing.
* **L5** additionally asserts the assembled M-matrix pattern — `D_f >= 0`, diagonal `>= 0`, every
  off-diagonal `<= 0` on non-reference rows — which is the sign chain's consequence and is checked
  without reference to the CPU at all.

`faceCoefficient` and `explicitFaceFlux` are compared too. They are not by-products: the
face-flux correction must reuse the exact coefficients the matrix was built with, and they are the
only place a wrongly-coupled Neumann boundary face shows up before that step exists. 67,500 coupled
FixedValue boundary faces and 141,660 non-zero explicit-term faces were compared.

## 5. Coverage

`differential/differential.log`

Meshes: `cartesian2d 8`, `cartesian2d 16`, `graded2d 10`, `distorted q16`, `cartesian3d 4`,
`warped 3d 3`.

Per mesh: 3 pressure BC sets (all-Neumann, one-FixedValue, mixed) × 5 fluxes (zero, uniform
positive, uniform negative, mixed with `+0.0`/`-0.0`, swirl) × 3 response fields (uniform, varying,
extreme anisotropic `1e2` vs `1e-10`) × 2 densities (1.0, 998.2) × 3 reference cells (first,
middle, last) × 3 option modes (orthogonal; non-orthogonal; non-orthogonal with a previous `p'`,
which activates the explicit term).

```text
2D / 3D                           3240 / 1620 cases
pinned / unpinned                 1620 / 3240 cases
nonOrthogonal off / on            1620 / 3240 cases
sparsity pattern mismatches       0
```

The sparsity comparison is two-sided: every CPU entry must be present on the device with the same
value, and any device entry the CPU dropped must be exactly `0.0` — `SparseMatrixBuilder` drops
entries whose sum is exactly zero, so the candidate CSR is only correct if that holds.

## 6. Negative controls — 8/8 observable detected

`negative-control/` — each: inject → build → run → restore → rebuild → sha256 → re-pass.
All ten restored to the baseline sha256 `6820d290…` and re-passed.

| control | mutation | detected |
| --- | --- | --- |
| G1 | RHS sign reversed (`rhs = +imbalance`) | yes |
| G2 | the whole face skipped when one endpoint is pinned | yes |
| G3 | additive pin (row accumulates, then `+= 1.0`) | yes |
| G4 | `decomposeAreaVector` fed the geometric area vector — precomputable | yes |
| G5 | off-diagonal sign reversed (`A(P,N) += D`) | yes |
| G6 | a Neumann-like boundary face given a real coupling and explicit flux | yes |
| G7 | continuity accumulated in reverse traversal order | yes |
| G8 | the `1e-6` well-posedness factor dropped, leaving bare positivity | yes — needs L7 |

G1–G5 are the five the brief named. G6–G8 cover the boundary filter, the accumulation order and
the guard constant.

### Two mutations that are provably null — run, documented, never counted

| control | mutation | why it cannot be observed |
| --- | --- | --- |
| G6n | the **matrix-side** FixedValue filter removed, leaving `faceTermsKernel`'s zeroing | `faceCoefficient[f]` is exactly `0.0` for a non-FixedValue boundary face, so `value += 0.0` changes nothing. The filter is redundant with the zeroing; G6 removes the zeroing and **is** detected. |
| G7n | continuity gathered in face-id order instead of `cell.faceIds()` order | `cell.faceIds()` is already ascending for every mesh generator in the repository — verified directly: 0 cells with unsorted face ids on `cartesian2d 8`, `cartesian3d 4`, `distorted q16`. The two traversals are the identical sequence. G7 reverses the order instead and **is** detected. |

Both were run and confirmed unobservable; the driver treats a "null" control that *is* detected as
a failure, so the claims are checked rather than asserted.

The device keeps both `cellFaceIds` and `sortedFaces` even though they currently coincide, because
the CPU semantics genuinely differ — continuity uses `cell.faceIds()`, the explicit-term RHS pass
uses face-id order — and a future generator emitting unsorted face ids would separate them.

## 7. Integrated chain

12 cases of

```text
CPU  momentum assembly -> response -> Rhie-Chow flux -> pressure-correction assembly
CUDA momentum assembly -> response -> Rhie-Chow flux -> pressure-correction assembly
```

across 6 meshes × 2 convection schemes, all bitwise, with five velocity BC types and a mixed
pressure BC set. So the assembly composes with its verified upstream rather than only matching in
isolation.

## 8. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck: **0 errors** each; racecheck: **0 hazards**.
Each log was verified to contain `PRESSURE CORRECTION EQUIVALENCE: PASS (bitwise)`, so none is
vacuous. `-fmad=false` now covers nine kernels.

## 9. Files

```text
new
  include/cfd/gpu/DevicePressureCorrection.hpp     plan + the assembly entry point
  cuda/kernels/DevicePressureCorrectionPlan.cpp    host plan (exact geometric predicates, CSR)
  cuda/kernels/DevicePressureCorrectionKernel.cu   kernels (-fmad=false)
modified
  cuda/CMakeLists.txt                              sources + the no-FMA property
```

No CPU production file was modified. The plan embeds the verified `DeviceMesh` and a
`DeviceGradientPlan` for the explicit term's `grad(p')`; the assembly consumes 001G's response
coefficients and 001H's predicted face flux.

## 10. What is NOT started

Velocity correction, face-flux correction, SIMPLE integration, GPU-PIPE-001 residency. Nothing
committed or pushed.
