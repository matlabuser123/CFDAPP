# GPU-DISC-001K — CUDA face-flux correction

**Result: PASS.** The production face-flux correction is **bitwise identical** on CUDA —
1,621,800 values across 1,848 cases, max absolute and relative discrepancy 0.

```text
direct cases            1800   (3 pressure BC sets x 5 p' fields x 5 predicted fluxes
                                x 2 response fields x 2 explicit-term settings x 6 meshes)
continuity cases          36   imbalance before/after, conservation, orientation
controlled p' cases        6   one real solved p' fed to BOTH correction paths
full chain cases           6   CPU assembly->solve->velocity+flux vs CUDA + GPU solve
values compared    1,621,800
bitwise-identical  1,621,800  (100%)
max absolute error         0
max relative error         0

observable controls       11
detected observable    11/11
documented null            0
source restoration     11/11 sha256 match, 11/11 re-passed
compute-sanitizer        4/4 clean, non-vacuous
GPU-DISC gates         12/12 green
full regression    1998/1998 passed, 0 failed, 1272.4 s (45 disabled of 2043 registered)
                   `ninja: no work to do` BEFORE and AFTER ctest; library and
                   001K source sha256 identical on both sides
```

Single-iteration integration was **not** started.

## 1. The exact CPU path

`correctFaceMassFlux` (`PressureCorrectionEquation.cpp:369`) — one production function, spelled
`correctFaceMassFlux`, not `correctFaceFlux`. Callers: `SIMPLE.cpp:623`,
`CompressibleSIMPLE.cpp:443` (both passing the explicit term only when
`nonOrthogonalCorrections > 1`), `PISO.cpp:300` and `:331` (never). There is **no separate
compressible variant**.

```text
for each face f:
    pOwner    = p'[owner]
    pNeighbor = isBoundary ? 0.0 : p'[neighbour]
    F'_f      = faceCoefficient[f] * (pOwner - pNeighbor)
    F_f       = F*_f + F'_f
    if explicitFaceFlux: F_f += explicitFaceFlux[f]        // a SEPARATE addition
```

`(F* + F') + E`, left to right — not `F* + (F' + E)`. Negative control **K9** is exactly that
regrouping, and it is detected.

**No density appears here.** `rho_f` is already inside `faceCoefficient`
(`pressureCorrectionFaceCoupling` returns `rho_f * |E| / |d|`), which is also why the compressible
solver can call the same function with its own per-face density. Control **K10** introduces a
density factor — the double-count — and is detected.

## 2. Sign and orientation

```text
Sf points owner -> neighbour;  F_f is owner-oriented;  D_f >= 0
F'_f = D_f * (p'_owner - p'_neighbour)
```

A p' higher at the owner drives more flux out along `+Sf`. The **face jump**, the **correction
increment** and the **final flux** are compared independently, so a reversal localises rather than
merely failing. Controls K1 (add/subtract), K2 (owner/neighbour swap), K3 (jump replaced by the
owner value) and K6 (applied twice) are each detected.

The single shared `faceCoefficient` is what guarantees the matrix, the RHS and this update cannot
disagree (TODO.md section 30). Control **K4** feeds the neighbouring face's coefficient — breaking
exactly that sharing — and is detected.

## 3. Boundary semantics

A boundary face is **not skipped**: it takes `pNeighbor = 0.0`, the Dirichlet term the assembly put
in the matrix. What makes every other type a no-op is the **value** of `D_f`, which the assembly
sets to exactly `0.0` there.

| pressure BC | `faceCoefficient` | effect |
| --- | --- | --- |
| `FixedValue` (open) | the real coupling | corrected by `D_f * p'_owner` — 52,500 faces |
| `FixedGradient`, and the Neumann pressure conditions paired with Wall / MovingWall / Inlet / Outlet / Symmetry velocity patches | exactly `0.0` | flux unchanged — 26,250 faces |

Three structural facts are checked per uncorrected boundary face, none inferred from CPU/GPU
equality: the coefficient is bitwise `0.0`, the explicit term is bitwise `0.0`, and the corrected
flux equals the predictor. Control **K5** makes a boundary face read the owner's own p' instead of
the Dirichlet zero — what a missing boundary case looks like — and is detected.

## 4. 2D/3D and orthogonality

This operator has **no dimensional branch and no orthogonality branch of its own**. Both enter
entirely through `faceCoefficient` and `explicitFaceFlux`, which 001I produces and qualifies. The
gate's coverage therefore comes from driving those inputs: 1,200 2D and 600 3D direct cases, 900
with and 900 without the explicit term, across six meshes including two non-orthogonal ones.

