# P17.8 — regression

Execution policy **FROZEN**, inherited verbatim from `results/p17/hllc/tools/p17_7_regress.sh`, which
inherited it from P13's G10 closeout. Harness: `tools/p17_8_regress.sh`. Raw log:
`logs/50_p17_8_regress.log`.

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
5 exclusion patterns, 13 tests, unchanged since P13's G10 closeout
build/qual-final (Release+CUDA+OpenMP) and build/qual-cpuonly (CUDA OFF)
build/qual-debug NOT run -- deferred to P17.11 by standing decision
```

The gate's own hash is re-checked at the start of the run and the harness **fails closed** if it has
moved. It had not: `a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e`.

---

## 1. The prediction, declared before the run

A test count is an arithmetic consequence of the suite, not a tolerance, so it is predicted rather
than observed.

```text
P17.7 close:   release_cuda 2696 registered / 2634 executed
               cpu_only     2490 registered / 2430 executed

P17.8 adds 36 tests and no others:
  tests/unit/compressible/test_density_based_solver.cpp            32 TEST macros
  tests/integration/compressible/test_density_based_canonical.cpp   4 TEST macros
Both files are pure CPU, so the delta is IDENTICAL in both configurations:

  release_cuda 2696 + 36 = 2732 registered    2634 + 36 = 2670 executed
  cpu_only     2490 + 36 = 2526 registered    2430 + 36 = 2466 executed
```

## 2. The result — every predicted number matched exactly

```text
                                    release_cuda        cpu_only
------------------------------------------------------------------------------
build                               exit 0, 58 s        exit 0, 63 s
staleness (FAIL CLOSED)             48 binaries         47 binaries
  stale vs production                 0                   0
  stale vs its own sources            0                   0
  unverifiable                        0                   0
registered                          2732  PRED 2732     2526  PRED 2526
matching the exclusions               13  PRED 13         13  PRED 13
IdealGasEOSTest rows                  30  PRED 30         30  PRED 30
DensityBased rows                     36  PRED 36         36  PRED 36
------------------------------------------------------------------------------
ctest exit status                      0                   0
EXECUTED                            2670  PRED 2670     2466  PRED 2466
passed                              100%                100%
FAILED                                 0  PRED 0           0  PRED 0
Not Run                                0                   0
runtime                             2282 s (38.0 min)   1648 s (27.5 min)
COUNT VERDICT                       MATCHES             MATCHES
------------------------------------------------------------------------------
```

```text
NEW FAILURES: none.
KNOWN FAILURES: none -- the 13 excluded tests are the frozen exclusion set, unchanged and not
widened by one entry. No failure was permitted, named in advance or otherwise.
```

## 3. Registration is not execution — the named rows that actually ran

A test can be registered and never executed. These are counted from the ctest log by name.

```text
                                 release_cuda   cpu_only
DensityBased rows Passed              36           36      PREDICTED 36
Hllc rows Passed (P17.7)              39           35      the 4-row difference is the GPU
                                                           equivalence suite, absent with CUDA OFF
IdealGasEOSTest rows Passed (P17.2)   30           30      PREDICTED 30
LowMach rows Passed (P17.6 / P3)      25           25
```

The `Hllc` rows matter here beyond P17.7's own preservation: P17.8 **changed
`include/cfd/compressible/HllcFlux.hpp`** (the star-energy sign correction, see
`AMENDMENT_A6_upstream_exception.md`). All 39 rows including the 4 GPU bitwise-equivalence rows pass
on the corrected source, which is a property of the one-shared-source design — the `.cu` contains 0
formulas, so both paths received the same change from the same line.

## 4. CPU-only has no CUDA dependency

```text
nvcc invocations in the CPU-only build log : 0
libcudart references                       : 0
cuda/libcfdcuda.a present                  : no
```

The check reads the build log explicitly and would fail closed if that file were missing — `grep -c`
on a missing file prints 0, the same answer a clean build gives, which is exactly the fail-open shape
this project has hit before.

## 5. Library hashes (CLAUDE.md §8 step 4)

```text
release_cuda  src/libcfdcore.a   188e50397ed13718f449f369ef996768cc4011ac1048955dc312da92716390f6
              cuda/libcfdcuda.a  fc97ea4e87aec23c385358f347e7589ec47dcf24698cd8afaa6391e3b8c125de
