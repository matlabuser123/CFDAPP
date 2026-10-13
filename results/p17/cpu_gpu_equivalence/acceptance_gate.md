# P17.10 — acceptance gate

```text
FROZEN before any CUDA source for the density-based path exists.
DRY-RUN   results below are from logs/01_dryrun.log, run BEFORE this file was written.
POLICY    tolerance_policy.md, sha256 e69d8ef2b8cb5f8c7bac320ab7b153dd0b914a7f658718f2f774845863b8d116
          frozen before this gate, and it says one word: BITWISE.
DESIGN    kernel_inventory.md — what is ported, what is shared, and the seven traps.
AUDIT     audit.md — the classification and the ascending-face-list finding this all rests on.
```

CLAUDE.md §5.2 warns about exactly the position this phase starts from:

> *"If the baseline cannot exercise the new behaviour being gated, the dry-run is insufficient…
> Ask separately whether a correct post-change implementation can satisfy the criterion, and add a
> control that can fail."*

There is no GPU density-based solver, so *"the GPU matches the CPU"* cannot be run against the
baseline at all. **§0 therefore dry-runs the two things that CAN fail today** — the mesh invariant
every bitwise claim depends on, and the comparison instrument itself — and every criterion below
names the control that makes it non-vacuous once the implementation exists.

---

## 0. The pre-freeze dry-run — what was measured before this file was written

```text
D1  THE LOAD-BEARING INVARIANT, measured rather than argued.
    Every cell's face list must be ASCENDING, or the per-cell gather cannot reproduce the CPU's
    ascending face-loop accumulation order and BITWISE is unreachable by design.

      cartesian2d_uniform            cells 35  faces  82  cellFaceRefs 140   ascending YES
      cartesian2d_single_column      cells 40  faces 121  cellFaceRefs 160   ascending YES
      graded2d_geometric_both_axes   cells 24  faces  58  cellFaceRefs  96   ascending YES
      cartesian3d                    cells 24  faces  98  cellFaceRefs 144   ascending YES
      cartesian3d_translated_origin  cells 27  faces 108  cellFaceRefs 162   ascending YES
      TOTAL cell-face references examined: 702

    NON-VACUITY: the same predicate applied to {9,7,3,1} reports NOT ascending. It is a detector,
    not a function that returns true.

D2  THE INSTRUMENT, positive behaviour.  180 values compared, 0 differing -> EQUAL.

D3  THE INSTRUMENT, negative behaviour. ONE ULP -- the smallest difference that exists -- in each
    of the five conserved components, at interior cell 17:
      density  momentumX  momentumY  momentumZ  totalEnergy   ALL DETECTED, 1 of 180 values
    momentumY is the sharpest of the five: it is exactly 0.0 there, so one ULP up is the smallest
    subnormal, ~4.9e-324. A RELATIVE tolerance would miss it completely. A bit comparison does not.

D4  THE INSTRUMENT, fail-closed. Five ways a comparison can silently not happen, each of which
    produces "zero differences found" and no error:
      a  the report was never touched          -> NOT equal ("the comparison never ran")
      b  the GPU side was never populated      -> NOT equal
      c  the CPU side was never populated      -> NOT equal
      d  BOTH sides empty, 0 values compared   -> NOT equal ("zero values were compared")
      e  size mismatch, 36 vs 0                -> NOT equal, and the overlap is NOT compared
    This is the failure mode this project has been bitten by before, so it is closed by
    construction: ComparisonReport's DEFAULT STATE IS FAILURE and nothing can reach a pass without
    a positive count of compared values from two populated sides.

D5  EQUIVALENCE IS NOT VALIDITY. Two identical NaN bit patterns ARE the same computed result and
    are reported equal -- while nonFinite cpu=1 gpu=1 is carried out separately, so a pair of
    matching NaNs can never be read as a clean pass on its own.

D6  THE BASELINE ITSELF. A 34-step CPU solve, run twice: state BITWISE equal, dt BITWISE equal at
    every step. If the CPU were not repeatable, no GPU comparison would mean anything.

ALL SEVEN dry-run tests PASS. No production source was touched to obtain this.
```

---

