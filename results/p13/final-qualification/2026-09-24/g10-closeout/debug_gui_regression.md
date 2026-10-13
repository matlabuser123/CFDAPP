# G10 — Debug+GUI regression

**PASS — 2550 / 2550, 0 failed.**

```text
configuration   Debug   CUDA=ON   GUI=ON   OpenMP=OFF      build/qual-debug
invocation      OMP_NUM_THREADS=1 ctest -j 8 --timeout 14400 -E <5 documented exclusions>
                timeout amended 7200 -> 14400 for this configuration only;
                see AMENDMENT_G10_debug_timeout.md

total tests     2550        passed 100%        failed 0
Not Run         0           Disabled 98        runtime 11850 s (197.5 min)
```

## The three tests the 7200 s ceiling could not accommodate

```text
DriverSeegmillerInlet.MappedInletPreservesTheMassFlow    Passed   9390.87 s
DriverSeegmiller.CorrectedSequenceIsDeterministic        Passed  11622.20 s
DriverSeegmillerInlet.MediumGridAB                       Passed  11849.28 s
```

All three **Passed**, not merely "did not time out". Zero assertion failures in
this or any earlier run of them.

## Qualification rows executed, not assumed

```text
CliGuiEquivalenceTest.AThermalCaseAgreesBitwise                 Passed  0.29 s
CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli  Passed  0.24 s
```

These are the two rows G4 added. A registry audit taken before this tree was
rebuilt showed both as absent, because `gtest_discover_tests` populates the test
list at build time and `build/qual-debug` dated from G1. Confirming them by name
is what distinguishes "the tests exist in the source" from "the tests ran".

## Integrity

```text
rebuild before testing   NONE -- the binaries are the ones the 7200 s run used
source changes           none
exclusions               the same 5, verbatim
SOURCE_FINGERPRINT_V2    8652dda0... before and after, MATCH
```

## The superseded 7200 s run

Preserved at `logs/G10_debug_gui_ctest.log`: 2547/2550 with three Timeouts. Not
deleted — it is the measurement that justified the amendment, and the amendment is
only defensible with it in view.

Full log: `logs/G10_debug_gui_ctest_amended.log` · driver: `logs/G10_debug_stage2.log`
