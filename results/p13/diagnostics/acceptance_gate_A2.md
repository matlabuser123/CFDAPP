# P13-005 — acceptance gate, amendment A2

**Control N1 reported UNDETECTED against C2.** Recorded rather than quietly dropped, because an
undetected control is exactly the signal that a criterion is not measuring what it claims.

```text
########## N1 -- route Info back to stdout; C1 and C2 must break
  corpus at default: stdout differing=0 stderr differing=0
  log lines on stdout at --log-level debug: 3
  N1-C1: DETECTED
  N1-C2: *** UNDETECTED *** (stdout files differing = 0)
```
(`logs/03_controls_N1_N4.log`)

## Why, and what it does and does not mean

Amendment A1 made the CLI's default level `Warning`. At that level nothing below Warning is
emitted **at all**, so routing Info to stdout cannot put anything on stdout in a default run.
N1 reaches C1 (which is measured at every level) and cannot reach C2 (which is measured at the
default level only).

So C2 is **PRESERVATION**, not DETECTION — the frozen gate's own dry-run record already classified
it that way (*"C2/C3/P1–P3 are PRESERVATION and already pass"*), while Group C's heading and N1's
must-break column called it DETECTION. That inconsistency is mine and is corrected here. C2 is
not vacuous; it is doing a preservation job correctly. What was wrong was claiming a control could
break it.

## The detection counterpart, added and measured

A criterion that pins stdout only where nothing is flowing is worth little on its own, so the
detection half is stated explicitly and verified:

| id | criterion |
| --- | --- |
| **C2b** | `cfdapp --case X` **stdout is byte-identical at every `--log-level`** — `error`, `warning`, `info`, `debug`, `trace` — to the default-level baseline, for all 7 cases (35 comparisons). |

Measured (`tools/verify_c2b.sh`, `logs/04_C2b_and_N1b.log`):

```text
                              intact     N1b (Info/Debug/Trace -> stdout)    restored
--log-level error                  0                                   0            0
--log-level warning                0                                   0            0
--log-level info                   0                                   4            0
--log-level debug                  0                                   7            0
--log-level trace                  0                                   7            0
                        total      0                                  18            0
```

Same instrument all three times, so the columns are directly comparable. `Logger.cpp` was restored
from a pre-control copy and the restore verified by sha256.

**N1b replaces N1's C2 half.** N1's C1 half stands as measured (3 log lines on stdout → 0).

| id | control | must break | result |
| --- | --- | --- | --- |
| N1 | route Info back to stdout | C1 | DETECTED (3 → 0 stdout log lines) |
| N1b | route Info/Debug/Trace back to stdout | **C2b** | DETECTED (18 → 0 of 35) |

No threshold was weakened: C2 is unchanged and still required, and C2b is an **additional**
criterion that the tree must pass.
