# P17.4 — regression

The execution policy, the predictions declared before the run, and the measured result.
Numbers are filled from `logs/50_*_ctest.log` and `logs/40_p17_4_regress_mandated.log`.

---

## 1. The frozen execution policy — inherited, not chosen

Copied verbatim from `results/p13/final-qualification/2026-09-24/g10-closeout/tools/G10_regress.sh`.
Changing any part of it would make the counts incomparable with P17.0 .. P17.3.

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

Three qualified build trees. `build/final` is **not** one of them — it has CUDA=OFF and is not
the P13 qualification tree, a trap this project has already fallen into once.

```text
build/qual-final     Release + CUDA + OpenMP     the P13 qualification tree
build/qual-cpuonly   Release, CUDA OFF
build/qual-debug     Debug + GUI                 deferred to P17.11 by decision
```

## 2. The predictions, and the one that moved

The freeze predicted **+9** tests -- the nine P17.4 planned at the time -- giving
`2602 / 2540` and `2400 / 2340`. **Fifteen** were implemented, so the prediction was revised
to **+15** *before* the run, with the frozen figure kept beside it rather than erased.

```text
                       registered            executed
                  frozen  revised       frozen  revised
release_cuda       2602     2608          2540    2546
cpu_only           2400     2406          2340    2346
debug_gui          2664     2670          2602    2608     (not run this phase)
```

Each of the six extra tests traces to a frozen criterion that was **measured by nothing**:

```text
IsWiredIntoTheRelaxedMomentumAssembly           D3   found when control N6 came back vacuous
DefaultIsDisabledBehaviourally                  D1   same control; D1 had only a header grep
UniformFlowResidualIsAtRoundOff                 C2   found by auditing the gate vs the suite
ConvectiveAssemblyIsGloballyConservative        C4   same audit
NonFiniteAssembledSystemThrowsNumericalError    V3   same audit
SymmetryCarriesZeroNormalMomentumFlux           B3   same audit; no momentum test used
                                                     the Symmetry BC at all
```

A test count is an **arithmetic consequence of the suite, not a tolerance**. No threshold,
tolerance, iteration requirement or negative control was changed, and the revised figure was
still declared before the run and compared after it. The arithmetic is
`P17.3's 2593 / 2531 + 15 = 2608 / 2546` and `2391 / 2331 + 15 = 2406 / 2346`.

## 3. Staleness — fail-closed, and why its shape is unusual

`CLAUDE.md` §8 wants every test binary to postdate the production source it tests. This harness
checks that directly instead of trusting the build step. Two earlier forms of the check were
wrong and are preserved:

```text
attempt 1   find -type f -executable   matched 285 "binaries" including .cpp.o objects and a
            vendored header, because /mnt/c drvfs reports every file as mode 0777.
            -> replaced by command[0] of every test ctest has REGISTERED
attempt 2   flagged /usr/bin/cmake, a 2024 system binary registered as a test command by the
            cmake -P driven CLI tests
            -> built-vs-external binaries are now partitioned and both printed
attempt 3   45 binaries "stale" because clang-format -i had just made a TEST file the newest
            file in the tree
            -> retargeted: newer than the newest PRODUCTION source, plus a separate clause for
               test sources in the binary's OWN directory
```

`clang-format -i` ran again in this phase, so the third clause is load-bearing here.

## 4. Result

```text
                  registered  predicted  executed  predicted  failed  passed  runtime
--------------------------------------------------------------------------------------
release_cuda          2608       2608      2546      2546        0     100%   39.7 min
cpu_only              2406       2406      2346      2346        0     100%   29.8 min
```

**Every predicted figure matched exactly.** Both `COUNT VERDICT` lines read
`MATCHES the frozen prediction`, `Not Run` was 0 in both trees, and 0 tests failed.

```text
closing arithmetic, which must balance exactly:
  release_cuda   2608 - 13 excluded - 49 disabled = 2546   measured 2546   BALANCES
  cpu_only       2406 - 13 excluded - 47 disabled = 2346   measured 2346   BALANCES
```

