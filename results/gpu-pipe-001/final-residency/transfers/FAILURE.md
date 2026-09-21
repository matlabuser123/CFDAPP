# Transfer gate — FAILED, and why

**Status of the run this file records: FAILED. Preserved verbatim, never re-run, never rewritten.**
**An authorised amendment followed — see section 6. Sections 1-5 are the record as written at the
time of the failure, before any authorisation existed; they are deliberately left unedited.**

The resident-SIMPLE-loop transfer guard was written and its criteria fixed before the
implementation was applied. It was run once, against the implementation, and it **failed on 4 of
its 6 cases**. The raw result is preserved verbatim at

```text
results/gpu-pipe-001/final-residency/transfers/after-FAILED-first-run.log
```

This file records the failure, its classification, and the derivation of a replacement criterion.
At the time sections 1-5 were written the replacement had **not** been run: an amendment found
after a gate run is the user's decision, and it had not yet been asked. Section 6 records what
happened next.

---

## 1. What failed

```text
case            H2D bytes/it   non-reduction D2H   measured non-red. bytes   bound        verdict
cavity 2d 16       0.0              8.00 calls            6,592.0            < 6,400.0    FAIL
channel 2d 16      0.0              8.00 calls            6,698.7            < 6,400.0    FAIL
cavity 3d 6        0.0              9.00 calls            7,387.3            < 7,776.0    pass
cavity 2d 80       0.0              8.00 calls          569,638.7            < 154,880.0  FAIL
cavity 2d 160      0.0              8.00 calls        4,091,770.7            < 616,960.0  FAIL
cavity 3d 12       0.0              9.00 calls           82,479.3            < 58,752.0   FAIL

steady-state allocations   0.00 on every case
steady-state reallocations 0.00 on every case
resident loop engaged      yes on every case
```

The 3D-6 case passing while the others fail is itself the clue: it has the largest face field
relative to its cell count, so its bound was the loosest.

## 2. Classification: the CRITERION is wrong, not the implementation

The failing criterion was

```cpp
nonReductionBytes = d2hBytes - (reductionGroups * 8.0);
bound: nonReductionBytes < (nFaces + nc) * 8
```

The subtraction assumes **a Krylov reduction round trip brings back 8 bytes**. It does not.
`reduceToHost` in `cuda/kernels/DeviceVectorOpsKernel.cu` downloads the **per-block partial sums**
and finishes the sum on the host:

```cpp
const int blocks = blockCountFor(n);          // (n + 255) / 256
partialSumsCache.resize(blocks * count);
...
partialSumsCache.downloadTo(hostPartials.data(), blocks * count);
for (int q = 0; q < count; ++q)               // host-side final sum
  for (int i = 0; i < blocks; ++i) sum += slice[i];
```

So one reduction round trip costs **`blockCountFor(nc) * count * 8` bytes**, where `count` is 1, or
2 for a fused `dot2`. At 160² that is `100 * 8 = 800` or `1600` bytes — **100 to 200 times** what
the criterion subtracted.

Checked against the measurements, the entire unexplained excess is exactly that:

```text
case            nc      blocks   face field   reduction bytes         measured   measured
                                              count=1 .. count=2      total D2H  minus face
cavity 2d 16      256       1         4,352     2,964 ..    5,928        9,556      5,204
channel 2d 16     256       1         4,352     3,108 ..    6,216        9,807      5,455
cavity 3d 6       216       1         6,048     1,763 ..    3,525        9,150      3,102
cavity 2d 80    6,400      25       103,680   272,866 ..  545,732      580,553    476,873
cavity 2d 160  25,600     100       412,160 2,116,264 ..4,232,528    4,112,933  3,700,773
cavity 3d 12    1,728       7        44,928    23,474 ..   46,947       85,833     40,905
```

Every "measured minus face" value falls inside its own `[count=1, count=2]` reduction range. There
is no residual byte traffic left over to explain. **Nothing except the face flux and the reductions
is coming back.**

## 3. What the run DID establish, on criteria that were correctly derived

Two criteria in the same run are unaffected by the arithmetic error, and both passed everywhere:

* **`H2D bytes per iteration = 0.00` on every case, 2D and 3D, at every grid.** The bound was
  "strictly less than one cell field"; the measurement is exactly zero. The upload side of the
  steady-state iteration is *completely* eliminated — not reduced, eliminated.
