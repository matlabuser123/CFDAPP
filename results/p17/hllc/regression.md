# P17.7 — regression

Frozen execution policy, inherited verbatim from P13's G10 closeout:
`OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200`, five exclusion patterns matching 13 tests.

```text
gate integrity   frozen 1f4ccd8af57f529e1654b2465df9e1c07124c22e0e8a500c56116372009a6465
                 live   1f4ccd8af57f529e1654b2465df9e1c07124c22e0e8a500c56116372009a6465
                 MATCHES the freeze -- recomputed at run time and FAIL-CLOSED, not a constant
log              logs/50_p17_7_regress_mandated.log
```

---

## 1. THE RESULT — every frozen prediction matched exactly

```text
                   registered            executed              failed   Not Run   runtime
Release+CUDA       2696 = PRED 2696      2634 = PRED 2634      0        0         42.9 min
CPU-only           2490 = PRED 2490      2430 = PRED 2430      0        0         32.3 min
exclusions         13 = PRED 13 in both configurations
P17.7 rows         39 (Release+CUDA)     35 (CPU-only)         both as predicted
IdealGasEOSTest    30 = PRED 30 in both configurations
COUNT VERDICT      MATCHES the frozen prediction, both configurations
```

The predictions are arithmetic on the P17.6 close, declared before the run:

```text
Release+CUDA   2657 + 39 = 2696 registered     2595 + 39 = 2634 executed
CPU-only       2455 + 35 = 2490 registered     2395 + 35 = 2430 executed
```

The two configurations differ by exactly 4 because `tests/unit/gpu` is added only under
`if(CFDAPP_ENABLE_CUDA)`, so the 4 GPU equivalence tests do not exist in the CPU-only tree. That is the
expected asymmetry, predicted rather than discovered.

## 2. The stale constant that is now CLOSED, not carried a sixth time

```text
IdealGasEOSTest rows : 30   PREDICTED 30
```

P17.2 predicted 21; the real count has been 30 since P17.3, and P17.3–P17.6 each reported it as stale
because correcting a frozen prediction mid-phase is not allowed. `p17_7_regress.sh` is a **new** script
carrying a **new** prediction, so this was the right moment to fix it. The instrument is unchanged —
those 30 rows are a live preservation detector and all 30 passed by name in both configurations.

```text
debug_gui's predicted counts (2670/2608) remain stale by construction: that stage is NOT run here and
is deferred to P17.11, which must re-derive them from a real run.
```

## 3. B8 — the fingerprint did not move during the run

```text
P17_CODE_FINGERPRINT before : 1dbc248ee185c0de32cf14a06079ccf60ca3f999b18a95c49c1386b94f5b722f
P17_CODE_FINGERPRINT after  : 1dbc248ee185c0de32cf14a06079ccf60ca3f999b18a95c49c1386b94f5b722f
                              IDENTICAL
```

It differs from the P17.6 close (`cf4d53a2…`) because P17.7 added code. That is expected; the value is
an identifier, and its only pass/fail use is that it must not move *during* a run.

## 4. Staleness — every binary against the sources it ACTUALLY compiles

```text
Release+CUDA   47 built test binaries   302 objects inspected, 302 mapped to a repo source, 0 generated
CPU-only       46 built test binaries   281 objects inspected, 281 mapped to a repo source, 0 generated
both           stale vs production 0    stale vs its own sources 0    unverifiable 0
```

The "302 mapped / 0 unresolved" row is what makes the verdict meaningful: every object resolved to a
real repository source, so nothing was silently skipped.

### Attempt 1 FAILED CLOSED, and it was right to

```text
python3: can't open file '.../results/p17/hllc/tools/staleness.py': No such file or directory
  *** STALE OR UNVERIFIABLE BINARIES -- FAIL CLOSED *** in BOTH configurations; no ctest ran
```

Adapting P17.6's script retargeted `$OUT` without moving the helper it calls. The probe treated a
**missing instrument as a failed check, not a passed one** — the fail-closed property P17.6's
`AMENDMENT_regression_probes.md` was written about, now demonstrated on a fresh phase. Preserved in
`logs/PRESERVED_invalid_missing_staleness_tool/` with `WHY_INVALID.md`. The fix was to copy the checker
in (byte-identical, `sha256 ba89acb6…`); nothing about it changed.

```text
This is the exact mirror image of the P17.6 defect: there, `grep -ac` on a MISSING log printed 0 --
what a clean build also prints -- and reported success without reading anything. Same class of mistake
(a wrong path), opposite outcome, purely because of how the result was consumed.
```

