# GPU-PIPE-001 — Final Residency

The GPU SIMPLE outer iteration now runs device-resident end to end. In steady state it uploads
**nothing**, and besides the one face-flux download the convergence definition requires, it brings
back **40 bytes**.

```text
                                        BEFORE                    AFTER
steady-state H2D, 160^2       13 calls / 4,085,760 B      0 calls /        0 B
non-reduction D2H, 160^2      17 calls / 6,330,912 B      8 calls /  412,200 B
  of which the justified face flux                                   412,160 B
  everything else                                                         40 B
steady-state allocations                       0                          0
steady-state reallocations                     0                          0

equivalence        BITWISE, 12 cases, 217,755 values, incl. two 60-iteration long runs
CPU/GPU production 15 cases + a 160/320/640 ladder, 0 failures
negative controls  12/12 observable detected, 2 documented null
CUDA diagnostics   16/16 runs, 0 errors / 0 hazards, all non-vacuous
known BiCGSTAB     UNCHANGED -- PressureCorrectionFailure at exactly 1845, as recorded
640^2 speed-up     3.593x  ->  17.362x      crossover 320^2 -> 160^2

regression         Release+CUDA 1998/1998   Debug+GUI 1984/1984   ASan/UBSan 1932/1932
                   generated outputs: 0 non-timing changes in 3014 files
                   clang-format: 0 violations in 590 files (105 pre-existing, cleared)
```

All seven GPU-PIPE-001 final-qualification gates are green. **GPU-PIPE-001 is FINISHED.**

**Six of my own instruments were wrong, and every failure is preserved rather than rewritten.** The
production code was never the cause of any of them:

| what failed | why | where |
| --- | --- | --- |
| the transfer gate | subtracted 8 B per Krylov reduction; `reduceToHost` downloads `blocks × count × 8` | §5.2, amended under authorisation |
| its own dry-run | demanded every criterion fail on the baseline, including two that must hold | §5.4 |
| the known-reproducer check | ran Jacobi where the recorded reproducer uses no preconditioner | §6.4 |
| the 640² ladder case | passed vacuously — both arms failed at iteration 0 | §6.2 |
| negative control `rl6` | pointed at a detector that never calls the mutated method | §8.1 |
| the Gate A assessor | aggregated `fail` inside a subshell, so it could not fail | §2.1 |

Every one was found by an instrument doing its job, and each is recorded with the evidence that
exposed it.

---

## 1. Part 1 — persistent GPU fields, re-verified

Re-verified on the binaries under test, not adopted from the earlier gate's record.

| property | how it was confirmed |
| --- | --- |
| pressure persists on device | `authority(Pressure) == DeviceOwned` after `updatePressure`; downloaded once after the loop |
| U/V/W persist on device | `authority(Velocity) == DeviceOwned` after `correctVelocityResident` |
| face flux persists on device | `authority(MassFlux) == DeviceOwned` after `correctFaceMassFluxResident` |
| response coefficients persist | the stage performs **0 D2H** — nothing on the device path reads a host copy |
| p' storage is persistent | `solvePressureCorrectionResident` returns an **empty** host solution |
| matrices / workspaces persist | a repeated assemble + resident solve allocates **0**, reallocates **0**; `residentSolveBytes()` unchanged |
| steady-state field reallocations | **0** on every case, 2D and 3D, both arms |
| 2D/3D field contracts | 2D: the resident velocity's W is exactly zero; 3D: W is live; sizes match the mesh |
| authority / dirty-state semantics | the persistent-fields dirty-state suite re-passes |

`persistent-fields/dirty-state.log`, `transfers/before.log`, `persistent-fields/gpu-gates.log`
(15/15 GPU-DISC differential gates, bitwise).

### 1.1 A finding, recorded before it could be mistaken for a defect

`computeResponseCoefficients` uploaded a **host zero-vector** into the W predictor on every 2D
iteration — correct behaviour, but a full-field H2D the device could do itself. My first probe
asserted "this stage transfers nothing", which 2D failed and 3D passed. The criterion was wrong,
not the code: the residency property is that the coefficients never come *back*. It was corrected
to `D2H == 0` and the upload recorded as its own measured fact **before** any gate ran against it.
Part 3 then eliminated it with a device fill.

### 1.2 The before-state

Fitted from a delta between outer-iteration budgets, with a third budget as a linearity control.

```text
                             H2D calls   H2D bytes    D2H calls   D2H bytes    sync    alloc  realloc
cavity 2d 16   disc-only          6.00      12,288        17.00      81,148    0.00     0.00     0.00
cavity 2d 16   production        13.00      41,984       387.50      66,892  370.50     0.00     0.00
cavity 3d 6    production        18.00      57,024       242.33      92,086  220.33     0.00     0.00
cavity 2d 160  production        13.00   4,290,560      2662.33  10,031,645 2645.33     0.00     0.00
```

---

## 2. Part 2 — Gate A, the GPU-resident pressure solve: **PASS**

