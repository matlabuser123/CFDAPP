# P18 — FINAL QUALIFICATION

**P18.9 STATUS: COMPLETE** — every one of its thirteen scope items was executed and reported.

**P18 OVERALL: PARTIAL** — **G8 (canonical reacting validation) is NOT CLOSED.** §64 requires all
eleven gates to PASS for P18 to be COMPLETE; ten do.

The two verdicts differ deliberately. P18.9's job was to *determine* whether the combined P18 tree
qualifies, and it completed that job. The determination is that it does not yet.

---

## 1. Qualification candidate

| | |
| --- | --- |
| HEAD | `3bd32f918656f471e72b5f6ac7cb6c2cbf75a274` |
| branch | `main` (upstream `origin/main`) |
| describe | `v0.2.0-29-g3bd32f9-dirty` |
| working tree | **DIRTY** — 126 tracked modifications, 168 untracked paths, spanning the uncommitted **P13–P18** series |
| submodules | none |
| nothing committed, nothing pushed | P18.9 did not commit, push, reset, clean or stash |

Frozen at `logs/00_candidate_freeze.log`.

## 2. Source fingerprint (§54, §55)

```text
P18_FINAL_SOURCE_FINGERPRINT = 83d43668dfd4b19877831b55ab44e187726f774d34e5ac68b591f1e8b2c3384d
files fingerprinted          = 433
methodology                  = sha256 of the sorted sha256 manifest of every source file under
                               include/ src/ cuda/ apps/ matching *.hpp *.h *.cpp *.cu *.cuh *.in
                               and every CMakeLists.txt
```

**STABLE: identical to the candidate fingerprint taken before any qualification work.** P18.9
changed **no source file**; everything it produced is evidence, probes under
`results/p18/final_qualification/tools/`, and documentation.

**The fingerprint contract, stated because §55 requires the distinction:** evidence is **not**
fingerprinted. `results/**`, `TODO.md`, `ROADMAP.md` and `KNOWN_LIMITATIONS.md` are outside it —
otherwise recording a result would invalidate the fingerprint of the code it describes. Build
outputs are outside it because **nvcc is not bit-reproducible in this tree** (P18.8 measured
`libcfdcuda.a` hashes changing across rebuilds of identical source), so a library hash would be
noise.

## 3. Environment

| | |
| --- | --- |
| OS | WSL2, Ubuntu 22.04, kernel 6.18.33.2-microsoft-standard-WSL2 |
| host | 32 logical CPUs, 31 GiB RAM visible to WSL |
| compiler | g++ 11.4.0, glibc 2.35 |
| CMake / Ninja | 3.22.1 / 1.10.1 |
| clang-format | 18.1.8 |
| **CUDA (build)** | **`/usr/local/cuda-12.9/bin/nvcc`, 12.9.86** |
| CUDA (PATH) | `/usr/bin/nvcc` 11.5.119 — **not** what built this; recorded because naming the wrong toolchain in a libm finding would be worthless |
| compute-sanitizer | 2025.2.1.0 |
| GPU | NVIDIA RTX 5000 Ada Generation Laptop, cc 8.9, driver 580.97, 15352 MiB |
| `--use_fast_math` | **0 occurrences** |
| `--fmad` | `false` on every qualified kernel; `true` only on the control TU — read from Ninja's own command lines, not the CMake text |

**WSL2 + CUDA evidence does not establish native Windows + CUDA.**

## 4. Final P18 gate table (§4)

