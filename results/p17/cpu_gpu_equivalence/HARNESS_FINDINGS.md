# P17.10 — three findings in the regression harness itself

None of the three is a numerical defect and none changes a P17 conclusion. All three are cases
where the evidence said something that was not true — two of them while reporting success, and one
of them (§2) while reporting three failures that were not failures.

```text
STATUS    REPORTED, NOT FIXED. Fixing the harness is P17.11's call; this project has a standing rule
          against editing an instrument mid-phase, and §2 in particular was found BY the instrument
          behaving badly, which is not the moment to start changing it.
AFFECTS   the wording of every P17 phase's regression evidence, and -- for §2 -- the validity of one
          CPU-only run, which was re-done rather than reinterpreted.
COST      §2 cost two full regression re-runs. It is written up at length because the next person to
          delete a build tree deserves to know what happens.
```

---

## 1. `Not Run : 0` has been false in every P17 phase

### What it says and what is true

```text
The harness prints, for each configuration:

    Not Run           : 0

produced by:

    notrun=$(grep -acE '\(Not Run\)' "$L/50_${label}_ctest.log" || true)      # line 192

ctest does not write "(Not Run)". It writes:

    2700/2763 Test #1944: SomeTest.SomeCase ...........   ***Not Run (Disabled)   0.00 sec

    matches for '(Not Run)'          : 0
    matches for 'Not Run (Disabled)' : 49
```

The pattern requires an open parenthesis immediately before `Not`, and there is none. It has
matched nothing, in every phase, and printed `0` — which is exactly what "there were none" looks
like.

### The 49, accounted for

```text
2776  registered
 -13  matching the 5 frozen exclusion patterns
=2763  selected, and 2763 result lines are printed
 -49  ***Not Run (Disabled)
=2714  with a counted outcome -> ctest: "100% tests passed, 0 tests failed out of 2714"
       of which 2712 Passed and 2 Skipped (P17.10's own sanitizer controls, by design)

The 49 are DELIBERATE: exactly 49 DISABLED_ macros exist in tests/, and ctest is reporting them
correctly. The defect is in the harness's reading of the log, not in ctest and not in the suite.

  11  CavityGhiaValidation          4  ChannelFlowValidation      1  each: TurbulentChannel-
   8  MeshQualityCampaign           3  BackwardFacingStep            Validation, SchemeValidation,
   5  Duct3DProductionCase          2  PoiseuilleValidation          SIMPLEMMS, ResidentTurbulent-
   4  LidDrivenCube3DProductionCase                                  Simple, ResidentChannel-
                                                                     Production, NaturalConvection,
                                                                     MeshMotionEvidence, MMSSimple3D,
                                                                     MMS3D, DriverSeegmiller,
                                                                     CaseLidDrivenCavity,
                                                                     AutomaticWallTreatment
```

### Why it does not change a P17 conclusion

```text
NONE OF THE 49 IS A P17 TEST. Searched for hllc / densitybased / shock / idealgas / totalenergy /
compressible / p17 among them: 0 matches. They are long-running production and diagnostic cases,
disabled as a standing project decision that predates P17 entirely.

AND THE EXECUTED COUNT WAS NEVER WRONG. The harness reads "out of 2714" from ctest, which already
excludes the disabled tests, and every phase's PREDICTION was built from the previous phase's
executed count. So the arithmetic has been internally consistent the whole time -- P17.10 predicted
2714 and measured 2714. What was wrong is only the separate line claiming nothing was skipped.

PRE-EXISTING, confirmed: P17.9's own ctest logs contain 96 "Not Run (Disabled)" lines across its two
configurations, while its regression.md records "Not Run 0" for both.
```

### What a fix should say

```text
Report the disabled count EXPLICITLY and assert it against a declared expectation, e.g.

    Disabled (not run)  : 49   PREDICTED 49

so that a test silently becoming DISABLED_ -- the realistic failure mode, since that is how a
failing test gets quieted -- moves a number that somebody predicted. Reporting 0 forever cannot
detect that. The count must be PREDICTED rather than merely printed, for the same reason every
other count in this harness is.
```

## 2. A deleted build tree is silently reconfigured in DEBUG — and the regression still "runs"

This is the most serious of the three, and it is the one I caused.

### What happened

```text
To clear the staleness false positive of section 3, I DELETED build/qual-cpuonly. The harness
configures with:

    cmake -S . -B "$dir"

with no -D flags, which on an EXISTING tree reuses the cached options and on a FRESH tree takes the
PROJECT DEFAULTS. The defaults are not the qualification configuration:

    option                     qualification   project default
    CMAKE_BUILD_TYPE           Release         Debug
    CFDAPP_ENABLE_OPENMP       ON              OFF
    (everything else matched)

So the CPU-only regression ran a DEBUG, NO-OPENMP build while reporting itself as the CPU-only
qualification configuration.
```

