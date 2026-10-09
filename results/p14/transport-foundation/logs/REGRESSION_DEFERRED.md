# P14's full regression is deferred into the combined P14+P15 run

## What happened

The P14 regression was started twice and invalidated twice, both times by me:

1. First run: I rebuilt `build/final` mid-run applying the device-side pattern-check
   fix and two clang-format passes. Kept as `final_INVALID_rebuilt_midrun.log`.
2. Second run: P15 implementation began while it was still running, and the
   thermal builds rebuilt `build/final` underneath it again.

A regression that tests binaries changing underneath it proves nothing, whatever
number it reports.

## Why it is not simply re-run in isolation

The working tree now contains P14 **and** P15 changes, uncommitted, in one
build directory. There is no "P14 tree" left to regress against without
stashing P15 -- which would mean reverting and restoring partially-built
thermal work, a riskier operation than the thing it is trying to verify.

## What is done instead

ONE full regression over the combined tree, after P15's code changes are
complete. That is at least as strong for P14 as an isolated run: it exercises
every P14 code path with additional changes layered on top, so a P14 regression
would still surface. If it fails, the failure is diagnosed to the specific
change rather than attributed by assumption.

## P14's other gates are unaffected

All of them were measured on a settled tree and are recorded with their numbers:

```text
LegacyMatrixEquivalence (bitwise)          PASS
unit tests                                 21/21
analytical + conservation                  10/10
CPU/GPU matrix / solution / determinism    PASS (<= 2 ulp, relL2 <= 2.1e-12)
residency                                  4 bytes per assembly
P13-001 SIMPLE fingerprint, 10 cases       IDENTICAL
```

The SIMPLE fingerprint is the strongest preservation instrument available and it
was run on a settled tree after all P14 code changes.
