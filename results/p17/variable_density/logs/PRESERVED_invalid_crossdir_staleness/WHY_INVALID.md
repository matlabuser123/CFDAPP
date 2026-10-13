# INVALID AS AUTHORITATIVE — P17.1 regression, attempt 1

Run 2026-09-26T~06:5xZ. **Preserved, not deleted**, per `CLAUDE.md` §4.

## What happened

```text
release_cuda   build exit 0 (17 s)   46 binaries, 45 "stale"  -> FAIL CLOSED, not tested
cpu_only       build exit 0 (54 s)   45 binaries,  0 stale    -> ran, then STOPPED by me
```

`release_cuda` executed no test, so there is no test evidence here. `cpu_only` was
stopped mid-run deliberately so that both configurations could be re-run under one
corrected harness rather than mixing two harness versions in one verdict.

## Why it is invalid — an over-strict proxy in my check, not real staleness

The check required **every** test binary to be newer than the newest source file found
anywhere under `include src cuda apps tests benchmarks`. After `clang-format -i` touched
`tests/unit/compressible/test_compressible_mass_flux.cpp`, that file became the newest
source in the tree — and 45 test binaries that **do not compile it** were older.

Measured mtimes:

```text
newest PRODUCTION source  src/compressible/CompressibleMassFlux.cpp   06:46:05Z
  build/qual-final/src/libcfdcore.a                                   06:46:20Z   newer
  .../tests/unit/core/CFDCoreTests                                    06:46:26Z   newer
  .../tests/unit/gpu/CFDGpuTests                                      06:46:58Z   newer
newest TEST source        test_compressible_mass_flux.cpp             06:52:00Z
  .../tests/unit/compressible/CFDCompressibleTests                    06:53:48Z   newer
```

So the property `CLAUDE.md` §8 actually requires — *the binary was built against the
source and library being tested* — **held for every binary.** CMake's dependency graph
rebuilt exactly what depended on the change. What failed was my proxy for that property.

A test source file only affects the binary that compiles it. Requiring unrelated binaries
to postdate it is a category error, in the same family as attempt 2 of the P17.0 baseline
treating `/usr/bin/cmake` as a stale product of this build.

## The fix — retargeted, and in one respect STRICTER

```text
before   every binary newer than the newest file under
         include src cuda apps tests benchmarks
         -> defeated by touching any single test file

after    (1) every binary newer than the newest PRODUCTION source (include src cuda).
             This is the real hazard: libcfdcore.a and libcfdcuda.a link into every test
             binary, so a production edit invalidates all of them.
         (2) AND each binary newer than the newest test source in ITS OWN directory.
             This is NEW and catches the case (1) cannot: a test file edited without its
             own binary being rebuilt.
```

(2) did not exist before. The net check is better targeted **and** covers a case the
original missed, so this is a correction of a mis-specified criterion, not a weakening of
a real one. `FAIL CLOSED` on a genuinely stale binary, or on zero binaries, is retained
verbatim.

## What was salvaged

```text
release_cuda build   exit 0, 17 s
cpu_only build       exit 0, 54 s
cpu_only staleness   0 stale, 45 binaries -- passed even under the over-strict rule,
                     because its build rebuilt everything (it had not been rebuilt since
                     the P17.0 baseline)
```

## Classification

A third harness defect in this family, found by the check failing closed rather than by a
wrong number passing through. Not a regression, not a source problem, and not grounds to
weaken a criterion. Attempt 2 supersedes this run.