The implementation was the previous session's. This milestone completed its four pending phases and
assessed the gate from the evidence **files** (`pressure-solve/gate-a.log`), because that session's
console output went to its own terminal.

```text
transfers/guard.log        0 H2D, 0 assemble D2H; 2 non-reduction D2H (12 bytes); 0 allocations
lifecycle/probe.log        14 properties hold, 0 FAIL
comparison/comparison.log  4 cases bitwise, both arms asserted on all 4
comparison/rejection.log   failure behaviour matches the host path, 7/7
gpu-gates/gates.log        15/15 GPU-DISC differential gates
negative-controls          8/8 observable detected, all restored byte-exact
cuda-diagnostics           12 runs, 0 errors / 0 hazards, non-vacuous, resident path seen
cpu-backend                1932/1932, 0 nvcc invocations, cfdapp does not link libcudart
performance                12 bitwise equivalence checks; all integrity checks passed
known-debt                 UNCHANGED (gpu-disc matches gpu-pipe: same status, same 1845 iterations)
regression                 1998/1998, 0 failed, 1321.40 s
```

### 2.1 A defect in my own assessment script

The first version printed **"GATE A: PASS" while one of its own checks had printed FAIL**: `verdict`
ran inside a command substitution, so its `fail=1` was set in a subshell and discarded. Two smaller
bugs came with it — `! grep -q "CHANGED"` matched the substring inside `"UNCHANGED"`, and
`grep -c '^PASS'` counted zero on logs that indent. A gate script that cannot fail is not a gate
script. The fixed version computes the verdict in the current shell and ends with a **deliberate
failing check** that proves the aggregator propagates. Gate A's PASS above is from the fixed script.

### 2.2 Library hashes differ across the Gate A evidence, and that is expected

`gpu-gates/gates.log` records `libcfdcuda.a = 78c2c064…`; every later artefact records
`f97216022f…`, with `libcfdcore.a` identical throughout and `ninja` reporting no work to do. The
negative-control suite rebuilds on restore and **nvcc is not bit-reproducible**, so identical
sources relink to different archive bytes. The authoritative identity is the **source** hash set,
and all four files those controls touch hash exactly to the baselines that session recorded.

---

## 3. Part 3 — what was still on the host, and what moved

`audit.md` §4 classifies every host operation in the GPU outer iteration. Eight were class **C**
(unnecessary legacy field transfer) and moved; three were class **B** (replaceable by a device
predicate) and moved; one full-field transfer is class **A** and stays (§4).

```text
removed   selectComponent -> previousU/V/W          the device holds velocity; take it there
removed   assembleMomentum -> host LinearSystem     4 D2H per component, to rebuild a device matrix
removed   momentumSolver->solve(hostSystem, guess)  uploaded the system straight back
removed   combineComponents -> host velocityStar
removed   setMomentumSolution                       H2D of a vector the device computed
removed   the 2D W-predictor zero-fill upload       replaced by a device fill, +0.0 either way
removed   downloadVelocity, per iteration           now one download after the loop
moved     allFinite(velocityStar) -> device         a predicate; reduction order is irrelevant
moved     allFinite(velocityNew)  -> device
kept      downloadMassFlux, per iteration           section 4
```

Momentum reuses **the same `bicgstabCore`** the pressure stage and the host entry point use — one
algorithm, three entry points. No second solver, no new numerics, no new scheme.

### 3.1 The structural prerequisite, checked before the design depended on it

`toHostSystem()` drops every entry whose assembled value is exactly `0.0`. If the momentum matrix
had any, the device CSR the resident solver adopts would not be the host CSR the old solver
received, and "bitwise" would be false before a line was written — the pressure stage hit exactly
this and had to change its assembled structure. So it was measured **first**, on every component,
every convection scheme, 2D and 3D, pinned and open-boundary, against a **mesh-derived pattern
neither side produced** (`simple-loop/momentum-roundtrip.log`):

```text
case                      full pattern   device nnz   host nnz   dropped   column order
cavity 2d 40 Upwind             7,840        7,840      7,840         0    matches
cavity 2d 40 QUICK              7,840        7,840      7,840         0    matches
cavity 2d 40 LinearUpwind       7,840        7,840      7,840         0    matches
cavity 2d 40 Central            7,840        7,840      7,840         0    matches
channel 2d 40 Upwind            7,840        7,840      7,840         0    matches
cavity 3d 8 Upwind (x3 comp)    3,200        3,200      3,200         0    matches
cavity 2d 160 Upwind          127,360      127,360    127,360         0    matches
```

**Zero dropped, everywhere.** Unlike the pressure matrix, the momentum matrix has no pinned
reference row and no exactly-zero coefficient, so it needed no structural change at all.

The same probe measured the round trip's cost: at 160², **12 H2D / 4.09 MB and 112 D2H / 5.45 MB**
per iteration, plus **12.36 ms** of host assemble-and-rebuild against **14.16 ms** of actual solve —
the host rebuild was very nearly as expensive as the solve it fed.

### 3.2 The controlled comparison — bitwise, 12/12

