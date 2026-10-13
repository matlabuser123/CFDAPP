# The wall-transfer gate: MIXED — an impossible criterion *and* a real defect

`ResidentWallExchange.WallTrafficScalesWithWallFacesNotCellCount` failed with
38 804 bytes against a 12 288-byte bound. The audit found **both** things were
wrong, independently of each other, and neither excuses the other.

```text
A  INVALID ACCEPTANCE CRITERION   the gate could not pass at zero wall cost
B  REAL IMPLEMENTATION DEFECT     an avoidable device -> host -> device round trip
```

Files here: `ledger_before.log`, `ledger_after_raw.log`, `gpu_suite_before.log`,
`gpu_suite_after.log`, `original_failing_gate.cpp.txt` (the removed test,
preserved verbatim).

---

## 1. The byte ledger, closing exactly

Every byte leaving the device during one `correctResidentWithWallTreatment`,
attributed. Established by tracing every individual `cudaMemcpy` of a single
correction with a temporary instrumentation of `DeviceBuffer::downloadTo`
(since reverted — `include/cfd/gpu/DeviceBuffer.hpp` is unmodified).

**512 cells, 128 wall faces, before the fix:**

| category | site | calls | bytes | scales with |
|---|---|---:|---:|---|
| wall gather | `GpuKEpsilonCorrectionCuda.cpp:324-326` | 3 | 3 072 | `O(N_wall)` |
| wall production round trip | `DeviceKEpsilonCuda.cpp:93-95` | 2 | 2 048 | `O(N_wall)` **avoidable** |
| convergence residual | `DeviceKEpsilonCuda.cpp:147-148, :195-196` | 4 | 16 384 | `O(N)` |
| publication | `GpuKEpsilonCorrectionCuda.cpp:278-280` | 3 | 12 288 | `O(N)` |
| Krylov partial sums | `DeviceVectorOpsKernel.cu:249` | 181 | 4 960 | `O(iters·N/256)` |
| validity flags, 5 × 4 B | scalar-transport ×2, fix-imposed-rows ×1, effective-diffusivity ×2 | 5 | 20 | `O(1)` |
| resident-solve counters, 4 × 8 B | `GpuResidentSolveKernel.cu:134` (raw memcpy) | 4 | 32 | `O(1)` |
| **TOTAL** | | **202** | **38 804** | |

`3+2+4+3+181+5+4 = 202` and
`3072+2048+16384+12288+4960+20+32 = 38804` — **no unexplained bytes.**

The `control` term (the last two rows, 52 bytes in 9 calls) is **identical on
every grid measured**, from 128 to 2048 cells and 32 to 128 wall faces. It is
frozen in the test, so any new transfer anywhere in the correction lands there
and fails it.

### The category nobody had listed

The **convergence residual** downloads four full fields — `kOld`, `epsilonOld`,
`kNew`, `epsilonNew` — so that `max|k_new − k_old|` can be formed in a host
loop. `32·N` bytes to produce one scalar, in a solver where every other
reduction already runs on the device. At 512 cells it is **larger than the
publication** (16 384 vs 12 288).

It is not wall traffic and no interface requires it. It is recorded here as an
open residency cost and deliberately **not** fixed under this task, which was
scoped to the wall path. See §6.

---

## 2. A — the criterion was unsatisfiable

```text
gate:         downloadBytes  <  3 * sizeof(double) * cells
at 512 cells: 38804          <  12288                        FAIL
```

But `3 * sizeof(double) * cells` is **exactly** the publication, one of the
terms inside `downloadBytes`. Even with the wall exchange free and the round
trip gone, the total could not fall below 12 288 + 16 384 + 4 960 + 52. The
inequality is strict, so it is unsatisfiable by construction — not a threshold
missed by 3×.

It was also measured on 16×8 and 64×8. Wall faces are the top and bottom rows,
so `N_wall = 2·nx`: both the cell count and the wall-face count quadruple
together, and that ladder cannot separate `O(N)` from `O(N_wall)` whatever bound
it uses.

---

## 3. B — the real defect, and its removal

`applyWallProductionDevice` downloaded two already-resident buffers and handed
them straight back:

```text
wall.wallCells          --D2H-->  std::vector<Index>  --.
wall.wallCellProduction --D2H-->  std::vector<Real>   --+--H2D--> DeviceImposedCells
                                                                        |
                                                        reassertImposedValuesDevice
```

**No host computation consumes either vector.** The kernel needs two device
pointers, and `wall` already holds them.