* **`allocations = 0.00` and `reallocations = 0.00` per iteration on every case.**

And the D2H **call** count — which the error does not touch, because the error is in bytes — is
exactly **8 per iteration in 2D and 9 in 3D**.

## 4. The replacement, derived from first principles

Two criteria, neither fitted to an observed number.

### 4.1 Non-reduction D2H calls, by enumeration of the code

A steady-state resident iteration issues exactly these device-to-host calls that are not
reductions, and no others:

| call | source | size |
| --- | --- | --- |
| mass-flux download | `downloadMassFlux`, for `evaluateContinuity` (audit.md §5) | `nFaces * 8` |
| momentum system check ×(2 or 3) | `checkSystemAndBuildJacobi`, one per resident momentum solve | 8 B each |
| pressure system check | `checkSystemAndBuildJacobi` for the pressure solve | 8 B |
| p' finiteness | `residentPressureCorrectionAllFinite` | 4 B |
| predictor finiteness | `residentMomentumPredictorAllFinite` | 4 B |
| velocity finiteness | `residentVelocityAllFinite` | 4 B |
| pressure finiteness | `residentPressureAllFinite` | 4 B |

That enumerates to **8 in 2D and 9 in 3D**, of which **exactly one is a field**. The criterion is
equality with that enumeration, not a budget with slack: a slack budget is what let the first
version fail to say anything useful.

### 4.2 Non-reduction D2H bytes, with the reduction term computed correctly

Reduction bytes are exactly derivable from counters that already exist:

```text
reductionBytes = blockCountFor(nc) * 8 * reductionQuantities
```

`reductionQuantities` (GPUExecutionStats) counts the scalars produced across all groups, so summing
`blocks * count_g` over groups is `blocks * Σ count_g`. `blocks` is constant within a case because
every Krylov vector in a solve has length `nc`. Then:

```text
nonReductionBytes = d2hBytes - reductionBytes
bound:              nonReductionBytes < (nFaces + nc) * 8
```

— the one justified face field, plus strictly less than one more field, exactly as before. Only the
subtraction changes.

### 4.3 Non-vacuity

Both replacement criteria must be able to fail. Negative control `rl7`
(`rl7_velocity_redownloaded_every_iteration`) restores the per-iteration full-field velocity
download. It changes **no number**, so every equivalence gate passes it; it adds 3 D2H calls and
`3 * nc * 8` bytes, which breaks 4.1 and 4.2 alike. That control is written and ready
(`tools/negative_controls.py`) but has not been run.

## 5. What is NOT claimed  *(as of the failure, before authorisation)*

* The transfer gate has **not** passed. Gate B is **not** met.
* The replacement criteria have **not** been executed; no amended number exists.
* Parts 4 through 9 have not been run, and no later item is marked complete.

---

## 6. Outcome

The amendment was **authorised by the user** after this failure was reported and classified. It is
frozen at [`../acceptance_gate_A1.md`](../acceptance_gate_A1.md), with its own dry-run
(`A1-dryrun.log`) and freeze record (`A1-freeze.log`).

The dry-run itself failed once, on an instrument defect of its own — it demanded that *every*
criterion fail on the baseline, when two of the five are preservation criteria that must **hold**
on it. That first dry-run is preserved at `A1-dryrun-v1-instrument-defect.log`. Fixing an
instrument the dry-run exposes, before freezing, is the sanctioned path.

The amended gate then **passed on all six cases** (`after.log`). At 160², steady state, resident:

```text
H2D                        0.00 calls          0 bytes / iteration
non-reduction D2H          8.00 calls    412,200 bytes / iteration
  of which the face flux                 412,160 bytes   (justified, section 4.4 of summary.md)
  everything else                             40 bytes
allocations / reallocations   0.00 / 0.00
```

Those 40 bytes are the enumeration of §4.1 **exactly**: two momentum system checks at 8 B, one
pressure system check at 8 B, and four finiteness reads at 4 B. In 3D it is 48 B, which is the same
enumeration with a third momentum solve. The derivation is therefore confirmed byte for byte, not
merely call for call.

**The original failure above stands on record. It is not rewritten and was not re-run.**
