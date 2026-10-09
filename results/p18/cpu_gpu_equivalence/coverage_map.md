# P18.8 — Coverage map: every frozen criterion → the test that measures it

Built **after** freezing, per the standing rule that a criterion measured by nothing, or by the
wrong probe, is the failure mode this project keeps rediscovering. Building it found four such
defects in this phase (§4), none of which any test failure would have surfaced.

Labels are the **original** gate's (`acceptance_gate.md`, `3fc7a020…`), corrected per
`acceptance_gate_A2_erratum.md`. Amended criteria are marked **A2**.

## 1. The criteria

| Criterion | Tolerance | Measured by | Result |
| --- | --- | --- | --- |
| **E1** mixture properties | BITWISE | `E1_EveryCompositionCaseInTheFrozenListIsBitwiseOnDevice` (4 compositions × 5 T, 1 280 comparisons) + `MixturePropertiesAreBitwiseOnDevice` (1 024 cells, spatially varying) | **PASS**, 0 differing |
| **E2** EOS + invalid-state decision | BITWISE on value, EXACT on decision and reason | `EquationOfStateIsBitwiseOnDevice` (512 cells); decision by `E11_TheDeviceRefusesWhatTheHostRefusesAndNamesTheSameReason` | **PASS** |
| **E3** Arrhenius **A2** | ≤ 8 ULP + identical branch | `E3_RateCoefficientsWithinTheDerivedBoundAndBranchesIdentical`; the predicate sweep by `DIAGNOSTIC_CanHostAndDeviceTakeDifferentArrheniusBranches` | **PASS**, worst 3 ULP, 0/8192 branch disagreements |
| **E4** reaction progress **A2** | ≤ 8·ε·Σ\|terms\| | `E4_E5_E6_ProgressRatesBoundedAndAccumulationsBitwise` | **PASS**, worst 2.74 (gate run) / 3.139 (dry-run) |
| **E5** stoichiometric sources **A2** | BITWISE from the device's own `q`; `Σω` → 0 to `32·ε·max\|ω\|` independently | same test + `DRYRUN_5` | **PASS**, 0/12288 differing; `Σω` within bound |
| **E6** heat release **A2** | BITWISE from the device's own `q`; sign equivalence hard pass/fail | same test, both routes | **PASS**, 0/4096 and 0/4096 |
| **E7** energy update + T recovery, every intermediate | BITWISE (**not amended**) | `E8_TheCoupledStepStateIsBitwiseAndTheRateIntermediatesAreBounded` | **PASS**, 1024/1024 |
| **E8** one-step full thermochemistry **A2** | BITWISE state; rate intermediates bounded | same test | **PASS** |
| **E9** multi-step trajectory **A2** | BITWISE state on a trajectory that MOVED | `DIAGNOSTIC_HowTheDivergenceGrowsAlongATrajectory` (bitwise at 1/5/20/50 steps, `T` moved 28.6389 K, `Y` moved 0.0075085) | **PASS** |
| **E10** determinism | BITWISE | `RepeatsAndBlockSizesAreBitwise` (5 repeats; blocks 64/128/256/512) | **PASS** |
| **E11** invalid-state parity | same refusal, same **first** reason | `E11_...NamesTheSameReason` (7 rows) + link one's `P188InvalidStateTest` | **PASS** |
| **E12** transfer and residency | 0 field transfers in the resident loop | `E12_TheResidentLoopPerformsNoFullFieldTransfer` | **PASS**, 0 calls / 0 bytes |
| **E13** CUDA diagnostics | clean, with the tool proven live | `tools/run_cuda_diagnostics.sh` | **PASS** (memcheck, initcheck with controls firing; racecheck, synccheck clean, scope stated) |

## 2. G9's five structural conditions

| Condition | How it is satisfied | How that is checked |
| --- | --- | --- |
| (1) one shared source for both | `DeviceSpeciesThermochemistry.hpp`, `CFD_HLLC_HOST_DEVICE` | the CPU-only configuration compiles and runs link one; **violated for the transcendentals** — see `libm_divergence.md` |
| (2) per-cell gather in the CPU's accumulation order | cell-local loops, ascending species then reaction | **vacuous**: no cross-thread accumulation exists. Measured: 0 atomics, 0 shuffles, 0 cooperative groups |
| (3) geometry uploaded, not recomputed | no geometry is used by the thermochemistry | **vacuous, and stated as such** rather than claimed as evidence |
| (4) `--fmad=false` | set per-source in `cuda/CMakeLists.txt` | the `--fmad=true` control TU, via **E1** (662/4096 `R_mix`, 543/4096 `cp_mix`). It cannot be policed by E3/E4 — see §3 |
| (5) order-independent reductions | there are no reductions | **vacuous**. What does cover cross-thread behaviour: E10's bitwise equality across 4 block sizes |