## 1. Group A — Level A, the arithmetic (§5, §6, §30)

| id | quantity | requirement | non-vacuity |
| --- | --- | --- | --- |
| A1 | device recovery `Q -> (u, T, p, a)` vs `TotalEnergyState::recover()` | **BITWISE**, every component, over a matrix of states | perturb one uploaded scalar (`cp`) by 1 ULP: A1 must fail |
| A2 | the accept/reject **decision** of that recovery, and its reason | **IDENTICAL**, including the `e < eFloor` and `T` out-of-range rejections | feed states that violate each rejection in turn; both sides must refuse, for the same reason |
| A3 | `p = (rho R) T` | **BITWISE** | covered by A1's parenthesisation control below |
| A4 | `a = sqrt((gamma R) T)` | **BITWISE** | as A3 |
| A5 | `hllcFlux` on device vs host, flux + wave speeds + branch + star states | **BITWISE** | P17.7's V2 control (HLL, contact wave deleted) on device must DIFFER from HLLC |
| A6 | `exteriorState` device vs the production free function, all 3 boundary kinds | **BITWISE** | drop the rhoE rebuild (trap 3); A6 must fail |
| A7 | device header compiled for **HOST** vs compiled for **DEVICE** | **BITWISE** | this is the half a single end-to-end comparison cannot attribute — see kernel_inventory.md §5 |

```text
A1's PARENTHESISATION CONTROL, stated in advance so it cannot be invented afterwards. The CPU
computes (rho*R)*T and (gamma*R)*T, left to right. A device build that writes rho*(R*T) is
mathematically identical and differs in the last bit. The control replaces one grouping with the
other and requires A1 and A3/A4 to FAIL. If they still pass, the criterion is measuring nothing.
```

## 2. Group B — Level B, the local operator (§7, §8, §9, §13)

| id | quantity | requirement | non-vacuity |
| --- | --- | --- | --- |
| B1 | per-face flux, all 5 components, every face | **BITWISE** | V2 (HLL) on device must break it |
| B2 | per-cell residual `R_P`, all 5 components | **BITWISE** | reverse the gather order in one cell: B2 must fail |
| B3 | boundary exterior states at every boundary face | **BITWISE** | A6's control |
| B4 | the residual on a **graded** mesh, where `V_P` varies cell to cell | **BITWISE** | a uniform mesh lets a dropped `1/V` hide — P17.8's N2 control found exactly this, so the graded mesh is mandatory here, not optional |
| B5 | interior-face flux cancellation: `+F` to owner, `-F` to neighbour | **exactly equal and opposite**, bit for bit | re-evaluate the neighbour side with its own normal (trap 1): B5 must fail |

## 3. Group C — Level C, one step and the controls (§10, §14, §15, §16)

| id | quantity | requirement | non-vacuity |
| --- | --- | --- | --- |
| C1 | per-cell local `dt` | **BITWISE** | recompute `a` from the mean temperature instead of averaging `a` (trap 5): C1 must fail |
| C2 | global `dt` (the min reduction) | **BITWISE** | C1's control |
| C3 | `Q` after **one** update from an identical `Q0` | **BITWISE** | B2's control |
| C4 | positivity / non-finite **decisions**: counts, and the first offending cell | **EXACTLY EQUAL**, and the first offender identical | plant an inadmissible cell; both backends must refuse, naming the same cell |
| C5 | retry and rejection counts over a run that actually retries | **EXACTLY EQUAL** | a case with 0 retries proves nothing, so the case must be one that retries — declared and shown |
| C6 | conservation sums after the step | **BITWISE** | computed host-side from the state, so this is equality by construction and is recorded as such, not claimed as a finding |

## 4. Group D — Levels D and E, trajectories and benchmarks (§11, §17–§19, §22)

