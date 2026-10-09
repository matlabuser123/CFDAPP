# INVALID AS AUTHORITATIVE — P17.0 baseline regression, attempt 1

Run 2026-09-26T04:14:07Z. **Preserved, not deleted**, per `CLAUDE.md` §4.

## What happened

Both mandated configurations were refused by the harness's own §8 step-3 check:

```text
release_cuda   build exit 0 (4 s)   -> 285 "test binaries", 285 "stale"  -> FAIL CLOSED
cpu_only       build exit 0         -> same                              -> FAIL CLOSED
```

No test was executed, so **no test result here is valid as evidence** — and equally,
**nothing here is evidence of a regression**.

## Why it is invalid — a defect in my instrument, not in the tree

The harness discovered test binaries with:

```bash
find "$dir" -type f -executable \( -name 'test_*' -o -name '*_test' -o -name '*_tests' \)
```

On WSL2 the repository lives on `/mnt/c`, a **drvfs** mount that reports every file as
mode `0777`. `-executable` therefore matches everything, and the name patterns then
swept in files that are not binaries at all:

```text
build/qual-final/tests/.../test_backward_facing_step.cpp.o      an object file
build/qual-final/_deps/nlohmann_json-src/tests/src/test_utils.hpp  a vendored header
```

Object files and a vendored header legitimately predate the newest source, so the
staleness comparison was true — of the wrong file set. Confirmed directly:

```text
$ ls -l build/qual-final/tests/unit/compressible/
-rwxrwxrwx CFDCompressibleTests            Sep 26 13:45   <- the real binary
-rwxrwxrwx CFDCompressibleTests[1]_tests.cmake Sep 26 13:45
-rwxrwxrwx CFDCompressibleTests[1]_include.cmake Sep 24 11:53
                ^ even .cmake files report executable — drvfs, not a build property
```

`CFDCompressibleTests` is dated **03:45Z**, newer than the newest production source
(`src/compressible/IdealGasEOS.cpp`, 03:44:26Z). The property being tested actually
holds; the instrument could not see it.

## What was salvaged, and is valid

These were measured before the defective check and stand on their own:

```text
build release_cuda            exit 0, 4 s      build/qual-final was already current
build cpu_only                exit 0
P17_CODE_FINGERPRINT before   1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
                              == the value frozen in baseline_gate.md §7
newest production source      2026-09-26T03:44:26Z  src/compressible/IdealGasEOS.cpp
nvcc on PATH                  /usr/local/cuda-12.9/bin/nvcc  12.9 V12.9.86 — correct toolkit
```

## The fix — strictly stronger, not weaker

The check is **not relaxed or removed**. Binary discovery is replaced with
`ctest --show-only=json-v1`, taking `command[0]` of every registered test. That is the
authoritative set — exactly the executables ctest will invoke — rather than a guess
from filenames and a permission bit that drvfs does not carry.

```text
before   find -executable -name 'test_*'      guesses; drvfs defeats -executable
after    ctest --show-only=json-v1            the registry ctest itself runs from
```

`FAIL CLOSED` on stale or missing binaries is retained verbatim.

## Classification

A harness defect. Not a regression, not a source problem, and not grounds to weaken
a criterion. Attempt 2 supersedes this run; this record stays so the fix is
defensible with the failure in view.
