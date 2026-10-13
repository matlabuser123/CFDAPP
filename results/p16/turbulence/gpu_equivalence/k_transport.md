# P16-GPU-TURB Stage 4 — the k transport equation

Status: **COMPLETE**, 9/9 tests green in `build/cuda`.

`[ ] GPU equivalence` stays unchecked. This is one equation of the port.

---

## 1. Audit (Stage 4A)

`KEpsilonModel::correct` builds k from these parts. Every one is now mapped:

| CPU part | where | on device |
|---|---|---|
| `Gamma_k = mu + mu_t/sigma_k` | `computeEffectiveDiffusivity` (public) | **NEW** — `computeEffectiveDiffusivityDevice` |
| `Su = P_k` | inline in the model | **NEW**, after promotion — `computeKSourceTermsDevice` |
| `Sp = -rho*epsilon/max(k,kFloor)` | inline in the model | same |
| convection | `assembleScalarConvectionContribution` | **REUSED** — `assembleScalarConvectionDevice` (P14) |
| diffusion | `assembleScalarDiffusionContribution` | **REUSED** — `assembleScalarDiffusionDevice` (P14) |
| Patankar source | `applyImplicitScalarSource` | **REUSED** — `applyScalarSourceDevice` (P14) |
| transient | not used — the k solve passes no time state | **REUSED** if ever needed (P14) |
| under-relaxation, alpha = 0.7 | `applyImplicitUnderRelaxation` | **REUSED** — `applyImplicitUnderRelaxationDevice` (P16) |
| imposed cells | **NONE for k** | not applied — see §4 |
| boundary conditions | `kBoundaries_`, own BC set | **REUSED** — the plans' encoded BCs |
| post-solve floor | `max(solved, kFloor)` | **NEW** — `applyScalarFloorDevice` |
| solver | BiCGSTAB via `kSolver` | unchanged |

**No second finite-volume assembler was written.** The three new kernels are
cell-local algebra; the matrix comes from `DeviceScalarTransport`.

### The decisive finding: k has NO imposed cells

`KEpsilonModel.cpp` passes `imposedEpsilon` to the **epsilon** solve only, and
says why in its own words:

> *"Only epsilon is imposed. k keeps its own boundary condition and responds to
> the modified production — imposing both over-constrains the wall cell."*

So the wall treatment reaches k through exactly **one** channel: the production
field, whose wall-cell entries are replaced with the equilibrium expression
before `Su` is built. Stage 4F is therefore not "port the imposed rows for k" —
it is "prove k has none, and prove the one channel that does exist works".

### A second reachability defect, same shape as mu_t's

The k source pair was **inline in `KEpsilonModel::correct`**, so a device port
could only be compared against a re-typed copy of the two lines. Promoted
verbatim to `cfd::turbulence::computeKSourceTerms` (`KEpsilonEquation.hpp`),
and the model now calls it. Same reasoning as the `mu_t` promotion in Stage 1–3:
a re-typed copy agrees with a wrong port for the same reason it agrees with a
right one.

Behaviour preservation, measured after BOTH promotions:

```text
turbulence unit tests            246/246 PASS
channel validation (P16 scope)    31/31  PASS   420.2 s
```

The channel suite carries the adopted Re_tau = 590 accuracy gates, so a change
to `Sp` or `mu_t` could not have passed it silently.

---

## 2. The frozen gate, and where each threshold came from

| gate | criterion | origin |
|---|---|---|
| 4B `Gamma_k` | BITWISE, 0 ulp | cell-local algebra, no association freedom |
| 4C `Su` / `Sp` | BITWISE, 0 ulp | same |
| 4D structure | exact, **both directions** | structural property, no tolerance is meaningful |
| 4D coefficients / RHS | ≤ 2 ulp | **P14's measured, frozen bound** for this assembly |
| 4E solved k | ≤ 2.1e-12 relative L2 | **P14's measured, frozen bound** |
| 4F wall | no imposed rows; production reaches RHS only | the model's own stated design |

Nothing here was derived after seeing a result. 4B/4C are bitwise because there
is no mechanism for a bit to differ. 4D/4E inherit P14's numbers because the
assembly they exercise **is** P14's assembly — the association-order difference
was measured and frozen there, and re-deriving it here would be inventing a
threshold to fit.

---

## 3. Measured

