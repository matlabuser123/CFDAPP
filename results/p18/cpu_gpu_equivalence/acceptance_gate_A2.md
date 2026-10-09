# P18.8 — Acceptance Gate, Amendment A2

**Amends:** `results/p18/cpu_gpu_equivalence/acceptance_gate.md`, sha256
`3fc7a020c2e7f3a6a494dd677b6f63927b8ca7571378a44b57c0d1d369103d03` — which is **not edited**. That
file and its criteria E1–E13 stand as written; this file replaces criteria **E3, E4, E6, E8 and E9**
and leaves the rest untouched.

**Authorized by the user, 2026-10-02**, after the failure was preserved and the cause diagnosed.
**Nothing in the original gate was relaxed before that authorization.**

- Original failure, verbatim: `logs/03_second_failure_PRESERVED.log` (2 478 assertions)
- Diagnosis and the derivation: `libm_divergence.md`, diagnostics in `logs/04_*`
- **Pre-freeze dry-run: `logs/06_A2_dryrun_PRE_FREEZE.log`** — 6/6, and it rejected four drafts of
  this amendment before this one

---

## 1. Why the original criteria could not be met

`std::exp` / `std::pow` (glibc 2.35) and `::exp` / `::pow` (CUDA 12.9 libdevice) are different
implementations. IEEE-754 requires correct rounding only for `+ − × ÷` and **`sqrt`**, so two
conforming libms may legitimately differ in the last bits of `exp`, `log` and `pow`. The shared
header's `species_detail::expR / logR / powR` shims dispatch to one under gcc and the other under
nvcc, so G9's condition (1) — one shared source — holds for the *arithmetic* and is violated for the
*transcendentals*, by construction.

P17.7 and P17.10 reached bitwise because their shared header calls **only `sqrt`**, four times
(`include/cfd/compressible/HllcFlux.hpp:63`). That result was evidence about `sqrt`, not about libm
agreement. P18.8 is the first phase whose shared arithmetic calls `exp`, `log` and `pow`.

