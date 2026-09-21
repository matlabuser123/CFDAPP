# Amendment A1 — the resident SIMPLE loop's transfer gate

**Status: FROZEN. Dry-run passed. Not yet executed against the resident path at freeze time.**

The original transfer gate was frozen before the implementation, run once, and **FAILED on 4 of 6
cases**. That failure is preserved verbatim and is permanently on record:

```text
transfers/after-FAILED-first-run.log     the raw result
transfers/FAILURE.md                     the classification and this derivation
```

This amendment replaces one arithmetic term in one criterion. It was authorised by the user after
the failure was reported and classified. **The original failure is not rewritten, not re-run, and
not presented as a pass.**

---

## 1. What was wrong

```cpp
nonReductionBytes = d2hBytes - reductionGroups * 8.0;     // the failing criterion
```

This assumes one Krylov reduction round trip returns 8 bytes. It does not. `reduceToHost`
(`cuda/kernels/DeviceVectorOpsKernel.cu`) downloads the **per-block partial sums** and finishes the
sum on the host:

```cpp
const int blocks = blockCountFor(n);              // (n + 255) / 256
partialSumsCache.resize(blocks * count);
partialSumsCache.downloadTo(hostPartials.data(), blocks * count);
for (int q = 0; q < count; ++q)
  for (int i = 0; i < blocks; ++i) sum += slice[i];
```

One round trip therefore costs `blockCountFor(n) * count * 8` bytes — 800 or 1600 bytes at 160²,
not 8. The criterion under-subtracted Krylov traffic by a factor of 100 to 200 and attributed it to
field transfers.

**The implementation was never in question.** In the same failing run, `H2D bytes per iteration`
measured `0.00` on every case and the non-reduction D2H **call** count measured exactly 8 (2D) and
9 (3D) — neither of which the arithmetic error touches.

## 2. The frozen criteria

Per case, per steady-state SIMPLE iteration, fitted as a delta between outer-iteration budgets
4 and 16 with budget 8 as a linearity control. Budgets are chosen so tolerances are unreachable and
every budget is exhausted.

| id | kind | criterion | derivation |
| --- | --- | --- | --- |
| **C1** | detection | `H2D bytes / iteration  <  nc * 8` | strictly less than ONE cell field, so nothing uploaded per iteration can be a field |
| **C2** | detection | `non-reduction D2H calls / iteration  ==  8` (2D), `== 9` (3D) | exact enumeration of the code, §3 |
| **C3** | detection | `non-reduction D2H bytes / iteration  <  (nFaces + nc) * 8` | the one justified face field (audit.md §5) plus strictly less than one more field |
| **C4** | preservation | `allocations / iteration  ==  0` | the brief's allocation target |
| **C5** | preservation | `reallocations / iteration  ==  0` | the brief's allocation target |

with, for C3,

```text
reductionBytes    = blockCountFor(nc) * 8 * reductionQuantities
blockCountFor(n)  = (n + 255) / 256                      DeviceVectorOpsKernel.cu
nonReductionBytes = d2hBytes - reductionBytes
```

`reductionQuantities` (GPUExecutionStats) counts the scalars produced across all reduction groups,
so `Σ_groups (blocks * count_g)` is `blocks * Σ_g count_g`. `blocks` is constant within a case
because every Krylov vector in a solve has length `nc`.

Nothing above is fitted to an observed value. C2's `8` and `9` come from enumeration, not from the
measurement that happens to agree with them.

## 3. C2's enumeration

A steady-state resident iteration issues exactly these non-reduction device-to-host calls:

```text
1      mass-flux download            downloadMassFlux, for evaluateContinuity   nFaces * 8 B
2 | 3  momentum system checks        checkSystemAndBuildJacobi, one per solve         8 B each
1      pressure system check         checkSystemAndBuildJacobi                        8 B
1      p' finiteness                 residentPressureCorrectionAllFinite              4 B
1      predictor finiteness          residentMomentumPredictorAllFinite               4 B
1      velocity finiteness           residentVelocityAllFinite                        4 B
1      pressure finiteness           residentPressureAllFinite                        4 B
-----------------------------------------------------------------------------------------
8 in 2D, 9 in 3D, of which exactly ONE is a field
```

