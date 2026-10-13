# P18.8 — CPU / GPU Equivalence: summary

**STATUS: P18.8 COMPLETE.** All thirteen gate criteria PASS under the authorized amendment A2, and
all three regression configurations conform to the frozen gate with **0 new failures**.

The `debug_gui` timeout count of 4 is what `P18_GATES.md` G11 requires — "THE 4 DEBUG+GUI TIMEOUTS
ARE INHERITED, NOT P18's … pre-P17 DriverSeegmiller cases that pass in both Release configurations".
All four do. My pre-run prediction of 3 was taken from P18.7's observed run rather than the gate's
stated contract; see `decision.md`.

Environment for every number here: WSL2 Ubuntu 22.04, **nvcc 12.9.86** at
`/usr/local/cuda-12.9/bin/nvcc` (not the 11.5 first on `PATH`), g++ 11.4.0, glibc 2.35, RTX 5000 Ada
cc 8.9, driver 580.97. `--use_fast_math`: **0 occurrences**. WSL2 evidence does not establish native
Windows CUDA.

## 1. What was built

The first species and chemistry physics on the device: **5 production kernels**, one thread per cell,
every formula living in **one shared `CFD_HLLC_HOST_DEVICE` header that gcc and nvcc both compile**.
Plus a `--fmad=true` control translation unit whose only caller is the test suite.

No CPU production file was modified — all 12 CPU species sources are **byte-identical** to the
hashes recorded in P18.8's own baseline. The CPU remains the authoritative reference, as the
authorization requires.

See `kernel_inventory.md` for the inventory, what is deliberately absent, and the capability limits.

## 2. The central finding: bitwise is unreachable for transcendental-dependent chemistry

The frozen gate required **BITWISE** host/device agreement. It holds for every quantity built from
`+ − × ÷` and fails, by **≤ 3 ULP**, for the four quantities downstream of a transcendental: `k`,
`q`, `ω_i`, `S_E`.

Cause: `species_detail::expR / logR / powR` dispatch to **glibc** under gcc and to **CUDA
libdevice** under nvcc. IEEE-754 requires correct rounding only for `+ − × ÷` and **`sqrt`**.
P17.7 and P17.10 reached bitwise because their shared header calls **only `sqrt`**, four times — that
result was evidence about `sqrt`, not about libm agreement. P18.8 is the first phase whose shared
arithmetic calls `exp`, `log` and `pow`.

**This was a gate-derivation error of mine**, not a code defect: I carried P17.10's five conditions
into chemistry without asking whether a correct implementation could satisfy them, which one grep of
the shared header would have answered. Diagnosed in `libm_divergence.md`; the failure is preserved
verbatim at `logs/03_second_failure_PRESERVED.log` (2 478 assertions).

The cause was **isolated, not inferred**. Setting `n = 0, Ea = 0` makes `pow(x,0)` and `exp(0)`
exactly 1, so that case contains no inexact libm call: **0 of 4 096 cells differ**. One measurement
clears operand order, parenthesisation, accumulation order, `--fmad=false`, the geometry upload and
the POD upload. `exp` alone gives 2 ULP; `pow` alone 2 ULP; both 3 ULP.

Two further measurements bound the consequence:

* **No branch bifurcation.** A1's fallback predicate reads `pow`'s *output*, so the two libms could
  have run different algorithms. Swept across 4 096 **consecutive doubles** straddling the predicate
  (and asserted to cross it): **0 disagreements**.
* **It does not amplify.** The ≤ 3 ULP in `ω` enters the state as `Y + dt·ω/ρ`, lands **below one ULP
  of `Y`**, and bisection-based `T` recovery returns the same double. The **marched state is bitwise
  over 50 steps** while `T` moved 28.6389 K and `Y` moved 0.0075085.

## 3. Amendment A2 — authorized, and what the dry-run rejected first

