# P16 Step 5 — the frozen final GPU-equivalence gate

This is written BEFORE the campaign runs. Nothing below may be relaxed to
obtain a pass; a criterion that turns out to be wrong is amended in writing,
with the reason and the measurement that exposed it, as P16 has done four times
already.

`* [x] GPU equivalence` is ticked only if every gate below passes.

---

## Scope of the claim

```text
IN    k-epsilon turbulence on the GPU, 2D, through the resident production
      SIMPLE path: strain rate, production, k and epsilon assembly and solve,
      mu_t, the device reductions and the sparse wall-face viscosity override.
OUT   k-omega SST (deferred to P20, checkbox stays unchecked)
OUT   3D turbulence (the eddy-viscosity framework is 2D-only by construction)
OUT   the host wall treatment (recorded P16 scope decision; WallTreatment = Cpu
      is expected and allOnGpu() must stay false)
```

A gate that passes says something about what is IN scope only. This is stated
first because the failure this phase kept repeating was a claim true of what
was measured and broader than what was measured.

---

## S5-1 — CUDA diagnostics

**Criterion.** `compute-sanitizer` over `CFDGpuTests`, four tools, with
`0 errors` / `0 hazards` each. An unparsed summary is a FAILURE, never a
default pass.

### Instrument validity — established before the gate, not after

A tool reporting "0 errors" because it instrumented nothing is indistinguishable
from one reporting "0 errors" on clean code. So each tool was first required to
detect a deliberate planted defect (`scratchpad/sanitizer_control.cu`,
standalone — no defect is introduced into CFDApp):

```text
memcheck    DETECTS    Invalid __global__ write of size 8 bytes
initcheck   DETECTS    ERROR SUMMARY: 64 errors
racecheck   DETECTS    1 hazard, correct file:line for both accesses
synccheck   NOT DEMONSTRATED
```

Three findings came out of that dry run, and they change how the results must
be read:

1. **memcheck missed an 8-byte overrun.** Writing one element past a 64-element
   `cudaMalloc` was NOT reported; overrunning by 4096 elements was. The 512-byte
   request is padded, and a small overshoot lands inside the padding. That is a
   property of the allocator, not of the checker — but it means a clean memcheck
   does not prove the absence of small out-of-bounds accesses.

2. **synccheck detection could not be demonstrated.** Warp-aligned divergence
   (`if (i < 32) __syncthreads()`) went unreported, which is correct on Volta+
   independent thread scheduling. A barrier in a per-thread-trip-count loop
   deadlocked the kernel and had to be killed. So a clean synccheck result here
   carries LESS weight than the other three, and is reported as such rather
   than counted as a fourth validated tool.

3. **The repository's own sanitizer script has a parse defect.**
   `results/gpu-pipe-001/phase2-reductions/tools/gate3_sanitizer.sh` matches
   `RACECHECK SUMMARY: [0-9]+ hazards displayed \([0-9]+ errors`. With exactly
   one hazard the tool prints the SINGULAR form — `1 hazard displayed (1 error,
   0 warnings)` — which does not match, so `errs` is empty. The script treats an
   unparsed summary as failure, so it fails CLOSED and is not dangerous, but it
   would report "could not parse" instead of "1 hazard". That file is preserved
   phase evidence and is NOT edited; the Step-5 tool handles both forms.

**Pass condition.** 0 errors from memcheck, initcheck and racecheck; synccheck
reported with its caveat. Any unparsed summary fails.

## S5-2 — CPU/GPU equivalence, breadth

Step 4 qualified ONE family: Re_tau = 590, k-epsilon, wall functions on. The
checkbox claims the scope above, so the campaign must cover what Step 4 did not.

**Criterion.** For each configuration below, the GPU arm must satisfy the frozen
physical benchmark independently, and the CPU/GPU field differential must meet
the unchanged `1e-6` relative-L2 bound:

```text
Re_tau = 590, wall functions      already PASS in Step 4, re-run on the final tree
Re_tau = 180, wall functions      NOT YET RUN on the GPU arm
no-wall-function path             NOT YET RUN on the GPU arm
```

The `1e-6` bound is the one already adopted across P16 and GPU-PIPE-001. It is
not re-derived here, and it is not relaxed for the new configurations.

**Non-vacuity.** Each configuration must report `turbulenceGpuPathQualified()
== true` with per-operation execution counts equal to the iteration count, and
the CPU arm must report zero executions — the control that caught the
process-lifetime trace defect in Step 4.

## S5-3 — Determinism

**Criterion.** The GPU turbulent path is bitwise reproducible across
independent processes: two separate runs of the same case produce identical
`tau_w`, `Re_tau` and `u+ Linf` in all 17 printed digits.