One binary, one build, one mesh, one set of physics. The arms differ in exactly one thing: whether
the outer iteration is resident. The toggle is a **real production decline condition** — a
turbulence model numerically identical to `LaminarModel` but not named `laminar`, so the dispatch
changes and the arithmetic cannot. `SIMPLEResult::residentSimpleLoop` is asserted TRUE on one arm
and FALSE on the other on every case, or the case fails.

```text
case                    outer  momentum/pressure Krylov  reductions  fields    first divergence
cavity 2d 16                8        192 / 559                3012   bitwise   none
cavity 2d 32                6        149 / 836                3940   bitwise   none
channel 2d 16               8        267 / 475                2970   bitwise   none
cavity 3d 6                 6        151 / 224                1496   bitwise   none
cavity 2d 80                6        145 / 1999               8564   bitwise   none
cavity 2d 160               4         90 / 2762              11404   bitwise   none
cavity 2d 16 QUICK          8        199 / 574                3090   bitwise   none
cavity 2d 16 LinearUpwind   8        194 / 578                3084   bitwise   none
channel 2d 16 Central       8        258 / 466                2892   bitwise   none
cavity 3d 8                 5        134 / 254                1554   bitwise   none
cavity 2d 16  LONG         60       1318 / 3733              20210   bitwise   none
channel 2d 24 LONG         60       1682 / 5118              27194   bitwise   none

cases 12   failures 0   values compared 217,755   bitwise 217,755
```

Status, outer-iteration count, all five residual histories, the final pressure/velocity/face-flux
fields, and the **Krylov work itself** (equal linear-iteration totals *and* equal reduction-group
counts) are identical. The two 60-iteration runs matter on their own: residency state persists
across outer iterations, so a fault that accumulates needs iterations to surface.

### 3.3 Dispatch — where the resident loop declines, and that declining is inert

`simple-loop/dispatch.log`. Every decline condition is exercised, and the production configuration
is asserted to engage first so none of them passes vacuously.

```text
PASS  the production configuration ENGAGES the resident loop (non-vacuity)
PASS  a CPU momentum backend DECLINES it -- and the resident PRESSURE solve is unaffected
PASS  a CG momentum solver DECLINES it -- GpuCG has no resident entry point
PASS  a non-laminar turbulence model DECLINES it -- correct() reads the host velocity
PASS  without device discretization it is DECLINED -- there is no device system to solve
PASS  with the linear-solver fallback enabled it is DECLINED
PASS  with the residency mirror enabled it is DECLINED
PASS  two unrelated reasons for declining produce BIT-IDENTICAL solves
```

The last line is the point: declining changes dispatch, not arithmetic.

---

## 4. The one full-field transfer that stays

`downloadMassFlux` remains — one D2H of `nFaces` doubles per iteration. It feeds
`evaluateContinuity` and then `rms()`, a **strictly sequential** floating-point sum whose result is
`continuityResidual`, which enters the production convergence decision. The existing CPU/GPU
equivalence gate requires `cpu.iterations == gpu.iterations` and **bitwise** residual histories,
continuity included. A device tree reduction sums in a different order, so it would change the
convergence decision — forbidden by this milestone's own constraints. A bitwise device reduction
would have to be single-threaded: ~409,600 dependent adds at 640², slower than the 6.6 MB copy it
replaces. So it is **justified and documented, not eliminated**. Changing it needs authorisation to
change the convergence definition.

---

## 5. Transfers — the gate, its failure, and the amendment

### 5.1 The result

`transfers/after.log`, against the frozen `acceptance_gate_A1.md`.

```text
case             H2D bytes/it   non-red D2H calls   non-red D2H bytes   of which face   other   alloc
cavity 2d 16          0.00            8.00                4,392            4,352         40     0.00
channel 2d 16         0.00            8.00                4,392            4,352         40     0.00
cavity 3d 6           0.00            9.00                6,096            6,048         48     0.00
cavity 2d 80          0.00            8.00              103,720          103,680         40     0.00
cavity 2d 160         0.00            8.00              412,200          412,160         40     0.00
cavity 3d 12          0.00            9.00               44,976           44,928         48     0.00
```

Those 40 bytes are the enumeration **exactly**: two momentum system checks at 8 B, one pressure
system check at 8 B, four finiteness reads at 4 B. In 3D it is 48 B — the same enumeration with a
third momentum solve. The derivation is confirmed byte for byte, not merely call for call.

### 5.2 The failure, and why it was mine and not the code's

The gate was frozen before the implementation, run once, and **FAILED on 4 of 6 cases**. Preserved
at `transfers/after-FAILED-first-run.log`, classified in `transfers/FAILURE.md`.

The failing criterion subtracted `reductionGroups * 8` bytes as Krylov traffic. `reduceToHost`
actually downloads **`blockCountFor(nc) * count * 8`** bytes of per-block partial sums — 800 or
1600 bytes per round trip at 160², not 8. Case by case, the entire unexplained excess falls inside
the range that formula predicts; there was no residual field traffic left to explain. In the same
failing run, `H2D bytes per iteration` measured `0.00` everywhere and the non-reduction D2H **call**
count measured exactly 8 and 9 — neither of which the arithmetic error touches.

