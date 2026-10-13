# P17.10 — the CPU-only staleness failure, diagnosed

The first full regression **failed closed** on the CPU-only configuration and never ran its tests.
The failing run is preserved verbatim at `logs/50_p17_10_regress_FAILED_STALENESS.log`. This
records what it was, why it is a false positive, and what was done about it — which was **not** to
change the check.

---

## 1. What the run reported

```text
### cpu_only  (build/qual-cpuonly)
  configure exit: 0
  build exit: 0   (14 s)

--- section 8 step 3: staleness, FAIL CLOSED ---
  built test binaries       : 49
  objects inspected         : 286   mapped to a repo source: 286   generated: 0
  oldest built binary       : 2026-09-28T07:06:13Z
  stale vs its own sources  : 0        <-- the per-binary check PASSED
  stale vs production       : 47       <-- the global check FAILED
  unverifiable (fail closed): 0
  *** STALE OR UNVERIFIABLE BINARIES -- FAIL CLOSED ***

release_cuda had already completed: 2714 of 2714 executed, 0 failed.
```

## 2. The two checks are not the same check

```text
"stale vs its own sources"   compares each binary against the sources CMake says it actually
                             compiles, taken from the per-target object lists. This is the strong
                             check, and the one this project adopted after a directory-glob
                             heuristic was found wrong in both directions. It reported 0, over 286
                             objects, every one of which resolved to a repo source.

"stale vs production"        compares each binary's mtime against the NEWEST production source
                             ANYWHERE under include/, src/ and cuda/. It is a single global
                             timestamp and knows nothing about which binary compiles what.
```

## 3. Why the global check fired, and why it is wrong here

```text
The newest production sources at the time of the run, in order:

    cuda/CMakeLists.txt
    include/cfd/gpu/DeviceDensityBasedSolver.hpp
    cuda/kernels/DeviceDensityBasedKernel.cu

ALL THREE ARE CUDA-ONLY. cuda/ is added by CMakeLists.txt:106 inside `if(CFDAPP_ENABLE_CUDA)`, and
DeviceDensityBasedSolver.hpp carries DeviceBuffer.hpp's "never include from a CPU-only-compiled
file" rule. With CUDA OFF, NO TARGET COMPILES ANY OF THEM. A CPU-only binary built at 07:06 is not
made stale by a CUDA source written at 09:00, because it could not have compiled it either way.

THE CONFIRMING DETAIL, which is what separates a diagnosis from a story: DeviceThermodynamics.hpp
is ALSO new, and it is NOT CUDA-only -- it is host/device and tests/unit/compressible/
test_device_thermodynamics_equivalence.cpp includes it in the CPU build. If the CPU-only tree were
genuinely out of date, CFDCompressibleTests would be the first binary to show it.

    occurrences of "STALE vs PRODUCTION: tests/unit/compressible/CFDCompressibleTests" : 0

It was rebuilt (that is what the 14-second build did) and is current. The 47 flagged binaries are
precisely the ones with no dependency on anything that changed.
```

## 4. What was done about it

```text
NOT DONE: the check was not edited, relaxed, or given an exception for CUDA-only sources. The
frozen execution policy is inherited verbatim from P13's G10 closeout, and "the newest production
source" is a deliberately conservative rule that fails in the SAFE direction. Teaching it which
sources a configuration compiles would be a real improvement and is also exactly the kind of
mid-phase edit to an instrument that this project has a standing rule against.

DONE: build/qual-cpuonly was DELETED and rebuilt from scratch, so every binary postdates every
source, and the check then passes ON ITS OWN TERMS. A clean rebuild is strictly stronger evidence
than an incremental one -- it also proves the CPU-only configuration builds from nothing, which the
incremental build did not.

The full harness was then re-run unchanged, both configurations, into one log. The release_cuda
result in the preserved log and in the re-run should agree; if they do not, that is a finding and
not a rounding.
```

## 5. Recorded for whoever owns the harness next

```text
The global "stale vs production" rule will fire again on ANY phase that adds a CUDA-only source and
then runs the CPU-only configuration without a clean rebuild. It is not wrong to fail closed there,
but it costs a full rebuild each time. The per-binary check already answers the question correctly
and is the one the verdict should rest on; the global rule is now doing little except forcing
rebuilds. That is a judgement for P17.11 or later, with its own gate -- not a change to make while
a regression is running.
```