| Gate | Requirement | Metric | Bound | Measured | Control | Status |
| --- | --- | --- | --- | --- | --- | --- |
| **G1** | baseline / regression preservation | test counts vs a prediction frozen before the run | 0 new failures | see §5 | harness fails closed on stale binary or config mismatch | **PASS** |
| **G2** | species-state validity | `max\|ΣY−1\|` | `4·eps·N_species` = 1.776e-15 | **2.664535e-15** at `N=3`; within that case's own bound | 18 rejection tests + N1/N4 | **PASS** |
| **G3** | species conservation | per-species imbalance; `max_cell\|Σω\|` | `8·eps·N_cells`; `32·eps·max\|ω\|` | within bound; `Σω` **exactly 0.000e+00** at rate scales q = 1, 7.5, 1000 | N1/N2/N11/N12/N13 | **PASS** |
| **G4** | transport / diffusion | `max_f\|Σ_i J_i\|` | 1.7540254962763639e-19 | **6.7762635780344027e-21** (25× margin) | N1/N3/N4 | **PASS** |
| **G5** | mixture thermodynamic closure | single-species `M_mix` collapse to P17 | round-off | **1.238495e-16 = 0.56 eps** | N1–N8 (8 controls) | **PASS** |
| **G6** | reaction-rate / source correctness | `Σ_i M_i ν_i`; `ω` vs mpmath | `4·eps·max\|M_i ν_i\|`; `32·eps` | closure within bound; `ω` worst **11.05 eps of 32** | N1–N10 | **PASS** |
| **G7** | reacting-energy conservation | closed-reactor total-energy drift | `8·eps·N_cells` | **abs 4.470348e-08 J, rel 1.396984e-15 = 6.29 eps**; worst step 1.513399e-15 = 6.82 eps | N1/N5/N6/N8/N9/N11/N12 | **PASS** |
| **G8** | canonical reacting validation | 1D laminar premixed flame: flame speed in a **cited published spread**, peak T, species profiles | the published spread | **NOTHING MEASURED — the capability does not exist** | the two required controls cannot run | **NOT CLOSED** |
| **G9** | CPU/GPU equivalence | per-quantity, under amendment **A2** | bitwise except `k`/`q` ≤ 8 ULP | `k` **3 ULP**, `q` **2.74**; everything else **bitwise** incl. the marched state | 12 controls + `--fmad=true` | **PASS** (scoped, §10) |
| **G10** | deterministic repeatability | repeats, block sizes, separate processes | bitwise | bitwise over 5 repeats and block sizes 64/128/256/512 | N10 (dt sensitivity) | **PASS** |
| **G11** | full regression / qualification | all three configurations + every foundation | counts matching a pre-declared prediction, 0 new failures | see §5 | the 3 `DISABLED_` deliberate-defect controls must stay disabled | **PASS** |

**G8 is the single blocking gate.** It requires a reacting-flow solver, a stiff integrator and
flame-speed extraction. None exists; building them is new feature development, which P18.9's
authorization excludes. The gate was **not weakened, not restated and not amended** — §53 permits an
amendment only when the criterion itself is demonstrably wrong, and G8's criterion is sound. It is
simply unmet.

## 5. Full regression results (§33–§35, §58)

Run on the final candidate, fingerprint `83d43668dfd4b198…`. Harness:
`results/p18/cpu_gpu_equivalence/tools/run_regression.sh` — **P18.8's, reused deliberately and
unmodified**, so the execution policy is provably identical to the run it is compared against. Its
header still says "P18.8 §57"; the label is P18.8's, the run is P18.9's.

Execution policy, inherited verbatim from P18.7 ← P17.10 ← P13's G10 closeout:
`OMP_NUM_THREADS=1  ctest -j 8  --timeout 7200  -E <5 frozen exclusions>`.

| Configuration | Registered | Ran | Passed | Failed | Skipped | Disabled | Status |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| **Release+CUDA** | 3081 | 3016 | **3014** | **0** | 2 | 52 | **PASS** |
| **CPU-only** | 2835 | 2775 | **2775** | **0** | 0 | 47 | **PASS** |
| **Debug+GUI** | 3143 | 3078 | **3072** | **4** | 2 | 52 | **PASS** — 4 inherited |

**All eighteen figures match the prediction frozen before the run**
(`results/p18/cpu_gpu_equivalence/regression_prediction_addendum_2.md`, `610f388a…`).

| | build | ctest | exit | P18.8's run, same source family |
| --- | ---: | ---: | ---: | ---: |
| Release+CUDA | 249 s | 2217 s | 0 | 2226 s |
| CPU-only | 240 s | 1514 s | 0 | 1495 s |
| Debug+GUI | 452 s | 8403 s | 8 (expected) | 8478 s |

