# P13-007 — Production benchmark suite: audit

Performed on the tree that closed P13-006, before any change. Measured on this host, not read out
of the sources.

```text
environment  WSL2 Ubuntu 22.04, gcc 11.4, CUDA 12.9, RTX 5000 Ada (sm_89)
probes       tools/measure_noise.sh
```

---

## B1 — every benchmark writes to a hardcoded path, relative to the working directory

```text
occurrences of "results/performance/..." in benchmarks/     15
of those, configurable in any way                            0
benchmarks accepting an output path argument                 0
```

```cpp
std::filesystem::create_directories("results/performance/baseline");   // benchmark_runner.cpp:249
const std::string outDir = "results/performance/openmp_scaling";       // benchmark_openmp_scaling.cpp:262
const std::string outDir = "results/performance/cuda_end_to_end";      // benchmark_cuda_end_to_end.cpp:384
```

Where a benchmark's output lands is decided by the directory it happened to be launched from. From
the repository root — the obvious place — it lands **on top of committed evidence.**

## B2 — and that evidence is tracked

```text
results/performance/baseline             9 tracked files
results/performance/large_grid_stress   13
results/performance/cuda_end_to_end      5
results/performance/openmp_scaling       4
results/performance/preconditioner       3
                                        34 tracked files a benchmark run can overwrite
```

Not hypothetical. During this session `results/validation/grid_convergence/natural_convection_ra1e3.json`
changed underneath the work — only `runtime_seconds` (14.557→5.856, 60.775→21.999, 99.555→48.292),
every physical quantity, solver status and iteration count identical. Timing noise from a run
writing into a tracked path; reverted. The same mechanism applies to all 34 files above.

## B3 — nothing runs them

```text
benchmarks registered with add_test()          0
references to benchmarks in ci.yml             0
a suite driver script                          none  (scripts/ holds ci/ and windows-release/)
```

Seven benchmark executables are built by every Release configuration and then never executed by
anything. A benchmark that no automation runs decays silently: it can stop compiling meaningfully,
stop measuring what it claims, or start crashing, and nothing notices.

## B4 — no benchmark can fail

```text
sites returning nonzero (return 1 / EXIT_FAILURE) across all 7 benchmarks     0
```

They compute, they write CSV and JSON, they return 0. Unconditionally. So the brief's requirement
of **"no severe performance regression"** is, today, undetectable by any mechanism other than a
person opening a CSV and remembering what it said last month.

This is the finding that makes "benchmark suite" a misnomer: there are benchmark *programs*, and
there is no *suite* — nothing that runs them together, compares them to anything, or reports a
verdict.

## B5 — the recorded baselines describe a different program

`results/performance/baseline/environment.json`:

```json
{
  "build_type": "Release (NDEBUG)",
  "cfdapp_version": "0.1.0-dev",
  "compiler": "11.4.0",
  "cuda_enabled_at_build": false,
  "openmp_enabled": false
}
```

The committed baselines are from **0.1.0-dev**, built with OpenMP **off** and CUDA **off**.
Comparing a current `0.2.0-dev` CUDA+OpenMP build against them would not be a regression test; it
would be comparing two different programs and attributing the difference to a regression.

Any regression detection this item adds therefore needs a **fresh baseline measured on this
machine, with the environment recorded alongside it** — not the committed one.

## B6 — argument handling exists in exactly one benchmark

```text
benchmark_runner.cpp    --large        (adds bigger grids)
every other benchmark   no arguments at all
```

## B7 — how noisy is this machine? (the number every threshold depends on)

Five identical runs of `cfd_benchmark_cpu`, same binary, same host
(`logs/00_noise.log`). Spread is `(max-min)/median`:

```text
wall clock per run        25.32  24.11  24.04  24.42  23.90 s          spread  5.9%

AGGREGATE metrics                                     median      spread
  80x80  runtime_per_iteration_ms                    30.5893       3.5%
  40x40  runtime_per_iteration_ms                     6.8588       5.2%
  20x20  runtime_per_iteration_ms                     1.5030       7.9%

SUB-STAGE metrics (sub-millisecond, near timer resolution)
  80x80  linear_solve_seconds                         0.0018      27.0%
  80x80  pressure_assembly_seconds                    0.0017      30.8%
  40x40  mass_flux_seconds                            0.0001     362.1%
  20x20  mass_flux_seconds                            0.0000      60.1%
```

Two things follow, and they decide the whole design of the regression check:

1. **Aggregate metrics are usable; sub-stage timers are not.** `runtime_per_iteration_ms` is
   stable to 3.5–7.9%, and gets *tighter* as the grid grows. The stage timers measure tenths of a
   millisecond and vary by 27–362% between identical runs — a threshold on `mass_flux_seconds`
   would be a noise detector.

2. **The floor must be dimensional, not a round number.** The noisy metrics are exactly the ones
   whose values sit near the clock's resolution. So the comparison admits a metric only when its
   baseline value is large enough to be measured, rather than trying to pick a tolerance loose
   enough to cover a 0.0001-second quantity.

---

## Dry-run record, measured on the unchanged tree

```text
B1  benchmarks accepting an output path           0        -> all 7
B2  tracked files overwritable by a run          34        -> 0
B3  benchmarks reachable from one entry point     0        -> all 7
B4  benchmarks that can report a failure          0        -> regression detection exists
B5  baseline build identity          0.1.0-dev, no OpenMP  -> measured on this machine
```

Vacuity check: every row has a measured baseline differing from its target, so none can pass for
free.

## What the audit rules out

* **No new benchmark programs.** Seven exist and measure real things. The gap is that nothing runs
  them, nothing compares them, and they write where they should not.
* **No threshold invented from a round number.** A regression threshold not derived from this
  machine's measured run-to-run spread would either miss real regressions or fire on noise. The
  spread is measured first (`tools/measure_noise.sh`) and the threshold quoted against it.
* **No change to what any benchmark measures.** Changing the measurement and then comparing against
  a baseline would make the comparison meaningless.
