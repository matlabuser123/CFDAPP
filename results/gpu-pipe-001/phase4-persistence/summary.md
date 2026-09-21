# GPU-PIPE-001 Phase 4 — persistent matrices and solver workspaces

**Status: PASS — already implemented by P6-GPU-001/002, now verified by measurement.**

No code was changed in this phase. The work was to establish, with evidence, whether the required
persistence exists and where it does not — not to reimplement machinery that is already there.

## 1. What already exists

**Structure/value split** — `DeviceCsrMatrix` (`include/cfd/gpu/DeviceCsrMatrix.hpp`) carries
`uploadStructureAndValues()` and `updateValues()`, and `updateValues()` refuses to run against a
matrix whose structure differs rather than silently reinterpreting it.

**Structure uploaded once per solver** — `syncMatrix()` in `cuda/kernels/GpuLinearSolverCuda.cpp`
re-uploads structure only when it is absent or the dimensions/nonzeros changed; otherwise it
uploads values alone.

**The device matrix and the whole Krylov workspace are solver members**, not locals:
`deviceMatrix_`, `x_`, `b_`, `r_`, `rHat_`, `p_`, `v_`, `s_`, `t_`, `pHat_`, `sHat_` — with
`resize()` a no-op once large enough (`DeviceBuffer`'s never-shrink contract). The reduction
partial-sum buffer is a persistent `static` (Phase 2 kept this).

## 2. Verified, not assumed

`measure_persistence.sh`, 160² with a growing number of linear solves, and then a fixed number of
solves across grids:

| edge | outer | linear solves | allocations | reallocations | frees | H2D calls |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 160 | 1 | 3 | **28** | 0 | 26 | 13 |
| 160 | 2 | 6 | **28** | 0 | 26 | 22 |
| 160 | 4 | 12 | **28** | 0 | 26 | 40 |
| 160 | 8 | 24 | **28** | 0 | 26 | 76 |
| 320 | 2 | 6 | **28** | 0 | 26 | 22 |
| 640 | 2 | 6 | **28** | 0 | 26 | 22 |

**Allocations are constant at 28 whether the run performs 3 or 24 linear solves, and constant
across a 16× range in problem size. Reallocations are 0 in every case.** Device storage does not
grow with solve count — the persistence claim holds.

H2D calls scale with the number of solves (3.0–3.2 per solve: matrix values, RHS, initial guess)
and H2D bytes scale with the grid. Both are expected: the momentum and pressure coefficients are
reassembled every outer iteration, so their values genuinely change and must be re-uploaded.
Only immutable structure is exempt, and it already is.

## 3. A dormant path, recorded

`SIMPLE.cpp` calls `GpuResidencyManager::syncMatrix`/`syncField` each outer iteration, guarded by
`settings_.enableGpuResidency`, which **defaults to `false`**. That class's own header states it
"never changes SIMPLE's computed result" and that its resident data is not read back by any solver:
it was built (P6-GPU-001) to prove residency mechanics, and the GPU solvers keep their own
`deviceMatrix_` instead.

So in every configuration measured here it performs no uploads at all, which is why H2D is
accounted for entirely by the solver's own 3 per solve. If it were ever enabled as-is it would
upload momentum matrices and velocity fields that nothing reads — 4 redundant uploads per outer
iteration. **Not changed** (it is outside this phase's scope and harmless while disabled), but
recorded here so it is not mistaken for a live persistence path.

## 4. Why no further work was done here

H2D traffic is **40–76 calls for an entire run**, against 18 000–19 500 D2H calls. Phase 1 measured
the transfer cost at 79–101 µs per *call* regardless of payload, so the upload side is not, and
cannot become, the bottleneck. Making the remaining per-solve uploads persistent would be a large
change to the solver interface for a saving that the measurements say is not there.

The one upload that could be removed — re-uploading an initial guess the device already holds from
the previous solve — is Phase 6's concern (GPU-resident pressure solve), and its value is also
bounded by the same argument: it is 24 calls per run, not 19 500.
