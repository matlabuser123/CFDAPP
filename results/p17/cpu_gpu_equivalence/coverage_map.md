# P17.10 — gate-criterion coverage map

Written **after** the gate was frozen and **after** the measurements, which is the only order in
which this document can be honest. Its job is not to show that everything was covered — it is to
find the criteria that were measured by nothing, or by the wrong probe.

That is not a hypothetical concern here. Building this map found **three frozen criteria that
nothing I had run actually measured** (B5, C4/C5, D4). They were closed by adding tests, not by
arguing that the other criteria implied them. Their rows below say so.

```text
GATE   acceptance_gate.md, sha256 c5b86211dce2128152f5fe8d5a93b8ba739fc596da562f485af18d3ce6efbb51
LEGEND
  MEASURED        a test drives it and would fail if it broke
  BY COMPOSITION  not separately measured; follows from a measured criterion by an argument that
                  is stated, so a reader can judge the argument
  BY CONSTRUCTION the property cannot fail without a different implementation; the design note is
                  cited
  NOT MEASURED    nothing tests it. Said plainly.
```

---

## Group A — the arithmetic

| id | status | what actually measures it |
| --- | --- | --- |
| A1 | **MEASURED** | `A1_RecoveryIsBitwiseIdenticalToProduction` — 120 comparisons, 2 gases incl. a nonzero enthalpy reference. Control: `A1_NonVacuity_RegroupingTheProducts…`, 1 of 20 values |
| A2 | **MEASURED** | `A2_RefusalsAgreeAndForTheSameReason` (6 refusals, reason compared) + `A2_TemperatureRangeIsEnforced…` (trap 7, control accepts 4730.93 K) + `C4_InadmissibleStatesAreRefusedByBoth…` on device |
| A3 | **MEASURED** | the `pressure` column of A1; the parenthesisation control is its non-vacuity |
| A4 | **MEASURED** | the `speedOfSound` column of A1, same control |
| A5 | **BY COMPOSITION** | HLLC is not restated — the kernel calls the shared `CFD_HLLC_HOST_DEVICE` source. Its *output* is compared face by face in B1 (2360 values), and V1 shows a different scheme on device is detected. There is no separate device-flux-vs-host-flux row for the density path because there is nothing separate to compare; P17.7's `test_hllc_gpu_equivalence.cpp` covers the standalone evaluator and re-runs in the regression |
| A6 | **MEASURED** | `A6_ExteriorStateIsBitwiseIdenticalForAllThreeKinds` — 144 values. Control: carrying rhoE over fires on 2 of 3 non-axis-aligned normals |
| A7 | **MEASURED** | `A7_DeviceRecoveryMatchesProductionToPrimitiveBitwise` — 1736 values from the actual kernel |

## Group B — the local operator

| id | status | what actually measures it |
| --- | --- | --- |
| B1 | **MEASURED** | `B1_FaceFluxesAreBitwise` — 472 faces × 5 |
| B2 | **MEASURED** | `B2_B4_ResidualIsBitwise…` — 2495 values across three meshes. Controls V1/V3/V4 |
| B3 | **BY COMPOSITION** | boundary exterior states are not downloaded per face. A6 measures the function bitwise; B1 includes every boundary face, so a wrong exterior state changes a compared flux. **Weaker than a direct per-boundary-face comparison, and named as such** |
| B4 | **MEASURED** | the `graded_2d` row, volume ratio 6849:1 — the mesh where a dropped 1/V cannot hide |
| B5 | **MEASURED — and it was NOT, until this map was written** | `B5_InteriorFaceContributionsCancelExactlyInTheGather`: 510 values rebuilt on the host from the *downloaded device face fluxes* and compared bitwise, over 362 interior cell-face references. Before this test the property was true by construction and measured by nothing |

## Group C — one step and the controls

