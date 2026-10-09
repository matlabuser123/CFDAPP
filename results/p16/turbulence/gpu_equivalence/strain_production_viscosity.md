# P16-GPU-TURB — strain rate → P_k → mu_t

The first turbulence physics that executes on the device. Three of the eight
traced operations now report `Gpu`; the other five still report `NotAttempted`,
which is the honest state.

Status: **COMPLETE**, 7/7 tests green in `build/cuda`.

---

## 1. Audit findings

### 1a. The velocity gradient was ALREADY ported and qualified

`computeVelocityGradientDevice` (`include/cfd/gpu/DeviceMomentumConvection.hpp:173`)
describes itself as the *"CUDA port of `discretization::computeVelocityGradient`
(GreenGauss branch) — exposed so the differential can compare it directly, not
only through LinearUpwind."* And `KEpsilonModel::correct` calls
`computeVelocityGradient(mesh, velocity, velocityBoundaries_)` with the default
scheme, which is GreenGauss.

**Same operator.** So this stage does not re-port gradients — the same shape as
P14, where the two hard halves already existed and the milestone was a
promotion rather than a construction.

### 1b. The mu_t formula had NO callable API — a reachability defect

`computeTurbulentViscosityField` lived in an **anonymous namespace inside
`src/turbulence/KEpsilonModel.cpp`** and was declared in no header. The formula
production runs had exactly one caller, in one translation unit.

That blocks an honest differential. A CPU/GPU comparison for `mu_t` would have
had nothing to compare the device against except a **re-typed copy** of the
expression — and a re-typed copy agrees with the device for the same reason it
would agree with a *wrong* device: both came from the same fingers, not from the
code that ships.

**Resolved by PROMOTION, not rewrite.** The body was moved verbatim to
`cfd::turbulence::computeTurbulentViscosityField` in `TurbulenceProduction.hpp`
— its natural home, which already owns the other two eddy-viscosity algebra
functions. `KEpsilonModel` calls it by the same unqualified name (same
namespace), so no call site changed. No size validation was added: that would
be a behaviour change in a phase not authorized to make one.

Behaviour preservation, measured:

```text
turbulence unit tests                246/246 PASS
channel validation (P16 scope)        31/31  PASS   422.6 s
```

The channel suite includes the adopted Re_tau = 590 accuracy gates, so a change
to `mu_t` could not have passed it silently.

### 1c. What was genuinely new

Three cell-local algebraic steps, and nothing else:

```text
S^2   = 2(du/dx)^2 + 2(dv/dy)^2 + (du/dy + dv/dx)^2
P_k   = mu_t * S^2
mu_t  = density * cMu * kSafe * kSafe / epsSafe
```

---

## 2. The frozen gate

Written down before any number was measured.

### G1 — BITWISE, 0 ulp, against the production CPU functions

Not a tolerance, and deliberately stricter than P14's. P14's assembly allows
≤ 2 ulp on diagonals because the host and device accumulate face contributions
in **different orders**, and floating-point addition is not associative. These
three steps have no reduction and no association-order freedom: every cell is
one short expression over its own data. There is no mechanism by which a single
bit could legitimately differ, so a tolerance here would only hide a
transcription error.

Operand order is reproduced term for term. Three places where an algebraically
equal rearrangement is a different double:

```text
shear formed FIRST and squared once   (dudy + dvdx)^2
                                      NOT dudy^2 + 2 dudy dvdx + dvdx^2
mu_t left-to-right                    (((density*cMu)*kSafe)*kSafe)/epsSafe
                                      NOT density*cMu*(kSafe*kSafe/epsSafe)
P_k with mu_t as the LEFT operand     turbulentViscosity[i] * s2[i]
```

`DeviceTurbulenceKernel.cu` is in `cuda/CMakeLists.txt`'s `-fmad=false` list,
like every other qualified kernel, so nvcc cannot contract `a*b + c` into an FMA
and produce a different — more accurate — number than the CPU's rounded
multiply-then-add.

### G2 — execution evidence, including the honest negative half

Exactly `StrainRate`, `Production` and `TurbulentViscosity` report `Gpu`. The
other five (`KAssembly`, `KSolve`, `EpsilonAssembly`, `EpsilonSolve`,
`WallTreatment`) are **asserted to still report `NotAttempted`**. That assertion
fails the moment anyone claims coverage this stage has not built.

### G3 — the floors must activate

At least one state per floor where the floor **changes the answer**, proven
against the unfloored formula.