Each configuration was clean-first rebuilt, its four qualification options asserted against its own
cache **after** configuring, and every test binary verified newer than the newest production source.

### P18's own rows

| Configuration | Passed | Disabled | **Failed** |
| --- | ---: | ---: | ---: |
| Release+CUDA | 37 | 3 | **0** |
| CPU-only | 17 | 0 | **0** |
| Debug+GUI | 37 | 3 | **0** |

**0 P18 failures in any configuration.** The 3 `DISABLED_` rows are the sanitizer controls — a
deliberate out-of-bounds access and a deliberate uninitialised read — and they report
`***Not Run (Disabled)` in both CUDA configurations, never a pass.

`cpu_only` is the only configuration that compiles the shared host/device arithmetic header with
CUDA **OFF**, and link one's 17 rows pass there, so "one shared source" is exercised by a build with
no CUDA at all.

### Debug+GUI: every non-pass classified (§35)

| Row | Time | Classification |
| --- | ---: | --- |
| `DriverSeegmillerInlet.MediumGridAB` | Timeout 7201.96 s | **INHERITED** |
| `DriverSeegmiller.MeshSequence` | Timeout 7202.00 s | **INHERITED / MARGINAL, TIMING-SENSITIVE** |
| `DriverSeegmiller.CorrectedSequenceIsDeterministic` | Timeout 7202.03 s | **INHERITED** |
| `DriverSeegmillerInlet.MappedInletPreservesTheMassFlow` | Timeout 7202.04 s | **INHERITED** |

**NEW: none. RESOLVED: none.**

Classified on evidence, not assumption. `P18_GATES.md` G11's acceptance clause states it directly:

> *"THE 4 DEBUG+GUI TIMEOUTS ARE INHERITED, NOT P18's. They are pre-P17 DriverSeegmiller cases that
> pass in both Release configurations; P17.11 left G10 PARTIAL for exactly this. P18 reproduces them
> as baseline behaviour and does NOT raise the frozen 7200 s timeout to clear them."*

Every clause is satisfied, and the "pass in both Release configurations" half is **measured here**:

| test | Release+CUDA | CPU-only |
| --- | ---: | ---: |
| `DriverSeegmiller.MeshSequence` | Passed 1323.84 s | Passed 1012.19 s |
| `DriverSeegmiller.CorrectedSequenceIsDeterministic` | Passed 2001.74 s | Passed 1471.38 s |
| `DriverSeegmillerInlet.MediumGridAB` | Passed 2011.30 s | Passed 1493.78 s |
| `DriverSeegmillerInlet.MappedInletPreservesTheMassFlow` | Passed 1587.17 s | Passed 1206.40 s |

`MeshSequence` is the one nearest the cap in Debug and now has **9 recorded measurements: 5 pass /
4 timeout, every one within 6 % of the cap.** Closing that contract is G10's outstanding P17
business, which `ROADMAP.md` records as outside a qualification phase's remit. **The timeout was not
raised; no test was excluded, re-run until it passed, or reclassified.**

**So G1 and G11 both PASS: 0 new failures in any configuration.**

### A superseded earlier attempt, preserved

P18.8's first regression attempt is marked
`results/p18/cpu_gpu_equivalence/logs/regression_INVALID_AS_AUTHORITATIVE.md`. It deviated from the
inherited policy (`-j 16`, no `OMP_NUM_THREADS=1`, no exclusions) and 15 of 16 long tests timed out
from oversubscription, inflating by 3.8× to 62×. It produced no evidence and is kept, not deleted.


## 6. P14–P17 preservation (§36–§39, §59)

Suites defined by their **test binaries**, not by keyword: a grep for "conservation" or "species"
picks up P14's `TransportConservation`, P15's `ThermalConservation`, P17's `HllcConservation` and
P19's `PhaseConservation`, which would both inflate P18's figures and blur whose preservation is
being measured.