| id | status | what actually measures it |
| --- | --- | --- |
| C1 | **MEASURED** | `C1_C2_LocalAndGlobalTimeStepsAreBitwise`, graded mesh, 102 values. Control V6: 217 of 217 differ |
| C2 | **MEASURED** | same test, global dt bitwise. R5 is the real control: a different reduction tree gives the same answer |
| C3 | **MEASURED** | D1 step 1 — 1025 values |
| C4 | **MEASURED — and it was NOT, until this map was written** | `C4_InadmissibleStatesAreRefusedByBothNamingTheSameCell`. Three kinds of inadmissible state, **two offending cells each**, so a reduction returning *some* offender rather than the first would diverge. Both name cell 9; reasons 5/2/4. The happy path is asserted first so the refusal is not "this fixture always refuses". Before this test, every trajectory asserted `admissible == true` and the reject path was never exercised at all |
| C5 | **PARTIAL, and stated as such** | the retry loop is host control logic with **no device counterpart by design** (audit.md §6). What is compared is the *decision* that triggers a retry (C4). **A run in which the CPU actually retries and the retry counts are compared was NOT performed.** The gate asked for one; it is not here |
| C6 | **BY CONSTRUCTION** | conservation sums are computed host-side from the conserved state by the same code, so they are identical whenever the state is (audit.md §6). Recorded as a decision before implementation, not discovered as a limitation |

## Group D — trajectories and benchmarks

```text
NOT to be confused with the DRY-RUN's tests D1..D6, which share the letter and measure the
comparison instrument. The rows below are the frozen gate's Group D.
```


| id | status | what actually measures it |
| --- | --- | --- |
| D1 | **MEASURED** | 1, 2, 5, 10, 25, 50 steps; dt compared bitwise at *every* step |
| D2 | **MEASURED** | first divergence: none through 50 steps |
| D3 | **MEASURED** | Sod, 500 steps, 3000 values, with 101 cells at an intermediate state asserted |
| D4 | **MEASURED — and it was NOT, until this map was written** | `D4_DetectedWavePositionsAgreeOnBothBackends`: P17.9's own detectors on both solutions. Shock at 0.7649115536763863 on both, bitwise; transition width 7 cells on both. The wave must be *found*, so "identical" cannot be two identical absences |
| D5 | **NOT MEASURED** | the normal-shock M=2 case was not run on the GPU. P17.9 validated it on the CPU and D3/D4 show the backends agree bitwise on Sod, but **the jump ratios were not measured on the GPU** and this document does not claim they were |
| D6 | **PARTIAL** | the benchmark matrix A–G was not run as a labelled matrix. What ran: uniform 2D, graded 2D, Cartesian 3D, a 50-step smooth trajectory, and Sod. **The low-Mach and normal-shock entries were not run on the GPU** |

```text
A MEASUREMENT ABOUT D4 WORTH KEEPING. Under --fmad=true, D3 FAILS (the state differs) while D4
PASSES (the detected shock position is still bitwise identical). A crossing-position detector
quantises, so it is LESS sensitive than the state comparison. D4 is therefore supplementary
evidence, not a substitute for D3, and anyone tempted to gate equivalence on wave positions alone
should read that pair of results first.
```

## Group R — determinism

| id | status | what actually measures it |
| --- | --- | --- |
| R1 | **MEASURED** | dry-run D6 — 34 steps, state and dt bitwise across repeats |
| R2 | **MEASURED** | `R2_R5_GpuIsBitwiseRepeatable…` — 30 steps, same process |
| R3 | **MEASURED — closed after this map was first written** | `tools/separate_process_determinism.sh`: two independent processes, complete stdout compared byte for byte (timings stripped), sha256 `6e44d998…` both. Non-vacuity: the comparison is refused unless the output actually contains the high-precision GPU numbers, and a one-character perturbation is confirmed detected |
| R4 | **MEASURED** | zero atomics by source **and** racecheck 0 hazards — with a deliberately racy control kernel reported by name, so the zero is not a fail-open read |
| R5 | **MEASURED** | 64 vs 128 threads: state and every per-step dt bitwise. This is the criterion that actually tests the order-independence argument |

## Group T — transfer and residency

