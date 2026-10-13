# P17.3 — regression

**PASS. Every frozen prediction matched exactly, in both configurations.**

Raw: `logs/40_p17_3_regress_mandated.log`, `logs/40_release_cuda_ctest.log`,
`logs/40_cpu_only_ctest.log`. Harness: `tools/p17_3_regress.sh`.

---

## 1. Result against the frozen prediction

Predictions were the P17.2 baseline plus exactly twelve, derived before the run: 11 new
`CompressibleContinuityTest` rows and 1 new `CompressibleMassFluxTest` row, all plain unit
tests with no `DISABLED_` prefix and no name matching an exclusion.

```text
                          P17.2 baseline   PREDICTED   MEASURED   verdict
Release+CUDA  build/qual-final
  registered                    2581         2593        2593     MATCH
  matching the exclusions         13           13          13     MATCH
  disabled (unique)               49           49          49     MATCH
  executed                      2519         2531        2531     MATCH
  failed                           0            0           0     MATCH
  runtime                     38.6 min          -      37.3 min

CPU-only      build/qual-cpuonly
  registered                    2379         2391        2391     MATCH
  matching the exclusions         13           13          13     MATCH
  disabled (unique)               47           47          47     MATCH
  executed                      2319         2331        2331     MATCH
  failed                           0            0           0     MATCH
  runtime                     26.6 min          -      28.2 min
```

Both arithmetics close with nothing left over:

```text
2593 - 13 - 49 = 2531     exact
2391 - 13 - 47 = 2331     exact
```

```text
ctest exit status   0 and 0
summary lines       100% tests passed, 0 tests failed out of 2531
                    100% tests passed, 0 tests failed out of 2331
Not Run             0 and 0
```

Debug+GUI was not run, per the standing decision in
`results/p17/baseline/DECISION_debug_gui_scope.md`. It is `NOT RUN`, not `PASS`.

## 2. Integrity — CLAUDE.md section 8

```text
build                     exit 0 (5 s) and exit 0 (61 s)
built test binaries       46 (Release+CUDA), 45 (CPU-only)
stale vs production       0 and 0
stale vs own test source  0 and 0
external tools            1, listed: /usr/bin/cmake (2024-02-03)
cuda/libcfdcuda.a         0a64478a5d1929248ebdbc6026381346aa8697f2df26df41ce9ab5bde6b77a60
```

## 3. Fingerprints

```text
P17_CODE_FINGERPRINT
  P17.2 close         4d08c2367c1d24fca640f2c4d3ec47084cfdc6ba52788972edaf2a19d3704491
  P17.3, before run   437ac0e3d9f09692f6bf415d7b1cce38b65d2c02e0158a346869232ad800a272
  P17.3, after run    437ac0e3d9f09692f6bf415d7b1cce38b65d2c02e0158a346869232ad800a272
                      UNMOVED across the regression -- running tests changed no code
```

Moved from the P17.2 close by the **authorized delta and nothing else**:

```text
tracked modified                     171 -> 174   (+3)
untracked                            107 -> 107   (+0)
tracked engineering files changed     120 -> 123   (+3)
```

The `+3` are `CompressibleContinuity.hpp`, `CompressibleContinuity.cpp` and
`test_compressible_continuity.cpp`. `test_compressible_mass_flux.cpp` was already modified
by P17.1, so extending it does not add a new entry.

## 4. G3 — the P13-001 SIMPLE fingerprint

```text
probe        results/p13/restart/tools/solve_fingerprint.cpp   (unmodified)
invocation   OMP_NUM_THREADS=1
cases        10        lines  130
diff         EMPTY -- 0 bytes, preserved as logs/20_fp_diff.txt
verdict      BITWISE IDENTICAL -- P17.3 moved no bit of the incompressible solver
```

```text
C2  EOS references under src/ and include/cfd/pressure_velocity: 0
```

That is the strongest single statement that P17.3 left the incompressible path alone — and
it is reinforced structurally: `src/physics/ContinuityEquation.cpp` and its header have an
**empty delta**, verified as a gate step rather than asserted.

## 5. B6 — CPU-only has no CUDA dependency

```text
cuda/libcfdcuda.a present                     no
ldd cudart refs in the CPU-only CLI           0
```

The CPU-only build was a **real compile** (61 s), not an incremental no-op, so the
build-log-derived evidence is meaningful here rather than vacuous. The harness's two
log-derived counters print blank because `grep -c` exits 1 on zero matches — a known
reporting defect carried since P17.1, with the values re-measured directly.

## 6. A file with a newer mtime that P17.3 did not change

`src/compressible/CompressibleMassFlux.cpp` carries a post-freeze mtime because control
**N4** wrote to it and restored it. Its content is **byte-identical to its P17.1 state**,
proven two ways:

```text
sha256  2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7
        == the controls' recorded baseline AND the value recorded at the P17.1 close
git diff --numstat HEAD  ->  34  0        i.e. still exactly P17.1's +34 -0
```

So P17.3 changed **four** files; the fifth mtime is an artefact of the control harness.

## 7. Watched for, and clean

```text
incompressible SIMPLE     P13-001 fingerprint BITWISE IDENTICAL; ContinuityEquation
                          untouched
CompressibleSIMPLE        161/161 compressible suite, incl. the MMS and both
                          production-case suites
P17.1 variable density    21/21 CompressibleMassFluxTest
P17.2 EOS                 30/30 + 20/20, unchanged
P14 transport             68/68
P15 thermal              161/161
P16 turbulence           315/315
mass-balance diagnostics  physics::computeMassBalance reused, not replaced
```

No failure appeared anywhere, so nothing had to be classified as related or unrelated.

## 8. What this regression does NOT cover

```text
Debug+GUI          NOT RUN -- deferred by decision to P17.11
sanitizers         NOT RUN. CLAUDE.md section 8 names them as part of the full regression;
                   P17.3 ran Release+CUDA and CPU-only only.
clang-format clean CLEAN on the four files P17.3 changed (gate G6). NOT clean tree-wide:
                   22 pre-existing files fail, none of them P17.3's, and
                   results/p13/final-qualification/ contains ZERO mentions of
                   clang-format. Standing debt, first recorded in P17.1.
GPU equivalence    G8 VACUOUS -- no GPU compressible continuity path exists.
native Windows     WSL2 only, per CLAUDE.md section 4.
```
