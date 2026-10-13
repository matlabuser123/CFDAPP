# G10 — first regression invocation: INVALID, preserved

Killed after 5 h 38 m with **64 of 2537 tests complete** and 7 Timeouts.
Not a regression. An invalid invocation, and the mistake is mine.

## What was wrong

The frozen invocation policy is, in full:

```text
OMP_NUM_THREADS=1 ctest --test-dir build/qual-final -j 8 --timeout 7200 \
  -E "SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller.CorrectedMeshSequence" \
  --output-on-failure
```

`tools/G10_regress.sh` omitted `OMP_NUM_THREADS=1`. I quoted the
`-j 8 --timeout 7200` half of the policy in `source_fingerprint.md` and missed the
prefix, which is stated in the same amendment I was citing.

## What it cost, as that amendment already measured

`2026-09-24/AMENDMENT_G3.md`:

```text
OMP_NUM_THREADS    unset, so it defaults to nproc = 32
OMP_NUM_THREADS unset   304.95 s wall   TESTS FAILED (hit the 300 s cap)
OMP_NUM_THREADS=1         1.72 s wall   21 tests, all passed
```

A 177x slowdown on one suite. `build/qual-final` has `CFDAPP_ENABLE_OPENMP=ON`, so
`-j 8` with threads unlimited runs 8 tests x 32 threads = ~256 runnable threads on
32 cores. Observed load average: **255.94**.

## How it failed

Seven `CFDBackwardFacingStep` instances occupied 7 of the 8 parallel slots for two
full hours and then hit the 7200 s timeout. Effective concurrency was 1, not 8,
which is why only 64 tests finished in 5 h 38 m.

```text
7 x Timeout          CFDBackwardFacingStep, at exactly 2:00:00
completions          64 / 2537
load average         255.94
```

## Classification

Category 6, **invalid invocation** — the same failure mode as G3's first attempt
(`-j16`, default 1500 s timeout, 250 timeouts, 0 assertion failures), which is
preserved at `2026-09-24/logs/G3_FAILURE_PRESERVED_j16_default_timeout.log`. Timeouts
with no assertion failures are the signature of resource contention, not of broken
code.

No timeout here is reported as a failure, and the 7200 s ceiling was **not** raised
to accommodate the contention. The variable was controlled instead, exactly as
amendment A1 of the benchmark gate argues: control the variable, do not widen the
tolerance.

## Corrected invocation

`OMP_NUM_THREADS=1` added for all three configurations. Also `-j 8` for CPU-only
where the frozen line used `-j16`; both are execution-environment settings, not
test criteria (AMENDMENT_G3.md states this explicitly), and both are strictly more
conservative — they can only remove contention-induced timeouts, never hide a real
failure.