cpu_only      src/libcfdcore.a   1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a
```

```text
libcfdcuda.a's hash MOVES across rebuilds of identical source -- nvcc is not bit-reproducible. It is
RECORDED, never used as an identity check. The fingerprint OUTPUT is the instrument for that, not
the archive hash.
```

## 6. The code fingerprint did not move across the regression

```text
P17_CODE_FINGERPRINT before : 577cb482945440af848b5886f6b3a02d699cb202f557d05b780bad6ef176f8a9
P17_CODE_FINGERPRINT after  : 577cb482945440af848b5886f6b3a02d699cb202f557d05b780bad6ef176f8a9
```

So no PRODUCTION source changed while the regression ran.

```text
SCOPE OF THAT CLAIM, stated because the fingerprint is easy to over-read: P17_CODE_FINGERPRINT
EXCLUDES results/ (and the top-level docs) by construction. It therefore says nothing about the 50
tracked results/validation/** reports this regression DID rewrite -- those are handled separately
and explicitly in section 10, not covered here.
```

## 7. The P13-001 SIMPLE fingerprint

Run separately, against a freshly rebuilt `libcfdcore.a` — `logs/18_fingerprint.log`.

```text
cases 10 / lines 130          matching P13-001's own record
BITWISE IDENTICAL             the diff file is EMPTY (0 bytes) and is preserved as evidence
NON-VACUITY                   a 0.1 % perturbation of the SIMPLE velocity relaxation moves 112 of
                              the 130 lines; restore verified by SHA-256, and the script ABORTS if
                              the restore does not match
```

## 8. What this regression does NOT cover

```text
Debug+GUI          NOT RUN -- deferred by decision to P17.11. Same standing decision every P17
                   phase since P17.0 has recorded.
sanitizers         NOT RUN. CLAUDE.md section 8 names them as part of the full regression; P17.8 ran
                   Release+CUDA and CPU-only only, as P17.1 through P17.7 each did.
clang-format       CLEAN on every file P17.8 added or edited. NOT clean tree-wide: 22 pre-existing
                   files fail, none of them P17.8's. Standing debt, first recorded in P17.1.
                   logs/20_format_verified.log lists them and attributes each.
GPU equivalence    G8 NOT IMPLEMENTED for the SOLVER -- there is no GPU density-based path. The
                   FLUX layer's bitwise equivalence is P17.7's and was re-verified here (4 GPU rows).
native Windows     WSL2 Ubuntu-22.04 only, per CLAUDE.md section 4. This evidence does not establish
                   native Windows CUDA.
shock validation   G7 NOT CLAIMED. P17.9 owns it.
```

## 9. Environment actually used

```text
host        Intel i9-14900HX, 64 GB, NVIDIA RTX 5000 Ada Laptop (CC 8.9), Windows 11 Pro
linux       WSL2, Ubuntu-22.04    nproc 32    free -h shows 31 GiB visible to WSL
build jobs  16 (policy: 16-24, never all 32 logical processors)
ctest       -j 8, OMP_NUM_THREADS=1, --timeout 7200
nvcc        on PATH from /usr/local/cuda-12.9/bin
timings     WSL wall clock, recorded as indicative only -- CLAUDE.md section 7 notes the clock jumps
```

## 10. Runtime-noise audit (CLAUDE.md §7)

A full `ctest` run rewrites tracked `results/validation/**` reports with timing-only changes. §7
requires those to be **classified and restored**, not committed.

```text
files rewritten by this regression : 50   (15 .md + 35 .json)
JSON: the ONLY keys that changed   : "runtime_seconds" and "seconds"   -- nothing else
MD:   the ONLY columns that changed, checked column-by-column against each table's own header:
        | grid | nx x ny | cells | h | runtime (s) | solver | accepted |          -> col 5 only
        | grid | cells | h | status | iterations | mass imbalance | runtime (s) | -> col 7 only
        | run | Re | mesh | scheme | status | iterations | linear its | mass
          imbalance | runtime (s) | passed |                                      -> col 9 only
      51 changed row pairs, 0 row-shape mismatches, and in every one the differing column IS the
      runtime column. Iteration counts, solver status, cell counts, h, mass imbalance and the
      accepted/passed flags are byte-identical.

CLASSIFICATION : TIMING ONLY. No physical or convergence quantity moved.
ACTION         : restored with `git checkout -- results/validation`.
AFTER          : 0 files modified under results/validation.
```

```text
WHY THE COLUMN CHECK AND NOT A GLANCE. A runtime column and a mass-imbalance column are both just
numbers in a table, and "the diff looks like timings" is the kind of judgement that would hide a
moved residual. Naming each table's columns from its own header and showing that ONLY the runtime
index differs is what makes "timing only" a measurement rather than an impression.
```
