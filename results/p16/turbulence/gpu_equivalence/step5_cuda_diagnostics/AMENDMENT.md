# AMENDMENT A2 — S5-1 CUDA diagnostics: exhaustive sweep → coverage-driven scope

Authorized amendment to the frozen P16 Step 5 gate. The original criterion is
reproduced verbatim below and is **not** rewritten. The terminated attempt is
preserved in `terminated_full_attempt/`.

---

## 1. The original frozen requirement

From `results/p16/turbulence/gpu_equivalence/step5_gate.md`, S5-1:

> **Criterion.** `compute-sanitizer` over `CFDGpuTests`, four tools, with
> `0 errors` / `0 hazards` each. An unparsed summary is a FAILURE, never a
> default pass.

That remains the ideal. It is not being declared satisfied by anything smaller.

## 2. Measured runtime evidence for terminating it

The sweep was started at 08:26:03 and terminated at ~08:36 after measurement
showed it was unbounded on this hardware:

```text
uninstrumented full suite                584 s   (196 tests)
  of which ResidentChannelProduction     515 s   (15 tests, 88% of the time)

memcheck, instrumented
  183 of 196 tests cleared               ~10 min
  ResidentTurbulentSimple.HostVelocitySeedCannotReachTheResidentAnswer
                                          49 s   for ONE test
  ResidentTurbulentSimple.ResidentGpuSolveMatchesTheCpuProductionSolve
                                          >8 min and still running when
                                          progress was measured
```

`ResidentChannelProduction` had only just been reached when the sweep was
stopped. Projecting the observed per-test slowdown across that group gives
45–85 min per tool at a conservative 5–10x, and 2–7 hours per tool at the
16–50x the blocking test suggested. Across four tools the full gate projects to
somewhere between **4 hours and over a day**, and the upper bound could not be
tightened from the data available.

**What the terminated attempt DID establish, and it is kept as evidence:**

```text
memcheck    183 / 196 tests    0 errors
initcheck    60 / 196 tests    0 errors   (turbulence subset)
racecheck    60 / 196 tests    0 hazards  (turbulence subset)
synccheck    60 / 196 tests    0 errors   (turbulence subset)
```

No defect was found before termination. The sweep was stopped for cost, not
because it was failing.

## 3. Why exhaustive sanitizer repetition is disproportionate

A sanitizer finds defects in **kernels**: an out-of-bounds access, a
shared-memory hazard, a read of uninitialised device memory. These are
properties of the kernel and its launch configuration, and memcheck/racecheck
report them on the execution in which they occur — not after N repetitions.

`ResidentChannelProduction` is 88% of the suite's runtime because it runs
multi-hundred-iteration channel solves. Those solves launch the same kernels
thousands more times than the focused tests do. The marginal defect-finding
value of the ten-thousandth `strainRateSquaredKernel` launch, on the same
grid with the same indexing, is very close to zero; the marginal cost under
racecheck is hours.

**This argument is NOT accepted on its own.** It is the reason to look, not the
evidence. Section 4 replaces it with a measurement.

## 4. Exact new scope

```text
DeviceTurbulenceAlgebra.*        strain rate, P_k, mu_t
DeviceKTransport.*               k transport
DeviceEpsilonTransport.*         epsilon transport, imposed wall cells
DeviceKEpsilonCorrection.*       one complete correction
DeviceScalarTransportSemantics.* under-relaxation, imposed rows
DeviceEffectiveViscosity.*       mu_eff on device
DeviceWallFaceViscosity.*        sparse wall-face override
DeviceMaxAbsDifference.*         the device convergence reduction
ProductionKEpsilonBackend.*      the production KEpsilonModel path
ResidentKEpsilonEntry.*          resident entry point
ResidentWallExchange.*           compact CPU wall exchange
ResidentTurbulentSimple.*        resident SIMPLE turbulence integration
KrylovRestartParity.*            Krylov restart parity
GpuBiCGSTABRestartTest.*         restart contract, breakdown reachability
TurbulenceBackendTrace.*         per-operation provenance
GpuJacobiTest.*                  Jacobi preconditioner apply
```