### 5.3 The amendment

Authorised by the user after the failure was reported and classified. Frozen at
`acceptance_gate_A1.md` with its own dry-run and freeze record. C1, C4 and C5 are unchanged; C2
replaces a slack budget with an **exact enumeration** of the code; C3 changes only the
reduction-byte subtraction. Not one line of production code changed for it.

### 5.4 The dry-run failed too, and that is what dry-runs are for

The first dry-run demanded that *every* criterion fail on the pre-residency baseline, and reported
`VACUOUS` when C4/C5 passed. That was the instrument's error: the pre-residency path already had
zero steady-state allocations, and the brief's allocation target is that the resident loop **keeps**
that. Criteria are now classified — **detection** criteria must fail on the baseline, **preservation**
criteria must hold on it — and the re-dry-run passes on all six cases. The first dry-run is
preserved at `transfers/A1-dryrun-v1-instrument-defect.log`.

The dry-run also produced the authoritative **before** numbers, measured by the same instrument that
measured the after:

```text
case            C1 H2D bytes/it   C2 non-red calls   C3 non-red bytes
cavity 2d 16    FAIL   39,936     FAIL   17 != 8     FAIL     61,728
cavity 3d 6     FAIL   57,024     FAIL   22 != 9     FAIL     89,032
cavity 2d 80    FAIL 1,018,880    FAIL   17 != 8     FAIL  1,578,272
cavity 2d 160   FAIL 4,085,760    FAIL   17 != 8     FAIL  6,330,912
cavity 3d 12    FAIL  476,928     FAIL   22 != 9     FAIL    749,992
```

---

## 6. Part 4 — CPU production vs GPU production

Two axes, each with its own already-existing standard. Nothing was loosened and no new tolerance
was invented.

### 6.1 The converged matrix — `equivalence/production-matrix.log`

GPU arm = device discretization + device BiCGSTAB for momentum **and** pressure + resident loop.
CPU arm = the reference. Identical meshes, BCs, initial state, properties, relaxation, scheme,
convergence criteria, solver tolerances and iteration budget. Bounds adopted verbatim from
`tests/solver/simple/test_simple_gpu_solver.cpp`: velocity 1e-6, pressure 1e-6, boundary flux 1e-8.

```text
case            outer cpu/gpu   p Linf      u Linf     v Linf     w Linf     flux Linf   mass imbalance
cavity 2d 8      1093 / 1093    1.67e-16   2.22e-16   1.54e-16       0        2.44e-17   0 / 0
cavity 2d 16     2611 / 2611    9.60e-10   4.62e-10   4.33e-10       0        3.05e-11   -
channel 2d 12    2938 / 2938    1.33e-15   4.44e-16   6.66e-16       0        5.55e-17   2.86e-10 / 2.86e-10
cavity 3d 4        23 / 23      2.08e-17   5.55e-17   3.51e-17   5.55e-17     3.47e-18   0 / 0

2D / 3D 7 / 3    pinned / open 7 / 3    non-Upwind schemes 3    long-running 7
acceptance cases 6    determinism checks 4    non-converging cases classified 4
cases 15   failures 0
```

Every discrepancy is 9.6e-10 or smaller against a 1e-6 bound — four orders of margin. Outer
iteration counts are **equal on every acceptance case**. Residual histories differ in the last bits,
which is expected and is what the repository's own test says in as many words: two different linear
solvers sum in different orders. The measured history divergence is 1e-17 to 6e-11.

The four cases that do not converge inside 3000 outer iterations are not acceptance results.
Each is compared instead against the **pre-residency GPU arm** and is IDENTICAL to it — residency
changed nothing about them.

### 6.2 The representative ladder — `equivalence/production.log`

At 160²/320²/640² a converged comparison is impractical (the 640² CPU case is ~93 s per 8
iterations), so the comparison is made at a fixed budget. A transient compared against a bound
calibrated on converged solutions means nothing, so at these grids the acceptance criterion is the
one that **is** meaningful: the resident GPU arm must be bitwise identical to the pre-residency GPU
arm after the same number of outer iterations.

```text
grid        budget   resident vs pre-residency        cpu vs gpu pressure (REPORTED ONLY)
160x160       60     0 differing, histories bitwise   Linf 1.33e-3
320x320       20     0 differing, histories bitwise   Linf 2.99e-2
640x640        8     0 differing, histories bitwise   Linf 2.03e-2
```

**This gate passed vacuously once and the result is preserved.** The first version kept this
harness's own Jacobi-preconditioned settings and matched only the budgets; at 640² the pressure
correction then failed on iteration 0 on **both** arms, and the comparison reported "0 differing
values" because it was comparing two untouched initial states. Identical failures are not evidence
of identical behaviour. The settings are now adopted verbatim from the benchmark's own
`benchmarkSettings()`, and a non-vacuity guard fails the case unless both arms run the whole
budget. Preserved at `equivalence/production-ladder-VACUOUS-at-640.log`.

