# INVALID AS AUTHORITATIVE — P17.0 baseline regression, attempt 2

Run 2026-09-26T04:2xZ. **Preserved, not deleted**, per `CLAUDE.md` §4.
Supersedes `PRESERVED_invalid_binary_discovery/` (attempt 1); superseded by attempt 3.

## What happened

Attempt 1's defective `find -executable` discovery was replaced with ctest's own
registry, which worked — 47 and 46 real binaries instead of 285 objects and headers.
Both configurations were then refused by the same §8 step-3 check, for a different
reason:

```text
release_cuda   build exit 0 (5 s)   47 binaries, 1 stale  -> FAIL CLOSED
cpu_only       build exit 0 (5 s)   46 binaries, 1 stale  -> FAIL CLOSED
the single stale entry, both times:
  /usr/bin/cmake    2024-02-03T23:33:56Z
```

No test executed. Nothing here is evidence of a regression.

## Why it is invalid

`ctest --show-only=json-v1` reports `command[0]` for every registered test. Some CLI
rejection tests are driven by **`cmake -P`**, so `/usr/bin/cmake` is legitimately a
registered test command. It is a distribution-packaged tool from February 2024.

`CLAUDE.md` §8's property is that *the test binary was demonstrably rebuilt against
the exact source being tested*. That applies to binaries **this build produces**. A
fixed external tool is not stale — it is not ours, it is not built from this source,
and it never will be newer than a source edit. Comparing it against the newest
production source is a category error in my harness.

## The fix — a visible partition, not a silent exclusion

The registry is partitioned by whether the binary lives inside the configuration's
build tree:

```text
inside  build/qual-*/...     BUILT by us    -> staleness-checked, FAIL CLOSED
outside (e.g. /usr/bin/cmake) EXTERNAL tool -> listed with its date, not checked
```

Both sets are **printed with counts**, so the exclusion is auditable rather than
hidden. `FAIL CLOSED` on a stale built binary, or on zero built binaries, is retained
verbatim. No test, threshold or tolerance is touched.

## What was salvaged, and is valid

```text
build release_cuda / cpu_only       exit 0, 5 s each
binary discovery via ctest registry WORKS -- 47 (CUDA) / 46 (CPU-only)
                                    the 1-binary difference is the GPU test target,
                                    a sanity signal that the registries really differ
P17_CODE_FINGERPRINT before/after   1e307fcd…  unmoved
B6 CPU-only has no CUDA            0 nvcc invocations, 0 libcudart references,
                                    no cuda/libcfdcuda.a, 0 cudart refs in ldd
```

B6 is a genuine PASS and did not depend on the refused check.

## Classification

A second harness defect, in the same check, found by the check failing closed rather
than by a wrong number slipping through. Not a regression and not grounds to weaken a
criterion.
