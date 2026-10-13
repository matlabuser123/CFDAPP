# P13 Final Qualification — G8, Production Benchmarks: status report

**Result: 13 of 15 criteria PASS. G8 is NOT closed.**

Two findings, both qualification-harness defects, both with live reproducers:

```text
G8-F1  criterion E2 does not refuse a build-configuration mismatch
G8-F2  control NV5 has always passed for the wrong reason
```

Date 2026-09-24 (UTC) / 2026-09-25 local. Tree `3bd32f918656-modified`.
**No production, test or harness source was changed.** `SOURCE_FINGERPRINT_V2` is
byte-identical before and after.

---

## 1. Scorecard

```text
Benchmark driver                    PASS       exit 0 on the complete suite
All required benchmarks             PASS       3 of 3 ran ok
Output isolation                    PASS       0 files touched in any default dir
Regression comparator               PASS       returns a verdict; threshold detects
Severe-regression control           DETECTED   NV2, on the threshold, naming the metric
Loaded-machine refusal              PASS       NV7: the guard is what refuses
Frozen threshold preserved          PASS       1.25x, unchanged
CTest smoke registered              PASS       by name, in all 3 build trees
CTest smoke executed                PASS       26.44 s, Passed
Non-vacuity controls                DETECTED   7 of 7
Control failure reasons             CORRECT for 6 of 7 -- NV5 is G8-F2
Per-control restoration             VERIFIED   3 of 3 files, checked twice
Committed evidence                  UNCHANGED  46 tracked files byte-identical
Filesystem hygiene                  PASS       no strays, no markers
SOURCE_FINGERPRINT_V2               MATCH      before == after

Comparator environment guard (E2)   FAIL       G8-F1
Control NV5 exercises E2's claim    FAIL       G8-F2

G8 -> NOT CLOSED
```

## 2. The suite itself is healthy

Complete suite against `build/qual-final` (the G1 qualification tree, Release +
CUDA ON), on a machine that had settled to loadavg 0.00 before recording began:

```text
cfd_benchmark_cpu                   ok ( 64 s)
cfd_benchmark_cuda_end_to_end       ok (514 s)
cfd_benchmark_gpu_preconditioner    ok ( 68 s)
driver exit status                  0
environment captured                incl. A1's loadavg / loaded keys, loaded: no
14 output files                     all under this gate's directory
tracked evidence after              46 files byte-identical
default output dirs                 0 files modified
```

Output isolation, the CTest smoke entry, the frozen 1.25x threshold and all seven
controls are covered in §5–§7 and pass.

## 3. G8-F1 — E2 does not refuse a build-configuration mismatch

### The measurement

```text
benchmark_20x20   runtime_per_iteration_ms   1.9442 ->  5.1992   2.67x  REGRESSION
benchmark_40x40                              9.0337 -> 21.0391   2.33x  REGRESSION
benchmark_80x80                             38.1676 -> 61.2405   1.60x  REGRESSION
FAIL: 3 severe regression(s) of 3 metrics compared
```

### It is not a production regression

An earlier run in this same gate, same source, same quiet machine, but against
`build/final`, **passed**: 0.87x / 0.86x / 0.82x. The variable is the build tree:

```text
                        committed baseline   build/final   build/qual-final
openmp_enabled          false                false         TRUE
cuda_enabled_at_build   false                false         false
```

`build/qual-final` has OpenMP enabled. The slowdown carries the signature of
small-grid OpenMP overhead — a fixed per-parallel-region cost amortising as the
grid grows:

```text
  400 cells  2.67x
 1600 cells  2.33x
 6400 cells  1.60x
```

No numerical work changed, and the comparator's own metric set is unchanged. This
is a build flag, not a code regression.

### The defect: the comparator cannot see build configuration

`scripts/benchmarks/compare_benchmarks.py` reads only `suite_environment.txt`:

```python
ENVIRONMENT_KEYS = ("cpu", "cores", "compiler", "cfdapp", "loaded")
```

`openmp_enabled` and `cuda_enabled_at_build` live in **`environment.json`**,
written by the benchmark binary. The comparator never opens that file, and
`suite_environment.txt` — written by the driver — carries **zero**
build-configuration keys (verified: `grep -cE 'openmp|cuda_enabled'` returns 0).

