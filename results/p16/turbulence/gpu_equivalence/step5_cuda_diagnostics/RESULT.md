# S5-1 CUDA diagnostics — RESULT

```text
CUDA kernel-set coverage        PASS    44/44, both set differences empty
Representative state coverage    PASS    see section 3
memcheck                         PASS    0 errors, 114 tests
racecheck                        PASS    0 hazards, 114 tests
initcheck                        PASS    0 errors, 114 tests
synccheck                        PASS*   0 errors, 114 tests

* the tool passed scoped execution; negative-control detection could not be
  independently demonstrated and remains a documented limitation.
```

Run 09:11:26 → 10:17:54 (66 min) on the frozen scope, against a full-sweep
projection of 4 hours to over a day.

---

## 1. What this gate does and does not claim

**Claim, precisely:**

> All CUDA kernels exercised by the frozen full P16 GPU-equivalence suite,
> including `ResidentChannelProduction`, are also exercised by the amended
> sanitizer scope.

**NOT claimed:** "100% CUDA coverage". The scoped set launches 44 kernels;
`cuda/kernels/` defines 50 `__global__` functions. The other 6 are launched by
no test in this binary at all, so the tracer cannot see them and this gate says
nothing about them. Calling this complete CUDA coverage would silently annex
six kernels nothing has ever run.

## 2. Kernel coverage — measured, not inferred

```text
kernels in FULL suite                 44
kernels in SCOPED set                 44
kernels in ResidentChannelProduction  41

full \ scoped     = 0        (the decisive check)
channel \ scoped  = 0        (justifies excluding the 515 s group)
UNCOVERED rows    = 0
```

Full attribution in `coverage_matrix.txt`; per-suite traces in `per_suite/`.

Measured by `tools/kernel_tracer.c`, an `LD_PRELOAD` interposer on
`cudaLaunchKernel` resolving names through `cudaFuncGetName` (official CUDA
12.3+ API). No production source was modified and the traced binary is the same
one the sanitizer ran. Neither nsys, ncu nor CUPTI is installed here, and
compute-sanitizer filters kernels but will not enumerate them.

### The measurement corrected the scope, which is why it exists

The first scoped set missed a kernel:

```text
first scoped trace   43 kernels
full-suite trace     44 kernels
difference           { jacobiApplyKernel }
```

The scope already contained every turbulence suite plus `KrylovRestartParity`
and `GpuBiCGSTABRestartTest`, so "the CUDA linear-solver path is covered" was a
believable statement and was wrong: the turbulence solves do not select Jacobi
preconditioning, so nothing reached `jacobiApplyKernel`.
`PreconditionerType::Jacobi` is a selectable production option, so the kernel is
genuinely on the path. `GpuJacobiTest.*` was added and the scope re-traced to
zero difference.

Every plausible argument for the original scope was true and would still have
left that kernel unchecked.

### Two defects in the matrix tooling itself, both fixed

1. **Demangling cut in the wrong place, twice.** `s/.*:://` first cuts inside a
   parameter type containing `::`; stripping from the first `(` first cuts at
   `(anonymous namespace)` and blanks the name. Correct order: remove
   `(anonymous namespace)::`, then the parameter list, then the namespace. The
   tool now carries a comment recording both failures.

2. **Short names collide and the join must not use them.**
   `countNonFiniteKernel`, `assembleKernel` and `greenGaussSumKernel` each name
   more than one distinct kernel across translation units. The join key is the
   MANGLED name throughout. Keying on short names would have collapsed them and
   could have reported an empty difference while a kernel was unchecked.

## 3. Representative state coverage

Kernel-name coverage is not sufficient, so the scope includes tests that
exercise the conditions behaviour depends on:

```text
geometry / non-orthogonality   DeviceScalarTransportSemantics (skewCorrectKernel,
                               obliqueNeumannKernel — launched by that suite alone)
indexing / grid size           DeviceKTransport, DeviceEpsilonTransport across mesh sizes
boundaries                     boundaryFaceValuesKernel / interiorFaceValuesKernel
                               via DeviceKTransport, DeviceEpsilonTransport
imposed cells                  fixRowsKernel, reassertImposedKernel via
                               DeviceEpsilonTransport, DeviceScalarTransportSemantics
wall cells / wall exchange     gatherByIndexKernel via ResidentWallExchange (that
                               suite alone), DeviceWallFaceViscosity
convergence state              partialMaxAbsDiff2Kernel via DeviceMaxAbsDifference,
                               countNonFiniteKernel via the correction suites
Krylov restart                 KrylovRestartParity, GpuBiCGSTABRestartTest
                               (partialAbsDotKernel reached only by these + ResidentWallExchange)
full coupled path              ResidentTurbulentSimple (momentumAssemblyKernel,
                               correctVelocityKernel, correctFaceMassFluxKernel,
                               relaxedPressureUpdateKernel, responseKernel)
```

`ResidentTurbulentSimple` dominates the runtime — one of its tests accounts for
most of the 66 minutes, in every tool — and was deliberately kept. It is the
only scoped test that runs the complete resident SIMPLE turbulence coupling
under a sanitizer, which is the thing the amendment exists to preserve.

## 4. Non-vacuity controls

Each tool was required to detect a planted defect BEFORE its clean result was
accepted (`scratchpad/sanitizer_control.cu`, standalone — no defect introduced
into CFDApp):

```text
memcheck    DETECTS   Invalid __global__ write of size 8 bytes
initcheck   DETECTS   ERROR SUMMARY: 64 errors
racecheck   DETECTS   1 hazard, correct file:line for both accesses
synccheck   NOT DEMONSTRATED
```

The controls found three things worth keeping:

1. **memcheck missed an 8-byte overrun.** One element past a 64-element
   `cudaMalloc` was not reported; 4096 elements past was. The 512-byte request
   is padded and a small overshoot lands inside it. So a clean memcheck does not
   prove the absence of small out-of-bounds accesses. This is a tool/allocator
   property, not a consequence of scoping.

2. **synccheck detection could not be demonstrated.** Warp-aligned divergence
   went unreported, which is correct under Volta+ independent thread
   scheduling; a per-thread-trip-count barrier loop deadlocked the kernel and
   had to be killed. Reported as `PASS*` rather than promoted to a demonstrated
   detector.

3. **The repository's own sanitizer script mis-parses a single hazard.**
   `results/gpu-pipe-001/phase2-reductions/tools/gate3_sanitizer.sh` requires
   `hazards`/`errors` plural, but the tool prints `1 hazard displayed (1 error,
   0 warnings)` for exactly one. It fails CLOSED, so it is safe, but it would
   report "could not parse" instead of the count. That file is preserved phase
   evidence and was not edited; `tools/step5_sanitizer.sh` handles both forms.

## 5. Scope accounting

```text
tests in full CFDGpuTests        196
tests in the sanitizer scope     114
excluded                          82   of which ResidentChannelProduction is 15
```

The other 67 excluded tests are non-turbulence suites (SpMV, buffers, vectors,
CSR, fields, residency manager, CG, plain BiCGSTAB, instrumentation, boundary
encoding). The coverage measurement shows they launch no kernel the scoped set
misses, which is what permits their exclusion from the SANITIZER.

**Their normal unsanitized coverage is unchanged and mandatory** — all 196 run
in every `regress_both.sh` pass.

## 6. Reporting rule

```text
FULL NORMAL REGRESSION      exhaustive       every test, unsanitized,
                                             regress_both.sh p16, both configs
CUDA SANITIZER REGRESSION   coverage-driven  114 tests + measured kernel
                                             coverage matrix
```

Never conflated. This gate is S5-1 only; it is not evidence for S5-2 through
S5-6.