---

## 3. Measured

```text
grid sizes n = 1, 7, 108, 1000        S^2, mu_t, P_k     0 ulp, every cell
```

Bitwise on all four sizes, including `n = 1` (a single thread) and `n = 1000`
(multi-block, 4 blocks of 256).

### Floor coverage

Seven states, chosen to straddle the floors rather than sit far below them:

| k | epsilon | which floor binds |
|---|---|---|
| 0.5 | 0.3 | neither |
| 1e-14 | 0.3 | k |
| 0.5 | 1e-15 | epsilon |
| 1e-20 | 1e-30 | both |
| 1e-10 | 0.3 | exactly at the k floor — must NOT bind |
| 0.0 | 1e-10 | k; epsilon exactly at its floor |
| −3.0 | 0.0 | both, from a negative k |

All bitwise against the host. Every result finite — which is the reason the
floors exist. The `1e-10` rows matter: `std::max(a, b)` returns `a` unless
`b > a`, so a cell sitting exactly on the floor must keep its own value, and a
kernel using `>=` instead of `>` would pass every other row and fail these.

`fmax` is deliberately **not** used in the kernel: it differs from `std::max` on
a NaN input, so a NaN `k` would silently become the floor on the device and stay
NaN on the host — the two backends disagreeing precisely where a solve had
already gone wrong and the difference would be hardest to read.

---

## 4. Non-vacuity controls

| control | asserts | result |
|---|---|---|
| NV1 | forcing each of the three operations onto the host is DETECTED, **while the numbers still agree bitwise** | PASS (3/3) |
| NV1b | forcing one operation does not downgrade the other two | PASS |
| NV2 | perturbing one input cell changes that cell's output and **no other**, and the host agrees on the perturbed field | PASS |
| NV3 | the floored result DIFFERS from the unfloored formula, so G3 is not satisfied by a floor that never binds | PASS |
| determinism | the same inputs produce the same bytes twice, n = 256 | PASS |
| promotion tie-back | the promoted `mu_t` reproduces the documented relation, and runs in a CPU-only build too | PASS |

**NV1 is the one that matters most.** The forced-fallback arm computes the
*correct* numbers — the caller runs the production CPU function — and the gate
must still fail. That is the realistic shape of a fallback: nothing looks wrong,
the physics is right, and only the trace knows CUDA was never touched. A gate
that compared numbers alone would wave it through.

---

## 5. The fallback contract, settled here

The first version of this stage had the injector make a primitive silently
no-op. The test caught the consequence immediately: a forced `StrainRate`
fallback left `S^2` **unsized**, and the downstream `P_k` call threw on a length
mismatch instead of reaching the gate.

The contract is now explicit: a forced fallback **records `Cpu` and returns
without writing its output; the CALLER owns the host path.** That matches what
the production integration will do — the k-epsilon step chooses device or host
for the whole operation, it does not expect a primitive to substitute one for
the other behind its back. The output is left unsized rather than zero-filled on
purpose: a downstream call then fails loudly instead of consuming a
plausible-looking buffer of zeros and producing a wrong answer that still looks
like turbulence.

---

## 6. Scope

Unchanged from the frozen P16-GPU-TURB scope. Explicitly NOT in this stage:

* the wall treatment's **replacement** of production in wall cells — a separate
  traced operation (`WallTreatment`) and a later step. `KEpsilonModel` applies
  it after `computeTurbulentProduction` on the host, and the device caller must
  do the same;
* k and epsilon transport;
* anything touching SST or P20.

## 7. Files

| file | change |
|---|---|
| `include/cfd/turbulence/TurbulenceProduction.hpp` | `computeTurbulentViscosityField` promoted (declaration) |
| `src/turbulence/TurbulenceProduction.cpp` | body moved here verbatim |
| `src/turbulence/KEpsilonModel.cpp` | anonymous-namespace duplicate deleted; call sites unchanged |
| `include/cfd/gpu/DeviceTurbulence.hpp` | new — three device primitives + the fallback contract |
| `cuda/kernels/DeviceTurbulenceKernel.cu` | new — three kernels, in the `-fmad=false` list |
| `tests/unit/gpu/test_device_turbulence_algebra.cpp` | new — 7 tests |
| `cuda/CMakeLists.txt`, `tests/unit/gpu/CMakeLists.txt` | registration |

Next: k transport on device, reusing `DeviceScalarTransport` with the
under-relaxation and imposed-row semantics already qualified.
