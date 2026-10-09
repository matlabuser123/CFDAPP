# P17.6 AMENDMENT — four defects in the regression INSTRUMENT, found before it ran

**No gate criterion is changed by this amendment.** Every fix is to `tools/p17_6_regress.sh`, the
measuring instrument. Three of the four made the instrument **fail open** — report a clean result
without having measured anything — which is the failure mode the evidence-first protocol treats as
worse than a failing probe, because a fail-open probe manufactures a PASS.

The script was inherited from P17.3's and adapted per phase. These defects are all **stale constants
and stale paths** that the adaptation missed.

---

## 1. The gate hash was a wrong hardcoded constant

```text
the script asserted   ded6feaa03b53e9fd65018491e57c9c318383798bf13fbabc4b910cf4fd6b673
the real P17.6 freeze f275c4ac3fab31509b9f80036e1c8b4b6f4f3e8c9ec20cf8b8ae7a1d8b303c9a
                      (confirmed in logs/02_freeze.log and logs/03_gate.log)
```

The regression log would have printed, as its provenance line, the hash of **a different phase's
gate**. Worse, the value was *hardcoded*: it was never compared against anything, so it could not
have detected gate drift even when correct.

```text
FIXED: the hash is RECOMPUTED at run time and COMPARED against the frozen value. A mismatch prints
       *** GATE INTEGRITY: the gate MOVED since the freeze *** and EXITS 1 -- the run refuses to
       produce results it cannot attribute to a frozen gate.
```

## 2. The fingerprint baseline named the P17.3 close, three phases stale

```text
was   "P17.3 close was : 437ac0e3d9f09692f6bf415d7b1cce38b65d2c02e0158a346869232ad800a272"
now   "P17.5 close was : d2e65555534bde2208cb7affd9caac7c7b4406c3f1cd17dfd473d441b882364a"
```

Cosmetic in effect — the line is a label, and B8 compares before/after **within** the run — but it
named the wrong baseline for the delta a reader is meant to check.

## 3. B6 read a log file this script never writes — FAIL OPEN

```text
the probe read    $L/10_cpu_only_build.log      <- does not exist; no P17.6 step writes it
the script writes $L/50_cpu_only_build.log
```

`grep -ac 'nvcc' <missing file>` prints **0** and returns nonzero, which `|| true` swallowed. **0 is
exactly the answer a clean CPU-only build gives.** So the probe reported "no nvcc invocations, no
libcudart references" *without reading a build log at all* — and would have reported the same for a
CPU-only build that did invoke nvcc.

```text
FIXED: path corrected to 50_cpu_only_build.log, AND guarded --
       if the file is absent the probe prints *** B6 INVALID ... FAILING CLOSED on B6 *** instead of
       0, and it now reports the line count of the log it actually read, so "I measured nothing" can
       never again look like "I measured zero".
```

This is the defect the P17.5 summary recorded as standing debt (*"two B6 probes read a stale filename
and print BLANK"*). It is now closed rather than carried into a fifth phase.

## 4. The "post-freeze additions" grep named P17.4's tests — FAIL OPEN

```text
was   grep -aE '(IsWiredIntoTheRelaxedMomentumAssembly|DefaultIsDisabledBehaviourally)'
```

Those are **P17.4's** test names. They do not exist in P17.6, so the grep matched nothing and printed
nothing — and an empty result under the heading "of which the two post-freeze additions" reads as
though there were none, rather than as a probe looking for the wrong thing.

```text
FIXED: grep -aE 'RelativePressureDifferenceFallsToItsConvergenceFloorAndStaysThere'
       -- P17.6's actual post-freeze addition, the A3 coverage test from
       AMENDMENT_thresholds.md section 2, which is precisely the row that most needs confirming
       actually EXECUTED in the mandated run.
```

## 5. The per-directory staleness heuristic — found by RUNNING it, and it FAILED CLOSED

Unlike the four above, this one was caught the hard way: the mandated run refused to execute.

```text
attempt 1   release_cuda   stale vs own test src : 1   -> *** FAIL CLOSED ***
            cpu_only       stale vs own test src : 1   -> *** FAIL CLOSED ***
            accused: CFDLowMachRegressionTests, which P17.6 never touched
```

The probe asked *"is this binary older than the newest test source in its own directory?"* — and
P17.6 is the first phase to put **two test targets in one directory**, deliberately, so that the
P3-PHYS-006 test could stay byte-for-byte unchanged. The new sibling source made the untouched binary
look stale.

```text
binary   CFDLowMachRegressionTests        2026-09-27 11:46:18
its own source                            2026-09-17 21:56:09   -> binary is 9.6 DAYS newer
the sibling's source, different target     2026-09-27 14:26:48   -> what actually tripped it
CMake's object lists confirm the split:
  CFDLowMachRegressionTests.dir/  test_low_mach_regression.cpp.o
  CFDLowMachFamilyTests.dir/      test_low_mach_family.cpp.o
```

The heuristic was wrong in **both** directions, and the second half is the dangerous one:

```text
TOO WIDE    a sibling target's source falsely accuses an untouched binary.
TOO NARROW  a source compiled in from ANOTHER directory was never checked AT ALL. A genuinely stale
            binary of that shape would have passed silently -- a fail-open hole in the very probe
            whose job is to fail closed.
```

Replaced by `tools/staleness.py`, which reads **CMake's own object list** per target and maps each
object back to its source. Non-vacuity proven in `logs/06_staleness_probe.log` before it was trusted:

```text
S1  the replacement at the unchanged tree            PASS, exit 0
    300 objects inspected, 300 mapped to a repo source, 0 unresolved
    -- so the `__/`-for-`../` decoding is complete; there are no silent skips behind that PASS
S2  the OLD heuristic, same tree                     reproducibly FAILS falsely on the same binary
S3  one compiled source's mtime moved forward,
    its CONTENT untouched (sha256 identical)          DETECTED, exit 1, "STALE vs ITS OWN SOURCE"
S4  mtime restored                                    PASS again, and the file's sha256 is
    593165a1a2fff9ccbad8b3675ccbdff093bcf22c82b5daad90462fe228949acc
    -- which is the FROZEN hash of test_low_mach_regression.cpp. The control left no trace.
```

```text
The instrument was STRENGTHENED, never relaxed. It now catches a case the old one could not see,
and the failing attempt is preserved verbatim in logs/PRESERVED_invalid_perdir_staleness/ with its
own WHY_INVALID.md rather than overwritten by the passing rerun.
```

## What was deliberately NOT fixed

```text
KEPT AS-IS   the IdealGasEOSTest "PREDICTED 21" constant, obsolete since P17.3 (the real count is
             30). It is a STALE CONSTANT attached to a LIVE INSTRUMENT: those 30 rows must go on
             passing, so the probe has value. Correcting the constant mid-run would edit a frozen
             prediction, so it is reported as stale -- for the fourth phase running -- and named as
             debt in summary.md. This is the distinction between an obsolete constant and a
             contaminated instrument.
KEPT AS-IS   the debug_gui predicted counts (2670/2608), last maintained at P17.4 and therefore
             stale by construction. That stage is NOT run in P17.6 and the script says so.
```

## How these were caught

Not by running the script. By reading it against the logs directory: `10_cpu_only_build.log` is not
in `logs/`, and `ded6feaa` does not appear anywhere in this phase's evidence. A probe's inputs are
checkable without executing it, and three of these four could only ever have been caught that way —
because when they run, they print exactly what success looks like.
