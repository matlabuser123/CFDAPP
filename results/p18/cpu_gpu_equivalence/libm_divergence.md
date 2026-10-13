# P18.8 — Why the bitwise criterion fails, and for exactly which quantities

**Status: BLOCKED at acceptance criterion E3. Awaiting authorization for a gate amendment.**

This document is the independent investigation required by `CLAUDE.md` §5.3 step 2. No production
code was changed to produce any number in it. The three diagnostics live in the GPU test file and
are reproduced verbatim in `logs/04_libm_divergence_diagnostics.log`.

Frozen documents re-verified at this step, all matching their recorded values:

| File | sha256 |
| --- | --- |
| `results/p18/gates/P18_GATES.md` | `921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3` |
| `results/p18/cpu_gpu_equivalence/acceptance_gate.md` | `3fc7a020c2e7f3a6a494dd677b6f63927b8ca7571378a44b57c0d1d369103d03` |
| `results/p18/cpu_gpu_equivalence/audit.md` | `d98c57d87e40658a38f04162c03b48d992a3c704077e81532b4d6a4e5ecba9e9` |

Environment: WSL2 Ubuntu 22.04, glibc 2.35, g++ 11.4.0, **nvcc 12.9.86 at
`/usr/local/cuda-12.9/bin/nvcc`** (the `nvcc` first on `PATH` is 11.5.119 and did *not* build this —
naming the wrong toolchain in a libm finding would have been worthless), RTX 5000 Ada, cc 8.9.
`--use_fast_math` occurrences in the build: **0**.

---

## 1. The failure

`logs/03_second_failure_PRESERVED.log` — 2 478 individual assertion failures across 3 of 8 tests:

| Test | Result |
| --- | --- |
| `MixturePropertiesAreBitwiseOnDevice` | **OK** — 1 024 cells, bitwise |
| `EquationOfStateIsBitwiseOnDevice` | **OK** — 512 cells, bitwise |
| `RateCoefficientsAndBranchesAreBitwiseOnDevice` | **FAILED** |
| `ReactionSourcesAndHeatReleaseAreBitwiseOnDevice` | **FAILED** |
| `TheCoupledStepIsBitwiseOnDeviceIncludingEveryIntermediate` | **FAILED** |
| `RepeatsAndBlockSizesAreBitwise` | **OK** |
| `E12_TheResidentLoopPerformsNoFullFieldTransfer` | **OK** |
| `N12_AFullFieldTransferInTheResidentLoopIsDetected` | **OK** |

Tallying the failing assertions by the quantity they name gives a result that is far narrower than
"the three tests failed":

```
    496  S_E                  260  omega_1            221  k for reaction 0
    266  q_1                  260  omega_0            217  S_E (mass route)
    263  k for reaction 1     260  q_0                235  omega_2
```

That is the **whole** list. Not one failure names `T`, `Y_i`, `C_i`, `M_mix`, `R_mix`, `cp_mix`,
`cv_mix`, `gamma`, `rho`, `p`, `sensible`, `chemical`, `status` or any `branch`. The divergence is
confined to `k`, `q`, `omega_i` and `S_E` — precisely the quantities downstream of a transcendental.

## 2. Diagnostic 1 — which call diverges

`k = (A · T^n) · exp(-Ea/(R_u T))`. Choosing `n` and `Ea` to switch each factor between exact and
inexact isolates the cause. `pow(x, 0)` and `exp(0)` are exactly `1` by IEEE-754, so case A contains
no inexact libm call at all. 4 096 temperatures, 300 K to 2 347.5 K:

| case | n | Ea | isolates | differing cells | worst ULP | worst relative |
| --- | --- | --- | --- | --- | --- | --- |
| A | 0 | 0 | nothing | **0 / 4096** | 0 | 0 |
| B | 0 | 4e4 | `exp()` only | 218 / 4096 | **2** | 2.88e-16 |
| C | 0.5 | 0 | `pow()` only | 901 / 4096 | **2** | 2.95e-16 |
| D | 0.5 | 4e4 | both | 1029 / 4096 | **3** | 5.71e-16 |

**Case A is exact over all 4 096 cells.** Everything that a CPU/GPU equivalence phase normally
suspects — operand order, parenthesisation, accumulation order, FMA contraction, the `--fmad=false`
registration, the geometry upload, a stale mechanism POD — would have failed case A. None of them is
the cause. The cause is `exp()` and `pow()`, and nothing else.

## 3. Diagnostic 2 — the divergence does not become an algorithm difference