```text
Gamma_k                       0 ulp, every cell
Su, Sp                        0 ulp, every cell (kFloor binding in 3 cells)
k matrix   473/498 bitwise    max value 2 ulp    structure OK both directions
k RHS                         max 2 ulp
solved k                      relative L2 = 2.385e-16   (bound 2.1e-12)
```

The solved field is four orders inside the frozen bound. The 25 non-bitwise
coefficients are diagonals where convection and diffusion both contribute — the
exact population P14 identified, at the exact bound P14 measured.

### The `Gamma_k` closure discriminator

`sigma_k = 1.0` in production, and at `sigma = 1` the two candidate closures

```text
mu + mu_t/sigma        (correct)
(mu + mu_t)/sigma      (wrong)
```

are **identical**. A gate run only at `sigma_k` would pass for a kernel
implementing the wrong one. So the test also runs at `sigma = 1.3` and asserts
the device result **differs from the wrong closure on every cell**.

---

## 4. Wall semantics (Stage 4F)

Two assertions, both about what must NOT happen:

* the k row of a wall-adjacent cell keeps its assembled coefficients — diagonal
  not 1, off-diagonals live. If that row ever becomes `{1, 0, 0, …}` someone has
  applied epsilon's imposed-row treatment to k, which the model explicitly
  rejects as over-constraining;
* replacing the wall cell's production **changes the RHS and leaves the matrix
  byte-identical**. `Su` is an explicit source; if it reached the matrix the
  wall treatment would be altering the operator rather than the forcing.

And `Su` is asserted to be the production field **verbatim**, cell by cell. A
kernel that recomputed `mu_t * S^2` internally would silently discard the wall
replacement while still producing a plausible source field.

---

## 5. Non-vacuity controls

| control | asserts | result |
|---|---|---|
| closure | the device disagrees with `(mu+mu_t)/sigma` on every cell | PASS |
| fail-closed | non-finite / negative `mu_t`, `mu <= 0`, `sigma <= 0` all throw, matching the host | PASS |
| floor binds | `Sp`'s denominator floor engages (3 cells), and the post-solve floor engages | PASS |
| one-ulp perturbation | a single coefficient moved by `nextafter` is DETECTED | PASS |
| RHS perturbation | `+1e-9` on one entry is DETECTED | PASS |
| stale off-diagonal | a device-only entry made non-zero is DETECTED by the bidirectional check | PASS |
| production→RHS only | changing `Su` must not change the matrix | PASS |
| determinism | same inputs, same bytes, twice | PASS |

---

## 6. What this stage does NOT claim

**The execution trace still reads `KAssembly` and `KSolve` as `NotAttempted`,
and that is correct.** The device k assembly proven here runs from a test
harness, not from production: `KEpsilonModel::correct` is still an
unconditional host call, and the GPU-resident SIMPLE loop still refuses every
model but `laminar`. Flipping those trace entries now would record an execution
that no shipped code path performs.

They flip in Stage 7 (production integration), when a real caller exists. The
trace gate for Stage 4 is therefore the honest partial state:

```text
StrainRate, Production, TurbulentViscosity   Gpu
KAssembly, KSolve, EpsilonAssembly,
EpsilonSolve, WallTreatment                  NotAttempted
```

Also not claimed: epsilon transport, the wall treatment itself, an integrated
turbulent GPU solve, benchmark equivalence, residency.

## 7. Files

| file | change |
|---|---|
| `include/cfd/turbulence/KEpsilonEquation.hpp` | `KSourceTerms` + `computeKSourceTerms` promoted |
| `src/turbulence/KEpsilonEquation.cpp` | body moved here verbatim |
| `src/turbulence/KEpsilonModel.cpp` | inline loop replaced by the call; same values, same order |
| `include/cfd/gpu/DeviceTurbulence.hpp` | `computeEffectiveDiffusivityDevice`, `computeKSourceTermsDevice`, `applyScalarFloorDevice` |
| `cuda/kernels/DeviceTurbulenceKernel.cu` | three kernels, in the `-fmad=false` list |
| `tests/unit/gpu/test_device_k_transport.cpp` | new — 9 tests |

Next: Stage 5, epsilon transport — which **does** use imposed cells, so it
exercises `fixImposedRowsDevice` and the post-solve re-assertion that k does not
need.
