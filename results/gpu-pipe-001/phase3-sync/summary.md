# GPU-PIPE-001 Phase 3 — remove unnecessary synchronizations

**Status: PASS.**

Full classification of every synchronization site: [`audit.md`](audit.md).

## Result

Four of five `cudaDeviceSynchronize()` sites removed — SpMV, `waxpby`, `fill` and the Jacobi apply,
all of which feed only subsequent kernels on the same stream. The reduction's synchronize is kept
and documented as required (the host blocks on the following D2H copy regardless).

Paired against HEAD, same session, medians of 3 — cumulative Phase 2 + Phase 3:

| grid | metric | before | after | change |
| --- | --- | ---: | ---: | ---: |
| 160² | synchronizations | 58 480 | 19 478 | **−66.7 %** |
| | D2H calls | 27 280 | 19 502 | −28.5 % |
| | gpu_solve | 3.208 s | 2.098 s | **−34.6 %** |
| | end-to-end | 3.930 s | 2.829 s | −28.0 % |
| 320² | synchronizations | 55 201 | 18 389 | **−66.7 %** |
| | gpu_solve | 4.768 s | 2.739 s | **−42.5 %** |
| | end-to-end | 7.239 s | 5.148 s | −28.9 % |
| 640² | synchronizations | 55 165 | 18 381 | **−66.7 %** |
| | gpu_solve | 4.806 s | 3.352 s | **−30.2 %** |
| | end-to-end | 14.006 s | 12.724 s | −9.2 % |

**Every remaining synchronization is a reduction round trip.** `synchronizations` now equals
`reductionGroups` exactly at every grid (19 478 / 18 389 / 18 381). There is no other source of
host-device serialization left in the solver path.

## Gates

| Gate | Result |
| --- | --- |
| CPU/GPU equivalence | **PASS** — pressure residual identical to 17 s.f., Krylov iteration counts identical, at 160²/320²/640² |
| Solvers | **PASS** — GPU 66/66, algebra 97/97, SIMPLE 123/123 |
| GPU-PCORR-001 regression | **PASS** — 6/6 |
| compute-sanitizer | **PASS** — memcheck / initcheck / synccheck / **racecheck**, 0 errors each |
| Determinism | **PASS** — 5 repeated solves bitwise identical at 160² and 640² |
| Negative control | **PASS** — injected round trip detected at exactly +2338 = `krylov_it` |

racecheck and the determinism check carry more weight here than in Phase 2: kernels now run
asynchronously with respect to the host, so an unsound ordering assumption would show up as a race
or as run-to-run drift. Neither appears.

## Disclosures

* **Error attribution moved.** Launch errors are still caught synchronously by
  `cudaGetLastError()`. Execution errors now surface at the next synchronizing call rather than at
  the offending kernel. Still detected, still fatal, reported later.
* **`kernelSeconds` changed meaning** — launch time for the four asynchronous kernels, execution
  time only for the reduction. Documented in `GPUExecutionStats.hpp` rather than left to be
  discovered.
* **One test contract changed deliberately**: `test_device_buffer.cpp` asserted
  `synchronizations == 1` after a single `spmv`; it now asserts `== 0`, turning the assertion into a
  guard against reintroducing the synchronize.

## Files changed

```text
cuda/kernels/CsrSpmvKernel.cu             removed sync
cuda/kernels/DeviceVectorOpsKernel.cu     removed 2 syncs (waxpby, fill)
cuda/kernels/GpuPreconditionerKernel.cu   removed sync
include/cfd/gpu/GPUExecutionStats.hpp     kernelSeconds / synchronizations re-documented
tests/unit/gpu/test_device_buffer.cpp     contract updated to guard the new behaviour
```

No numerical threshold, tolerance or breakdown criterion changed.
