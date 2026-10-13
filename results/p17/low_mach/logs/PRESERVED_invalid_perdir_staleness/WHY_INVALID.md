# PRESERVED — P17.6 mandated regression, attempt 1: FAILED CLOSED on a false positive

```text
date        2026-09-27T04:28:44Z .. 04:30:57Z
result      BOTH configurations refused to run ctest
            release_cuda   stale vs own test src : 1   -> *** FAIL CLOSED ***
            cpu_only       stale vs own test src : 1   -> *** FAIL CLOSED ***
accused     build/<cfg>/tests/integration/compressible/CFDLowMachRegressionTests
```

The instrument was **right to stop**. It was wrong about *why*, and the accusation is false. Nothing
was weakened to get past it; the probe was made **more precise**, which also makes it stricter.

---

## The accusation, and why it is false

The attempt-1 check asked: *is this binary older than the newest test source in its own directory?*

```text
binary  CFDLowMachRegressionTests            mtime 1790473578   2026-09-27 11:46:18
its own source
        test_low_mach_regression.cpp         mtime 1789646169   2026-09-17 21:56:09
        => the binary is 827,409 s (9.6 days) NEWER than the source it compiles. NOT STALE.

the source that triggered the accusation
        test_low_mach_family.cpp             mtime 1790483208   2026-09-27 14:26:48
        => newer than the binary, but it is compiled into a DIFFERENT TARGET.
```

Proof that the two are separate targets, from CMake's own object lists:

```text
CMakeFiles/CFDLowMachRegressionTests.dir/   test_low_mach_regression.cpp.o
CMakeFiles/CFDLowMachFamilyTests.dir/       test_low_mach_family.cpp.o
```

`tests/integration/compressible/CMakeLists.txt` declares `CFDLowMachFamilyTests` as a **separate
executable**, deliberately, so that the P3-PHYS-006 test stays byte-for-byte unchanged as the frozen
G5 criterion requires. The heuristic mistook "same directory" for "same translation unit".

Its timestamp is 14:26:48 because the Group N controls restored the harness from its pristine copy at
the end of that run — a restore that was itself SHA-256 verified.

## The heuristic was wrong in BOTH directions

```text
TOO WIDE    a sibling target's new source in the same directory falsely accuses an untouched binary.
            That is what happened here, and it happened in the first phase that put two test targets
            in one directory.
TOO NARROW  and this is the more serious half: a source compiled in from ANOTHER directory was never
            checked at all, because the probe only globbed the binary's own directory. A stale binary
            of exactly that shape would have passed silently.
```

## The replacement

`tools/staleness.py`, driven from `tools/p17_6_regress.sh`. For every built test binary it reads the
**object list CMake actually produced** under `CMakeFiles/<target>.dir`, maps each object back to its
source (decoding CMake's `__/` for `../`), and requires the binary to postdate every source it
genuinely compiles — plus, unchanged, the newest production source in `include/ src/ cuda/`.

```text
This is STRICTLY STRONGER than what it replaces: it covers cross-directory sources the old probe
never looked at, and it stops accusing binaries of sources they do not compile.
```

It also fails closed on its own blind spots rather than skipping them:

```text
no CMakeFiles/<target>.dir      -> "cannot verify, FAIL CLOSED"
no objects under it             -> "cannot verify, FAIL CLOSED"
zero sources resolved for a binary -> "cannot verify, FAIL CLOSED"
objects whose source is not in the repo (gtest_main and friends) are counted and the totals are
PRINTED, so a bad `__/` mapping would show up as a large resolved-vs-objects gap instead of hiding.
```

## What this attempt did establish

It was not wasted. Before it stopped, both configurations reported:

```text
build exit 0 in both configs;  stale vs PRODUCTION: 0 in both
P17_CODE_FINGERPRINT before == after : cf4d53a2e6b090228f68d58bc9b452d324abd3dde03539a53a70f44542c70bdf
B6 (already corrected this phase): nvcc 0, libcudart 0, no libcfdcuda.a, ldd cudart 0
   -- and it now names the log it read (50_cpu_only_build.log, 8 lines), so those zeros are
   attributable rather than being the zeros a missing file produces. See
   AMENDMENT_regression_probes.md section 3.
```

## Files here

```text
50_p17_6_regress_mandated.log    the failing run, verbatim
50_release_cuda_build.log        both builds exited 0
50_cpu_only_build.log
p17_6_regress.sh.attempt1        the instrument as it stood, with the per-directory heuristic
```