## 5. A claim of mine that was wrong — the signed zero

The audit originally said an uncorrected boundary face is left **bitwise** unchanged. **That was
false**, and the harness's own boundary check caught it on the first run.

```text
F'_f = 0.0 * (p'_owner - 0.0)   ->  +0.0 when p'_owner >= 0,  -0.0 when p'_owner < 0
x + (+0.0) == x   for every finite x EXCEPT x = -0.0, which becomes +0.0
```

So a `-0.0` predictor on an uncorrected face comes back as `+0.0` whenever `p'_owner` is
non-negative — **2,352 occurrences** across the suite. The magnitude is untouched and a wall still
carries zero flux, so the production contract (the flux is not corrected) holds; but "bitwise
unchanged" does not. The audit is corrected, the check is stated as numerical equality, and the
flips are counted and reported rather than quietly tolerated.

**The same fact then overturned a second claim of mine.** Control K11 replaces the arithmetic with a
short-circuit on `faceCoefficient == 0.0`. I classified it *provably null* — same answer, different
route. It was **DETECTED**: `maxAbs = 0`, yet 13 faces differ, because the branch returns `-0.0`
where production returns `+0.0`. So reproducing the CPU's expression rather than branching around a
known no-op is genuinely load-bearing, K11 is a real observable control, and this gate has **11
observable controls and 0 null** ones.

Both discoveries trace to one fact, and the driver — which fails any "null" control that is
detected — is what forced them into the open instead of letting a comfortable assumption stand.

## 6. Continuity and conservation

`differential/differential.log`, the `L3` lines. 36 cases: 6 meshes × 3 pressure BC sets × 2
predictors (one already divergence-free, one plainly not).

Per case the imbalance is computed **independently** — from the flux and `face.owner()`, not via
`evaluateContinuity` — before and after correction, and reported two ways:

```text
warped 3d 3 / all-Neumann   (pin=yes)  all[0.003709 -> 0.01925]  solved[0.003707 -> 1.615e-15]
warped 3d 3 / mixed         (pin=no)   all[0.003709 -> 4.586e-16] solved[same]
distorted q16 / mixed       (pin=no)   all[1.534 -> 1.513e-14]    solved[same]
distorted q16 / all-Neumann (pin=yes)  all[1.534 -> 0.8219]       solved[1.533 -> 1.245e-13]
```

### A criterion of mine that was also wrong

My first version required the all-cell RMS to fall. It **rose** on `all-Neumann` — correctly. When
the reference cell is pinned, its continuity equation is *replaced* by `p'[ref] = 0`, and a
fully-Neumann system is only solvable when the net source already matches the net boundary flux, so
the pin absorbs the entire mismatch into that one cell. Concentrating ~0.1 of imbalance into 1 of 27
cells gives exactly the 0.0193 observed.

The criterion is now stated over the cells the system actually solves (excluding the pinned
reference), where the RMS collapses by 13–15 orders of magnitude to the level of the solve's own
residual. And where `p'` comes back exactly zero — an already-conservative predictor, solver at
iteration 0, 12 cases — the claim is the exact one: **nothing changed**, flux and imbalance alike.
No convergence requirement stronger than the CPU path's own is invented.

The conservation invariant itself is the exactly-representable one: every interior face is visited
once as owner and once as neighbour, every boundary face once as owner and never as neighbour, and
`(+F) + (-F) == 0.0` exactly — 8,700 cancellations. (Counting boundary faces *before* the traversal
skip is the correction 001H had to make; it is made here from the start.)

## 7. Integrated pressure path

* **L4a (controlled, bitwise):** one real solved p' — from `CPU assembly → CPU BiCGSTAB` — fed to
  **both** correction paths. 6 cases, bitwise. This qualifies the operator independently of any
  solver difference.
* **L4b (full chain):** `CPU assembly → CPU solve → correctVelocity → correctFaceMassFlux` against
  the CUDA equivalents with the qualified GPU solve. 6 cases, comparing corrected U/V/W, corrected
  face flux **and** the per-cell continuity imbalance after correction:

```text
cartesian2d 8   |dp'|=1.17e-09  |du|=4.67e-10  |dF|=8.39e-11  |dR|=9.29e-11   (37/38 iters, 0 restarts)
cartesian2d 16  |dp'|=1.51e-09  |du|=3.51e-10  |dF|=2.68e-11  |dR|=1.22e-11   (82/84 iters, 0 restarts)
graded2d 10     |dp'|=1.31e-09  |du|=5.40e-10  |dF|=6.86e-11  |dR|=7.89e-11   (50/51 iters, 0 restarts)
distorted q16   |dp'|=8.13e-10  |du|=1.36e-10  |dF|=1.09e-11  |dR|=7.14e-12   (84/89 iters, 0 restarts)
cartesian3d 4   |dp'|=2.57e-10  |du|=1.63e-10  |dF|=1.51e-11  |dR|=3.07e-11   (26/26 iters, 0 restarts)
warped 3d 3     |dp'|=1.89e-10  |du|=2.11e-10  |dF|=4.28e-11  |dR|=6.41e-11   (19/20 iters, 0 restarts)
```