Bitwise, not tolerance-based. A reduction whose result depends on block
scheduling would show here.

## S5-4 — Transfer and residency re-verification

**Criterion.** On the final tree, unchanged from Step 4:

```text
H2D is O(N_wall), not O(N)      equal bytes across a doubled cell count
largest single D2H              <= one cell field
field-sized D2H copies          exactly 3 per correction (k, epsilon, mu_t)
steady-state allocations        0
steady-state reallocations      0
```

## S5-5 — Performance

**Criterion: MEASURED AND REPORTED. No speedup is required to pass.**

Rule 14 forbids claiming a performance improvement without equivalent
numerical solves; it does not require the GPU to be faster, and on P16's
benchmark meshes it is not. The measured figure on the Step-4 channel is
roughly 18x SLOWER than CPU (~356 s vs ~20 s per sweep), which is what launch
overhead does to 64xN meshes.

So this gate fails only if the measurement is not taken, or if a speedup is
claimed that the numbers do not support. A turbulent GPU path that is slower
at benchmark scale is an honest recorded result, not a gate failure — the
checkbox says "equivalence", not "faster".

## S5-6 — Full regression, both configurations

**Criterion.** `regress_both.sh p16` — the repository's own instrument, with
its own exclusions and its 7200 s timeout — green in `build/cuda` and
`build/final`, on a tree proven current by a second build reporting no work.

A bare `ctest` with a hand-written `-E` is NOT acceptable evidence. One was run
during Step 4's closeout and came back red with four P20 known-expected
failures, exactly as that script's header warns.

## S5-7 — The checkbox

Ticked only if S5-1 through S5-6 pass. The tick is accompanied by a written
scope note stating what it does and does not assert, in the form the other P16
checkboxes already use.

---

## Stopping rule

Stop at the first failed gate. Do not proceed, do not weaken, do not re-scope
to route around it. Preserve the failure.

---

# AMENDMENT A1 — S5-2, the no-wall-function configuration

**Raised before the gate was run, not after it failed.**

S5-2 as frozen requires, for EACH configuration, that "the GPU arm must satisfy
the frozen physical benchmark independently" AND that the differential meets
`1e-6`.

For the no-wall-function k-epsilon path there IS no adopted physical benchmark.
That path is the PRE-P16 BASELINE — `E_Re_tau = 0.333` on 64x16 at y+ 7.5 — and
the entire point of P16's wall-function work was to improve on it. No gate was
ever derived for it, deliberately: `WallFunctionChannel.WallFunctionsImprove
OnThePreP16Baseline` reports it rather than asserting a threshold, because the
frozen methodology sets thresholds AFTER the sweep that measures them.

Requiring the GPU arm to "satisfy the frozen physical benchmark" there would
mean inventing a threshold during qualification, which is the one thing a
frozen gate exists to prevent.

**Amended criterion for the no-wall-function configuration only:**

```text
the GPU arm reproduces the CPU arm's own result to the adopted 1e-6 bound
```

CPU numerics are the reference, so on a path with no independent physical gate,
reproducing the reference IS the equivalence claim. The other two
configurations are unchanged and still carry their adopted physical gates
(Re_tau 590: 0.062 / 1.6; Re_tau 180: 0.22 / 3.9).

This amendment WEAKENS nothing: no threshold is relaxed, and no configuration
that had a gate loses it. It removes a requirement that was never satisfiable
for a path that has no gate to satisfy.

---

# AMENDMENT A2 — S5-1, exhaustive sanitizer sweep → coverage-driven scope

**Authorized.** The S5-1 criterion above ("compute-sanitizer over CFDGpuTests,
four tools") was measured to be unbounded on this hardware: the full sweep
projects to between 4 hours and over a day across four tools, dominated by
`ResidentChannelProduction`, which is 88% of the suite's runtime and had only
just been reached when the sweep was stopped.

It is replaced by a coverage-driven scoped set whose kernel coverage is
MEASURED, not argued. The original criterion is preserved verbatim, the
terminated attempt's output is preserved, and the limitations the amendment
introduces are recorded.

Full text, evidence, scope, coverage matrix and limitations:
[step5_cuda_diagnostics/AMENDMENT.md](step5_cuda_diagnostics/AMENDMENT.md)

The terminated sweep found NO defect before it was stopped — memcheck cleared
183/196 tests at 0 errors. It was stopped for cost, not for failure.

Reporting rule fixed by this amendment:

```text
FULL NORMAL REGRESSION      exhaustive      regress_both.sh p16, both configs
CUDA SANITIZER REGRESSION   coverage-driven scoped set + kernel coverage matrix
```

These are never conflated.