`GpuJacobiTest.*` was NOT in the first draft of this scope. It was added
because the coverage measurement demanded it, and that sequence is the point of
the measurement:

```text
first scoped trace    43 kernels
full-suite trace      44 kernels
difference            { jacobiApplyKernel }   NOT empty
```

`jacobiApplyKernel` is the Jacobi preconditioner apply. The turbulence solves
in the scoped set do not select Jacobi, so no turbulence test reached it --
but `PreconditionerType::Jacobi` is a selectable production option on the CUDA
linear-solver path, which requirement 3 of the authorization explicitly
includes. It was added rather than waived, and the scope was re-traced.

Had the scope been justified by argument instead of measurement, this kernel
would have been missed: every plausible story about "the turbulence tests cover
the linear solver" is true and would still have left it unchecked.

**Excluded:** `ResidentChannelProduction.*` — and only if section 5's
measurement shows it launches no kernel the scoped set misses.

Excluded from the SANITIZER only. Its normal unsanitized regression coverage
remains mandatory and is unchanged; it runs in every `regress_both.sh` pass.

## 5. Required evidence for the amended gate

1. A kernel coverage matrix built by **measurement**, not inspection: every
   CUDA kernel launched anywhere in `CFDGpuTests`, and which scoped test
   launched it. Produced by `tools/kernel_tracer.c`, an `LD_PRELOAD`
   interposer on `cudaLaunchKernel` resolving names via `cudaFuncGetName`
   (official CUDA 12.3+ API). No production source is modified.

   Neither nsys, ncu nor CUPTI is installed on this machine, and
   compute-sanitizer filters kernels but will not enumerate them, which is why
   the tracer exists.

2. `kernels(full suite) \ kernels(scoped set)` must be **empty**, or every
   kernel in the difference must be added to the scope until it is.

3. All four tools clean on the scoped set, with unparsed summaries failing.

4. Non-vacuity controls per tool, with any tool whose detection cannot be
   demonstrated reported as such.

## 6. Limitations this amendment introduces

Stated plainly, because a scoped gate that is described as an exhaustive one is
worse than either.

1. **Repetition count is no longer covered.** A defect that only manifests
   after many launches — a slow leak, an allocator exhaustion path, a counter
   overflow, an accumulating corruption — would be less likely to be caught.
   Kernel-name coverage does not address this, and nothing here claims it does.

2. **Long-run grid/state combinations are not sanitized.** The scoped tests
   exercise the kernels, but `ResidentChannelProduction` reaches convergence
   states, Krylov restart sequences and mature turbulence fields that the short
   tests reach only partially. Section 5's matrix measures KERNEL coverage, not
   state-space coverage. Requirement 5 of the authorization (geometry,
   indexing, boundaries, grid size, imposed cells, wall cells, convergence
   state, Krylov restart) is addressed by explicitly including tests that
   exercise those conditions — but "exercised" is weaker than "exhaustive".

3. **memcheck cannot see small overruns.** Established by the Step-5 control: an
   8-byte overrun past a 64-element `cudaMalloc` was NOT reported, while a
   4096-element overrun was. The allocation is padded and a small overshoot
   lands inside the padding. This limitation is independent of the amendment
   and applies to the original gate equally.

4. **synccheck detection was never demonstrated.** See the gate document. A
   clean synccheck result carries less weight than the other three.

5. The excluded group's CUDA paths are covered **as kernels**, by measurement.
   If a future change gives `ResidentChannelProduction` a kernel of its own,
   the matrix must be regenerated — it is not a one-time argument.

## 7. What this gate may and may not be called

```text
FULL NORMAL REGRESSION      exhaustive      every test, unsanitized
                                            regress_both.sh p16, both configs
CUDA SANITIZER REGRESSION   coverage-driven scoped set + measured kernel
                                            coverage matrix
```

The two are reported separately and must never be conflated. The CUDA
diagnostics gate is NOT marked PASS on the strength of "60 tests passed"; it is
marked PASS only with the amended scope, the coverage matrix, all four tools,
and the limitations above recorded.