Every solve converged with **0 restarts**, so the recorded GPU BiCGSTAB restart asymmetry did not
fire and nothing had to be classified against it. The criterion is stated on `p'`, the quantity the
two solves actually differ on, at their own tolerance.

## 8. Negative controls — 11/11 observable detected

`negative-control/` — each: inject → build → run → restore → rebuild → sha256 → re-run. All 11
restored to the baseline sha256 `a05aa39c…` and re-passed.

| control | mutation | detected |
| --- | --- | --- |
| K1 | correction subtracted instead of added | yes |
| K2 | owner/neighbour swapped in the p' difference | yes |
| K3 | the face jump replaced by the owner value alone | yes |
| K4 | the neighbouring face's coefficient used, breaking the shared-coefficient guarantee | yes |
| K5 | a boundary face reads the owner's p' instead of the Dirichlet 0.0 | yes |
| K6 | the correction applied twice | yes |
| K7 | the owner's p' read from the next face's owner — a misindexed/stale p' | yes |
| K8 | the explicit non-orthogonal term dropped | yes |
| K9 | the explicit term folded in: `F* + (F' + E)` instead of `(F* + F') + E` | yes |
| K10 | a density factor introduced — the double-count | yes |
| K11 | the uncorrected boundary face short-circuited instead of evaluated | yes — see §5 |

### A driver defect this gate exposed

K7's first spelling referenced `cellCount`, which is not a kernel parameter, so the **mutated**
build failed to compile — and the driver's `raise SystemExit` on a build failure aborted the run
**with the mutation still in the working tree**. It was found and the file restored to its baseline
hash, but the design was the real problem: a mutation that does not compile proves nothing about the
port, while aborting mid-mutation can leave the next build silently using mutated code.

The driver now returns from a failed *mutated* build, records it as `BUILD-FAILED`, and restores
before continuing; it still raises on a failed *restore* build, where a dirty tree is the genuine
risk; and `BUILD-FAILED` is reported as `ILL-FORMED-MUTATION`, a failure of the control set rather
than a silent pass. K7 now indexes through `faceOwner[(f + 1) % faceCount]`, which stays in range
without a cell count the kernel does not take.

## 9. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck: **0 errors** each; racecheck: **0 hazards**.
Each log was verified to contain `FACE FLUX CORRECTION EQUIVALENCE: PASS (bitwise)`, so none is
vacuous.

## 10. Files

```text
new
  include/cfd/gpu/DeviceFaceFluxCorrection.hpp       the entry point (no plan)
  cuda/kernels/DeviceFaceFluxCorrectionKernel.cu     one per-face kernel (-fmad=false)
modified
  cuda/CMakeLists.txt                                sources + the no-FMA property (12 kernels)
```

No CPU production file was modified, and no plan object was added: the operator needs only the
device mesh's owner/neighbour arrays, because `faceCoefficient`, `explicitFaceFlux`, `F*` and `p'`
are all already device-resident and qualified upstream. Nothing is recomputed.

## 11. Gate chain and regression

`all_gates.log`, `regression.log`, `regression_freshness.log`

```text
libcfdcuda.a  a6f376e53ad8d13214dd10cd14ccc4be6be92216cb43d50651b2863cee1845f1
libcfdcore.a  eae75242473f6be813fc2926f07b14c877ce28bfe787441aca47119af4e2d16f

mesh                  96 checks   momentum response      260 cases
gradients            132 cases    face flux             2628 cases
diffusion            528 cases    pressure correction   4950 cases
convection          1684 cases    velocity correction    894 cases
momentum convection 10352 cases   face-flux correction  1848 cases
boundary conditions   75 cases
momentum assembly  27744 cases                    12/12 gates green, all bitwise

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998     (45 disabled of 2043 registered)
  Total Test time (real) = 1272.35 s
  `ninja: no work to do` BEFORE and AFTER ctest; library and 001K source sha256
  identical on both sides
```

## 12. What is NOT started

Single-iteration CPU/GPU differential, integrated SIMPLE, GPU-PIPE-001 residency. Nothing committed
or pushed.