| Phase | Suite / evidence | Ran | Passed | Failed | Result |
| --- | --- | ---: | ---: | ---: | --- |
| **P14** | generic transport — `CFDTransportTests`, `CFDTransportVerificationTests` | 32 | **32** | 0 | **PASS** |
| **P15** | thermal / energy — `CFDThermalTests` + 4 integration suites | 195 | **195** | 0 | **PASS** |
| **P16** | turbulence — `CFDTurbulenceTests` | 246 | **246** | 0 | **PASS** |
| **P17** | qualified compressible — `CFDCompressibleTests`, `CFDCompressibleSimpleTests`, low-Mach ×2, density-based canonical, shock validation | 282 | **282** | 0 | **PASS** |

`logs/04_preservation.log`, with per-binary counts.

**P17 remains historically PARTIAL.** Its G6 (HLLC star-density positivity) and G10 (Debug timeout
contract) are unresolved, were not reopened, and **P18 qualification does not relabel P17 complete.**

## 7. Species canonical validation (§40)

| | |
| --- | --- |
| state invariants, advection, diffusion, convection–diffusion, variable density, boundaries, species-dependent diffusivity, diffusion closure, conservation, MMS | `CFDSpeciesTests` **319/319** |
| integration-level canonical species | 4 suites, **15/15** |
| `max\|ΣY−1\|` | **2.664535e-15** |
| `min/max Y_i` | within `[0,1]`, boolean, no tolerance |
| `max_f\|Σ_i J_i\|` | **6.7762635780344027e-21** vs bound 1.7540254962763639e-19 |
| normalization used | **NONE.** `normalizeMassFractions` appears exactly twice — declaration and definition. **Nothing calls it**, verified by grep with a positive control |
| clipping used | **NONE.** P18.1 refuses an out-of-bounds state; P18.6's reactor refuses rather than clamping |

## 8. Chemistry canonical validation (§41)

Reaction topology, stoichiometry, mass conservation, irreversible and reversible reactions,
Arrhenius analytical cases, progress rates, species sources, enthalpy, heat release, temperature
feedback, batch reactor, reacting-flow benchmark — all inside `CFDSpeciesTests` **319/319**.

| | measured | bound / criterion |
| --- | --- | --- |
| `Σ_i M_i ν_i` stoichiometric closure | within bound | `4·eps·max\|M_i ν_i\|` |
| `max_cell\|Σ_i ω_i\|` | **exactly 0.000e+00** at q = 1, 7.5, 1000 | `32·eps·max\|ω\|` |
| `ω` vs independent mpmath | worst **11.05 eps** | `32·eps` |
| Arrhenius `A=0`, `Ea=0`, `n=0`, `n=0 ∧ Ea=0` | exact branches | exact |
| `Ea/(R_u T)` units | mol-based; the kmol factor-of-1000 control fires | — |
| batch reactor vs mpmath (dps 50) | **errors fall monotonically**, observed order ≈ **1** | refinement (explicit Euler) |
| reacting plug flow vs the same reference | **errors fall monotonically**, observed order ≈ **1** | refinement (first-order upwind) |

**Instrument limitation, stated rather than worked around:** `P187BatchReactorTest` and
`P187ReactingFlowTest` **assert** their per-checkpoint errors without printing them, so absolute
species/temperature errors are not available from the instrument. The frozen criterion for both is
**refinement**, not an absolute tolerance — G8's own wording explains why ("a reacting front is a
discontinuity-like structure and no single tight tolerance is derivable"), and §25 forbids inventing
an absolute bound after seeing results. Adding printing would have changed a test file and
invalidated the fingerprint that §33's regressions run against.

## 9. Conservation (§42)

| quantity | measured | bound |
| --- | --- | --- |
| per-species mass, closed reactor | within bound, every term reported separately | `8·eps·N_cells` |
| open plug flow `\|in − out + source\|/\|in\|` | within bound | `8·eps·N_cells` |
| `max\|ΣY−1\|` | **2.664535e-15** | `4·eps·N_species` |
| `max_cell\|Σ_i ω_i\|` | **exactly 0.000e+00** | `32·eps·max\|ω\|` |
| `max_f\|Σ_i J_i\|` | **6.7762635780344027e-21** | 1.7540254962763639e-19 |
| total energy, closed adiabatic reactor | **abs 4.470348e-08 J; rel 1.396984e-15 (6.29 eps)**; worst step 1.513399e-15 (6.82 eps) | `8·eps·N_cells` |
| elemental invariants | **NOT MODELED** — no elemental metadata exists (§19: not implemented during P18.9 to convert an N/A into a PASS) | N/A |

