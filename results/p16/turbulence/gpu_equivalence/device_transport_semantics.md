# P16-GPU-TURB -- device scalar-transport discrete semantics

The foundation the Phase A audit named as blocking: `DeviceScalarTransport` had
**no under-relaxation and no imposed-cell support**, and all six CPU turbulence
equations depend on both. No turbulence CUDA physics could be written before
this, because a device k-epsilon solve could not have reproduced the CPU one
even in principle.

Status: **COMPLETE**, 10/10 tests green in `build/cuda`.

---

## 1. What the CPU path does, and what the device now does

`cfd::turbulence::solveRelaxedScalarTransport` (src/turbulence/KEpsilonEquation.cpp):

```
assemble  ->  under-relax  ->  fix imposed rows  ->  solve  ->  floor  ->  re-assert imposed
```

| step | CPU | device (new) |
|---|---|---|
| assemble | `cfd::transport::*` | `assembleScalarTransportDevice` (P14) |
| under-relax | `applyImplicitUnderRelaxation` | `applyImplicitUnderRelaxationDevice` |
| fix imposed rows | `SparseMatrixBuilder::fixRow` + `rhs[cell] = value` | `fixImposedRowsDevice` |
| solve | BiCGSTAB | (existing GPU solver, unchanged) |
| floor + re-assert | in the wrapper | **still owed by the k-epsilon integration -- see section 6** |

The relaxation is the host expression term for term, because the target is
bitwise and `a_P/alpha` is not the same double as `a_P + a_P*(1/alpha - 1)`:

```
factor = 1/alpha - 1
extra  = a_P * factor          <- a_P is the PRE-RELAXATION diagonal
a_P   += extra
b_P   += extra * phi_P^old     <- the SAME extra, not one rebuilt from a_P/alpha
```

`alpha == 1` launches no kernel at all, so an unrelaxed device assembly cannot
be perturbed by this path merely existing. Asserted, not assumed.

---

## 2. The frozen methodology

Written down before any number was measured.

### G1 -- structure, exact, BOTH directions

The host builder drops entries that accumulate to exactly zero; the device keeps
the candidate pattern. So the host pattern is a **subset**, and the P14 probe
checked only `host -> device`.

That one-directional check is not sufficient here, and imposed rows are exactly
where it fails. A fixed row on the host holds literally one entry, `(row, row,
1)`. A device row that kept its assembled off-diagonals **passes a
host -> device check** -- every host entry (the single diagonal) is found -- and
is a completely different matrix.

So G1 is bidirectional: every host entry found on the device with an equal
value, **and** every device entry with no host counterpart exactly zero. No
tolerance; this is a structural property.

### G2 -- the semantics, BITWISE (0 ulp)

Assembly association order differs between host and device by design, and P14
measured that at <= 2 ulp on diagonals where convection and diffusion both act.
That pre-existing difference would mask the thing under test, so G2 removes it
**by construction**: both sides start from the device's own assembled system,
and only then does the host apply `applyImplicitUnderRelaxation` + `fixRow`
while the device applies `applyImplicitUnderRelaxationDevice` +
`fixImposedRowsDevice`. Identical inputs, identical operations -- anything but
bitwise equality is a semantic difference.

### G3 -- the composed path

Full CPU path vs full device path, including the assembly difference G2 removes.
The ulp numbers are **reported, not bounded by a threshold invented here**. The
acceptance criterion is P14's already-measured, already-frozen **2.1e-12
relative-L2 agreement of the solved field** (results/p14/transport-foundation/).

---

## 3. The six frozen configurations

Comparison #6 (CSR structure / coefficients / RHS) is applied to all of the
preceding configurations rather than being a case of its own.

| # | configuration | imposed cells |
|---|---|---|
| 1 | no imposed, alpha=1 | -- |
| 1b | no imposed, alpha=0.7 | -- |
| 2 | imposed cell 0, alpha=1 | {0} |
| 3 | imposed, alpha=1 | {0, 57, 107} |
| 4 | imposed + under-relaxation | {0, 57, 107} |
| 5 | imposed + non-orthogonal correction (distorted mesh) | {0, 57, 107} |

Cell 0 alone is deliberately not enough. It is the first row, its candidate
pattern puts the diagonal first, and an off-by-one in the row scan would survive
it. Cell 57 is interior, with neighbours on both sides of the diagonal; cell 107
is the last row, where a row-offset overrun would show.

## 4. Measured, 12x9 grid (108 cells)

```
config                                    structure          values bitwise   max value   max rhs
1  no imposed, alpha=1                    OK / OK (0 extra)  486/498          1 ulp       0 ulp
1b no imposed, alpha=0.7                  OK / OK (0 extra)  489/498          1 ulp       2 ulp
2  imposed cell 0, alpha=1                OK / OK (2 extra)  484/496          1 ulp       0 ulp
3  imposed {0,57,107}, alpha=1            OK / OK (8 extra)  479/490          1 ulp       0 ulp
4  imposed {0,57,107}, alpha=0.7          OK / OK (8 extra)  482/490          1 ulp       2 ulp
5  imposed + non-orthogonal, alpha=0.7    OK / OK (8 extra)  483/490          1 ulp       2 ulp
```