Removing the difference would mean replacing the host libm, which the P18.8 authorization forbids
("the CPU implementation remains the authoritative numerical reference"; "do not redesign P18
numerics merely to make CUDA easier").

## 2. The derivation — stated before any comparison

From the two libraries' documented accuracy and the operation count, **not** from the observed
differences:

* Each implementation is documented faithfully rounded or better: within 1 ULP of the exact result.
  Two such implementations therefore differ by at most **2 ULP** on the same input.
* `k = (A·T^n) · exp(−Ea/(R_u·T))` composes one `pow` and one `exp` (≤ 2 ULP each) and two IEEE
  multiplies on slightly different operands (≤ 0.5 ULP each): ≤ 5 ULP, taken to the next power of
  two → **8 ULP**.
* `q = k_f·ΠC′ − k_r·ΠC″`. The concentrations are bitwise (measured) and every order is exactly 1,
  for which `pow(x,1)` is exact, so each term inherits `k`'s bound plus one multiply. The bound is
  stated against the **sum of the term magnitudes**, not against `|q|`:

  > `|q_device − q_host| ≤ 8 · ε · ( |k_f·ΠC′| + |k_r·ΠC″| )`

  Bounding by `ε·|q|` would tighten without limit as a reversible reaction approaches equilibrium
  and the two terms cancel, which is a property of the cancellation and not of the implementations.

**Everything downstream of `q` keeps a zero tolerance**, by changing *what* is compared rather than
*how closely*. See E6′.

## 3. The amended criteria

`N_cells` is the cell count, `ε = 2^-52`. "BITWISE" means `==` on the raw doubles, zero tolerance.

### E3′ — rate coefficients (replaces E3)

For every reaction and every cell:

* `ulpDistance(k_device, k_host) ≤ 8`, with ULP distance computed through a monotonic key so it is
  correct across zero and across the sign;
* **the branch taken is identical** — amendment A1's `A == 0`, direct, and log-space-fallback
  decisions, including across a sweep of 4 096 **consecutive doubles** straddling A1's
  `isfinite(A·T^n)` predicate, with the sweep asserted to actually cross it.

*Control:* N4 (flip the Arrhenius exponent sign) — measured at 3.52 × 10²² times the bound.

### E4′ — progress rates (replaces E4)

`|Δq| ≤ 8 · ε · (sum of term magnitudes)`, per reaction per cell, with the scale built from the same
pieces `q` is built from. Plus identical branches, as E3′.

*Control:* N4 — 3.52 × 10²² times the bound.

### E5 — concentrations (unchanged, restated for completeness)

`C_i = (ρ·Y_i)/M_i` is **BITWISE**. No transcendental, no accumulation.

### E6′ — species sources and heat release (replaces E6)

Rather than loosen these, **change what is compared**. The libm difference originates in `k` and
reaches `ω` and `S_E` only through `q`. So feed the device's **own** `q` back through the host
accumulation and require zero tolerance:

* `ω_i` recomputed on the host from the device's own `q` is **BITWISE** equal to the device's `ω_i`;
* `S_E = −Σ_r q_r ΔH_r⁰` recomputed from the device's own `q` is **BITWISE**;
* `S_E = −Σ_i h_f,i ω_i` recomputed from the device's own `ω` is **BITWISE**.

This keeps the accumulation — its order, its parenthesisation, its coefficients — under a
zero-tolerance criterion, and confines the admitted difference to the two libm calls where it
originates. It is **stronger** than the ULP bound it replaces, not weaker.

*Controls:* N5 (omit `M_i`) 3 072 values; N6 (flip the `S_E` sign) 9.44 × 10¹⁸ ULP; N8 (omit the
last reaction) 4 096 values.

Additionally, the end-to-end comparison keeps the derived propagated bound
`|Δω_i| ≤ 8·ε·Σ_r|M_i·ν_i·q_r|` and the same form for `S_E`, so a divergence that enters through `q`
is still bounded and not merely unexamined.

### E7 — mixture properties and equation of state (unchanged, and now load-bearing)

`M_mix`, `R_mix`, `cp_mix`, `cv_mix`, `γ`, `ρ`, `p`, `T` are **BITWISE**. Unchanged from the original
gate, where they already passed.

**This criterion now carries G9's condition (4).** See section 4.

*Controls:* N1 (permute the species index) 3 072 values; N2 (one weight for all species) 1 024
values; **and `--fmad=true`** — 662 of 4 096 cells for `R_mix`, 543 for `cp_mix`.

### E8′ — the coupled step and the trajectory (replaces E8 and E9)

**BITWISE** for every quantity not downstream of a transcendental:

* the sensible and chemical energy pools, before and after;
* the recovered temperature `T`;
* the updated composition `Y_i`;
* the status and its reason, which must name the **same first-offending** condition;
* every branch index.

and, over a marched trajectory:

* the state `(Y, T)` is **BITWISE** at every sampled step count, **on a trajectory asserted to have
  moved by more than one ULP of each quantity's own magnitude**. Measured: bitwise at 1, 5, 20 and
  50 steps while `T` moved 28.6389 K and `Y` moved 0.0075085.

The non-vacuity requirement in that last clause is not decoration. The first version of the probe
used `dt = 1 × 10⁻⁷`, at which neither `T` nor `Y` moved by one ULP, and reported a ladder of zeros
while comparing two unchanged initial states.

*Controls:* N7 (stale temperature), N9 (stale composition), N11 (accept an invalid state) — all
already exercised in link one.

### E10–E13 — unchanged

Determinism (E10), invalid states (E11), transfer and residency (E12) and CUDA diagnostics (E13)
are unaffected by the libm difference and stand as frozen. E10 and E12 have already passed.

## 4. Where G9 condition (4) is policed, and why not where A2 first proposed

A relaxed bound is only honest if it cannot hide the thing `--fmad=false` exists to prevent. A2's
first draft asserted that FMA contraction must move `q` by **more** than the bound, so the bounded
criterion could still reject it. **The dry-run measured the opposite:**

| quantity | effect, in the bound's own units (`|Δ| / (ε · Σ|terms|)`) |
| --- | --- |
| the libm difference the bound must **admit** | **3.13865** |
| FMA contraction | **0.843356** |

Contraction moves `q` *less* than libm does. So no threshold that lets the correct implementation
pass can reject contraction on `q`. That is arithmetic, not a judgement call, and it disqualifies
the bounded criterion from policing condition (4).

**Condition (4) is therefore policed by E7′, the BITWISE mixture-property criterion**, where
contraction is plainly visible — 662 of 4 096 cells for `R_mix`, 543 for `cp_mix`. This is airtight
rather than a consolation: `R_mix`, `cp_mix` and every rate quantity are compiled from **one**
translation unit (`cuda/kernels/DeviceSpeciesKernel.cu`) carrying **one** `--fmad=false` property,
so the flag cannot be lost for the rate path alone without E7′ failing.

The dry-run asserts both halves of that argument standing: the ordering `contraction < libm` (the
fact that forces the allocation — if a future toolchain reversed it, a bounded criterion could carry
the duty and the assertion fires to say so) and the liveness of E7′'s FMA control.

**The signature that validates the control.** `deviceMolecularWeight` accumulates
`reciprocal += composition[i] / weight` — a divide then an add, which is not an `a*b+c` pattern, so
FMA **cannot** reach it, while `R_mix` and `cp_mix` accumulate `total += composition[i]·coefficient`,
which it can. Measured: `M_mix` 0 of 4 096, `R_mix` 662, `cp_mix` 543. A control that broke all three
or none of them would be measuring something other than contraction.

**Recorded, not a defect:** FMA cannot reach the `ω`/`S_E` accumulation either. The host's own
`std::fma` agrees with multiply-then-add on 12 288 of 12 288 values, so the products `M_i·ν_i·q` are
exactly representable for this mechanism and no fusion has any rounding to remove. The device
control found the same, and the dry-run asserts the two agree — a disagreement would mean one of
them is not measuring contraction. E6′'s controls are consequently N5, N6 and N8, not FMA.

## 5. The comparison instrument

The ULP instrument is self-tested on planted disagreements before it is trusted with a verdict,
because its first version **scored a sign flip as a perfect match**: reinterpreting two doubles as
signed integers and subtracting overflows when the signs differ, and the wrapped value read as 0.
Control N6 is what exposed it.

The instrument now:

* maps each double to a monotonic unsigned key, so the distance is exact across zero and across the
  sign (`ulpDistance(1e7, −1e7) > 10¹⁵`, asserted);
* treats a non-finite pair as the largest possible disagreement and **counts** it, rather than
  skipping it and leaving the worst-case figures looking clean;
* is checked on identical values, adjacent doubles, the two zeros, a pair straddling zero, a sign
  flip and a planted infinity (`DRYRUN_1B`).

## 6. Non-vacuity summary — every criterion has a control that fails it

| Criterion | Tolerance | Control | Measured control effect |
| --- | --- | --- | --- |
| E3′ `k` | ≤ 8 ULP + identical branch | N4 | 3.52 × 10²² × the bound |
| E4′ `q` | ≤ 8·ε·Σ\|terms\| | N4 | 3.52 × 10²² × the bound |
| E5 `C_i` | BITWISE | instrument self-test | planted disagreements detected (`DRYRUN_1B`) |
| E6′ `ω`, `S_E` from device `q` | BITWISE | N5 / N6 / N8 | 3 072 values / 9.44 × 10¹⁸ ULP / 4 096 values |
| E7′ mixture, EOS | BITWISE | N1 / N2 / **`--fmad=true`** | 3 072 / 1 024 / 662 and 543 cells |
| E8′ state and trajectory | BITWISE, state asserted to move | N7 / N9 / N11 | exercised in link one |
| E10 determinism | BITWISE | repeats and 4 block sizes | — |
| E12 residency | 0 transfers in the loop | N12 | a field-sized download is registered |

## 7. The correct implementation against these criteria, before freezing

| Quantity | Measured | Bound | Margin |
| --- | --- | --- | --- |
| `k` | 3 ULP | 8 ULP | 2.7× |
| `q` | 3.13865 | 8 | 2.5× |
| branch disagreements | 0 / 8 192 | 0 | exact |
| `ω_i`, `S_E`, `S_E` (mass route) from device `q` | 0 / 12 288 and 0 / 4 096 | BITWISE | exact |
| mixture properties, EOS | 0 differing | BITWISE | exact |
| state over 50 steps | 0 differing | BITWISE | exact |

The thresholds were derived in section 2 before these were measured, and the margins are ~2.5×, so
they are not fitted to the data.

## 8. What A2 does NOT do

* It does not relax any criterion on a quantity that is not downstream of a transcendental.
* It does not relax the state, the trajectory, the status, the branches or the accumulations.
* It does not touch G9's conditions (1), (2), (3) or (5), which are unaffected.
* It does not change any production CPU file, and it does not change the device arithmetic.
* It does not close G9 by itself: E11 and E13 remain to be run.
