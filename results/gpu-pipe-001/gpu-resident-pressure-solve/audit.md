# GPU-PIPE-001 — GPU-resident pressure solve — Phase A audit

Read-only. No production file was modified for this audit. Every number below was measured by
`tools/roundtrip_probe.cpp` against `build/final`, or read from the GPU-DISC-001Q qualified stage
timings; nothing here is a model of the code.

---

## 1. Verdict

The host round trip around the pressure solve is **removable**, and it is removable as a
*transport* change rather than a numerics change — with **one** exception that must be handled
deliberately rather than discovered later:

> `toHostSystem()` does not merely transport the device matrix. It **drops every entry whose
> assembled value is exactly `0.0`**. The solver therefore does not currently see the device
> matrix; it sees a compacted copy of it. A resident solve that feeds the device matrix directly
> would hand the solver a **different sparsity pattern**.

Measured, that drop is **exactly 2 entries at every grid size tested** — 40², 160², 320². It is
structural (the reference cell's off-diagonals, which the kernel *assigns* `0.0`), not a
data-dependent cancellation. Because it is structural it is knowable at plan time, so the resident
path can reproduce the host pattern exactly instead of approximating it.

This is the finding that decides the implementation shape, and it is the finding the audit existed
to produce.

---

## 2. Why the round trip exists at all

```cpp
// include/cfd/algebra/LinearSolver.hpp
LinearSolverResult solve(const LinearSystem& system);   // host interface
```

`LinearSystem` holds a host `SparseMatrix` and a host `Vector`. `SIMPLE.cpp:721` calls
`pressureSolver->solve(pAssembly->system)`, and `pAssembly->system` can only be a host object.
So `GpuSimpleDiscretization::assemblePressureCorrection` ends with:

```cpp
// cuda/kernels/GpuSimpleDiscretizationCuda.cpp:405
return toHostSystem(nc, downloadIndices(impl_->pressureSystem.rowOffsets),
                    downloadIndices(impl_->pressureSystem.columnIndices),
                    download(impl_->pressureSystem.values),
                    download(impl_->pressureSystem.rhs));
```

and `GpuBiCGSTAB::solveImpl` immediately uploads it all back. **The data is already on the device
on both sides of the boundary.** The round trip buys nothing but interface compatibility — the
file's own comment says so: *"it exists because LinearSolver takes a host LinearSystem."*

---

## 3. Formats already match — no conversion is required

| | device | host |
| --- | --- | --- |
| layout | CSR | CSR |
| row offsets | `DeviceBuffer<cfd::Index>` | `std::vector<Index>` |
| column indices | `DeviceBuffer<cfd::Index>` | `std::vector<Index>` |
| values | `DeviceBuffer<cfd::Real>` | `std::vector<Real>` |
| index type | `cfd::Index` = `std::size_t` | `cfd::Index` = `std::size_t` |
| column order within a row | `std::sort` + `unique` (`DevicePressureCorrectionPlan.cpp:158`) | `std::stable_sort` by (row, column) (`SparseMatrix.cpp:145`) |

`DeviceCsrMatrix` — what `GpuBiCGSTAB` already solves against — holds the *same three buffers of the
same three types*. The plan's own comment states the column sort exists to match
`SparseMatrixBuilder`'s stable sort.

**No widening, narrowing, reordering or re-indexing is needed anywhere.** That is the precondition
for treating this as transport removal.

---

## 4. What actually crosses the boundary, per pressure solve

`tools/roundtrip_probe.cpp` drives the production facade through the exact call order `SIMPLE` uses
and snapshots the transfer counters around the pressure window only.

```text
                       160x160 cavity, nc = 25,600, device nnz = 127,360
  window                       H2D calls   H2D bytes   D2H calls   D2H bytes
  assemble (device -> host)            0           0           4   2,447,368
  solve   (host -> device, back)       6   2,856,936       3,368   4,917,600
  setPressureCorrection                1     204,800           0           0
  ------------------------------------------------------------------------
  steady state, per solve              5   1,838,064       3,372   7,364,968
```

The `5` steady-state H2D calls decompose **exactly**, with no residual:

```text
  matrix values    host nnz x 8  = 1,018,864
  rhs b            nc x 8        =   204,800
  initial guess x0 nc x 8        =   204,800
  Jacobi diagonal  nc x 8        =   204,800     <- see section 6
  p' back to device (setPressureCorrection)  =   204,800
                                   ---------
                                   1,838,064   matches the counter
```

and the assemble D2H likewise: `8 * ((nc+1) + nnz + nnz + nc) = 2,447,368`. The instrument agrees
with the code to the byte, which is what makes it usable as a before/after detector.

**Avoidable traffic per pressure solve** — the assemble download, the solution download, and the
four H2D calls that push already-device-resident data back:

| grid | cells | device nnz | avoidable bytes / solve |
| --- | --- | --- | --- |
| 40² | 1,600 | 7,840 | 264,952 (0.25 MiB) |
| 160² | 25,600 | 127,360 | 4,285,432 (4.09 MiB) |
| 320² | 102,400 | 510,720 | 17,172,472 (16.4 MiB) |

The remaining solve-phase D2H (3,368 calls at 160²) is **Krylov reduction traffic**, not round-trip
traffic. This gate does not remove it and must not claim to.

---

## 5. The host rebuild is not free either

`toHostSystem` runs a `SparseMatrixBuilder`: it appends `nnz` triplets, `std::stable_sort`s them,
then coalesces. That is `O(nnz log nnz)` of host CPU work **per pressure solve**, on data the device
already had in the correct order.

```text
  grid     host rebuild   probe solve   rebuild share of the pair
  40²          1.03 ms       82.79 ms       1.2%
  160²         6.04 ms      327.31 ms       1.8%
  320²        24.93 ms      428.25 ms       5.5%
```

The probe's solves are **cold and tight** (a fresh zero guess, `absoluteTolerance` 1e-12), so they
run far more Krylov iterations than a production outer iteration does. The rebuild cost is fixed by
`nnz`; the solve cost shrinks with a warm start and a looser target. **The rebuild share in
production is therefore higher than the table above, not lower** — and the qualified production
timings confirm it:

```text
  GPU-DISC-001Q, cavity2d, gpu-disc arm
  grid     pressure-correction assembly    pressure solve
  20²        0.125 s   1.52%                4.899 s  59.45%
  160²       0.372 s   3.85%                6.937 s  71.76%
  640²       1.147 s  12.35%                3.933 s  42.37%
```

The `pressure-correction assembly` stage timer in `SIMPLE.cpp` wraps
`gpuDiscretization.assemblePressureCorrection(...)`, so **it includes the four D2H copies and the
host rebuild**. At 640² that stage is **12.35% of the entire solve — 1.147 s** — and its share has
grown monotonically with grid size (1.52% → 3.85% → 12.35%) because the discretization work around
it got faster in GPU-DISC-001 while this stage did not.

Together the pressure path is **54.7% of the 640² solve**. That is the case for this gate.

---

## 6. Dependencies that assume a host matrix

Two, both in `GpuBiCGSTAB::solveImpl`:

**a. `syncMatrix()`** — uploads a host `SparseMatrix` into a `DeviceCsrMatrix`, structure once and
values thereafter. A resident path skips it entirely; the device system *is* the matrix.

**b. `buildJacobiDiagonal(const cfd::algebra::SparseMatrix&, DeviceVector&)`**
(`GpuPreconditionerKernel.cu:41`) — takes a **host** matrix, calls
`cfd::algebra::computeInverseDiagonal(matrix)` on the host, and uploads the result. This is the
fifth H2D counted in section 4.

`computeInverseDiagonal` **throws** `InvalidArgumentError` on a missing, zero, near-zero or
non-finite diagonal, and `solveImpl` converts that into `SolverStatus::InvalidSystem`. A resident
path needs a device-side extraction that reproduces **both** the arithmetic (bitwise) and the
rejection (same status, same message class). The rejection needs one host-visible decision — the
precedent is already established: the finiteness counter added in persistent fields
(`resetNonFiniteCounter` / `countNonFiniteDevice` / `readNonFiniteCounter`), a 4-byte D2H.

This is the piece most likely to be overlooked, because the preconditioner is invisible from the
`assemblePressureCorrection` call site.

---

## 7. The solver refactor is provably equivalent

Every exit path in `GpuBiCGSTAB::solveImpl` — converged, breakdown, iteration limit, invalid system
— ends with the same two lines:

```cpp
result.solution = x_.downloadToVector();
return result;
```

and **no exit path modifies `x_` after the download**. Verified by reading all 629 lines of
`GpuLinearSolverCuda.cpp`. Therefore the download can be hoisted to a single point without changing
any result on any path.

That hoist is what makes a shared device-resident core possible:

```text
  solveImpl(host LinearSystem)        solveResident(DeviceCsrMatrix, b, x0)
        |  syncMatrix / uploads                |
        +--------------+-----------------------+
                       |
              solveCore(device state)     <- one algorithm, one copy
                       |
        +--------------+-----------------------+
        |  downloadToVector                    |  leaves x_ resident
```

**One solver, two entry points.** The brief's "do not create a second pressure solver" is satisfied
structurally, not by discipline, and the existing entry point keeps byte-identical behaviour because
it keeps running the same core.

---

## 8. The structural question, restated as a requirement

Measured drop counts:

```text
  grid    device nnz    host nnz    dropped
  40²          7,840       7,838          2
  160²       127,360     127,358          2
  320²       510,720     510,718          2
```

Two at every size. The source is `DevicePressureCorrectionKernel.cu:255`:

```cpp
if (isReference) {
  // The face loop never writes this row, so the diagonal is EXACTLY the
  // 1.0 added afterwards and every off-diagonal is exactly 0.0.
  values[k] = (column == c) ? 1.0 : 0.0;
  continue;
}
```

The reference cell is cell 0 — a corner of a Cartesian 2D mesh, with exactly 2 internal-face
neighbours. **Data-dependent cancellation contributed zero entries at every grid tested**, which is
expected: an off-diagonal is `-faceCoefficient[f]`, and a zero face coefficient would itself be a
degenerate-face defect.

Two implementation options, and the choice matters:

1. **Reproduce the host pattern at plan time** — exclude the reference row's off-diagonal columns
   when the plan builds `columnIndices`. The reference cell is fixed for the whole solve, so the
   structure stays static and the "structure once" property is preserved. The resident matrix is
   then *the same matrix the host solver receives today*, and bitwise equivalence is a consequence
   of the construction rather than a hoped-for measurement.
2. Keep the explicit zeros and rely on `0.0 * x` contributing nothing. **Rejected.** It is not
   provable: adding `+0.0` to a running sum of exactly `-0.0` yields `+0.0`, so a signed-zero
   difference can survive into the result and `memcmp` would see it. A gate whose standard is
   bitwise cannot rest on an argument with a known exception.

**Option 1 is the design.** It also keeps `nnz` identical, so the SpMV trip count, the reduction
sizes and the preconditioner length are unchanged — nothing downstream sees a different problem.

---

## 9. Risks, and the detector for each

| risk | detector |
| --- | --- |
| resident pattern ≠ host pattern | assert `nnz` and full CSR equality against `toHostSystem` output, per case |
| resident values ≠ host values | bitwise compare of values/rhs before the solve |
| device Jacobi diagonal ≠ host | bitwise compare of the inverse diagonal |
| diagonal rejection lost | negative control: inject a zero diagonal, require the same status |
| residency claimed but not achieved | transfer guard on per-solve H2D/D2H — numerically invisible, exactly like `pf7` |
| breakdown/failure path changed | negative control on the known BiCGSTAB breakdown reproducer |
| solution not left resident | dirty-state assertion on the p' field authority |

The transfer guard is the non-negotiable one. Re-uploading a matrix that is already resident is
**numerically identical** — every equivalence gate in this project passes it. Without a transfer
detector, "the pressure solve is resident" is unfalsifiable. This is the lesson `pf7` established in
persistent fields and it applies unchanged here.

---

## 10. Out of scope, stated so it is not silently absorbed

* **Krylov reduction traffic** (3,368 D2H per solve at 160²) — that is the reduction pattern, not the
  round trip. Untouched.
* **The known GPU BiCGSTAB restart asymmetry** — listed technical debt, not authorized, and the
  negative controls must keep reproducing it unchanged.
* **The momentum solves** — they round-trip too, by the same mechanism. This gate is scoped to the
  pressure solve; momentum belongs to the GPU-resident SIMPLE loop.
* **CPU numerics** — reference implementation, unchanged.
* **Convergence criteria and tolerances** — unchanged.

---

## 11. Baseline recorded for the before/after comparison

```text
build/final, RTX 5000 Ada, sm_89, CUDA 12.9
probe: results/gpu-pipe-001/gpu-resident-pressure-solve/tools/roundtrip_probe.cpp
raw:   results/gpu-pipe-001/gpu-resident-pressure-solve/baseline.log

per pressure solve, steady state, 160x160 cavity
  H2D            5 calls    1,838,064 bytes
  D2H        3,372 calls    7,364,968 bytes
  avoidable                 4,285,432 bytes
  allocations        0
  host rebuild    6.04 ms

production stage share (GPU-DISC-001Q, cavity2d, gpu-disc, 640x640)
  pressure-correction assembly   1.147 s   12.35%
  pressure solve                 3.933 s   42.37%
```