* **G1 PASS** on every configuration, both directions. The "extra" count is the
  device-only entries in imposed rows -- 2 for one boundary cell, 8 across the
  three -- every one of them exactly zero.
* **G2 PASS** -- bitwise, 0 ulp, on every configuration.
* **G3 PASS** -- solved-field relative L2 within P14's frozen 2.1e-12 on every
  configuration. Max composed value difference is **1 ulp**, inside P14's
  measured 2.
* The RHS column is the relaxation doing what it should: **0 ulp at alpha=1, 2
  ulp at alpha=0.7**, because relaxation is the only path by which the
  diagonal's association-order difference reaches the right-hand side. If that
  column had stayed 0 at alpha=0.7 the relaxation would not have been reading
  the diagonal at all.

## 5. Non-vacuity controls

| control | asserts | result |
|---|---|---|
| NV1 | alpha != 1 changes the system: every diagonal and every RHS entry differs, **no off-diagonal does** (relaxation is a diagonal operation), and each relaxed diagonal equals the device's own pre-relaxation diagonal times (1+factor) | PASS |
| NV1b | alpha == 1 is an **exact** no-op -- values and RHS byte-identical to never calling it | PASS |
| NV2a | dropping any one of the three imposed rows makes the comparison FAIL | PASS (3/3) |
| NV2b | perturbing any one imposed value by 1e-9 makes it FAIL, and the difference reaches the RHS | PASS (3/3) |
| NV3 | the **pre-relaxation-diagonal bug**, implemented on purpose: a RHS built from `a_P/alpha` instead of `a_P` must disagree with the device on every row | PASS (108/108) |
| ordering | imposing **before** relaxing produces a different matrix, and leaves the imposed value not imposed | PASS |
| determinism | the same inputs produce the same bytes twice | PASS |
| shape | an imposed row is a unit diagonal, explicit zeros elsewhere, RHS overwritten not accumulated; non-imposed rows untouched | PASS |
| validation | alpha outside (0,1], out-of-range cell, non-finite value, mismatched lengths, duplicate cell -- all refused | PASS |
| tie-back | the host replica in the test file reproduces `solveRelaxedScalarTransport` exactly | PASS |

NV3 is the one the user's brief singled out, and it is worth stating plainly:
using the already-relaxed diagonal in the RHS correction still converges, to a
different answer, with an alpha-dependent error that reads as a physics
difference. A test suite that cannot distinguish the two formulas has not pinned
either of them.

---

## 6. What this found, and still owes

**The tie-back test found a post-solve step the device path does not yet have.**
`solveRelaxedScalarTransport` re-asserts every imposed value *after* the solve
and after the floor, because an iterative solver only **approaches** a fixed
row -- measured here at **1.5e-10** on all three imposed cells. Fixing the row
is therefore necessary and **not sufficient**. The device k-epsilon integration
owes the same re-assertion after its own solve, or its wall cells drift off the
wall function that put them there. Recorded in
`include/cfd/gpu/DeviceScalarTransport.hpp` so it cannot be lost between here
and that integration.

**Duplicate imposed cells are refused, not resolved.** On the host `fixRow` is
idempotent and the RHS assignment is last-wins, both well defined because the
loop is sequential. On the device two threads would write one row and a list
carrying two different values for one cell would resolve to whichever the
scheduler ran last. `computeWallTreatment` keeps one slot per wall cell even
when several wall faces share it, so this refuses an input the producers cannot
emit rather than inventing a tie-break the host does not have.

**A row with no diagonal slot fails closed.** Without one there is nowhere to
put the 1 and the row would silently become all-zero -- a singular matrix
surfacing as a convergence failure somewhere else entirely.

---

## 7. Files

| file | change |
|---|---|
| `include/cfd/gpu/DeviceScalarTransport.hpp` | `applyImplicitUnderRelaxationDevice`, `DeviceImposedCells`, `uploadImposedCellsDevice`, `fixImposedRowsDevice` |
| `cuda/kernels/DeviceScalarTransportKernel.cu` | `underRelaxKernel`, `fixRowsKernel` + host wrappers (already in the `-fmad=false` list) |
| `tests/unit/gpu/test_device_scalar_transport_semantics.cpp` | new, 10 tests |
| `tests/unit/gpu/CMakeLists.txt` | registration + `DistortedMesh.hpp` include path |

## 8. Scope

Unchanged from the frozen P16-GPU-TURB scope. This is transport infrastructure
only -- no turbulence model, no thermal path, no SST, nothing touching P20. The
`TurbulenceBackendTrace` operations all still read `NotAttempted`, which is the
honest state until the first turbulence kernel exists.

Next: GPU strain rate -> P_k -> mu_t, the first operations that will flip
`NotAttempted` -> `Gpu`.