### 6.3 Determinism

Each arm repeated on four cases: status, outer-iteration count, every residual history and every
final field **bitwise identical**, errors = 0.

### 6.4 The known BiCGSTAB reproducer — UNCHANGED

```text
gpu-resident      PressureCorrectionFailure   it=1845
gpu-pre-residency PressureCorrectionFailure   it=1845
cpu               MaxIterations               it=3000
shape UNCHANGED; residency changed it: NO
```

Independently confirmed by the project's own `known_debt_probe.cpp` on the current binaries
(`known-bicgstab/after-resident-loop.log`): `gpu-disc` and `gpu-pipe` agree on status and on 1845
iterations, and the reproducer still triggers.

**This check FAILED first, on my own instrument.** It ran the 40² case with this harness's Jacobi
preconditioning, while the recorded reproducer uses `PreconditionerType::None` and differs in
nothing else — and with Jacobi the breakdown does not occur at all, so it asserted a baseline shape
on a configuration that has never produced it. Preserved at
`equivalence/production-FAILED-known-reproducer.log`. The settings are now the recorded ones, which
makes the check **stricter**: it now actually reproduces the breakdown, and a non-vacuity line fires
if it ever stops doing so.

---

## 7. Part 7 — lifecycle: **PASS**

`lifecycle/probe.log`. Every property is checked against an **authority**: a run in a solver with no
history. "It converged" proves nothing about leakage; "it produced bit for bit what a solver with no
history produced" does.

```text
PASS  the first solve converges; the second reproduces it bit for bit; both equal the authority
PASS  growing the mesh reproduces the large-mesh authority exactly
PASS  shrinking back reproduces the small-mesh authority exactly
PASS  an open-boundary case after a pinned one reproduces its authority; and back again
PASS  a 3D case after 2D ones reproduces its authority (W is not inherited); and back again
PASS  a CPU solve after GPU solves reproduces the CPU authority exactly
PASS  a GPU solve immediately after a CPU solve reproduces the GPU authority exactly
PASS  the CPU arm used neither device discretization nor the resident loop -- backends independent
PASS  the fifth construct-solve-destroy cycle reproduces the authority exactly
PASS  a later solve allocates no more than the first (359 vs 359.0) -- nothing accumulates
PASS  a valid solve after an unsuccessful GPU solve reproduces its authority exactly
PASS  and so does a different case after it
```

The shrink is the pointed one: `DeviceBuffer` never shrinks, so the larger mesh's capacity is still
there and a length bug would read it **without any allocation or transfer to give it away**. Only a
fresh solver that never saw the large mesh can catch that.

**One correction to my own instrument.** §6's case reported `MaxIterations`, not the recorded
`PressureCorrectionFailure`: under this probe's settings the 40² mesh runs its full budget. The
assertion only ever tested `status != Converged`, so the measurement and the result are unaffected,
but the comment called it "the recorded reproducer" and that was wrong. Corrected in the source;
the log predates the comment fix and nothing but the comment changed.

---

## 8. Part 5 — negative controls: 12/12 observable detected

`negative-controls/driver.log`. Run through the GPU-DISC-001P engine, so the discipline (record
sha256 → one mutation → rebuild → narrowest detector → restore → verify sha → rebuild → re-pass) is
the one already qualified rather than a second implementation.

| control | defect | detector | result |
| --- | --- | --- | --- |
| `rl1` | phiOld taken from the U component for every component | comparison | DETECTED |
| `rl2` | the momentum RHS never reaches the workspace | comparison | DETECTED |
| `rl3` | momentum warm start zeroed instead of the resident component | comparison | DETECTED |
| `rl4` | every component's solution lands in the U predictor slot | comparison | DETECTED |
| `rl5` | the guess is never written — the workspace keeps the previous solve's x | comparison | DETECTED |
| `rl6` | the velocity authority transition is missing | **residency_baseline** | DETECTED |
| `rl7` | the per-iteration velocity download is restored | **transfer guard** | DETECTED |
| `rl8` | the resident loop is silently disabled | comparison (non-vacuity) | DETECTED |
| `rl9` | the GPU-backend condition is dropped from the dispatch | **dispatch** | DETECTED |
| `rl10` | the laminar condition is dropped from the dispatch | **dispatch** | DETECTED |
| `rl11` | the 2D W-predictor device fill is removed | comparison | DETECTED |
| `rl12` | the resident momentum assembly silently does nothing | comparison | DETECTED |

All twelve restored byte-exact, with the baseline re-passing. Post-suite source hashes are identical
to the freeze and no `MUTATED` marker survives anywhere in `src/`, `include/`, `cuda/`, `apps/` or
`tests/`.

Three of these are the reason this gate needs detectors beyond a numerical comparison:

* **`rl7` changes no number.** Downloading a field the device already holds is bit-identical; every
  equivalence gate in this project passes it. Only a transfer count can tell, which is what makes
  "the SIMPLE loop is resident" falsifiable rather than asserted.