Inflow, outflow and integrated source are reported separately, never only a residual.

## 10. CPU/GPU equivalence (§27–§30, §60)

Under amendment **A2** (`acceptance_gate_A2.md`, `f0973e34…`) with erratum.

| Quantity | Criterion | Max difference | Status |
| --- | --- | --- | --- |
| mixture properties `M,R,cp,cv,γ` | BITWISE | **0** over 2 304 comparisons | **PASS** |
| equation of state `ρ,p,T` | BITWISE | **0** over 512 cells | **PASS** |
| Arrhenius `k` | ≤ 8 ULP + identical branch | **3 ULP**; 1 564/2 048 exactly bitwise; **0/8 192** branch disagreements | **PASS** |
| progress rate `q` | ≤ 8·ε·Σ\|terms\| | **2.74** (gate), 3.139 (dry-run) | **PASS** |
| `ω_i` from the device's own `q` | BITWISE | **0 / 12 288** | **PASS** |
| heat release `S_E`, both routes | BITWISE | **0 / 1 024** each | **PASS** |
| temperature, energy pools | BITWISE | **0 / 1 024** | **PASS** |
| marched state over 50 steps | BITWISE on a state that moved | **0**, while `T` moved 28.6389 K and `Y` 0.0075085 | **PASS** |
| conservation, verified **independently on each backend** | `32·eps` per cell | within bound on device | **PASS** |
| status and first-offending reason | exact | 7/7 rows | **PASS** |

**§30 — equivalence is not correctness, and both were checked.** `Σω`, `ΣY` and the energy balance
are verified **on the device in their own right**, not merely against the CPU. P18.8's N11 control
caught a real device defect this way: the status was last-write-wins, naming `NonFiniteEnergy` (a
consequence) where the CPU names `NonPositiveTemperature` (the cause).

**§28 — species transport equivalence is NOT established**, because device species transport does
not exist (`audit.md` §5). Control N3 is **N/A** in the frozen gate for that reason. The GPU carries
the thermochemistry only.

**Scope of G9's PASS:** the thermochemistry operators and a closed reactor, within the
**constant-`cp`** subset, with integer reaction orders, at library level. Not a reacting-flow solve.

## 11. Determinism (§31, §61)

| Case | Backend | Repeats | Variation | Max difference | Status |
| --- | --- | ---: | --- | --- | --- |
| batch reactor | CPU | ✓ | — | **bitwise** | **PASS** |
| batch reactor | GPU | 5 | block sizes 64/128/256/512 | **bitwise** | **PASS** |
| reacting flow | CPU | ✓ | incl. the outer-iteration count | **bitwise** | **PASS** |
| reacting flow | GPU | — | — | **N/A** — no device reacting-flow solve exists | N/A |
| production cases | CPU | ✓ | separate processes | **bitwise** (`SpeciesProductionCaseTest`, `HeatedSpeciesDiffusionProductionCaseTest`) | **PASS** |

Control **N10**: halving `dt` over the same total time moves `T` by **0.0124043 K** and `Y` by
**3.26358e-06** — bounded away from zero and from a blow-up, so the bitwise agreements above are
agreements about a computation that is actually integrating.

## 12. CUDA diagnostics (§43, §62)

compute-sanitizer 2025.2.1.0 on the final tree. **Kernel scope is measured, not assumed:** the
filter launches all five production kernels, a native run is required to pass **15/15** first, and
every sanitizer run must report the same 15 — because "0 errors" is also what a sanitizer prints when
the filter matched nothing.