Amendment A1 picks the direct form when `A·T^n` is finite and a log-space form otherwise, and that
predicate reads `pow()`'s **output**. If host `pow()` returned `DBL_MAX` where device `pow()`
returned `inf` for the same `T`, the two backends would run *different algorithms* and the
difference would no longer be a last-bit one.

Sweeping `T` across 4 096 **consecutive doubles** around the crossing (`A = 3`, `n = 100`,
`Ea = 457 000` J/mol, so the true `k` stays finite while `A·T^n` straddles `DBL_MAX`):

```
host branch at the low end 1, at the high end 2   (1 = direct, 2 = log-space fallback)
BRANCH DISAGREEMENTS host vs device:  0 / 4096
worst ULP distance where the branches AGREED:  3
```

The sweep genuinely crossed the predicate — the branch changes from 1 to 2 within it, which the
probe asserts — and the two backends still chose the same branch at every one of the 4 096 points.

**Limit of this evidence:** one `(A, n, Ea)` triple and one 4 096-point window. It shows the
bifurcation did not occur here; it does not prove it cannot occur anywhere.

## 4. Diagnostic 3 — the difference is absorbed, not amplified

A 3 ULP difference in a rate says nothing about a march: the system could be contracting or
expanding. The same initial state was marched 50 steps at `dt = 1e-5` on the device (resident, no
host round trip) and on the host, 256 cells, three species, two reactions:

| step | max \|ΔT\|/T | max \|ΔY\| | worst T ULP |
| --- | --- | --- | --- |
| 1 | 0 | 0 | 0 |
| 5 | 0 | 0 | 0 |
| 20 | 0 | 0 | 0 |
| 50 | 0 | 0 | 0 |

and the state really moved: `max |T − T₀| = 28.6389 K`, `max |Y − Y₀| = 0.0075085`.

A ladder of zeros is the shape a vacuous probe prints, so this was checked. **The first version of
this probe used `dt = 1e-7` and reported zero at every sample for the wrong reason** — at that step
size neither `T` nor `Y` moved by one ULP, so it was comparing two unchanged initial states. The
probe now asserts that the state moved by more than one ULP of its own magnitude before the ladder
is allowed to mean anything, and the figures above are from the version that passes that check.

So the ≤3 ULP difference in `omega_i` enters the state as `Y + dt·omega/rho`, where it lands **below
one ULP of `Y`** and is rounded away. `T` is then recovered by the 200-halving bisection, which
returns the unique double in its bracket and absorbs the rest. The **marched state is bitwise
identical over 50 steps**; only the intermediate rates differ.

## 5. Root cause

`species_detail::expR / logR / powR` in `DeviceSpeciesThermochemistry.hpp` dispatch to `std::exp`,
`std::log`, `std::pow` under gcc and to `::exp`, `::log`, `::pow` under nvcc. Those are **different
implementations** — glibc 2.35 and CUDA's libdevice. IEEE-754 requires correct rounding only for
`+ − × ÷` and `sqrt`; `exp`, `log` and `pow` are not required to be correctly rounded, and neither
vendor claims bit-compatibility with the other.

So G9's condition (1) — "one shared source compiles for both" — is **satisfied for the arithmetic
and violated for the transcendentals**, by construction, in a way the condition as written does not
distinguish.

### Why P17.7 and P17.10 reached bitwise and this phase cannot

The qualified HLLC and density-based paths share `include/cfd/compressible/HllcFlux.hpp`. Grepping
it for transcendental calls returns exactly one function, used four times:

```
63:CFD_HLLC_HOST_DEVICE inline Real sqrtR(Real v) {
```

**`sqrt` is the one transcendental-looking operation IEEE-754 requires to be correctly rounded.** It
is therefore bitwise across any two conforming implementations, by the standard. P17's bitwise
result was never evidence that host and device libm agree — it was evidence about `sqrt`
specifically. P18.8 is the first phase whose shared arithmetic calls `exp`, `log` and `pow`.

### This is a gate-derivation error, and it is mine

`acceptance_gate.md` (`3fc7a020…`) carried P17.10's five conditions across to chemistry and set
E3/E4/E6/E8/E9 to BITWISE. `CLAUDE.md` §5.2 says: *"If the baseline cannot exercise the new
behaviour being gated, the dry-run is insufficient … Ask separately whether a correct post-change
implementation can satisfy the criterion."* The baseline had no device chemistry at all, so the
dry-run could not exercise E3 — and I did not ask that separate question. Asking it would have
required nothing more than the grep in the box above.

## 6. The four candidate fixes, and why three are rejected