* **`rl8` also changes no number** — it just reverts to the host path. The comparison catches it
  *only* through its non-vacuity assertion, which is what that assertion is for.
* **`rl9` and `rl10` change no number on the case at hand.** They silently engage the resident path
  in a configuration it was never qualified for. Only an assertion about the **decline** sees that.

### 8.1 One control went UNDETECTED, and that was my detector's fault

`rl6` was first pointed at the persistent-fields `dirty_state` harness, which **never calls
`correctVelocityResident`** — it only exercises the `HostOnly -> Synchronized` transition that
`uploadInitialState` performs. A detector that never invokes the mutated method cannot see the
mutation, and the suite reported UNDETECTED. Preserved at
`negative-controls/driver-12controls-rl6-UNDETECTED.log` and
`negative-controls/rl6-UNDETECTED-by-dirty_state.log`.

`residency_baseline` drives the facade stage by stage and asserts `authority(Velocity) ==
DeviceOwned` immediately after the resident correction — exactly the transition the control removes.
Re-run against it: **DETECTED**, restored byte-exact, clean tree re-passes.

Re-pointing it exposed a second thing. `residency_baseline` is a **Part 1** instrument, and its 2D
assertion encoded the pre-change fact that the response-coefficient stage performs exactly one host
upload. Part 3 eliminated that upload, so the assertion failed on the clean tree — an **obsolete
constant**, not a defect detector and not a contaminated instrument. It is replaced by the
independently derived post-change value, zero, with the pre-change measurement of 1 preserved in
`transfers/before.log`. The elimination is not taken on trust: it is gated by the transfer guard's
C1 and by control `rl11`, which removes the device fill and is DETECTED.

### 8.2 Documented null controls

Two defect classes the brief names that this architecture cannot exhibit, recorded rather than
dropped — "we did not test it" and "it cannot happen" are different statements.

* **Skipping a synchronization before downstream p' consumption.** There is none to skip. Every
  device-to-device carry in the resident path is a `cudaMemcpy` on the **default stream**, and every
  kernel producing or consuming those buffers launches on the same stream. Stream ordering is the
  guarantee; there is no explicit `cudaDeviceSynchronize` between a resident solve and the
  corrections that read its result, so no mutation can remove one. The positive evidence is
  `compute-sanitizer --tool synccheck` over the resident workload, which examines exactly this.
* **Stale p, stale face flux, stale p', a lying authority flag, a forced full-field re-upload.**
  Already covered by **executed** controls in the predecessor gates — `pf2`, `pf4`, `pf6`, `pf7` and
  `rp4`, `rp5`, `rp7`. `rl6` and `rl7` above are the SIMPLE-loop analogues on the boundary this gate
  moved.

---

## 9. Part 6 — CUDA diagnostics: 16/16 clean

`cuda-diagnostics/`. Four tools over four workloads, each exercising many full resident SIMPLE
iterations, the resident momentum and pressure solves, persistent fields, persistent matrices, the
reused Krylov workspace, case resize/lifecycle, 2D and 3D.

```text
tool        cavity2d        case3d          gpusolver       lifecycle
memcheck    0 errors        0 errors        0 errors        0 errors
initcheck   0 errors        0 errors        0 errors        0 errors
synccheck   0 errors        0 errors        0 errors        0 errors
racecheck   0 hazards       0 hazards       0 hazards       0 hazards

kernel launches    100,326      30,617          31,376          68,144
resident workloads       3            2               2               6
vacuity        non-vacuous  non-vacuous     non-vacuous     non-vacuous
```

Non-vacuity is asserted twice: the workload refuses to report success unless every run launched
kernels **and** reported `gpuDiscretization + residentPressureSolve + residentSimpleLoop == 3`, and
the driver additionally requires at least one diagnosed workload to have taken the resident path.

---

## 10. Part 8 — performance

`performance/generations-analysis.txt`, `performance/generations/`. Release, no sanitizers, no
competing load, warm-up then repeated runs, medians reported with spread.

### 10.1 Three generations, plus the one this milestone inherited

```text
   grid     cells     CPU(s)   gen1(s) gen1 x   gen2(s) gen2 x  gen2.5(s) gen2.5 x  gen3(s) gen3 x  gen3/gen1
  20x20       400      0.343     5.780  0.049x    8.123  0.038x     8.382   0.038x    7.904  0.043x     0.88x
  40x40      1600      1.510    15.695  0.088x   14.282  0.102x    13.968   0.102x   14.269  0.106x     1.20x
  80x80      6400      6.592    22.909  0.277x   17.511  0.373x    17.510   0.372x   16.169  0.408x     1.47x
 160x160    25600     11.274    15.503  0.703x   10.153  1.114x     9.492   1.203x    8.732  1.291x     1.84x
 320x320   102400     28.655    15.213  1.773x    7.381  3.819x     7.479   3.863x    5.262  5.445x     3.07x
 640x640   409600     92.869    24.877  3.593x    8.799 10.319x     8.189  11.398x    5.349 17.362x     4.83x

crossover, original GPU-PIPE baseline : 320x320
crossover, final resident path        : 160x160
```

