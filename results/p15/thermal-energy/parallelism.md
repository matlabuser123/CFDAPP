# P15 — verification parallelism, measured

Reducing wall-clock through parallelism only. **No gate weakened, no tolerance touched, no test
skipped.** Every number below is measured on this machine, not assumed.

---

## The measurements that decide it

```text
nproc (as WSL sees it)            32 logical  (i9-14900HX, 24 physical)
MemTotal (as WSL sees it)         31.2 GiB    -- NOT the host's 64 GB; WSL is capped
CFDAPP_ENABLE_OPENMP              OFF  in build/final, build/debug, build/cpuonly
"#pragma omp" in src/             1 site (src/algebra/SparseMatrix.cpp), inactive with OpenMP OFF
tests calling omp_set_num_threads  0  (only benchmarks/, which are not in the suite)
memory with 4 tests live          612 MiB used, 30 GiB available  -> ~150 MiB per test
```

### The oversubscription risk does not apply here

The obvious hazard with `ctest -j N` is tests that are themselves threaded: N workers x M threads
saturates the machine and results get slower, not faster. **Measured: it cannot happen in this
project's test suite.** OpenMP is compiled out of all three configurations, and the one `#pragma
omp` in the tree is inactive without it. The suite is single-threaded per test.

That is why a higher `-j` is safe here and would not be in a project that shipped threaded kernels
in its tests.

## Where the wall-clock actually goes

```text
                          tests   > 5 s   > 30 s   total serial cost
build/final (Release)      2154      75       24    2330 s   ( 38.8 min)
build/debug (Debug+GUI)    2100     125       76   14342 s   (239.0 min)
```

Debug is **6x** the serial cost of Release -- assertions plus no optimisation. Its six worst:

```text
CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic   796.7 s
NaturalConvectionValidation.GridConvergence                        745.6 s
SIMPLEMMS.DistortedMesh                                            672.6 s
GradedMeshProductionCase.GradedGridConvergence                     590.3 s
PoiseuilleValidation.ProductionGridConvergence                     543.1 s
CompressibleCoupledProductionCaseTest.ExportsCoupledCompressibleFields  443.4 s
```

## Choosing -j

With single-threaded tests the model is simply

```text
ideal wall = max( total_serial_cost / j , longest_single_test )
```

For build/debug (14342 s total, 796.7 s longest):

```text
j = 4    max(3586, 797) = 3586 s   ~60 min      <- what was running
j = 8    max(1793, 797) = 1793 s   ~30 min
j = 16   max( 896, 797) =  896 s   ~15 min      <- adopted
j = 20   max( 717, 797) =  797 s   ~13 min      <- floor
j = 32   max( 448, 797) =  797 s   ~13 min      <- no further gain
```

**One 13.3-minute test sets the floor.** Past j≈20 there is nothing left to win, so the extra
contention buys nothing. `j = 16` is the conservative choice: ~4x the previous setting, half the
logical cores left free, and memory use around 2.4 GiB against 30 GiB available.

Build parallelism is set to `-j 20` separately -- compilation IS memory-hungry per job, but at
~1 GiB per C++ translation unit that is still far inside the envelope.

## What was NOT done, and why

* **No concurrent full suites.** Running `debug` and `cpuonly` at the same time would split the
  same 32 cores two ways and gain nothing: both are floor-bound by their own longest test, not by
  core count. They run in sequence.
* **No raising -j past 20.** The floor is a single test's duration; more workers cannot divide one
  test.
* **No `--repeat`/timeout changes, no test exclusions, no tolerance changes.** The instruction was
  wall-clock through parallelism, and parallelism alone.

## The pyramid, adopted for the rest of P15

Full suites are expensive precisely because they are thorough. They run at gates, not after edits:

```text
change
  -> unit tests                    seconds     ctest -R "Thermal|Transport|Boundary"
  -> targeted integration          seconds
  -> thermal validation suite      ~1 min
  -> affected subsystem regression ~1 min
  -> ONE final complete regression  ~35 min at j=16 (all three configs)
```

Only two complete regressions are planned for all of P15: the pre-patch gate (running) and the
final one. Fix-verify cycles in between use the upper tiers.

## Live measurement at j=16 (Debug suite running)

```text
live test processes     20          (16 workers + ctest + shell)
loadavg                 23.0 / 32   ~72% of logical cores
memory                  784 MiB used, 30827 MiB available
swap                    0 MiB       no pressure
processes in D state    0           not I/O blocked
repo filesystem         9p          /mnt/c via WSL drvfs
```

Two conclusions:

* **Headroom exists for j=20.** 72% utilisation with no memory or swap pressure means the machine
  is not saturated at 16 workers.
* **The repository is on a 9p mount.** That is slow for many-small-file operations and is why
  BUILD time does not scale linearly with `-j`; it is not currently the test bottleneck (0
  processes in I/O wait), but it caps what higher build parallelism can buy.

## Planned j=20 benchmark -- without a duplicate long regression

The instruction is to prefer j=20 if measured throughput improves, without paying for a duplicate
multi-hour run. The Release suite is the right instrument: 2330 s serial, so about 2.5 min at
j=16 -- cheap enough to run three times.

```text
after the pre-patch gate closes, on build/final only:
  ctest -j 16   -> wall
  ctest -j 20   -> wall
  ctest -j 24   -> wall
```

Roughly 8 minutes total to choose the setting for the final full regression, rather than guessing.
Release is floor-bound at 120 s (its longest test), so the measurable range is 146 s (j=16) down
to 120 s -- a small absolute window, which is itself the finding: past j=16 the Release suite has
almost nothing left to give. The Debug suite, floor 797 s, is where any gain would show, and its
model says j=20 reaches the floor exactly.

## Performance record

```text
Host CPU                Intel Core i9-14900HX, 24 physical / 32 logical
WSL visible memory      ~31.2 GiB (host has 64; WSL is capped)
Repo filesystem         9p (/mnt/c)
OpenMP in test suite    OFF -- tests are single-threaded, ctest -j cannot oversubscribe
Build jobs              20   (memory-safe at ~1 GiB/TU; 9p caps further gain)
CTest jobs              16   (baseline; j=20 benchmark pending, see above)
Longest test            796.7 s  CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic (Debug)
Observed bottleneck     the longest individual Debug CFD case -- not cores, memory or I/O
```

## Recorded results

| run | config | j | wall | outcome |
|---|---|---|---|---|
| pre-patch | release_cuda | 4 | ~30 min | 2106/2106 PASS |
| pre-patch | debug_gui | 16 | (measuring) | |
| pre-patch | cpu_only | 16 | (measuring) | |