Conditions (2), (3) and (5) hold **because the problem has no such structure**, not because effort
was spent on them. Recorded that way deliberately: a vacuously satisfied condition is not evidence,
and presenting three of five as "met" without that note would overstate the result.

## 3. Where G9 condition (4) lives, and why it had to move

A2 first proposed that the bounded criteria on `k` and `q` would still reject FMA contraction.
Measured in the bound's own units: contraction moves `q` by **0.843356**, while the libm difference
the bound must admit is **3.13865**. Contraction is the *smaller* effect, so no threshold that lets a
correct implementation pass can reject it. Condition (4) therefore sits on **E1**, a zero-tolerance
criterion on the *same translation unit* with the *same single* `--fmad=false` property — the flag
cannot be lost for the rate path alone without E1 failing.

The dry-run asserts both the ordering that forces this and the liveness of E1's FMA control, so if a
future toolchain reverses the ordering the suite says so instead of leaving it unexamined.

## 4. What this mapping found — four defects, none caught by a failing test

1. **E1's case list four fifths unmeasured on device.** Only the spatially-varying ternary case
   existed. Closed; see erratum §3.
2. **Control N10 never implemented.** The frozen table lists it against E9. Closed.
3. **N1 and N2 pointed at the wrong criterion.** Run against the reaction sources, which never
   consult them, they reported zero difference — indistinguishable from dead controls. Re-aimed at
   E1, where they fire at 3 072 and 1 024 values.
4. **N8 classified against the bounded `q` criterion**, which it does not touch (measured 0 in `q`'s
   bound units). It is a control for the zero-tolerance accumulation clause, where it fires at 4 096
   values.

Also found, by the instrument's own self-test: **the ULP comparator scored a sign flip as a perfect
match** (signed-integer overflow on opposite-sign operands). Control N6 exposed it. The comparator is
now self-tested on planted disagreements (`DRYRUN_1B`) and shared, so the code under test and the
code the gate trusts are the same code.

## 5. The controls

| Control | Aimed at | Measured effect |
| --- | --- | --- |
| N1 species index permuted | E1 | 3 072 values differ |
| N2 one weight for all species | E1 | 1 024 values differ |
| N3 `ΣJ_i` omitted | — | **N/A**, transport not implemented (frozen gate's own note) |
| N4 Arrhenius exponent sign flipped | E3, E4 | 3.52 × 10²² × the `q` bound |
| N5 `M_i` omitted in `ω_i` | E5 | 3 072 values differ |
| N6 heat-release sign flipped | E6 | 9.44 × 10¹⁸ ULP |
| N7 stale temperature | E7, E9 | link one |
| N8 one reaction omitted | E5 (not E4) | 4 096 values differ |
| N9 stale composition in recovery | E7 | link one |
| N10 different timestep | E9 | `max \|ΔT\|` 0.0124043 K, `max \|ΔY\|` 3.26 × 10⁻⁶ |
| N11 invalid state silently accepted | E2, E11 | link one; **caught a real device defect** (status was last-write-wins) |
| N12 full-field transfer in the resident loop | E12 | a field-sized download is registered |
| `--fmad=true` | E1 → G9 (4) | 662 and 543 of 4 096 cells |

## 6. What no test in this phase measures

* **Ranged `cp`.** The device carries constant `cp` only (`kernel_inventory.md` §4). Equivalence is
  established for the constant-`cp` subset and is silent outside it.
* **Fractional reaction orders.** All orders here are 1, for which `pow(x,1)` is exact.
* **Species transport on device.** Not implemented; N3 is N/A for that reason.
* **Native Windows CUDA.** Everything here is WSL2 Ubuntu 22.04, CUDA 12.9.86, glibc 2.35, RTX 5000
  Ada cc 8.9, driver 580.97. WSL2 evidence does not establish native Windows.
* **Case reachability.** No case file can reach any of this. P18.2–P18.8 remain library-level; see
  `TODO.md`. A device kernel no case can invoke is not shipped capability.