gen1 = the original GPU-PIPE baseline (GPU solver, CPU discretization) — its 0.702× / 1.773× /
3.599× are reproduced here as 0.703× / 1.773× / 3.593×. gen2 = GPU-DISC integrated. gen2.5 = the
resident pressure solve. gen3 = this milestone.

**The timed arm is asserted, not assumed:** every gen3 `gpu-disc` row records `resident_loop=1`, and
every `disc-only` row records `resident_loop=0` — the attribution arm correctly declines.

### 10.2 Stage breakdown, and the new bottleneck

```text
                           160x160              320x320              640x640
momentum assembly           0.18%                0.25%                0.33%
momentum solves            11.43%                7.40%                2.99%
response coefficients       0.01%                0.01%                0.01%
face-flux prediction        0.00%                0.01%                0.01%
pressure assembly           0.04%                0.07%                0.16%
pressure solve             85.04%               82.77%               69.04%
velocity correction         0.07%                0.05%                0.04%
face-flux correction        0.77%                0.89%                0.39%
residual / bookkeeping      0.63%                1.08%                1.56%
setup (one-time)            1.46%                6.79%               24.31%
  of which upload           0.88%                2.24%                4.02%
  of which download        46.79%               46.89%               34.25%
synchronizations / iter     788.2               1525.9               2891.2
reduction round trips/iter  788.2               1525.9               2891.2
```

**The dominant stage is the pressure solve**, and inside it the dominant cost is `download` —
34–47% of the whole solve — which is the BiCGSTAB reduction round trips, one per synchronization.
Every remaining synchronization in the loop is a reduction's. That is the algorithm's own pattern,
explicitly out of scope for this milestone, and it is now the single clear target for any future
work.

Momentum assembly fell to **0.18–0.33%** of the solve; at 160² the `disc` stage time went from
1.125 s to 0.092 s, because the host matrix rebuild is gone.

### 10.3 Transfer summary

```text
   grid     iters   H2D calls   H2D bytes    D2H calls    D2H bytes   reductions  H2D/iter  nonred D2H/iter
  20x20       200         297   1,672,736       43,620    2,531,456       42,016      1.49            8.02
  40x40       200         297   6,526,176       77,690   12,728,600       76,086      1.49            8.02
  80x80       200         297  25,775,456       92,812   52,748,400       91,208      1.49            8.02
 160x160       60         297 102,443,616       47,778   91,607,200       47,294      4.95            8.07
 320x320       20         297 408,458,336       30,682  206,826,400       30,518     14.85            8.20
 640x640        8         297 1,631,201,376     23,198  583,391,040       23,130     37.12            8.50
```

**H2D calls are 297 at every grid and every budget** — 8 outer iterations or 200, the number does
not move. All of it is one-time initialization; the steady-state H2D is zero, exactly as the
transfer guard's delta measurement found. `allocations` = 358 total, `reallocations` = **0**.

---

## 10.4 Part 9 — the final regression

All ten items the authorization enumerates, plus the two legs of CLAUDE.md §8's "full regression"
that it does not (`regression/`).

```text
 1  fresh production build + freshness proof   test binaries newer than the newest source;
                                               ninja reports "no work to do" AFTER ctest
 2  complete CTest, Release + CUDA             1998 / 1998 passed, 0 failed, 1319.68 s
 3  the 15 GPU-DISC differential gates         15 / 15 green on the final binaries
 4  determinism                                all four backend combinations bitwise, 400/400 iters
 5  production GPU CLI smoke                   exit 0, 4 output files, 3 workloads non-vacuous
 6  CPU-only build + full suite                1932 / 1932, 0 nvcc invocations, no libcudart
 7  sanitizer freshness                        16 / 16 clean and non-vacuous on the FINAL build
 8  no negative-control mutation remains       source hashes identical to the freeze; no markers
 9  working-tree audit                         66 tracked modified, 50 untracked
10  clang-format                               105 violations, all pre-existing -> cleared,
                                               0 in 590 files; see §11 for the proof

    Debug + GUI (CLAUDE.md §8)                 1984 / 1984 passed, 0 failed, 1295.22 s
    sanitizers at CI settings (CLAUDE.md §8)   1932 / 1932 passed, 0 failed, 4118.51 s,
                                               0 AddressSanitizer / UBSan / LeakSanitizer reports
```

### 10.5 The strongest single piece of regression evidence

The generated-output classifier (`regression/13-generated-output-classification.log`) compares every
generated artefact in the repository against `git HEAD`, distinguishing timing keys from physics:

```text
checked 3014 files;  IDENTICAL 1841,  RUNTIME-ONLY 50,  VALUES 0,  NEW 1123
```

**VALUES 0.** Not one generated output in the entire validation corpus — MMS reports, grid
convergence studies, natural convection, turbulence channels, production cases — changed in a
non-timing way. The 50 RUNTIME-ONLY are the ctest runs' timing churn and must be restored before
any commit; the NEW files are the uncommitted GPU-DISC/GPU-PIPE evidence trees.

