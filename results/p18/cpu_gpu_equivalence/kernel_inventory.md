# P18.8 — Kernel inventory (§3, §40)

Every device kernel P18.8 adds, what it computes, and what it deliberately does not.

## 1. Production kernels — `cuda/kernels/DeviceSpeciesKernel.cu`

Compiled with **`--fmad=false`** (verified from Ninja's own command line, not from the CMake text).
One thread per cell. No shared memory, no atomics, no `__syncthreads`, no cooperative groups, no warp
shuffles — all five measured as 0 occurrences in `logs/09_E13_cuda_diagnostics.log`.

| # | Kernel | Computes | Launcher |
| --- | --- | --- | --- |
| 1 | `mixturePropertiesKernel` | `M_mix`, `R_mix`, `cp_mix`, `cv_mix`, `γ`, + status | `computeMixturePropertiesDevice` |
| 2 | `equationOfStateKernel` | `ρ(p,T,Y)`, `p(ρ,T,Y)`, `T(p,ρ,Y)` | `computeEquationOfStateDevice` |
| 3 | `rateCoefficientKernel` | `k(T)` per reaction, **the branch taken**, + status | `computeRateCoefficientsDevice` |
| 4 | `reactionSourcesKernel` | `C_i`, `q_r`, `ω_i`, `Σω`, `S_E` by both routes, + status | `computeReactionSourcesDevice` |
| 5 | `advanceReactorKernel` | one coupled step in P18.6's frozen order, **every intermediate written out** | `advanceReactorDevice` |

**Where the arithmetic lives.** Not in the `.cu`. Every formula is in
`include/cfd/gpu/DeviceSpeciesThermochemistry.hpp`, marked `CFD_HLLC_HOST_DEVICE`, which gcc and
nvcc both compile — G9's condition (1). The kernels are launch geometry and a gather, nothing else.
That is also why the equivalence suite's first link runs in the **CPU-only** configuration.

**Residency.** Every field is a caller-owned `DeviceBuffer`. The launchers never allocate, never
upload and never download, so a per-step full-field transfer cannot hide inside one. Measured: 0
H2D calls, 0 D2H calls, 0 bytes across a 10-step resident loop (E12), with control N12 proving the
instrument registers an injected field-sized transfer.

**Layout.** Composition is **species-major** — `N_species` blocks of `N_cells` — matching P18.1's
host `MassFractionState` (a `ScalarField` per species), so the upload is a straight copy per species
and no transpose exists anywhere.

## 2. Control kernels — `cuda/kernels/DeviceSpeciesFmadControl.cu`

Compiled with **`--fmad=true`**. **Never called by production**; the only caller is the equivalence
suite, and every name carries `FmadControl` so a stack trace or a grep cannot mistake it for a solver
path.

| Kernel | Purpose |
| --- | --- |
| `mixturePropertiesFmadKernel` | byte-identical body to #1 above, contraction enabled |
| `reactionSourcesFmadKernel` | byte-identical body to #4 above, contraction enabled |
| `fmadDetectKernel` | `a*b+c` on a dyadic triple where fused and unfused answers differ, so "the control is really contracting" is measured on the device rather than assumed |

Why a second translation unit rather than a flag: a compile option cannot be a runtime flag, and the
bodies must be identical or the comparison would attribute to the edit instead of to the flag. This
is the control that carries **G9 condition (4)**; see `negative_controls.md`.

## 3. What is NOT on the device, and why

| Not implemented | Why |
| --- | --- |
| **Species transport** — convection and diffusion of `Y_i`, and the `ΣJ_i` correction | Out of P18.8 scope, recorded in `audit.md` §5 before any code was written. The device has the **thermochemistry** only. This is why the frozen control **N3** (`ΣJ_i` omitted) is marked **N/A** in the gate itself rather than left unimplemented. |
| A device reacting-flow solver | G8 is not closed by any P18 phase; see `TODO.md`. P18.8 qualifies operators and a closed reactor, not a flow solve. |
| A stiff / implicit integrator | The reactor march is explicit and first order. Its step-size budget is a measured limit (P18.1's sum-to-one tolerance), not a device matter. |
| Device-side reductions | Nothing here reduces across cells, which is why G9's conditions (2) and (5) hold vacuously. |

## 4. Capability limits of the device restatement — read this before claiming equivalence

These are gaps between what the CPU production path supports and what the device PODs can carry.
Each one bounds the *scope* within which P18.8's equivalence result holds.

1. **Constant `cp` only.** `DeviceSpecies` carries one `Real specificHeat` per species. The CPU
   `MixtureThermodynamics` supports a **ranged / polynomial** `cp(T)` — P18.6 fixed a real defect in
   exactly that machinery (temperature recovery bracketing outside the polynomial's validity range).
   **So P18.8's equivalence is established for constant-`cp` mixtures, and says nothing about the
   ranged-`cp` path.** This also makes E1's frozen "× 5 temperatures" clause inert on device
   (`acceptance_gate_A2_erratum.md` §3).
2. **Fixed capacities.** `kMaxDeviceSpecies = 8`, `kMaxDeviceReactions = 4`. A larger mechanism
   cannot be uploaded. The suite asserts the test mechanisms fit (`P188RestatementTest`).
3. **`h_ref = 0`.** The sensible-enthalpy restatement is exact only for the constant-`cp`,
   zero-reference-enthalpy form the suite constructs.
4. **Reaction orders.** The suite's mechanisms use integer orders of 1, for which `pow(x,1)` is
   exact. Fractional orders would route through `pow` and acquire the same libm difference `k` and
   `q` already carry — bounded by A2, but not measured here.

## 5. The one place the bitwise claim does not reach

`species_detail::expR / logR / powR` dispatch to glibc under gcc and to CUDA libdevice under nvcc.
Those are different implementations, and IEEE-754 requires correct rounding only for `+ − × ÷` and
`sqrt`. So G9's condition (1) holds for the arithmetic and is violated for the transcendentals, by
construction. Measured consequence: `k`, `q`, `ω` and `S_E` differ by ≤ 3 ULP; everything else is
bitwise, including the marched state. Full diagnosis in `libm_divergence.md`; the amended criteria
are in `acceptance_gate_A2.md`.

P17.7 and P17.10 reached bitwise because their shared header calls **only `sqrt`**, four times.