| Tool | Scope | Tests | Findings | Control proven? | Status |
| --- | --- | ---: | ---: | --- | --- |
| memcheck (`--leak-check full`) | 5 production kernels | 15/15 | **0** | **YES** — a deliberate OOB `cudaMemcpy` → 2 errors | **PASS** |
| initcheck | 5 production kernels | 15/15 | **0** | **YES** — a **production** kernel reading an uninitialised buffer → 2 972 errors | **PASS** |
| racecheck | shared-memory hazards | 15/15 | **0** | **NO — none possible** | **CLEAN, SCOPE-LIMITED** |
| synccheck | divergent `__syncthreads` | 15/15 | **0** | **NO — none possible** | **CLEAN, SCOPE-LIMITED** |

racecheck and synccheck detect hazards in constructs these kernels **do not contain** — measured
from source: 0 `__shared__`, 0 `__syncthreads`, 0 atomics, 0 cooperative groups, 0 warp shuffles in
both translation units. A clean result there follows from that absence and is **not** evidence about
cross-thread interference in general. What does cover it: one thread per cell with no cross-thread
reads (structural) plus G10's measured block-size independence.

## 13. Host sanitizers (§44, §45, §62)

**No P18 phase had ever run these.** P18.1 named the gap honestly at the time — *"There is
correspondingly little for ASan/UBSan to find here, but that is an argument for expecting a clean run,
NOT a substitute for one."* P18.9 ran it.

| Tool | Scope | Tests | Findings | Control proven? | Status |
| --- | --- | ---: | ---: | --- | --- |
| **ASan** | `build/asan` = Debug, CUDA **OFF**, OpenMP OFF; 2 835 registered, 13 excluded | **2 775 ran, 2 768 passed** | **0** | **YES** | **PASS** |
| **UBSan** | same binary, same run | same | **0** | **YES** | **PASS** |
| **LeakSanitizer** | `detect_leaks=1` | same | **0** | **YES** | **PASS** |