The disabled counts come from ctest's `(Disabled)` marker, which it prints twice per test
(98 and 94 raw lines -> 49 and 47 tests). Runtimes are WSL wall clock and indicative only;
the Release+CUDA figure of 39.7 min sits below the 41.6 min baseline, so nothing was starved
by a concurrent job this time.

### Staleness, measured

```text
release_cuda   46 built test binaries   all postdate the newest production source
cpu_only       45 built test binaries   all postdate the newest production source
```

`clang-format -i` ran twice in this phase, so the retargeted clause described in section 3 -- the
one that ignores test files outside the binary's own directory -- is what keeps this from a false
"45 stale binaries" verdict.

## 5. The B4 execution check — registration is not execution

A count can be right while the new tests never ran. Both configurations therefore enumerate the
P17.4 rows **by name** out of the ctest log:

```text
DilatationalStressTest                 8 rows
CompressibleMomentumTermsTest          3 rows
CompressibleMomentumConservationTest   4 rows
                                      15 total, PREDICTED 15
```

The P17.2 `IdealGasEOSTest` enumeration is **kept alongside** it. That block is a live
preservation detector — those rows must go on passing — not an obsolete constant, so it was
added to rather than replaced.

## 5b. A carried-forward OBSOLETE CONSTANT, distinguished from a failure

The harness prints:

```text
IdealGasEOSTest rows    : 30   PREDICTED 21 = 12 existing + 9 from P17.2
```

That mismatch is **not** a P17.4 failure and it is not new: P17.3's log
(`results/p17/compressible_continuity/logs/40_p17_3_regress_mandated.log:94`) printed exactly the
same 30-vs-21. The constant `21` was correct when P17.2 wrote it and went stale when later work
added EOS tests. The distinction that matters:

```text
the INSTRUMENT   "enumerate every IdealGasEOSTest row from the ctest log and require it to
                 PASS" -- still valid, still discriminating, and it reports 30 rows passing.
the CONSTANT     "21" -- obsolete. It describes a suite size that no longer exists.
```

So the constant is reported as obsolete rather than either silently updated mid-run or treated as
a regression, and it was deliberately NOT edited while the run was in flight. Fixing it belongs
to the P17.5 harness; it is recorded as debt in `summary.md`.

The P17.4 equivalent is a live prediction and is checked in section 5: 15 rows, predicted 15.


## 6. B6 — the CPU-only tree really has no CUDA

```text
nvcc invocations in the CPU-only build log      0
libcudart references                            0
cuda/libcfdcuda.a present                       no
cudart refs in ldd of apps/cli/cfdapp           0
compressible/DilatationalStress.cpp compiled    1   <- THE POINT OF THIS CHECK
```

**An instrument defect found while reading this output.** The harness's own `nvcc` and `libcudart`
probes printed **blank, not 0**, because they grep `logs/10_cpu_only_build.log` -- a filename
inherited from an earlier phase that this harness never writes (it writes
`logs/50_cpu_only_build.log`). A blank read as "fine" is a fail-open instrument, the same shape as
the L5 probe. The four figures above were re-measured directly from the log that does exist. The
stale filename is recorded as debt rather than patched after the fact.

This matters because a file added inside `if(NOT CFDAPP_ENABLE_CUDA)` is compiled by no routine
gate. `compressible/DilatationalStress.cpp` was deliberately placed in the **main** source list,
before that block, so the CPU-only configuration compiles it too — and this run is what proves
it rather than assumes it.

## 7. B8 — the code fingerprint must not move during the run

`P17_CODE_FINGERPRINT` is recorded before and after. It is a whole-tree state identifier, so it
**must** differ from the P17.3 value — P17.4 changed code — but it must not move *within* a
regression run, which would mean the tree was edited mid-measurement. That is not hypothetical:
earlier in this phase a patch script rewrote a gate script while it was executing.
