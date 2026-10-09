# P13-005 — Diagnostics / logging: acceptance gate

Frozen before any production change, on the tree that closed P13-004.

Stop at the first failure. No threshold weakened; amendments recorded separately. Every baseline
is **measured** and recorded below — never inferred.

---

## The findings this gate is built around

**F1 — the logging framework exists and is unused.** `cfd::core::Logger` has five levels, is
thread-safe, and is called from exactly **2 places** in all of `src/`, `apps/` and `cuda/`
(`LinearSolverFactory.cpp`, `SIMPLE.cpp` — both `warning()`).

**F2 — Info-level output goes to stdout, which is the CLI's report channel.**

```cpp
std::ostream& stream = targetsStderr(level) ? std::cerr : std::cout;
bool targetsStderr(LogLevel level) { return level == Warning || level == Error; }
```

Default level is `Info`. So adding the brief's dozen log points at Info would **interleave
diagnostics with results** on stdout, corrupting a machine-readable report and risking the CLI
tests that assert on it (`CFDAppCliValidCase` matches `"Converged: yes"`).

Measured: stderr is **0 bytes** for every successful run today. Results and diagnostics share one
channel because only the second one is in use.

**F3 — the state is reported but not emitted.** Almost everything the brief wants logged is
already in a struct — `caseDiagnostic`, `gpuDiscretizationFallbackReason`, `SIMPLEStatus`,
`robustness.linearSolverFallbacks`, `checkpointsWritten`, `exportSummary`. A caller that does not
print a field loses it, and a long run shows nothing until it ends.

---

## Group C — channel separation  (DETECTION)

| id | criterion |
| --- | --- |
| C1 | Every log level writes to **stderr**. Results go to stdout; diagnostics never share that channel. |
| C2 | `cfdapp --case X` **stdout is byte-identical** to the baseline corpus, for all 7 cases. |
| C3 | The two existing `warning()` call sites behave exactly as before (they already targeted stderr). |

C2 is the criterion that makes everything else safe, and it is measurable: `tools/cli_output_corpus.sh`
captures stdout and stderr **separately** for 7 cases and is proved reproducible (14 files, 0
differ across two runs).

## Group L — the log points the brief lists  (DETECTION)

Emitted, at a level that does not disturb a default run:

| id | point | level |
| --- | --- | --- |
| L1 | case loading and validation failure | Debug / Error |
| L2 | backend selection, including a GPU→CPU fallback and its reason | Info / Warning |
| L3 | solver start and termination, with the status | Debug / Info |
| L4 | checkpoint written, and resume-from | Info |
| L5 | linear-solver and pressure-correction failure | Error |
| L6 | non-finite state detected | Error |
| L7 | export failure | Error |
| L8 | a completion summary: converged, outer iterations, final residuals, mass imbalance, runtime, backend | Info |

## Group V — verbosity control  (DETECTION)

| id | criterion |
| --- | --- |
| V1 | `cfdapp --log-level <error\|warning\|info\|debug\|trace>` sets the level. |
| V2 | An unknown level is rejected with a message naming the valid ones, not silently ignored. |
| V3 | Level filtering works: a `--log-level error` run emits strictly less than `--log-level debug`. |
| V4 | The **default** run's stderr is unchanged from the baseline — new diagnostics are opt-in. |

V4 is deliberate. A production CLI that suddenly became chatty would break scripts that treat
stderr as "something went wrong". Diagnostics are available on request, not imposed.

## Group P — preservation

| id | criterion |
| --- | --- |
| P1 | The full regression passes: Release+CUDA, Debug+GUI, CPU-only, format. |
| P2 | P13-001..004 gates still pass. |
| P3 | No logging added to a hot loop: the per-iteration path emits nothing at default level. |

P3 matters because the brief asks for "no severe performance regression from logging", and the
cheapest way to guarantee that is not to log per iteration at all.

## Group N — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| N1 | Route Info back to stdout | C1, C2 |
| N2 | Emit one new log point at Info unconditionally | V4 |
| N3 | Compare the corpus against itself | the check must be shown able to fail (it was: an earlier cross-filesystem comparison reported all 14 files differing, which was a broken comparison, not a real difference) |

---

## Dry-run record, measured on the unchanged tree

```text
F1  Logger call sites in src/ + apps/ + cuda/          2        target: the brief's points
F2  levels routed to stdout                            3 of 5   (Trace, Debug, Info)
    stderr bytes on a successful run                   0
F3  log points already reported in a struct            8 of 9   only "solver start" is not
C2  corpus reproducibility                             14 files, 0 differ across two runs
V1  --log-level                                        does not exist
```

Vacuity check: C1 (3 levels on stdout → 0), V1 (absent → present) and L1–L8 (2 sites → the brief's
list) all have measured baselines differing from their targets. C2/C3/P1–P3 are PRESERVATION and
already pass, which is correct for that kind and is **not** counted as an achievement.

## Gate verdict

P13-005 closes only when C, L, V and P pass and every N control fails.