So criterion E2 cannot do what the frozen gate says it does:

> **E2** | Comparing across different build configurations is **refused**, not
> silently performed.
>
> *"E2 matters more than it looks: comparing a CUDA build's run against a serial
> one and calling the difference a regression is worse than not comparing at
> all."*

That sentence is an exact description of what this gate just produced. The guard
does catch a different CPU, core count, compiler, version or a loaded run — it
does not catch a different build.

**Why it matters beyond bookkeeping.** Amendment A1 refused to widen the
threshold specifically because a check that fires on non-code variation "ends
with someone disabling the check". A false REGRESSION verdict from a build-flag
difference is that same failure mode arriving through the environment guard
instead of the threshold.

## 4. G8-F2 — NV5 has always passed for the wrong reason

NV5's frozen description:

> **NV5** | Compare a **CUDA build's run against a serial baseline** | E2 must refuse

Its implementation:

```bash
sed -i 's/^cpu     :.*/cpu     : Some Other CPU/' $X/suite_environment.txt
```

It mutates `cpu`, a *machine* key the guard does check. It therefore proves the
guard catches a different CPU — which it does, and NV5 reports DETECTED — while
never once exercising the build-configuration claim in its own title. A control
that cannot reach the half of the criterion it names cannot certify it.

This is the **third** instance of this exact pattern in this gate's history, and
the gate's own evidence documents the first two:

```text
NV2, NV4   reported DETECTED without the comparator reaching the regression
           logic -- the pre-A1 baseline had no `loaded` key, so the environment
           check refused every comparison. Exposed by NV6, fixed in
           controls_rerun.sh.
NV5        reports DETECTED without reaching E2's build-configuration half.
           Exposed here.
```

In all three cases the environment check short-circuited the thing the control
was aimed at. That is a pattern in the gate's design, not three coincidences.

## 5. Controls — 7 of 7 DETECTED, restores verified

Both frozen scripts were run unmodified. `controls_rerun.sh` checks the *reason*
for each detection, which is why it is run alongside `controls.sh`.

```text
NV1  ignore --output-dir            DETECTED  output ignored the requested dir   RESTORE VERIFIED
NV2  inject a 1.60x slowdown        DETECTED  exits 1 naming the regressed metric
NV3  baseline with no metric        DETECTED  comparing nothing is a failure
NV4  delete a case                  DETECTED  reported the missing case, not a skip
NV5  mismatched environment         DETECTED  refused, naming the cpu mismatch   <- see G8-F2
NV6  raise threshold above the hit  DETECTED  exits 0 at 2.0x -- the THRESHOLD
                                              detects, not the plumbing
NV7  remove the load guard          DETECTED  the guard is what refuses a loaded run
                                                                RESTORE VERIFIED
```

NV6 is the sharpest of them: by requiring R4 to **stop** firing when the
threshold is raised to 2.0x, it proves the verdict comes from the threshold rather
than from any incidental plumbing failure. It is also what caught the NV2/NV4
defect historically.

Restoration verified twice, independently — each frozen script checks its own
sha256, and this gate's wrapper kept its own hashes and re-checked afterwards:

```text
benchmarks/BenchmarkOutput.hpp          OK
scripts/benchmarks/run_suite.sh         OK
scripts/benchmarks/compare_benchmarks.py OK
tracked evidence, 46 files              byte-identical
control markers in benchmarks/ scripts/ none
repo-root CSV/JSON strays               none
```

## 6. The driver's first exit 1 is preserved as evidence, not as a mistake

The first invocation pointed the driver at `build/final`, which has
`CFDAPP_ENABLE_CUDA:BOOL=OFF`, so the two GPU benchmarks were never built there.
The driver reported both `MISSING` and exited 1. Its own comment states the
intent:

> *Not silently skipped: a suite that quietly runs fewer benchmarks than it claims
> still reports green.*

That is criterion S1 demonstrated live — a driver that will not report success
over an incomplete suite. The run is preserved in `logs/G8_measure.log` rather
than discarded. The error was mine: the wrong `--build`.