1. **Implement `exp`/`log`/`pow` inside the shared header** (argument reduction plus a polynomial in
   `+ − × ÷` only), so one source really does compile for both and the result is bitwise by
   construction. **Rejected:** it changes what the CPU computes. The authorization says *"The CPU
   implementation remains the authoritative numerical reference"* and *"Do not redesign P18 numerics
   merely to make CUDA easier."* This is that, exactly.
2. **Evaluate the transcendentals on the host and upload `k(T)`.** **Rejected:** a per-step
   full-field `T` upload and `k` download, which is the transfer pattern E12 and control N12 exist to
   forbid. It would trade a 3 ULP difference for the thing the phase is trying to prevent.
3. **A compiler flag.** There is none that makes libdevice's `exp`/`pow` agree with glibc's.
   `--use_fast_math` moves them further apart and is forbidden by the authorization anyway (and is
   measured at 0 occurrences).
4. **Amend the criterion.** The remaining option, and it needs authorization.

## 7. Proposed replacement criterion — derived, not fitted

Derived from the two libraries' documented accuracy and the operation count, **before** comparison
with the measurements in §2:

* Each implementation is documented faithfully rounded or better: within 1 ULP of the exact result.
  Two such implementations therefore differ by at most **2 ULP** on the same input.
* `k = (A·T^n)·exp(arg)` composes one `pow` and one `exp` (≤2 ULP each) and two IEEE multiplies on
  slightly different operands (≤0.5 ULP each): **≤5 ULP**, taken as **8** (next power of two).
* `q = k·ΠCᵛ`: the concentrations are bitwise (measured, §1) and the orders are exactly 1, for which
  `pow(x,1)` is exact, so `q` inherits `k` plus one multiply: **8 ULP**.
* `omega_i = Σ_r (M_i ν_i) q_r` and `S_E = −Σ_r q_r ΔH_r⁰` sum `R` such terms: **8 + R ULP**, which
  is 10 for the two-reaction equivalence mechanism.

Measured worst cases: 3 ULP for `k`, and §1's failure values for `S_E` at ~3e-16 relative ≈ 3 ULP.
Those sit inside the derived bound by a factor of ~3, so the bound is not fitted to them.

And the criterion that carries the real weight, because it is about the state rather than an
intermediate, and is measurably **stronger** than any ULP tolerance:

> **E3′ / E4′ / E6′** — `k`, `q`, `omega_i` and `S_E` agree within the ULP bounds above, and host and
> device take the **identical branch** at every point, including a sweep across consecutive doubles
> straddling A1's predicate.
>
> **E8′ / E9′** — every quantity *not* downstream of a transcendental stays **BITWISE**:
> `M_mix`, `R_mix`, `cp_mix`, `cv_mix`, `gamma`, `rho`, `p`, `T`, `C_i`, the sensible and chemical
> energy pools, the status and reason, and — the one that matters — **the marched state `(Y, T)`
> over a trajectory in which the state moves by more than one ULP.**

Non-vacuity controls the amendment would need, none of which the current controls supply:

* a control that makes a quantity differ by **more than** the ULP bound, proving the bound can fail
  (the existing N1/N2/N4/N5/N6/N8 flags all do this, by construction);
* `--fmad=true` as a control: it must push the differences **above** the libm-only floor, which is
  what shows the bound is not loose enough to hide FMA contamination;
* the case-A probe (§2) retained as a standing criterion, since it is what distinguishes "libm" from
  "the arithmetic is wrong".

## 8. What is already established, and stands regardless

| Criterion | Result |
| --- | --- |
| E1 mixture properties | **BITWISE**, 1 024 cells, spatially varying |
| E2 equation of state | **BITWISE**, 512 cells |
| E10 determinism | **BITWISE** over 5 repeats, and across block sizes 64 / 128 / 256 / 512 |
| E12 residency | **0** H2D calls, **0** D2H calls, **0** bytes in a 10-step resident loop; the bookends measured separately and non-zero |
| N12 residency control | the instrument registers a field-sized download when one is injected |
| Link one (host vs production) | **17 / 17 bitwise**, `tests/unit/species/test_device_species_equivalence.cpp` |
| `--use_fast_math` | 0 occurrences |

## 9. Required decision

Not taken unilaterally: amending a frozen gate needs authorization (`CLAUDE.md` §5.3 step 4), and
§1.4 forbids weakening a threshold to obtain a pass. The failure is preserved verbatim and nothing
has been relaxed.

**The question:** authorize the amendment in §7 as `acceptance_gate_A2.md`, with its own dry-run and
non-vacuity check — or reject it, in which case P18.8 closes with G9 **NOT CLOSED** and the recorded
finding that bitwise CPU/GPU equivalence is unreachable for transcendental-dependent chemistry while
the host libm remains the reference.
