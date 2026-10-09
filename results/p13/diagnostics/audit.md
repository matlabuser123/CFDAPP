# P13-005 — Diagnostics / logging: audit

Performed on the tree that closed P13-004, before any change. Every number below was measured.

---

## A1 — a logging framework exists, and almost nothing uses it

`cfd::core::Logger` is a thread-safe process-wide singleton with five levels
(`Trace/Debug/Info/Warning/Error`), no Qt and no JSON dependency, default level `Info`.

```text
Logger call sites in src/ + apps/ + cuda/     2
  src/solver/LinearSolverFactory.cpp          warning()  -- GPU backend requested, unavailable
  src/pressure_velocity/SIMPLE.cpp            warning()  -- GPU discretization declined, with reason
```

So the framework is sound and the coverage is nil. This is the item's real finding: nothing needed
inventing, only wiring — and wiring it carelessly would have broken something, which is A2.

## A2 — Info-level output went to **stdout**, which is the CLI's report channel

```cpp
std::ostream& stream = targetsStderr(level) ? std::cerr : std::cout;
bool targetsStderr(LogLevel level) { return level == Warning || level == Error; }
```

Trace, Debug and Info — 3 of the 5 levels — wrote to stdout. The default level is `Info`. So
adding the brief's log points at Info would have interleaved diagnostics with the run report on one
stream: a machine-readable report corrupted, and `CFDAppCliValidCase` (which matches
`"Converged: yes"`) put at risk.

```text
levels routed to stdout                       3 of 5   (Trace, Debug, Info)
stderr bytes on a successful CLI run          0
```

The two channels were effectively one, because only the second of them was in use.

## A3 — the state is reported but never emitted

Almost everything the brief asks to be logged already exists as a field, returned to the caller:

```text
log points already carried in a struct        8 of 9
  caseDiagnostic                    P13-002
  gpuDiscretizationFallbackReason   SIMPLEResult
  SIMPLEStatus, iterations, residuals, globalMassImbalance, stageSeconds
  robustness.linearSolverFallbacks, statusDetail
  checkpointsWritten, resumedFromIteration     P13-001
  exportSummary
  not carried: solver start (there is nothing to report before the solve runs)
```

A caller that does not print a field loses it, and a long run shows nothing at all until it ends.
The gap is emission, not information.

## A4 — no verbosity control anywhere

```text
--log-level (or any equivalent)                does not exist
Logger::setLevel call sites in apps/           0
```

The level was whatever the singleton defaulted to, with no way for a user to change it.

## A5 — the baseline corpus

`tools/cli_output_corpus.sh` captures stdout and stderr **separately** for 7 cases
(3 valid, 1 non-converging, 3 rejected), normalising the temp path. Proved reproducible before
being used as a baseline:

```text
14 files, 0 differ across two consecutive runs
```

Separately is the point: an instrument that merged the streams could not measure the one thing this
item is about.

## What the audit rules out

* **No new logging framework.** One exists, is dependency-free, and is thread-safe.
* **No per-iteration logging.** The brief asks for no severe performance regression from logging;
  the cheapest guarantee is not to log in the hot loop at all (criterion P3).
* **No change to what the CLI prints.** stdout is a contract (criterion C2).