Fixed by giving `reassertImposedValuesDevice` an overload that takes the device
buffers directly; the `DeviceImposedCells` overload forwards to it, so there is
one kernel path and no duplicated code.

```text
                      D2H bytes / calls        H2D bytes / calls
512 cells,  before    38 804 / 202             7 188 / 12
128 walls   after     36 756 / 200             5 140 / 10
            delta     -2 048 / -2              -2 048 / -2   == 16 * N_wall each way
```

Required AFTER state, confirmed on all seven grids:

```text
wallCells D2H            0
wallCellProduction D2H   0
replacement re-upload    0
```

No new transfer appeared: the ledger still closes, and `control` is still
52 bytes in 9 calls.

### Validation, stated rather than glossed

`uploadImposedCellsDevice` checked index range, value finiteness and cell
uniqueness. The new overload cannot — its inputs are already on the device.
That is sound for this one caller and the reason is in the header: the same
`wall.wallCells` list is validated by `uploadImposedCellsDevice` when
`wall.imposedEpsilon` is built, and uniqueness is guaranteed upstream by
`computeWallTreatment`, which keeps one slot per cell. The length check is kept.

---

## 4. Numerical inertness

The whole 152-test GPU suite, before and after, diffed with wall-clock timings
stripped: **every single difference is a transfer count. No numeric value
changed.**

k and epsilon iteration counts are identical on all seven grids —
15/14, 15/16, 14/18, 16/20, 28/14, 24/19, 26/34 — which means identical
matrices, identical right-hand sides and identical solves. Wall-modified `P_k`,
the k matrix and RHS, solved k, epsilon, `mu_t`, `y+`, wall shear and the
imposed epsilon values are all covered by tests in that suite whose output is
byte-identical.

### Negative controls

Counters alone would fall just as far if the call had been deleted, so two
controls prove the kernel really reads the resident buffers:

* `WallProductionReassertionReadsTheResidentBuffers` — perturbs one value in the
  resident production buffer and requires exactly that wall cell to move,
  bitwise, with every other cell untouched.
* `WallProductionReassertionReadsTheResidentIndexBuffer` — substitutes a
  different resident index list and requires the replacement to follow it.

---

## 5. The amended gate

The original is removed, not weakened, and preserved in
`original_failing_gate.cpp.txt`. The replacements assert the **exact compact
representation** rather than a bound on an unrelated total — a bound can be
satisfied by accident, an equality cannot.

| test | asserts |
|---|---|
| `WallExchangeIsExactlyThreeRealsPerWallFace` | `gather == 3·sizeof(double)·N_wall` exactly, at 32/64/128 wall faces, and proportional (2× and 4×); round trip still 0 |
| `WallExchangeIsCompactAndIndependentOfCellCount` | fixed wall, 8× interior: gather and control unchanged, while publication and residual grow exactly 8× |
| `NoFullFieldLeavesTheDeviceBeyondTheSevenAudited` | `total − gather − krylov − control == 7·sizeof(double)·N` |
| `TransferLedgerAccountsForEveryDownloadedByte` | the ledger closes and control is frozen at 52 B / 9 calls |

The third one is how the required residency properties are proven. Asserting
"no full velocity D2H", "no full k D2H for the wall law", "no epsilon/mu_t D2H
for the wall law" and "no face-flux D2H" separately would mean asserting
absences, which a counter cannot see. One equality covers all of them: a
velocity download makes it `9N`, a face-flux download adds a face count that is
not a multiple of `N`, the round trip adds `16·N_wall`. Any of them breaks it.

Scaling families, both required, both measured:

```text
A  fixed wall, growing interior   nx=16, ny=8..64     N_wall=32 constant
   gather   768   768   768   768                     N: 128 -> 1024
   control   52    52    52    52

B  growing wall                   ny=8, nx=16,32,64    N_wall=32,64,128
   gather   768  1536  3072                            exactly proportional
```

---

## 6. Left open, on purpose

**The publication** (3 full fields) is a current interface cost, not wall
traffic: `TurbulenceModel` publishes host `ScalarField`s. Recorded, not hidden,
and not addressed here.

**The convergence residual** (4 full fields) is a genuine avoidable residency
cost — a host-side reduction that belongs on the device. It is 4/7 of the
full-length traffic per correction, more than the publication. It is *not* wall
traffic, so the wall gate does not depend on it, and fixing it needs a new
device reduction kernel: a larger change than this task authorises. **Flagged
for an explicit decision before resident SIMPLE integration**, where seven
full-field transfers per outer iteration is precisely the cost that design
exists to remove.