### How it presented — as three test failures

```text
  ctest exit status : 8
  failed            : 3           PREDICTED 0
        DriverSeegmiller.CorrectedSequenceIsDeterministic    ***Timeout 7200.09 sec
        DriverSeegmillerInlet.MappedInletPreservesTheMassFlow ***Timeout 7200.09 sec
        DriverSeegmillerInlet.MediumGridAB                   ***Timeout 7200.09 sec

In P17.9's CPU-only run those same three PASSED, in 1389.60 s, 1671.16 s and 1690.10 s. A Debug
build is roughly 5-10x slower, which puts all three past the frozen 7200 s timeout. Nothing was
broken; the build was.
```

### The evidence that settles it, rather than an argument

```text
Reconfigured to Release + OpenMP ON, CUDA OFF, and rebuilt from scratch:

    build/qual-cpuonly/src/libcfdcore.a
      1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a
    P17.9 recorded for cpu_only
      1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a     IDENTICAL

and the option sets of the two trees now differ in exactly one line:

    < CFDAPP_ENABLE_CUDA:BOOL=ON        (qual-final)
    > CFDAPP_ENABLE_CUDA:BOOL=OFF       (qual-cpuonly)

The Debug archive had been 1f204a48..., a difference I initially had to treat as a possible
production change. It was the build type. THREE THINGS FOLLOW: the configuration is now right, P17.10
changed no CPU production code, and -- incidentally but usefully -- the HOST toolchain here IS
bit-reproducible, since a from-scratch rebuild reproduced P17.9's archive byte for byte. (nvcc is
not; that is separately recorded.)

The Debug run is preserved as logs/50_p17_10_regress_INVALID_DEBUG_CPUONLY.log and
logs/50_cpu_only_ctest_INVALID_DEBUG.log, and is INVALID rather than FAILING.
```

### Two harness weaknesses this exposes

```text
(a) THE CONFIGURATION IS PRINTED BEFORE IT EXISTS. run_cfg greps CMakeCache.txt and THEN configures,
    so on a fresh tree the evidence block reads:

        grep: build/qual-cpuonly/CMakeCache.txt: No such file or directory

    and the run continues. The line that is supposed to record WHICH CONFIGURATION WAS TESTED
    silently recorded nothing, and nothing failed closed. Print it AFTER configuring, and ASSERT the
    four options against expected values rather than echoing them.

(b) THE OPTIONS LIVE ONLY IN THE TREE. build/qual-cpuonly's configuration is not in CMakePresets.json
    and not in the harness -- it existed only as cache state created by hand in some earlier phase.
    Delete the directory and the qualification configuration is gone with no record. The harness
    should configure with EXPLICIT -D flags, or a preset, so the configuration is reproducible from
    the repository rather than from a build directory nobody backs up.

Until (b) is fixed, DO NOT DELETE build/qual-cpuonly. I did, and reconstructing it needed the
surviving qual-final cache plus P17.9's recorded library hash to confirm.
```

## 3. The global staleness rule fires on any CUDA-only source

Recorded in full in `STALENESS_FAILURE.md`. In short: the harness compares every binary's mtime
against the newest production source **anywhere** under `include/`, `src/` and `cuda/`. When a phase
adds a CUDA-only source, every CPU-only binary is flagged stale even though no CPU-only target
compiles it. The per-binary check (`stale vs its own sources`, from CMake's own object lists)
reported 0 over 286 objects and is the one that actually answers the question.

It fails in the **safe** direction, so it is far less serious than §1 — but it costs a full rebuild
of the CPU-only tree on every phase that touches CUDA, and it was resolved here by doing exactly
that rather than by touching the check.

---

## 4. How the three were found

```text
§3 announced itself by FAILING CLOSED. That is the system working, and it is the only one of the
   three that the harness itself caught.

§2 announced itself as THREE TEST FAILURES -- the most misleading possible presentation, because
   the honest reading of "3 failed, PREDICTED 0" is that the code broke. It took comparing a
   library hash against P17.9's record to establish that the BUILD was wrong rather than the code.
   A configuration assertion would have caught it in one line, before any test ran.

§1 was found by nothing at all: by asking where a 49-test gap between the progress denominator
   (2763) and the reported total (2714) came from. That gap has been printed in every P17
   regression log for many phases and nobody, including me until now, had subtracted it.

THE LESSON IS THE ONE THIS PROJECT KEEPS RELEARNING, in two halves:
  a probe that reports zero is not evidence until something has made it report non-zero; and
  a number that is merely PRINTED rather than PREDICTED is not a check.
```
