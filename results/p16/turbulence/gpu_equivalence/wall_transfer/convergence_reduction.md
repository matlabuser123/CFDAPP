# The turbulence convergence residual, moved onto the device

`correctKEpsilonDevice` downloaded **four complete fields** per correction —
`kOld`, `epsilonOld`, `kNew`, `epsilonNew` — to run a host loop that produced
one scalar. `4 * sizeof(double) * N` bytes for 8 bytes of answer, and at every
grid measured it cost **more than publishing k, epsilon and mu_t combined**.

It was found by the byte ledger, not by reading the code: the ledger refused to
close, with a residue of exactly `32*N + 52` bytes that no number of 4- or
8-byte control transfers could explain.

---

## 1. What the four downloads were for

Exactly one scalar. Nothing else reads them.

```cpp
// src/turbulence/KEpsilonModel.cpp:297-302 -- the reference
Real maxChange = 0.0;
for (Index i = 0; i < n; ++i) {
  maxChange = std::max(maxChange, std::abs(newK[i] - k_[i]));
  maxChange = std::max(maxChange, std::abs(newEpsilon[i] - epsilon_[i]));
}
lastConvergenceResidual_ = maxChange;
```

It is a **single interleaved accumulator over both fields**, not two separate
maxima — worth stating because the natural reading of "max absolute k change and
max absolute epsilon change" suggests two metrics, and the code has one.
`convergenceResidual` surfaces through `TurbulenceModel::convergenceResidual()`
and is consumed by `SIMPLE.cpp:1192` as an optional outer-loop criterion.

## 2. Two semantics that were easy to lose

**`std::max` ignores NaN.** It is `(a < b) ? b : a`, so when the candidate is a
NaN, `a < NaN` is false and the accumulator is kept. Seeded at `0.0`, it can
never become NaN. A NaN difference is therefore *silently ignored* by
production. That is not obviously the ideal behaviour, but it is the behaviour,
and a reduction that propagated NaN instead would stop the turbulence solve ever
being declared converged.

`fmax` would agree for a single NaN argument but disagree when combining two
accumulators, which is exactly what a tree reduction does. So the comparison is
written out, and the induction is stated in the kernel: every accumulator starts
at `0.0` and is only replaced by a value comparing strictly greater — which NaN
never does — so no accumulator is ever NaN and the tree never sees one.

**Bitwise is achievable here, unlike a dot product.** A sum reduced in a
different block order gives a different answer because addition does not
reassociate. A maximum *selects* one of its inputs rather than computing a new
value, so there is nothing to round. Each term is bitwise equal to the host's
too: subtraction is exactly rounded, `fabs` only clears a sign bit, and neither
admits an FMA contraction. The gate is therefore `EXPECT_EQ`, not a tolerance —
a tolerance here would be hiding a real difference in the operation rather than
allowing for arithmetic.

## 3. What was implemented

`maxAbsDifference2` in `DeviceVectorOps` — **not** a turbulence-specific
reduction. It mirrors `dot2`: two independent quantities over the same length,
two disjoint shared-memory halves, one halving tree each, fused into **one**
host round trip.

The existing `reduceToHost` was generalised rather than duplicated: it now takes
the host-side combine as a parameter, and the summing path forwards to it with
the identical loop it always had, over the same partials in the same order. The
Krylov callers are bitwise unchanged; the loop body moved into a lambda and
nothing else.

`correctKEpsilonDevice` then reads `state.k` and `state.epsilon`, which are
still the OLD fields at that point — the "before either member is overwritten"
ordering `KEpsilonModel.cpp` documents for this metric — and combines the two
maxima on the host. `max(max_i|Δk|, max_i|Δε|)` is the same number as the
interleaved loop: max is associative and commutative, and neither partial can
be NaN.

## 4. Transfer, measured

```text
                     D2H bytes / calls
grid                 before            after           delta
128 cells /  32 w     9 596 / 135       5 476 / 132    -4 120 / -3
512 cells / 128 w    36 756 / 200      20 404 / 197   -16 352 / -3
2048 cells / 128 w  145 268 / 270      79 860 / 267   -65 408 / -3
```

Four calls of `8N` bytes are replaced by one reduction of `2 * blocks * 8`
bytes, which is why the call count falls by three rather than four — the new
reduction lands inside `reductionCalls`, where every other device reduction
already is. At 2048 cells: `65 536` bytes out, `128` bytes in.

**Total D2H per correction is now 46–47 % of what it was at the start of this
phase**, across both the wall round trip and this.

```text
kOld full D2H          0
kNew full D2H          0
epsilonOld full D2H    0
epsilonNew full D2H    0
convergence scalars    one fused reduction, 2 * blocks * 8 bytes
```

The ledger closes exactly on all seven grids with `convergence residual = 0`,
and `control` is still 52 bytes in 9 calls.

## 5. Equivalence

**Nothing moved.** The full GPU suite was captured before and after and diffed
with wall-clock timings and transfer counters stripped: the only differences are
the tests that were added or removed between the two runs. No numeric value
changed.

The Step 3 ladder is **bit-identical** before and after:

```text
nx    cells  walls  k relL2     eps relL2   mu_t relL2  gatherB   determinism
16    128    32     4.656e-16   1.231e-14   6.483e-15   768       bitwise
24    192    48     5.712e-16   2.938e-15   2.121e-15   1152      bitwise
28    224    56     6.611e-16   2.785e-14   1.401e-14   1344      bitwise
32    256    64     1.693e-15   1.638e-14   9.201e-15   1536      bitwise
48    384    96     7.829e-16   3.161e-14   1.552e-14   2304      bitwise
64    512    128    6.973e-16   6.097e-14   2.893e-14   3072      bitwise
```

k, epsilon, mu_t, transport iteration counts, wall outputs and determinism are
all unchanged, and the frozen `2.1e-12` bound was not touched.

## 6. Gates

`tests/unit/gpu/test_device_max_abs_difference.cpp`:

| test | asserts |
|---|---|
| `MatchesTheHostLoopBitwiseAcrossBlockPartitions` | `EXPECT_EQ` against the transcribed host loop at n = 1, 255, 256, 257, 1000, 4096 — degenerate, block-aligned and block-straddling |
| `EmptyFieldsReduceToZeroNotToAnError` | an empty partition reports 0, not NaN and not a throw |
| `NaNDifferencesAreIgnoredExactlyAsStdMaxIgnoresThem` | NaNs mid-block and at a block edge; result is not NaN and equals the host's |
| `AnInfiniteDifferenceIsCarriedThroughLikeTheHostLoop` | infinity *does* win, so a diverging solve stays visible |
| `PerturbingTheMaximumCellMovesTheMetricInEachField` | a spike in k moves the k maximum and not epsilon's, and vice versa |
| `PerturbingBelowTheMaximumLeavesTheMetricUnchanged` | a real but sub-maximal change moves nothing — this fails for a reduction that sums, averages or counts while the test above still passes |
| `RepeatedReductionIsBitwiseReproducible` | four repeats, bitwise |
| `DownloadsOnlyTheReductionPartialsNotTheFields` | exactly one D2H call of `2 * blocks * 8` bytes |

Suites: GPU 162/162, turbulence 246/246 (CUDA **and** CUDA-OFF), SIMPLE 123/123,
CPU-only build clean.