| id | status | what actually measures it |
| --- | --- | --- |
| T1 | **MEASURED** | 20-step resident run: 180 kernel launches, **0** host→device per step, 40 device→host against 100 for a full per-step round trip. Asserted with the comparison number printed, not as "0 transfers", which would be false |
| T2 | **MEASURED** | 460 values round-tripped bitwise |
| T3 | **REPORTED, not gated** | 50488 resident bytes — as the gate says |
| T4 | **NOT MEASURED** | re-uploading the same mesh performing no allocation was not tested for the new solver. `DeviceBuffer`'s never-shrink contract is qualified elsewhere (GPU-DISC-001A), and this phase did not re-verify it |

## Group N — CUDA diagnostics

| id | status | evidence |
| --- | --- | --- |
| N1 | **MEASURED, with a live control** | memcheck 0 errors on 15 tests; control run: 68 errors, 64 lines naming `sanitizerOobKernel` |
| N2 | **MEASURED, with a live control** | racecheck 0 hazards; control run: 1 hazard naming `sanitizerRaceKernel` |
| N3 | **MEASURED, no dedicated control** | initcheck 0 errors. Shares compute-sanitizer's instrumentation with memcheck, which is *demonstrated* active on this library — so not baseless, but **weaker than N1/N2** |
| N4 | **MEASURED, no dedicated control** | synccheck 0 errors. Same caveat |

```text
THE FIRST VERSION OF THE SANITIZER EVIDENCE WAS FAIL-OPEN, and it is recorded rather than quietly
replaced. It counted log lines naming our kernels and got 0 for all four tools -- which is what a
CLEAN run looks like, because a sanitizer names a kernel only when something is wrong. "0 errors"
and "never instrumented" produced identical output. The controls exist because that probe printed
exactly what success looks like while measuring nothing.
```

## Group V — negative controls

| id | status | measured |
| --- | --- | --- |
| V1 | **MEASURED** | HLL on device: 868 of 1085 residual values differ |
| V2 | **MEASURED** | regrouped products: 1 of 20 — weak but non-zero, reported as measured |
| V3 | **MEASURED** | reversed gather: 821 of 1085. The load-bearing one |
| V4 | **MEASURED** | neighbour's own normal: 3 of 1085 — small, and not zero |
| V5 | **MEASURED** | rhoE carried over: 2 of 3 non-axis-aligned normals |
| V6 | **MEASURED** | sound speed from mean T: 217 of 217 dt values |
| V7 | **MEASURED** | `--fmad=true`: 8 of 15 tests pass; trajectory divergence grows 106 → 704 of 1025 over 50 steps |
| V8 | **MEASURED** | range check removed: device accepts 4730.93 K against a 2500 K ceiling |
| — | **MEASURED** | the positive case: production with all controls off, 0 of 1085 |

## Group P — preservation

| id | status | evidence |
| --- | --- | --- |
| P1 | see `regression.md` | library hashes recorded per configuration |
| P2 | **MEASURED** | the CPU results are the comparison baseline throughout; P17.9's 13 shock rows and P17.8's solver rows re-run in both configurations |
| P3 | see `regression.md` | CPU-only build, nvcc/libcudart counts |
| P4 | see `regression.md` | counts against a prediction declared before the run |
| P5 | **MEASURED** | both inherited gate hashes re-checked at the head of every step and by the regression harness, which fails closed |
| P6 | **MEASURED** | the three P17.9 amendments listed by the harness; none modified |

---

## Summary of the gaps, collected so they are not spread across a table

```text
NOT MEASURED AT ALL
  C5  a run in which the CPU actually RETRIES, with retry counts compared
  D5  the normal-shock M=2 jump ratios, on the GPU
  T4  no-reallocation on mesh re-upload, for this solver

PARTIAL / WEAKER THAN THE CRITERION READS
  B3  boundary exterior states, covered through B1 rather than directly
  D6  the benchmark matrix ran 5 configurations, not the labelled A-G set
  N3, N4  no dedicated non-vacuity control

NONE OF THESE WERE DISCOVERED BY A FAILING TEST. They were found by writing this table against the
frozen gate line by line, which is the only reason they are visible. FOUR others found the same way
(B5, C4, D4 and R3) were closed rather than argued away -- three with new tests, R3 with a script
over the existing binary. The difference between the two groups is cost, not principle, and the
three above are the honest remainder.
```