| id | quantity | requirement | non-vacuity |
| --- | --- | --- | --- |
| D1 | `Q` after 1, 2, 5, 10, 25, 50 steps | **BITWISE at every one** | any Group A/B/C control also breaks these |
| D2 | the step at which CPU and GPU first differ | **never** | reported as "no divergence through N steps", with N stated |
| D3 | Sod shock tube, full run, final state | **BITWISE** | V2 on device |
| D4 | Sod: the detected shock, contact and rarefaction positions, on both backends | **BITWISE**, and each inside P17.9's frozen bounds | P17.9's own controls, re-run per backend |
| D5 | normal shock M=2: every jump ratio, both backends | **BITWISE**, and inside P17.9's bounds | as D4 |
| D6 | the benchmark matrix A–G | **BITWISE** each | each case names what it would catch that the others do not |

```text
D3-D5 DO NOT RE-DERIVE P17.9's THRESHOLDS. Those bounds are frozen and inherited unchanged
(tolerance_policy.md §4). What is new here is that BOTH backends must independently sit inside
them AND agree with each other bitwise. A GPU that matched the CPU bitwise while both drifted
outside P17.9's bounds would fail D4/D5, not pass them -- equivalence is not correctness.
```

## 5. Group R — determinism (§20, §21)

| id | quantity | requirement | non-vacuity |
| --- | --- | --- | --- |
| R1 | CPU run twice | **BITWISE** | **already measured**, dry-run D6: 34 steps, state and dt bitwise |
| R2 | GPU run twice, same process | **BITWISE** | achievable because the gather uses NO atomics — see R4 |
| R3 | GPU run twice, separate processes | **BITWISE** | |
| R4 | atomic operations in the new kernels | **exactly 0**, by source inspection AND by the racecheck run in N2 | a grep alone is a fail-open probe; N2 is the live control |
| R5 | GPU determinism under a different block size | **BITWISE** | a result that depends on launch configuration is not deterministic; this is the control that a gather really is order-independent of the launch |

## 6. Group T — transfer and residency (§23, §24, §25)

| id | quantity | requirement | non-vacuity |
| --- | --- | --- | --- |
| T1 | host->device and device->host transfers **per time step**, counted | the conserved state must NOT round-trip per step; only the `dt` scalar and the control flags may cross | a counter that reads 0 on a path that transfers is fail-open — T1 asserts a POSITIVE expected count for the upload and download that do happen, and 0 only where 0 is the claim |
| T2 | the upload/download pair is exactly inverse | **BITWISE**: upload `Q`, download it unchanged, compare | perturb one value before download: must fail |
| T3 | device-resident bytes, reported | reported, not gated | — |
| T4 | re-uploading the same mesh performs no allocation | reallocation count unchanged | DeviceBuffer's never-shrink contract, re-verified |

## 7. Group N — CUDA diagnostics (§26, §27)

| id | tool | requirement |
| --- | --- | --- |
| N1 | `compute-sanitizer --tool memcheck` | **0 errors** |
| N2 | `compute-sanitizer --tool racecheck` | **0 hazards** — and this is what makes R4's "no atomics" claim a measurement rather than a grep |
| N3 | `compute-sanitizer --tool initcheck` | **0 uninitialised reads** |
| N4 | `compute-sanitizer --tool synccheck` | **0 errors** |

```text
EVERY ONE OF N1-N4 MUST BE SHOWN TO HAVE ACTUALLY EXECUTED. §50 forbids writing PASS for an
unexecuted diagnostic, and a sanitizer that failed to launch prints no errors either -- which is
indistinguishable from success in the log. Each run therefore records the tool's own banner, its
exit status, and the kernel launch count it observed. A run reporting ZERO KERNELS LAUNCHED is a
FAILURE, not a pass.
```

## 8. Group V — negative controls, the evidence the gate means something (§28, §29)

| id | control | must |
| --- | --- | --- |
| V1 | HLL (contact wave deleted) on **device only**, HLLC on CPU | **FAIL** Group A/B/D — a GPU path that is silently a different scheme must be caught |
| V2 | one uploaded thermodynamic scalar perturbed by 1 ULP | **FAIL** A1 |
| V3 | the residual gather visiting a cell's faces in REVERSE order | **FAIL** B2 — this is the direct test that the ascending order is load-bearing and not decorative |
| V4 | the neighbour contribution re-evaluated with its own outward normal (trap 1) | **FAIL** B5 |
| V5 | `exteriorState` simplified to carry rhoE over unchanged (trap 3) | **FAIL** A6/B3 |
| V6 | the CFL face sound speed recomputed from the mean temperature (trap 5) | **FAIL** C1 |
| V7 | `--fmad=true` on the new kernels | **REPORTED**, with the measured difference — P17.7 did this and found 8/17, so the cost of contraction is a number, not a hypothesis |
| V8 | the device range check (trap 7) removed | **FAIL** A2 — the GPU would accept states the CPU refuses |

