# PRESERVED — P17.7 regression attempt 1: FAILED CLOSED on a MISSING instrument

```text
date    2026-09-27T07:0x
error   python3: can't open file '.../results/p17/hllc/tools/staleness.py': No such file or directory
result  *** STALE OR UNVERIFIABLE BINARIES -- FAIL CLOSED *** in BOTH configurations
        no ctest was executed
```

## What happened

`p17_7_regress.sh` was adapted from P17.6's by retargeting `$OUT` to `results/p17/hllc`. The staleness
checker it calls, `$OUT/tools/staleness.py`, was written during P17.6 and lives in
`results/p17/low_mach/tools/`. Retargeting `$OUT` moved the *call* without moving the *file*.

## Why this is a GOOD failure

The probe could have been written to skip the check when its helper is absent. It was not:

```text
python3 "$OUT/tools/staleness.py" ... ; if [ "$strc" -ne 0 ]; then FAIL CLOSED; fi
```

A missing interpreter target exits nonzero, so a **missing instrument is treated as a failed check,
not as a passed one.** That is the fail-closed property P17.6's `AMENDMENT_regression_probes.md` was
written about, now demonstrated on a new phase: the one thing worse than a probe that fails is a probe
that cannot run and says nothing.

```text
Compare with the P17.6 defect this is the mirror image of: `grep -ac` on a MISSING build log printed
0 -- exactly what a clean build prints -- and reported success without reading anything. Same class of
mistake (a wrong path), opposite outcome, purely because of how the result was consumed.
```

## The fix

`staleness.py` copied into `results/p17/hllc/tools/`, so this phase's evidence is self-contained and
does not depend on a sibling phase's directory surviving. Nothing about the checker changed — it is
byte-identical to the version P17.6 proved non-vacuous in
`results/p17/low_mach/logs/06_staleness_probe.log` (passes clean with 300/300 objects mapped, detects
a real mtime staleness, restores to the frozen hash).

No threshold was altered and no check was removed or relaxed.