---

## 11. Incidents and pre-existing state

**A duplicate run destroyed a log.** This session launched a second copy of the
resident-pressure-solve finalise script while the previous session's copy was still running, without
first checking for in-flight work. Both write the same files. The duplicate was stopped within two
minutes, but had already truncated `regression/known-debt.log` to zero bytes. The probe was re-run
against the same binaries and the log records that it is a re-run (`tools/part2_repair.sh`).
Nothing else was damaged.

**A hardcoded benchmark output path overwrites each phase's raw data.**
`performance_benchmark.cpp` writes to `results/gpu-disc-001/performance/raw/runs-<mode>.csv`
unconditionally, so every phase that reuses it destroys the previous phase's CSV. GPU-DISC-001Q's
was already gone before this milestone started; what survives for that generation is its derived
analysis text, in two byte-identical copies that both predate the overwrite. Each generation's
record is now copied into `performance/generations/` immediately after its run. The hardcoded path
is **not** changed — out of scope, and recorded for a later phase.

**The tree was not clang-format clean, and was not before this work — now it is.** 105 violations
across 12 files, all in GPU-DISC-001 / GPU-PIPE-001 code this milestone did not write. **My own
edits added zero**: the six violations my patch introduced were fixed by hand rather than by
reformatting other sessions' lines, and every remaining hunk was verified to sit in pre-existing
code. Clearing them was a **separate authorised step**, and it is proven semantically null:

* **`libcfdcore.a` is byte-identical** after the rebuild. g++ is deterministic, and
  `SIMPLE.cpp` — 23 of the 105 violations and the only changed `.cpp` — compiles into it.
* 11 of the 12 files are **whitespace-identical** with whitespace stripped. The twelfth,
  `SIMPLE.cpp`, differs by **one reordered `#include`** (`Logger.hpp` ahead of `Gradient.hpp`, for
  alphabetical ordering); both were already included.
* Re-verified on the reformatted bytes: clang-format **0 violations in 590 files**, CTest
  **1998/1998**, **15/15** GPU-DISC gates, and the resident-loop comparison still **bitwise across
  217,755 values**.

The first version of that proof was wrong twice — its file list came from a grep over
clang-format's own output (so it contained entries like `#include` that are not files), and it
compared with `diff -w`, which ignores whitespace within a line but still reports a difference when
clang-format *joins* lines, which is most of what it does. Both made every file look changed,
contradicting the byte-identical archive. Recorded in `regression/14-format-remediation.log`.

**`enableGpuDiscretization` still has no case-file key**, so the resident path is reachable only
through the C++ `SIMPLESettings` API. GPU-DISC-001R found this, recorded it, and called it "the
natural first item for the work that follows". It is unchanged, and unchanged by this milestone.
"Production path" here means `SIMPLE::solve` — the one solver entry point the CLI, the GUI and the
library all use. The resident loop is in it, not beside it; there is no benchmark-only solver.

**`results/validation/**` carries timing-only churn** from the full ctest runs. It must be
classified and restored before any commit.

---

## 12. Files changed

```text
edit  include/cfd/pressure_velocity/SIMPLEResult.hpp    residentSimpleLoop
edit  include/cfd/gpu/GpuResidentSolve.hpp              copyToWorkspaceGuess
edit  cuda/kernels/GpuResidentSolveKernel.cu            its implementation
edit  include/cfd/gpu/DevicePersistentFields.hpp        fillFieldDevice
edit  cuda/kernels/DevicePersistentFieldsKernel.cu      its kernel and implementation
edit  include/cfd/gpu/GpuSimpleDiscretization.hpp       the resident momentum API
edit  cuda/kernels/GpuSimpleDiscretizationCuda.cpp      resident momentum + the device zero-fill
edit  src/gpu/GpuSimpleDiscretization.cpp               CPU-only parity (compiled only with CUDA OFF)
edit  src/pressure_velocity/SIMPLE.cpp                  the production dispatch
edit  results/gpu-disc-001/performance/tools/performance_benchmark.cpp   four added CSV columns
edit  results/gpu-disc-001/negative-controls/tools/control_engine.py     four harness registrations
```

`GpuCG` was deliberately not touched. The CPU branch of `SIMPLE::solve` is untouched. No CMake
change was needed: no new translation unit was added.

---

## 13. What is NOT done

* **The Krylov reduction round trips are untouched** and out of scope by the audit — and they are
  now the dominant cost (§10.2).
* **The known GPU BiCGSTAB restart asymmetry is not fixed**, and reproduces at exactly 1845
  iterations.
* **The face-flux download stays** (§4), because removing it would change the convergence decision.
* **`enableGpuDiscretization` still has no case-file key** (§11).
* **The pre-existing clang-format violations are not fixed** (§11).
* No numerics, tolerance, convergence criterion or iteration budget moved.
* Nothing committed or pushed.