`acceptance_gate_A2.md` (`f0973e34…`), with `acceptance_gate_A2_erratum.md` correcting three
criterion labels. The original gate is **not edited** (`3fc7a020…`, re-verified).

A2 relaxes only `E3`, `E4` and the `q`-dependent part of `E5`/`E6`. **Ten of thirteen criteria
remain at zero tolerance**, including the state, the trajectory, the status and reason, every branch,
and all accumulations.

Its structural move: rather than loosen `ω` and `S_E`, **change what is compared** — feed the
device's own `q` back through the host accumulation. That confines the admitted difference to the two
libm calls and keeps every accumulation under zero tolerance. Stronger than the bound it replaces.

**The pre-freeze dry-run rejected four drafts** (`logs/06_A2_dryrun_PRE_FREEZE.log`):

1. The ULP comparator **scored a sign flip as a perfect match** — signed-integer overflow on
   opposite-sign operands, wrapping to 0. Control **N6** exposed it. Now monotonic-key based,
   self-tested on planted disagreements, and **shared** so the code under test is the code the gate
   trusts.
2. **N1 and N2 were aimed at the wrong criterion**, run against sources that never consult them.
3. **N8 was classified against the `q` bound** it measurably does not touch.
4. **The substantive one.** A bounded criterion on `q` *cannot* police G9 condition (4): contraction
   moves `q` by **0.843356** in the bound's units while the libm difference the bound must **admit**
   is **3.13865**. Contraction is the smaller effect, so no passing threshold can reject it. That is
   arithmetic, not a judgement call.

Condition (4) therefore rests on **E1**, the bitwise mixture-property criterion, where contraction is
visible (662/4096 `R_mix`, 543/4096 `cp_mix`) — and which is compiled from the **same translation
unit** with the **same single** `--fmad=false` property, so the flag cannot be lost for the rate path
alone without E1 failing. The dry-run asserts the ordering that forces this, so a toolchain change
that reverses it reports itself.

## 4. Results

| Criterion | Tolerance | Measured |
| --- | --- | --- |
| E1 mixture properties | BITWISE | 0 differing, 1 280 + 1 024 comparisons |
| E2 EOS + invalid-state decision | BITWISE / exact | 0 differing; 7/7 reason rows |
| **E3** `k` | **≤ 8 ULP** + identical branch | **3 ULP**, 1 564/2 048 exactly bitwise, 0/8 192 branch disagreements |
| **E4** `q` | **≤ 8·ε·Σ\|terms\|** | **2.74** (gate), 3.139 (dry-run) |
| **E5** `ω_i` from device's own `q` | BITWISE | 0/12 288; `Σω` within `32·ε` per cell |
| **E6** `S_E`, both routes | BITWISE | 0/1 024 and 0/1 024 |
| E7 energy + T recovery, every intermediate | BITWISE | 1 024/1 024 |
| E8 one coupled step | BITWISE state | 1 024/1 024 |
| E9 trajectory | BITWISE on a moving state | bitwise at 1/5/20/50 steps |
| E10 determinism | BITWISE | 5 repeats; blocks 64/128/256/512 |
| E11 invalid-state parity | same first reason | 7/7 |
| E12 residency | 0 transfers in the loop | 0 calls, 0 bytes, 10 steps |
| E13 CUDA diagnostics | clean, tool proven live | memcheck 0 (control 2), initcheck 0 (control 2 972) |

Focused tests: **link one 17/17**, **link two + dry-run + E11 23 rows** (20 active, 3 disabled).

All **12** frozen controls implemented and firing; N3 is **N/A** by the frozen gate's own note
(transport not implemented). See `negative_controls.md`.

## 5. The regression

| | registered | ran | passed | failed | skipped | disabled |
| --- | --- | --- | --- | --- | --- | --- |
| `release_cuda` | 3081 ✅ | 3016 ✅ | 3014 ✅ | 0 ✅ | 2 ✅ | 52 ✅ |
| `cpu_only` | 2835 ✅ | 2775 ✅ | 2775 ✅ | 0 ✅ | 0 ✅ | 47 ✅ |
| `debug_gui` | 3143 ✅ | 3078 ✅ | 3072 | **4** ✅ | 2 ✅ | 52 ✅ |