```text
A CONTROL THAT FAILS FOR THE WRONG REASON IS NOT A PASSING CONTROL. Each of V1-V8 must fail on the
NAMED criterion. A control that instead crashes, or fails a different criterion, is recorded as
such and investigated -- an identical crash on both sides is not "identical behaviour".
```

## 9. Group P — preservation (G1, §36, §37, §40)

| id | quantity | requirement |
| --- | --- | --- |
| P1 | `src/libcfdcore.a` | Any change from P17.9's `188e5039…` / `1305a82a…` must be explained by a named, intended edit — no silent drift |
| P2 | the CPU density-based solver's own results | **BITWISE unchanged** from P17.8/P17.9 |
| P3 | CPU-only build (`CFDAPP_ENABLE_CUDA=OFF`) | builds and passes; 0 nvcc invocations, 0 libcudart references |
| P4 | full regression, Release+CUDA and CPU-only | 0 failures, counts matching a prediction declared **before** the run |
| P5 | P17.8's and P17.9's frozen gates | sha256 unmoved: `a88738e4…` and `b7185cea…` |
| P6 | the three P17.9 amendments | present and unmodified; their historical records not replaced (§41) |

```text
P1 IS DELIBERATELY NOT "BYTE-IDENTICAL". P17.9 could claim that, because it added no production
source. P17.10 IS an implementation phase: it adds device code. If that code lands in cfdcore
rather than cfdcuda the archive changes legitimately, and a criterion demanding byte-identity would
either be violated or would push the implementation into the wrong library to satisfy it. What is
gated is that any change is ACCOUNTED FOR, and that P2 -- the CPU's actual numerical output -- is
bitwise unchanged. That is the property that matters.
```

## 10. Execution policy — FROZEN, inherited verbatim

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
5 exclusion patterns, 13 tests, unchanged since P13's G10 closeout
build/qual-final (Release+CUDA+OpenMP) and build/qual-cpuonly (CUDA OFF)
build/qual-debug NOT run -- deferred to P17.11 by the standing decision every P17 phase has made
The exclusion set is NOT widened by one entry.
```

## 11. What P17.10 will NOT claim

```text
PERFORMANCE          §32 forbids a speed claim without equivalence, and this phase makes none even
                     with it. No timing number in this phase's evidence is a claim.
2D/3D shock structure inherited scope from P17.9: 1D problems on 2D meshes.
viscous terms         Euler only, as P17.8 built it.
subsonic open BCs     NOT IMPLEMENTED in P17.8; not added here.
restart on GPU        the CPU refuses; §34 requires preserving the refusal, not building around it.
Debug+GUI, sanitizers (host) NOT RUN -- deferred to P17.11, the same standing decision.
native Windows        WSL2 Ubuntu-22.04 only.
G10 full qualification P17.11's, not claimed here.
```

## 12. Failure policy

```text
Run the criteria in the frozen order. STOP AT THE FIRST FAILURE.
Classifying or explaining a failure does not waive it.

If a criterion fails, the response is NOT to relax it. tolerance_policy.md §2 already refuses to
carry a fallback tolerance for exactly this reason. The response is:
  1  preserve the failure verbatim;
  2  identify which of the five bitwise preconditions was violated -- they are individually
     checkable, which is why they were enumerated before any code was written;
  3  fix the GPU implementation, never the CPU formula (§2) and never the threshold;
  4  if and only if the violation is irreducible, derive a tolerance from THAT mechanism, request
     authorization, and freeze it as a separate amendment file with its own dry-run.

NOTHING IN THIS FILE IS EDITED AFTER THIS POINT. Amendments live in amendments/ and leave the
frozen text untouched, exactly as P17.9's three did.
```