Equality, not a budget with slack. A slack budget is what let the first version fail to say
anything useful.

## 4. Dry-run and non-vacuity — `transfers/A1-dryrun.log`

Every criterion was run against the **pre-residency arm** of the same binary: the resident loop
declined through a real production condition (a turbulence model numerically identical to
`LaminarModel` but not named `laminar`), so the arithmetic is unchanged and only the dispatch
differs.

A criterion is one of two kinds and the dry-run demands opposite things of them:

* **detection** — gates the new behaviour, so it must **FAIL** on the baseline;
* **preservation** — gates an existing guarantee the change must not break, so it must **PASS** on
  the baseline. "A sanity criterion that the baseline fails is a broken criterion."

### 4.1 The first dry-run failed, and that is why the dry-run exists

The first version of the dry-run demanded that *every* criterion fail on the baseline and reported
`VACUOUS` because C4 and C5 passed. That was the instrument's error, not the criteria's: the
pre-residency path already had zero steady-state allocations, and the brief's allocation target is
that the resident loop **keeps** that. The instrument was corrected to classify the criteria, and
re-dry-run. The first, failing dry-run is preserved at
`transfers/A1-dryrun-v1-instrument-defect.log`. Fixing an instrument exposed by a dry-run, before
freezing, is the sanctioned path (CLAUDE.md §5.2).

### 4.2 The dry-run result, all six cases

```text
case            C1 H2D bytes/it   C2 non-red calls   C3 non-red bytes   C4 alloc  C5 realloc
cavity 2d 16    FAIL   39,936     FAIL   17 != 8     FAIL     61,728    pass 0    pass 0
channel 2d 16   FAIL   39,936     FAIL   17 != 8     FAIL     61,728    pass 0    pass 0
cavity 3d 6     FAIL   57,024     FAIL   22 != 9     FAIL     89,032    pass 0    pass 0
cavity 2d 80    FAIL 1,018,880    FAIL   17 != 8     FAIL  1,578,272    pass 0    pass 0
cavity 2d 160   FAIL 4,085,760    FAIL   17 != 8     FAIL  6,330,912    pass 0    pass 0
cavity 3d 12    FAIL  476,928     FAIL   22 != 9     FAIL    749,992    pass 0    pass 0

LOOP TRANSFER GUARD DRY-RUN: PASS (every criterion is falsifiable)
```

Every detection criterion fails on the baseline; every preservation criterion holds on it. These
are also the authoritative **before** numbers, measured by the same instrument that will measure
the after.

### 4.3 C4/C5 falsifiability, on record

A preservation criterion that never failed anywhere would be worth little. This instrument family
has demonstrably detected a per-iteration allocation: an early version of the resident pressure
solve allocated a counter per solve and allocations were measured rising **375 → 406 over 8 outer
iterations** (`gpu-resident-pressure-solve/summary.md` §12). Additionally, negative control `rl7`
in Part 5 changes no number and is detectable only by this harness.

## 5. Freeze

```text
harness      results/gpu-pipe-001/final-residency/tools/loop_transfer_guard.cpp
gate file    results/gpu-pipe-001/final-residency/acceptance_gate_A1.md   (this file)
dry-run      results/gpu-pipe-001/final-residency/transfers/A1-dryrun.log
superseded   results/gpu-pipe-001/final-residency/transfers/after-FAILED-first-run.log
             results/gpu-pipe-001/final-residency/transfers/A1-dryrun-v1-instrument-defect.log
```

sha256 of the harness, the libraries and the production sources at freeze time are recorded in
`transfers/A1-freeze.log`, written immediately before the authoritative run.

## 6. What this amendment does NOT do

* It does not change any threshold's *intent*: C1, C4 and C5 are unchanged, C2 replaces a slack
  budget with an exact enumeration, and C3 changes only the reduction-byte subtraction.
* It does not touch production code. Not one line of `src/`, `include/` or `cuda/` changed for it.
* It does not erase, re-run or reinterpret the original failure.