**Two defects in my own wrapper, also recorded.** The first attempt ran
`verify_smoke.sh`'s three rebuilds immediately before the timing measurement,
driving the 1-minute load to **8.38** against a guard limit of **8.00**. The
driver happened to proceed at 7.63, which is luck rather than method, and is
exactly the contamination A1 exists to prevent — two of A1's four anomalous
recordings were taken while builds ran. The re-run (`tools/G8_suite_full.sh`)
builds nothing and waits for load to fall below half the limit first. It recorded
at loadavg 0.00.

`--allow-loaded` was deliberately not used anywhere: it stamps a recording
`NOT COMPARABLE` and would make the comparator verdict worthless.

## 7. Threshold and stated limitations, unchanged

```text
frozen threshold     1.25x   (acceptance_gate.md, and amendment A1 did NOT raise it)
current threshold    1.25x   (compare_benchmarks.py: default=1.25)
match                PASS
```

Carried forward verbatim:

* A regression smaller than 25% is invisible to this check. The tool documents it.
* No benchmark timing assertions in ordinary CTest; the smoke entry runs the
  suite in `--quick` form and asserts nothing about time.
* No shared-runner CI performance gate.
* No portable timing baseline: a baseline recorded on this i9-14900HX is not
  valid for another machine, and E2's environment check is what is supposed to
  stop that comparison being made silently — which is the subject of G8-F1.
* Loaded-machine measurements are refused, not absorbed by a wider tolerance.
* **The committed baseline contains only CPU benchmark output.** It has no GPU
  metrics, so even a full-suite comparison matches only the three CPU metrics. No
  GPU timing was compared, and none is claimed.

## 8. Source fingerprint

```text
V2 before G8   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 after  G8   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

Equal. G8 changed no source.

## 9. What closing G8 requires

A decision on G8-F1 and G8-F2. The minimum honest fix is harness-only:

1. The comparator's environment guard must see build configuration — either by
   reading `environment.json` alongside `suite_environment.txt`, or by having the
   driver copy `openmp_enabled` and `cuda_enabled_at_build` into
   `suite_environment.txt` and adding them to `ENVIRONMENT_KEYS`.
2. NV5 must be rewritten to mutate a **build-configuration** key, so it exercises
   the half of E2 its title names. The existing CPU-mutation case is worth keeping
   as a separate control — it is a real check, just not the one NV5 claims.
3. Re-run the comparison that produced this finding. With the guard fixed it must
   **refuse**, naming the OpenMP mismatch, instead of reporting three regressions.

Neither is a production change. Both need authorization, and neither has been
made. No criterion was weakened, no threshold moved, and the failing comparison
is preserved alongside the passing one as the two halves of the reproducer.

---

# G8 CLOSEOUT — F1 and F2 repaired, 2026-09-26

**G8 → PASS.** Harness-only; no production code, no threshold, no exclusion changed.

## G8-F1 — the environment guard now sees the build configuration

`scripts/benchmarks/compare_benchmarks.py`: added `BUILD_CONFIG_KEYS` and
`load_build_config()`, which reads `environment.json` — the file the guard had
never opened — and extended `check_environments()` to report those mismatches.

`environment.json` was chosen over having the driver copy the keys into
`suite_environment.txt` because **the committed baseline already carries them
there**, so no baseline needed re-recording. Amendment A1 exists because a
re-recorded baseline silently broke every comparison once already.

Verified three ways, because a guard that refuses everything would satisfy the
first test alone while breaking all real use:

```text
1  the reproducer -- OpenMP-ON run vs OpenMP-OFF baseline
     BEFORE:  2.67x / 2.33x / 1.60x  REGRESSION
     AFTER:   exit 2
              "environment mismatch between the two runs:
                 openmp_enabled: baseline False vs current True
               FAIL: refusing to compare runs that are not comparable."
     REFUSED, naming the key.

2  non-vacuity -- a MATCHING configuration must still compare
     suite_current (openmp false) vs baseline (openmp false)
     exit 0, "PASS: 3 metrics compared", 1.04x / 0.99x / 0.97x
     The guard does not refuse legitimate work.

3  absence fails closed -- a run with environment.json removed
     exit 2, "the current run has no readable environment.json, so its build
              configuration cannot be shown to match"
     Absence is not treated as comparable to everything.