**0 new failures.** P18.8's own rows: **0 failures in any configuration**.

`debug_gui`'s 4 timeouts are the inherited baseline the frozen gate describes, all four
DriverSeegmiller cases passing in **both** Release configurations:

| test | Release+CUDA | CPU-only | Debug+GUI |
| --- | --- | --- | --- |
| `DriverSeegmiller.MeshSequence` | 1323.84 s | 1012.19 s | Timeout 7200.08 s |
| `DriverSeegmiller.CorrectedSequenceIsDeterministic` | 2001.74 s | 1471.38 s | Timeout 7200.05 s |
| `DriverSeegmillerInlet.MediumGridAB` | 2011.30 s | 1493.78 s | Timeout 7200.06 s |
| `DriverSeegmillerInlet.MappedInletPreservesTheMassFlow` | 1587.17 s | 1206.40 s | Timeout 7200.02 s |

My pre-run prediction said 3 because I took it from P18.7's *observed run* instead of the gate's
*stated contract*; P18.7's run is the deviation, having had `MeshSequence` squeeze under the cap at
6963.18 s that day. `regression.md` §3 reported it as a regression and §6 corrects that;
`decision.md` records the reasoning.

**Carried to P18.9's G10:** `MeshSequence` now has **8 measurements, 5 pass / 3 timeout, every one
within 6 % of the cap** in Debug. P18.8 modified no CPU file and this is a CPU test, so a code-level
slowdown is ruled out by construction — which is what makes the gate's "inherited, not P18's" claim
verifiable rather than asserted. `ROADMAP.md` records that closing G10 needs a decision about the
Debug timeout contract, outside a qualification phase's remit.

The 7200 s timeout was **not raised**, the test was **not** excluded or re-run until it passed.

**Attempt 1 of the regression was invalid and is preserved as such**
(`logs/regression_INVALID_AS_AUTHORITATIVE.md`): my harness used `-j 16`, no `OMP_NUM_THREADS=1` and
no exclusions, deviating from the policy inherited from P13's G10 closeout, and 15 of 16 long tests
timed out from oversubscription — inflating by 3.8× to 62×.

## 6. What P18.8 does NOT establish

* **Ranged `cp`.** `DeviceSpecies` carries one **constant** `cp`; the CPU path supports ranged `cp`.
  Equivalence is established for the constant-`cp` subset and is **silent outside it**.
* **Fractional reaction orders.** All orders here are 1, for which `pow(x,1)` is exact.
* **Species transport on device** — not implemented; N3 is N/A for that reason.
* **A reacting-flow solve.** These are operators and a closed reactor. **G8 remains unclosed**; no
  flame speed, no ignition delay, no stiff integrator.
* **Elemental conservation** — not modelled by any P18 phase.
* **Case reachability.** No case file can reach any of this. **P18.2–P18.8 remain library-level**: a
  device kernel no case can invoke is not shipped capability (CLAUDE.md §9).
* **Any performance claim.** None is made. E12 reports transfer *counts*, not timings.
* **Native Windows CUDA.**

## 7. Gate status

| Gate | Status |
| --- | --- |
| G1–G7 | as closed by P18.0–P18.7 |
| **G8** reacting benchmarks | **NOT CLOSED** — no reacting-flow solver exists |
| **G9** CPU/GPU equivalence | **CLOSED for the thermochemistry operators and the closed reactor**, under A2's amended criteria, within the constant-`cp` subset. Library-level: no case file can reach it |
| G10 long-run / timing branch | **P18.9**. This phase supplies `MeshSequence`'s 8th data point and reproduces the 4 inherited Debug timeouts the gate requires |
| **G11** final qualification | **NOT YET QUALIFIED** |
