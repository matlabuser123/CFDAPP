# P17.5 — regression

The execution policy, the prediction declared before the run, and the measured result.

---

## 1. The frozen execution policy — inherited, not chosen

Copied verbatim from `results/p13/final-qualification/2026-09-24/g10-closeout/tools/G10_regress.sh`
via P17.4's harness. Changing any part of it would make the counts incomparable with P17.0–P17.4.

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 frozen exclusion patterns>
  Debug+GUI, when run:  --timeout 14400
```

```text
the 5 exclusion patterns, unchanged since P13:
  SSTAutomaticWallChannel
  SSTCompositeWallChannel
  SSTMomentumFluxChannel
  WallArchitectureDiagnostic
  DriverSeegmiller.CorrectedMeshSequence
-> 13 registered tests match them
```

```text
build/qual-final     Release + CUDA + OpenMP     the P13 qualification tree
build/qual-cpuonly   Release, CUDA OFF
build/qual-debug     Debug + GUI                 deferred to P17.11 by standing decision
```

`build/final` is **not** one of these — it has CUDA=OFF and is not the P13 qualification tree.

## 2. The prediction, declared before the run

P17.5's gate froze the baseline as "Release+CUDA **2608** registered / **2546** executed, plus new"
and "CPU-only **2406 / 2346**, plus new". The phase implemented **34** tests, all in
`tests/unit/compressible/test_total_energy.cpp`:

```text
                registered            executed
              baseline  predicted   baseline  predicted
release_cuda    2608      2642        2546      2580
cpu_only        2406      2440        2346      2380
debug_gui       2669      2703        2607      2641     (not run this phase)
```

A test count is an arithmetic consequence of the suite, not a tolerance. No threshold, tolerance,
iteration requirement or negative control moved in either direction.

**Unlike P17.4, this prediction did not have to be revised.** P17.4's freeze predicted +9 and the
phase implemented +15, because five frozen criteria turned out to be measured by nothing and needed
new tests mid-phase. Here the coverage map (`coverage_map.md`) was built against the frozen criteria
*before* implementation, so the test count was known once and stayed put.

## 3. Staleness — fail-closed

```text
release_cuda   46 built test binaries   all postdate the newest production source
cpu_only       45 built test binaries   all postdate the newest production source
```

The check derives its binary set from `command[0]` of every test ctest has **registered**, not from
`find -executable` — `/mnt/c` drvfs reports every file as mode 0777, so a `find` matches object files
and vendored headers too. It also has a separate clause for test sources in a binary's own directory,
because `clang-format -i` can make a test file the newest file in the tree. Both clauses were earned
by earlier failures and are inherited here rather than re-derived.

## 4. Result

```text
                  registered  predicted  executed  predicted  failed  passed  runtime
--------------------------------------------------------------------------------------
release_cuda          2642       2642      2580      2580        0     100%   42.2 min
cpu_only              2440       2440      2380      2380        0     100%   33.0 min
```

**Every predicted figure matched exactly.** Both `COUNT VERDICT` lines read
`MATCHES the frozen prediction`, `Not Run` was 0 in both trees, and 0 tests failed.

```text
closing arithmetic, which must balance exactly:
  release_cuda   2642 - 13 excluded - 49 disabled = 2580   measured 2580   BALANCES
  cpu_only       2440 - 13 excluded - 47 disabled = 2380   measured 2380   BALANCES
```

The disabled counts come from ctest's `(Disabled)` marker, printed twice per test (98 and 94 raw
lines -> 49 and 47 tests). Runtimes are WSL wall clock and indicative only.

### B8 -- the fingerprint did not move during the run

```text
P17_CODE_FINGERPRINT before  d2e65555534bde2208cb7affd9caac7c7b4406c3f1cd17dfd473d441b882364a
P17_CODE_FINGERPRINT after   d2e65555534bde2208cb7affd9caac7c7b4406c3f1cd17dfd473d441b882364a
                             IDENTICAL -- the tree was not edited mid-measurement
```

### The P17.5 rows really executed

```text
release_cuda   P17.5 rows that Passed: 34   PREDICTED 34
cpu_only       P17.5 rows that Passed: 34   PREDICTED 34
```

## 5. The B4 execution check — registration is not execution

A count can be right while the new tests never ran, so both configurations enumerate the P17.5 rows
**by name** out of the ctest log:

```text
TotalEnergyStateTest              9 rows
TotalEnergyConversionTest         5 rows
TotalEnergyEquationTest           6 rows
TotalEnergyConservationTest       4 rows
TotalEnergyInvalidStateTest       6 rows
TotalEnergyContractTest           4 rows
                                 34 total, PREDICTED 34
(counted with grep -oE '^TEST\(TotalEnergy[A-Za-z]*Test' | sort | uniq -c, not estimated -- a
first draft of this table had 11 and 7 in the first two rows and still summed to 34, which is
exactly how a wrong breakdown hides behind a right total)
```

## 6. A carried-forward OBSOLETE CONSTANT, distinguished from a failure

The harness prints:

```text
IdealGasEOSTest rows    : 30   PREDICTED 21 = 12 existing + 9 from P17.2
```

Not a P17.5 failure, and not new: P17.3 and P17.4 printed the same 30-vs-21. The constant `21` was
correct when P17.2 wrote it and went stale when later work added EOS tests.

```text
the INSTRUMENT   "enumerate every IdealGasEOSTest row and require it to PASS" -- still a live
                 preservation detector, and it reports 30 rows passing
the CONSTANT     "21" -- obsolete; it describes a suite size that no longer exists
```

Reported as obsolete rather than silently corrected mid-run. It has now been carried through three
phases, which is long enough: fixing it is named in `summary.md` as debt for the P17.6 harness.

## 7. B6 — the CPU-only tree really has no CUDA

```text
nvcc invocations in the CPU-only build log      0
libcudart references                            0
cuda/libcfdcuda.a present                       no
TotalEnergy.cpp compiled                        1   <- THE POINT OF THIS CHECK
TotalEnergyEquation.cpp compiled                1   <- ditto
```

The last line is the point of the check. Both new sources were placed in the **main** list of
`src/CMakeLists.txt`, before the `if(NOT CFDAPP_ENABLE_CUDA)` block, so the CPU-only configuration
compiles them; a file added inside that block is built by no routine gate. This run is what proves
it rather than assumes it.

```text
NOTE on the harness: two of these probes grep logs/10_cpu_only_build.log, a filename inherited from
an earlier phase that this harness never writes (it writes 50_cpu_only_build.log), so they print
BLANK rather than 0 -- a fail-OPEN instrument. P17.4 recorded the same defect. The figures are
re-measured directly from the log that exists, and fixing the stale filename is debt.
```

## 8. B8 — the code fingerprint must not move during the run

`P17_CODE_FINGERPRINT` is recorded before and after. It is a whole-tree state identifier, so it
**must** differ from the P17.4 value (`4e253ce4…`) — P17.5 added code — but it must not move *within*
a regression run, which would mean the tree was edited mid-measurement. That is not hypothetical: it
happened in P17.4 when a patch script rewrote a gate script while it was executing.
