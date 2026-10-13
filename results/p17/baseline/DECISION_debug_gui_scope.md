# DECISION — P17.0 baseline scope: the Debug+GUI configuration is NOT RUN

Decided 2026-09-26 by the user, on an explicit question. Recorded as a separate file
because `baseline_gate.md` was **frozen before any run** and is not edited after the
fact. `CLAUDE.md` §10: amendments are recorded as separate files.

---

## What changes

```text
id  configuration                     gate status       outcome
-------------------------------------------------------------------------------
B1  Release+CUDA  build/qual-final    MANDATED, RUN     see cuda_regression.md
B2  CPU-only      build/qual-cpuonly  MANDATED, RUN     see cpu_regression.md
B3  Debug+GUI     build/qual-debug    DEFINED, NOT RUN  <- this decision
```

**B3 is `NOT RUN`. It is not `PASS`, not `assumed PASS`, and not "covered by P13".**
No statement anywhere in P17.0 may be read as establishing Debug+GUI behaviour at the
P17 baseline.

## Why

The P17.0 authorization lists exactly two regression items — "Run baseline CPU
regression" and "Run baseline CUDA regression" — and says of the three qualified
configurations only that they should be used "where practical". B1 and B2 are the two
mandated items: B1 is the CUDA baseline, B2 is the CPU baseline. B3 is P13's third
configuration, not a P17.0 checklist item.

Measured cost, from P13's G10 record on this machine with the identical invocation:

```text
Release+CUDA   2160 s   36.0 min
CPU-only       1976 s   32.9 min
Debug+GUI     11850 s  197.5 min   at the amended 14400 s timeout
                                   (AMENDMENT_G10_debug_timeout.md)
```

Debug+GUI alone is ~2.9x the other two combined.

## What makes deferring it defensible — and what does not

Defensible:

```text
the tree differs from G10's qualified state by EXACTLY 4 files, all compressible:
  include/cfd/compressible/EquationOfState.hpp    +24 -0
  include/cfd/compressible/IdealGasEOS.hpp         +5 -0
  src/compressible/IdealGasEOS.cpp                +13 -0
  tests/unit/compressible/test_ideal_gas_eos.cpp +123 -0
none touches GUI, Qt, CUDA or any Debug-only path
G10 measured Debug+GUI at 2550/2550, 0 failures, on the immediately preceding state
the 9 added tests are plain unit tests -- no CUDA, no GUI, no DISABLED_ prefix
B1 and B2 both exercise those same 4 files and the same 9 tests
```

**Not defensible, and therefore not claimed:**

```text
that Debug+GUI would pass. It very probably would. "Probably" is not evidence, and
CLAUDE.md §1.2 forbids recording a result that was not measured. The honest entry is
NOT RUN.
```

## Where B3 becomes mandatory

At **P17.11 final qualification**, where `gates/P17_GATES.md` G10 requires all three
configurations, reported separately, with the Debug+GUI timeout at 14400 s per
`AMENDMENT_G10_debug_timeout.md`. That amendment's own caveat carries forward: the
real headroom there is **1.22x**, not the 1.43x the amendment originally stated, so a
further ~20% slowdown in the three long Driver–Seegmiller tests would exceed the
ceiling.

P17.0 is therefore complete against its own two checklist items, and **not** against
P13's full three-configuration regression definition. That distinction is stated in
`P17_0_SUMMARY.md` rather than left for a reader to infer.

## How to run it later

```bash
bash results/p17/baseline/tools/p17_baseline_regress.sh debug_gui
```

The harness already carries the stage, the 14400 s timeout, the frozen exclusions and
the predicted counts (registered 2621, excluded 13, disabled 49, executed 2559).
Nothing needs to be re-derived.