```

## G8-F2 — NV5 split so it exercises the criterion it names

`results/p13/benchmarks/tools/controls.sh` (under `results/**`, outside V2):

```text
NV5   mutates openmp_enabled in environment.json   DETECTED -- refused, NAMING openmp_enabled
NV5b  mutates cpu in suite_environment.txt         DETECTED -- refused, NAMING cpu
```

NV5b is the original case, kept: it is a real check of the guard, just not the
build-configuration one NV5's title promised. Each control now requires the
refusal to **name its own key**, not merely to exit non-zero — the discipline that
exposed F2 in the first place.

Pre-split `controls.sh` preserved at `logs/PRESERVED_controls_sh_before_NV5_split.sh`;
the pre-split control log at `logs/PRESERVED_G8_controls_before_NV5_split.log`.

## All eight controls, after the repair

```text
NV1  ignore --output-dir                    DETECTED   RESTORE VERIFIED
NV2  inject a 1.60x slowdown                DETECTED   names the regressed metric
NV3  baseline with no comparable metric      DETECTED
NV4  delete a case                          DETECTED   missing case, not a skip
NV5  mismatched BUILD CONFIGURATION          DETECTED   names openmp_enabled   <- F2 fixed
NV5b mismatched MACHINE                      DETECTED   names cpu
NV6  raise the threshold above the injection DETECTED   the THRESHOLD detects, not the plumbing
NV7  remove the load guard                   DETECTED   RESTORE VERIFIED

tracked evidence   46 files byte-identical
```

## G8-F3 — I overwrote the committed baseline, and had flagged the hazard myself

`controls_rerun.sh` line 20 re-records the committed baseline **in place**:

```bash
bash scripts/benchmarks/run_suite.sh --quick --output-dir $BASE
```

The first G8 control run therefore destroyed the original 2026-09-21 baseline
files (their mtimes became 2026-09-25 08:08). I did not copy them first, despite
having recorded this exact hazard in G7's report two gates earlier.

What survived: the values, in `logs/G8_compare_normal.log` —
`1.9442 / 9.0337 / 38.1676` — and the environment line
`captured: 2026-09-21T22:47:32Z, loadavg 4.04, loaded: no`. What was lost: the
files themselves.

The current baseline (`1.6361 / 7.8336 / 32.4971`) is a legitimate re-recording by
the frozen driver on the same host — that is what `controls_rerun.sh` exists to do
post-A1 — but it is not the artifact P13-007 froze. The `2.67x / 2.33x / 1.60x`
ratios in the F1 finding were computed against the original and are not
reproducible from the current files; the finding does not depend on them, because
the reproducer now refuses before any ratio is computed.

The current baseline is now copied to
`data/PRESERVED_baseline_2026-09-25_0808/` with a README, so the next invocation
cannot destroy it as the first destroyed the original.

## Source fingerprint — an authorized, attributed delta

```text
V2 before the fix   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 after the fix    721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955
```

Attribution, by component rather than by assertion:

```text
tracked diff sha256       2f71bb14...  ->  2f71bb14...   IDENTICAL, no tracked file changed
solver-output sub-hash    3bf622aa...  ->  3bf622aa...   IDENTICAL
untracked ENGINEERING     750012b4...  ->  94a95899...   changed
file counts               112 / 122    ->  112 / 122     unchanged, nothing added or removed

the one file:  scripts/benchmarks/compare_benchmarks.py   mtime 2026-09-26 12:16
```

### Why this does not invalidate G10's regression matrix

No registered test executes the changed file:

```text
CFDAppBenchmarkSuiteSmoke  ->  tests/cmake/RunBenchmarkSmoke.cmake  ->  the benchmark binary
compare_benchmarks.py      <-  called only by run_suite.sh, and only with --baseline
```

Confirmed rather than argued — the smoke test was re-run in all three qualification
trees after the fix:

```text
qual-final     1/1 passed
qual-debug     1/1 passed
qual-cpuonly   1/1 passed
```

So G10's matrix — Release+CUDA 2488/2488, Debug+GUI 2550/2550, CPU-only 2288/2288 —
still describes this tree's behaviour. The qualified fingerprint for the completed
qualification is **721ce161…**, with `8652dda0…` recorded as the value the
regressions were measured at and the delta attributed above.
