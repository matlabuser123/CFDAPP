# P16-GPU-TURB Stage 5 — the epsilon transport equation

Status: **COMPLETE**, 8/8 tests green in `build/cuda`.

`[ ] GPU equivalence` stays unchecked. Both transport equations now have a
verified device operator; none of it runs in production yet.

---

## 1. Audit (Stage 5A)

| CPU part | where | on device |
|---|---|---|
| `Gamma_eps = mu + mu_t/sigma_eps` | `computeEffectiveDiffusivity`, `sigma_eps = 1.3` | **REUSED** — the Stage 4 kernel, different sigma |
| `epsOverK = epsilon/max(k,kFloor)` | inline in the model | **NEW**, after promotion |
| `Su = c1*epsOverK*P_k` | inline | same |
| `Sp = -c2*rho*epsOverK` | inline | same |
| convection / diffusion / Su-Sp | `cfd::transport` | **REUSED** (P14) |
| transient | not used by this solve | — |
| under-relaxation, alpha = 0.7 | `applyImplicitUnderRelaxation` | **REUSED** (P16 Stage 0) |
| **imposed cells** | `imposedEpsilon` from `computeWallTreatment` | **REUSED** — `fixImposedRowsDevice` |
| imposed-row timing | AFTER under-relaxation, BEFORE solve | reproduced |
| post-solve floor | `max(solved, epsilonFloor)` | **REUSED** — Stage 4's floor kernel |
| **post-solve re-assertion** | `solved[cell] = value`, AFTER the floor | **NEW** — `reassertImposedValuesDevice` |
| solver | BiCGSTAB via `epsilonSolver` | unchanged |

No second assembler. Two new kernels: the source pair, and the re-assertion.

### The third reachability defect, same shape as the first two

The epsilon source pair was inline in `KEpsilonModel::correct`. Promoted
verbatim to `cfd::turbulence::computeEpsilonSourceTerms`. Behaviour
preservation, measured after all three promotions (`mu_t`, k sources, epsilon
sources):

```text
turbulence unit tests            246/246 PASS
channel validation (P16 scope)    31/31  PASS   421.7 s
```

**`epsOverK` is formed once and reused by both terms**, and that is preserved
rather than tidied. Recomputing `epsilon/kSafe` for `Sp` happens to give the
same double, but writing `Su` as `c1*epsilon*production/kSafe` does **not** — it
reassociates the division past two multiplications.

---

## 2. Measured

```text
Gamma_epsilon                  0 ulp, every cell
Su, Sp                         0 ulp, every cell (kFloor binding in 3 cells)
epsilon matrix  478/490 bitwise  max value 2 ulp   structure OK both directions
epsilon RHS                      max 2 ulp
solved epsilon                 relative L2 = 9.107e-17   (bound 2.1e-12)
imposed cells                  EXACT on both arms, all 3
```

The 2 ulp and 2.1e-12 are P14's frozen numbers, inherited because the assembly
is P14's assembly. The solved field is five orders inside.

### `sigma_epsilon = 1.3` makes the closure gate non-vacuous for free

Stage 4 had to contrive a second sigma, because `sigma_k = 1.0` makes
`mu + mu_t/sigma` and `(mu + mu_t)/sigma` **identical**. Epsilon's production
sigma is 1.3, so the wrong closure is discriminable at the value that actually
ships — asserted to differ on every cell.

---

## 3. THE FINDING: the post-solve order is undetectable with realistic data

The host order is

```text
solve  ->  FLOOR  ->  RE-ASSERT imposed
```

and `solveRelaxedScalarTransport` states the reason: *"An imposed value is a
boundary condition, not a solver output, so the floor must not move it.
Re-asserted rather than assumed: a wall epsilon below the floor would otherwise
be silently replaced by the floor, and the cell would stop satisfying the wall
function that put it there."*

**Reversing those two steps is invisible unless an imposed value lies below the
floor.** Wall epsilon values in this repository's cases are hundreds to
thousands; the floor is `1e-10`. For every such value, floor-then-reassert and
reassert-then-floor give bit-identical answers. A suite built only from
realistic wall values would pass with the steps swapped, and the defect would
surface later as a wall cell quietly not satisfying its own wall function.

`ReversedPostSolveOrderIsDetected` is built around exactly that case: an imposed
value of `1e-9` against a floor of `1e-4`. It implements the reversed order on
purpose and asserts

```text
correct order    imposed cell = 1e-9        the wall value survives
reversed order   imposed cell = 1e-4        the floor destroyed it
```

and, as the non-vacuity half, that the imposed value **above** the floor is
identical under both orders — which is precisely why it cannot serve as the
discriminator.

---

## 4. Imposed rows (Stage 5D)

Verified independently of the solve:

* the three imposed rows have a unit diagonal and explicit zeros elsewhere;
* their RHS entries are the wall epsilon values exactly;
* unrelated rows are byte-identical to the un-imposed assembly;
* the imposition happens after under-relaxation and before the solve, matching
  the host.

Negative controls: **dropping** any one of the three, and **corrupting** any
one's value by `1e-6`, are each detected (3/3 and 3/3).

The imposed cells are `{0, interior, n-1}` — first row, interior, last row — so
an off-by-one at either end of the index range is caught.

---

## 5. Non-vacuity controls

| control | asserts | result |
|---|---|---|
| closure | device disagrees with `(mu+mu_t)/sigma` at the PRODUCTION sigma | PASS |
| floors bind | `kFloor` engages inside `epsOverK` (3 cells) | PASS |
| wall production → `Su` | changing production changes `Su` in that cell and no other | PASS |
| `Sp` independence | `Sp` does NOT change with production — the destruction term contains no `P_k` | PASS |
| dropped imposed row | 3/3 detected | PASS |
| corrupted imposed value | 3/3 detected | PASS |
| reversed post-solve order | detected, via a below-floor imposed value | PASS |
| determinism | same inputs, same bytes, twice | PASS |

---

## 6. Provenance (Stage 5G)

`EpsilonAssembly`, `EpsilonSolve`, `KAssembly`, `KSolve` and `WallTreatment`
all still read **`NotAttempted`**, and a test asserts it.

The epsilon system proven here is assembled from a test harness.
`KEpsilonModel::correct` remains an unconditional host call and the GPU-resident
SIMPLE loop still requires `activeModel->name() == "laminar"`. Recording CUDA
execution for those operations now would attribute it to a shipped path that has
not performed it. They flip in the integration stage, with a production caller.

## 7. Files

| file | change |
|---|---|
| `include/cfd/turbulence/KEpsilonEquation.hpp` | `EpsilonSourceTerms` + `computeEpsilonSourceTerms` promoted |
| `src/turbulence/KEpsilonEquation.cpp` | body moved here verbatim |
| `src/turbulence/KEpsilonModel.cpp` | inline loop replaced by the call |
| `include/cfd/gpu/DeviceTurbulence.hpp` | `computeEpsilonSourceTermsDevice`, `reassertImposedValuesDevice` |
| `cuda/kernels/DeviceTurbulenceKernel.cu` | two kernels, in the `-fmad=false` list |
| `tests/unit/gpu/test_device_epsilon_transport.cpp` | new — 8 tests |

Next: Stage 6, wall-function / turbulence-state integration.
