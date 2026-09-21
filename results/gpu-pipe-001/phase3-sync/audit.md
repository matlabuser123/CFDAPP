# GPU-PIPE-001 Phase 3 — synchronization audit

Every explicit `cudaDeviceSynchronize()` in the GPU path, classified. The rule applied: a
synchronization is **REQUIRED** only if the host must observe device work before it can continue.
If the only consumer of a kernel's output is another kernel on the same stream, CUDA's stream
ordering already guarantees correctness and the synchronize buys nothing.

## The five sites

| # | Site | Output consumed by | Verdict |
| --- | --- | --- | --- |
| 1 | `CsrSpmvKernel.cu` — `csrSpmvKernel` | the next kernel (device) | **UNNECESSARY — removed** |
| 2 | `DeviceVectorOpsKernel.cu` — `waxpbyKernel` | the next kernel (device) | **UNNECESSARY — removed** |
| 3 | `DeviceVectorOpsKernel.cu` — `fillKernel` | the next kernel (device) | **UNNECESSARY — removed** |
| 4 | `DeviceVectorOpsKernel.cu` — reduction | a blocking D2H copy, then host arithmetic | **REQUIRED — kept** |
| 5 | `GpuPreconditionerKernel.cu` — `jacobiApplyKernel` | the following SpMV (device) | **UNNECESSARY — removed** |

### Why #4 is required

The reduction is immediately followed by `partialSumsCache.downloadTo(...)`, and the host then sums
the partials and makes a control-flow decision (breakdown tests, convergence) on the result. The
host cannot proceed without the device's output, so it must wait.

Note the synchronize is *strictly* redundant even here — the synchronous `cudaMemcpy` on the default
stream is itself stream-ordered after the kernel and blocks the host. It is kept because it costs
nothing (the host blocks either way) and it preserves the only remaining point where kernel
execution time is separable from copy time. Removing it would make `downloadSeconds` silently
absorb kernel execution, which is how a timing counter starts lying.

### Why the other four are unnecessary

All four write device memory whose only reader is a subsequent kernel issued on the same (default)
stream. CUDA guarantees that kernels on one stream execute in issue order, so the consumer cannot
observe a partial result. Nothing on the host reads these buffers between the producing and
consuming kernels.

They were not removed blindly — each was checked for a host read of its output between producer and
consumer. There is none in `GpuCG`, `GpuBiCGSTAB` or the preconditioner path.

## What was deliberately NOT done

* **No stream was created, and no global synchronize was replaced by an event.** The default stream
  already provides the required ordering; introducing streams would add ordering obligations without
  removing any host wait. The phase's instruction is to prefer stream semantics over global
  synchronization, and the correct application here is to *rely on* the ordering that already
  exists rather than add machinery.
* **Device-side reduction finalisation was not reintroduced.** Phase 2 measured it 25 % slower at
  640²; nothing in Phase 3 changes that evidence.

## Consequences that must be stated

**Error attribution moved.** `cudaGetLastError()` still follows every launch and still catches
launch-configuration errors synchronously. Execution errors (illegal address, etc.) are
asynchronous: they previously surfaced at that kernel's own synchronize, and now surface at the next
synchronizing call — in practice the reduction, or a later allocation. The error is still *detected*
and still fails the run; only the reported call site is later. compute-sanitizer remains exact, and
Gate 3 runs all four tools.

**`kernelSeconds` changed meaning, and is documented as such.** It used to be device execution time,
because every kernel synchronized. For the four asynchronous kernels it is now launch time; only the
reduction's contribution is still execution time. `GPUExecutionStats.hpp` says this explicitly —
a counter that quietly changes meaning is worse than one that is removed. `gpuSolveSeconds` and the
end-to-end benchmark remain the authority on cost.

**A test contract changed, and now guards the new behaviour.**
`test_device_buffer.cpp` asserted `synchronizations == 1` after a single `spmv`. It now asserts
`== 0`. This is not a weakening: it converts the assertion into a regression guard, so
reintroducing the synchronize fails the test rather than silently costing two host round trips per
Krylov iteration.

## Measured result

Paired against HEAD, same session, medians of 3 (cumulative Phase 2 + Phase 3):

| grid | syncs before | syncs after | change |
| --- | ---: | ---: | ---: |
| 160² | 58 480 | 19 478 | **−66.7 %** |
| 320² | 55 201 | 18 389 | **−66.7 %** |
| 640² | 55 165 | 18 381 | **−66.7 %** |

**Every remaining synchronization is a reduction round trip**: `synchronizations` now equals
`reductionGroups` exactly (19 478 = 19 478 at 160², 18 389 = 18 389 at 320², 18 381 = 18 381 at
640²). There is no longer any other source of host-device serialization in the solver.

Runtime, same runs:

| grid | gpu_solve before | after | change | end-to-end before | after | change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 160² | 3.208 s | 2.098 s | **−34.6 %** | 3.930 s | 2.829 s | −28.0 % |
| 320² | 4.768 s | 2.739 s | **−42.5 %** | 7.239 s | 5.148 s | −28.9 % |
| 640² | 4.806 s | 3.352 s | **−30.2 %** | 14.006 s | 12.724 s | −9.2 % |

Correctness unchanged: pressure residual identical to 17 significant figures and Krylov iteration
counts identical at all three grids.