Settings are CLAUDE.md §7's verbatim: `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0`,
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`, `--timeout 7200`, plus the inherited
`OMP_NUM_THREADS=1 -j 8` policy. Elapsed **16 425 s (4.56 h)**.

**Live control (§45), because 0 findings is what a dead tool prints:**

* an **actual qualification binary** emits ASan's runtime option banner, links `libasan.so.6` and
  `libubsan.so.1`, and carries 52 `__asan_`/`__ubsan_` dynamic symbols;
* planted defects, **one program each**, under the **same options**: heap-buffer-overflow → ASan
  reports it; signed overflow → UBSan reports it; oversized shift → UBSan reports it; `malloc`
  without `free` → LeakSanitizer reports it.

The first version of that control put all three defects in one program and reported "UBSan did not
fire". UBSan was fine — ASan aborts on a memory error regardless of `halt_on_error=0`, so execution
never reached the others. Recorded because a control that tests three tools through one abort tests
one tool.

**Scope limits, measured:** 7 non-passes, **all `DriverSeegmiller*` timeouts** at 7200 s. Those tests
pass in Release at 1012–2011 s; under Debug+ASan's slowdown they cannot complete. They were left in
the run so the timeouts are **measured rather than silently excluded**. ASan covers **no CUDA kernel**
(the configuration is CUDA OFF); CUDA is covered by §12.

## 14. Transfer / residency (§46)

| Phase | H2D calls | H2D bytes | D2H calls | D2H bytes |
| --- | ---: | ---: | ---: | ---: |
| initialization | **> 0**, asserted | > 0 | 0 | 0 |
| **10-step resident loop** | **0** | **0** | **0** | **0** |
| final output | 0 | 0 | **> 0**, asserted | > 0 |

`largestDeviceToHostBytes` inside the loop is **strictly less than one field** — the quantity that
distinguishes "many small scalars" from "one field". Field classification:

| classification | fields |
| --- | --- |
| **RESIDENT** | composition (species-major), density, temperature, and every kernel output |
| **UPLOAD ONCE** | the `DeviceMechanism` POD |
| **PER-STEP H2D** | **none** |
| **PER-STEP D2H** | **none** |
| **HOST ONLY** | species transport (not implemented on device), case I/O |

Not inspected from source — **measured** with `GPUExecutionStats`, with control **N12** proving the
instrument registers an injected field-sized transfer. The bookends are asserted non-zero, so a run
that transferred nothing because it did nothing could not pass.

## 15. Performance measurements (§47) — NON-GATING

**P18 makes no performance claim, and none is frozen as a gate.**

No timing was measured: no mesh/steps/iterations/CPU-time/GPU-time comparison exists. E12 reports
transfer **counts**, not durations. Two reasons: there is no reacting-flow solve to measure (§4 G8,
§18 1.4), and CLAUDE.md §4 requires end-to-end measurement with a size ladder for any speed-up claim.

Consequently **no GPU slowdown is claimed either** — a slowdown would be as unevidenced as a
speed-up.

## 16. Negative controls (§32)

**113 control and rejection tests across P18.1–P18.8, every one passing** on this tree.

| §32 requirement | control | detects |
| --- | --- | --- |
| Y bounds | `RejectsNegative/AboveOne/NaN/Inf/ATinyNegative` | ✓ |
| `ΣY=1` | `RejectsASumMeaningfullyAbove/BelowOne` | ✓ |
| `ρY` transient semantics | `SpeciesControlTest.N1`, `SpeciesTransientTest` | ✓ |
| diffusion sign / species diffusivity mapping | `SpeciesControlTest.N4`, `SpeciesClosureTest` | ✓ |
| `ΣJ` closure | `P187NegativeControlTest.N3` | ✓ |
| boundary lag | P18.2's G4(c), ≤ 2 outer iterations | ✓ |
| mixture molecular-weight formula | `MixtureThermodynamicsTest.N1` | ✓ |
| `R_mix` composition dependence | `N2_RMixIsNotLeftConstant` | ✓ |
| reaction stoichiometric sign | `ReactionTest.N1` | ✓ |
| molecular-weight source factor | `ReactionSourceTest.N2` | ✓ |
| reaction mass balance | `ReactionTest.N4`, `ReactionSourceTest.N3` | ✓ |
| Arrhenius exponent sign | `ArrheniusTest.N1`, `P188ArrheniusTest.N4` | ✓ |
| `Ea` term | `ArrheniusTest.N2` | ✓ |
| gas-constant units (kmol ×1000) | `ArrheniusTest.N4`, `ReactionSourceTest.N9`, `HeatSourceTest.N9` | ✓ |
| stale temperature | `ClosedReactorTest.N4`, `P188CoupledStepTest.N7_N9` | ✓ |
| heat-source sign | `HeatSourceTest.N1`, `P188HeatReleaseTest.N6` | ✓ |
| energy double counting | `ClosedReactorTest.N5` | ✓ |
| missing heat source | `HeatSourceTest.N11` | ✓ |
| species/energy progress mismatch | `ClosedReactorTest.N8` | ✓ |
| GPU species permutation | `P188MixturePropertyTest.N1_N2` | ✓ |
| **GPU omitted diffusion closure** | — | **N/A — device transport not implemented** |
| GPU reaction/heat defect | `P188ReactionSourceTest.N5_N8`, `P188HeatReleaseTest.N6` | ✓ |

Every required control is present and effective except the one whose subject does not exist.
Restoration verified: all 113 pass, and each perturbation is a **runtime flag** on the production
path — a control run and a production run are the same binary.

## 17. Amendments

| Amendment | Why it was required | What was preserved |
| --- | --- | --- |
| **P18.5 A1** — Arrhenius rate-coefficient evaluation | the direct form overflows for large `A·T^n`; a log-space fallback is needed, and the branch must be reported | original criterion, original failure, the branch index as a compared quantity |
| **P18.8 A2** — CPU/GPU criteria E3/E4/E5/E6/E8/E9 | **bitwise is unreachable for transcendental-dependent quantities**: glibc and CUDA libdevice `exp`/`pow` are different implementations and IEEE-754 mandates correct rounding only for `+ − × ÷ sqrt`. Removing the difference would mean replacing the host libm, i.e. changing the CPU reference | the original gate is **unedited** (`3fc7a020…`); the failure is preserved verbatim (2 478 assertions); the derivation, the dry-run that rejected four drafts, and the authorization are all recorded |
| **A2 erratum 1** | A2 mislabelled three criteria (it put mixture properties under E7, which is the energy update) | A2's bytes unchanged; the erratum states the correction and that E7 is **not** amended and passes bitwise |

**No gate was amended because an implementation failed it** (§53). A1 and A2 both replaced criteria
shown to be *instrumentally or mathematically* wrong, and **G8 — the one gate P18 fails — was not
amended at all.**

## 18. Known limitations

`results/p18/KNOWN_LIMITATIONS.md` — 12 demonstrated P18 limitations and 5 inherited items, each with
evidence, plus a section naming three limitations deliberately **not** claimed because nothing
supports them. The load-bearing ones:

1. **G8 not closed** — no reacting-flow solver, stiff integrator or flame-speed extraction.
2. **No stiff integrator**; the explicit march has a measured `ΣY=1` step budget (~72 steps at 3
   species, tighter for fewer).
3. **Elemental conservation NOT MODELED.**
4. **Library-level only** — **0 files outside `species/` reference the chemistry, 0 case keys define a
   mechanism.** P18.2–P18.8 are not case-file reachable, so under CLAUDE.md §9 they are a verified
   library, not production capability.
5. Bitwise CPU/GPU unreachable for transcendental-dependent quantities (the A2 finding).
6. Device carries **constant `cp` only**; integer reaction orders; capacities 8 species / 4 reactions.
7. **No** species transport on device.
8. **No** performance claim, reacting-shock, detonation, or 3D reacting qualification.
9. WSL2 only.

## 19. Repository integrity (§50, §51)

| | |
| --- | --- |
| `git status` | 126 tracked modified, 168 untracked — **identical to the candidate freeze** |
| runtime noise | **reverted**: `results/validation` 50→0 and case metadata 4→0, twice (after the ASan run and after the regression), each classified with a fail-closed instrument before reverting |
| `git diff --check` | **328 warnings**, measured per file: `cuda/CMakeLists.txt` 206, `tests/unit/gpu/CMakeLists.txt` 90, `tests/unit/compressible/CMakeLists.txt` 32 |
| whitespace attribution | all three are CRLF files under `.gitattributes` `* -text`, where **every** added line trips the check. The first two were already modified before P18.8; P18 added ~50 and ~15 lines. The third is **P17's, not P18's** |
| `clang-format-18` | **0 violations** across every P18 source |
| unexplained changes | **none** |

Every modification classifies as P18 production, P18 tests, P18 evidence, or pre-existing. **The
pre-existing dirty state was not cleaned** (§50).

**The 328 warnings remain a push blocker**, recorded in `TODO.md`. They are **not** P18-introduced in
kind: P18 added lines to files that were already modified and already CRLF.

## 20. Final P18 status

```text
G1  : PASS
G2  : PASS
G3  : PASS
G4  : PASS
G5  : PASS
G6  : PASS
G7  : PASS
G8  : NOT CLOSED        <-- blocking
G9  : PASS   (scoped: thermochemistry operators + closed reactor, constant-cp, library-level)
G10 : PASS
G11 : PASS

P18 OVERALL: PARTIAL
```

**The single blocker:** G8 requires a 1D laminar premixed flame with a flame speed inside a cited
published spread. Closing it needs a reacting-flow solver, a stiff integrator and flame-speed
extraction — none of which exist, and all of which are new feature development that P18.9's
authorization excludes.

**A second, wider limitation that is not a gate but bears on what "qualified" means here:** no case
file can reach any of P18.2–P18.8. Under CLAUDE.md §9 the reacting-species capability is a **verified
library**, not production capability.

So the answer to P18.9's own question — is the combined P18 tree genuinely qualified as one
conservative, thermodynamically consistent, quantitatively validated, deterministic CPU/GPU
reacting-species capability? — is: **it is conservative, thermodynamically consistent, deterministic
and CPU/GPU-equivalent to the measured bounds, and it is quantitatively validated against an
independent reference for a batch reactor and a steady plug flow. It is not validated against a
canonical reacting benchmark, and it is not reachable from a case file.**

**NO NEXT PHASE AUTHORIZED.**