## 5. B4 — registration is not execution

```text
P17.7 rows that PASSED:   39 of 39 (Release+CUDA)     35 of 35 (CPU-only)
of which the two post-freeze additions (authorization sections 26 and 16B, added after the gate froze):
    HllcGalileanTest.AddingAConstantVelocityTransformsTheFluxConsistently        Passed
    HllcAnalyticTest.ZeroVelocityUniformPressureCarriesOnlyPressure              Passed
```

Those two rows matter most: they were written *after* the freeze to close two authorization items the
gate had not enumerated, so confirming they executed — rather than merely being registered — is the
point of B4.

## 6. B6 — CPU-only carries no CUDA dependency

```text
build log read                    : 50_cpu_only_build.log (28 lines)
nvcc invocations in the build log : 0
libcudart references              : 0
cuda/libcfdcuda.a present         : no
build/qual-cpuonly/apps/cli/cfdapp  cudart refs in ldd: 0
```

This run read a genuine 28-line build log, so the two zero counts are attributable rather than the
zeros a missing file produces. That probe was repaired in P17.6 and the repair holds. The structural
probes — no `libcfdcuda.a` in the tree, zero `cudart` references by `ldd` — are the load-bearing ones.

```text
NOTE ON THE CPU-ONLY BUILD: cuda/kernels/DeviceHllcFluxKernel.cu is in the cfdcuda target, which is
only configured when CUDA is ON, and src/compressible/HllcFlux.cpp is in the MAIN source list so it is
compiled in BOTH configurations. The CPU-only build therefore compiles the shared header for the host
and never sees the kernel -- which is why 2490/2430 is 35 more than baseline, not 39. This also means
the CPU-only build is NOT a blind spot for P17.7: the flux layer is fully exercised there.
```

## 7. Configurations NOT run

```text
build/qual-debug (Debug + GUI)   NOT RUN -- deferred to P17.11 by standing decision; its predictions
                                 were last maintained at P17.4 and the script says they are stale
build/final                      NOT a qualified tree (CUDA=OFF) and never used for a gate result
```

## 8. Preservation and repository integrity

`logs/07_fingerprint.log` and `logs/20_integrity.log`, both run after the controls had restored and the
regression had finished.

```text
P13-001 SIMPLE fingerprint   BITWISE IDENTICAL -- 10 cases, 130 lines, 0-byte diff
upstream files               22 UNTOUCHED, 0 TOUCHED, by SHA-256 (incl. Vector3.hpp and both
                             SIMPLE paths)
frozen documents             all FOUR match their freeze hashes, including the independent reference
                             exact_riemann.py -- the generator IMPORTS it, never edits it
HEAD                         3bd32f91... unchanged, branch main, 0 stashes; nothing committed or pushed
control markers              0 in the planted file (see below)
clang-format                 exit 0 on all 5 hand-written files
no duplicate physics         0 pressure formulas, 0 sound-speed formulas, 0 (gamma-1) inversions in
                             the flux layer; 0 wave-speed or star-state formulas in the .cu
section 35                   0 HLLC code references from any SIMPLE path
P17.7 delta                  7 new files (all untracked), 4 CMakeLists edits, 39 TESTs
stray artefacts              0 __pycache__ under the evidence tree
```

### Three integrity findings, all instrument defects rather than code defects

```text
1  clang-format FAILED (exit 74) on HllcFlux.hpp -- a REAL criterion-E5 failure, from over-long
   comment lines. Fixed with clang-format -i. Proven to be formatting only: the token multiset is
   IDENTICAL before and after, 39/39 still pass, and the measured-value diff against the pre-format
   run is EMPTY. logs/HllcFlux.hpp.preformat is kept so that is checkable.
   CONSEQUENCE: HllcFlux.hpp's hash moved from db7d90f5... (the controls baseline) to 98885f49...
   after the controls had finished. That is recorded in the integrity log with its reason rather than
   left as an unexplained mismatch.
2  the marker scan reported 8 surviving '// V<id>' markers. All 8 are P17.5's OWN test-case labels in
   test_total_energy.cpp -- the same contamination P17.5 hit when '// N[0-9]' matched Newton-node
   comments in Diffusion.cpp. The probe is now scoped to the file the controls planted in, the
   authoritative check remains the SHA-256 restore, and the collision is REPORTED rather than hidden.
3  the "no HLLC in src/" probe counted src/CMakeLists.txt, which legitimately names HllcFlux.cpp as a
   source. A build-system listing is not a code dependency; excluded, and the exclusion is printed.
```
