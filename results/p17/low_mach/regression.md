# P17.6 — regression

Frozen execution policy, copied verbatim from P13's G10 closeout and unchanged since:
`OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200`, five exclusion patterns matching 13 tests.

```text
gate integrity   frozen f275c4ac3fab31509b9f80036e1c8b4b6f4f3e8c9ec20cf8b8ae7a1d8b303c9a
                 live   f275c4ac3fab31509b9f80036e1c8b4b6f4f3e8c9ec20cf8b8ae7a1d8b303c9a
                 MATCHES the freeze -- recomputed at run time, not asserted from a constant
log              logs/50_p17_6_regress_mandated.log
```

---

## 1. THE RESULT — every frozen prediction matched exactly

```text
                   registered            executed              failed   Not Run   runtime
Release+CUDA       2657 = PRED 2657      2595 = PRED 2595      0        0         42.8 min
CPU-only           2455 = PRED 2455      2395 = PRED 2395      0        0         30.8 min
exclusions         13 = PRED 13 in both configurations
P17.6 rows         15 = PRED 15 in both configurations
COUNT VERDICT      MATCHES the frozen prediction, both configurations
```

The predictions were pure arithmetic on the P17.5 close, declared in the script before it ran:

```text
Release+CUDA   2642 + 15 = 2657 registered     2580 + 15 = 2595 executed
CPU-only       2440 + 15 = 2455 registered     2380 + 15 = 2395 executed
```

P17.6 changes no production source, so nothing else was permitted to move — and nothing did. A test
count is an arithmetic consequence of the suite, not a tolerance.

## 2. B8 — the code fingerprint did not move during the run

```text
P17_CODE_FINGERPRINT before : cf4d53a2e6b090228f68d58bc9b452d324abd3dde03539a53a70f44542c70bdf
P17_CODE_FINGERPRINT after  : cf4d53a2e6b090228f68d58bc9b452d324abd3dde03539a53a70f44542c70bdf
                              IDENTICAL
```

This is the fingerprint's only pass/fail use. It *differs* from the P17.5 close
(`d2e65555…`) because P17.6 added a test file and a CMake line — that is expected, and the value is
an identifier, not a verdict.

## 3. Section 8 step 3 — staleness, on a REBUILT instrument

```text
Release+CUDA   47 built test binaries   300 objects inspected, 300 mapped to a repo source, 0 generated
CPU-only       46 built test binaries   280 objects inspected, 280 mapped to a repo source, 0 generated
both           stale vs production 0    stale vs its own sources 0    unverifiable 0
               1 external tool excluded and NAMED (/usr/bin/cmake, 2024-02-03)
```

Attempt 1 of this run **FAILED CLOSED** and executed no tests. It was right to stop and wrong about
why: the per-directory heuristic accused the untouched `CFDLowMachRegressionTests`, which compiles
only `test_low_mach_regression.cpp` and is 9.6 days newer than it. Preserved verbatim in
`logs/PRESERVED_invalid_perdir_staleness/` with `WHY_INVALID.md`; the replacement and its non-vacuity
proof are in `AMENDMENT_regression_probes.md` §5 and `logs/06_staleness_probe.log`.

```text
The "300 mapped / 0 generated" row is the part that makes the PASS meaningful: every object resolved
to a real repo source, so there are no silent skips hiding behind it.
```

## 4. B4 — registration is not execution

```text
P17.6 rows that PASSED                      15 / 15   in BOTH configurations
of which the post-freeze A3 addition:
  RelativePressureDifferenceFallsToItsConvergenceFloorAndStaysThere
    Release+CUDA  Test #2592  Passed  32.37 sec
    CPU-only      Test #2390  Passed  30.42 sec
```

That row matters most: it is the test written *after* the freeze to cover criterion A3, which the
coverage map found had no assertion behind it. Confirming it executed — rather than merely being
registered — is the point of B4. The probe that reports it had to be corrected first: it was still
grepping for P17.4's test names, so it printed nothing and looked clean
(`AMENDMENT_regression_probes.md` §4).

## 5. B6 — CPU-only carries no CUDA dependency

```text
build log read                    : 50_cpu_only_build.log (1 lines)
nvcc invocations in the build log : 0
libcudart references              : 0
cuda/libcfdcuda.a present         : no
build/qual-cpuonly/apps/cli/cfdapp  cudart refs in ldd: 0
```

**Stated honestly: the first two rows are weak evidence in this run.** The build log is one line,
`ninja: no work to do.` — everything was already compiled, so "0 nvcc invocations" is nearly vacuous.
The claim is carried by the two *structural* probes: `libcfdcuda.a` does not exist in that tree, and
the linked CLI binary has zero `cudart` references by `ldd`. Those do not depend on what the build
happened to do this time.

This is visible only because the probe now prints the log it read and its line count. Before this
phase it read `10_cpu_only_build.log`, a file the script never writes, and `grep -ac` on a missing
file prints 0 — the same answer a clean build gives. It had been reporting "clean" without reading
anything, and had been carried as debt since P17.5. See `AMENDMENT_regression_probes.md` §3.

## 6. The stale prediction that was deliberately NOT corrected

```text
IdealGasEOSTest rows : 30   PREDICTED 21 = 12 existing + 9 from P17.2
                            -- reported as STALE in both configurations, for the FOURTH phase running
```

The constant is obsolete: the real count has been 30 since P17.3. The **instrument** is not — those
30 rows are a live preservation detector and all 30 passed, by name, in both configurations. An
obsolete constant attached to a working detector is fixed by correcting the constant, not by deleting
the probe; correcting it mid-run would edit a frozen prediction, so it is reported as stale and
carried as debt. `debug_gui`'s predicted counts (2670/2608) are stale for the same reason and that
stage is not run here.

## 7. Configurations NOT run

```text
build/qual-debug (Debug + GUI)   NOT RUN -- deferred to P17.11 by standing decision. Its predictions
                                 were last maintained at P17.4 and are stale by construction; the
                                 script says so rather than guessing at them.
build/final                      NOT a qualified tree (CUDA=OFF) and never used for a gate result.
```

## 8. Repository integrity, run last

`logs/20_integrity.log`, after every control had restored and the regression had finished.

```text
HEAD unchanged 3bd32f91..., branch main, 0 stashes -- nothing committed, pushed, reset or stashed
frozen documents            all THREE match their freeze hashes
upstream files              17 UNTOUCHED, 0 TOUCHED, by SHA-256
surviving control markers   0, with the pattern proved to match a synthetic marker (so 0 means
                            absent, not blind) and the scan widened to cover tests/
harness sha256              01838fd3... = the controls baseline
clang-format                exit 0
harness duplicates nothing  0 hand-rolled EOS, rho_ref derived from the EOS (1 site), 0 hard-coded
                            rho_ref literal, P17.5's conservedTotalEnergyField and
                            recoverPrimitiveFields each reused once
P17.6 delta                 1 new file (untracked), 1 tracked CMakeLists +19 -0, 15 TESTs (derived)
stray artefacts             0 __pycache__ under the evidence tree
```

**The integrity check found a real violation of my own:** `audit.md`, a frozen document, had been
edited 174 seconds after it was frozen. It is restored, the change is proved to have been the only
one by reverse-applying it to the frozen hash bit for bit, and the whole episode is reported in
`AMENDMENT_audit_post_freeze.md`. The gate itself was unaffected and independently verified twice.
