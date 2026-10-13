# GATE_TOL — the production `PremixedFlameSettings::tolerance` default. Frozen before the change.

```text
AUTHORIZED:  by the user, 2026-10-13, "fix the production tolerance".
SCOPE:       ONE default value in include/cfd/species/PremixedFlame.hpp, and the comment that
             justifies maximumCellDiffusionNumber by citing it. Nothing else. No threshold moves,
             the cap stays 5.0, and C3/C3-A4 are NOT touched by this gate.
DRY-RUN:     logs/TOL_01_dryrun.log, on the unchanged tree. C3 FAILS there, which is what makes
             this gate non-vacuous.
```

## 1. WHAT IS ACTUALLY WRONG — and a correction to my own earlier claim

```text
include/cfd/species/PremixedFlame.hpp:196   Real tolerance{1.0e-4};        the DECLARED default
include/cfd/species/PremixedFlame.hpp:256-8  "...sqrt(1+tolerance)-1, i.e. 0.05 % at tolerance
                                              1e-3. At cap 20 the temporal error was 80x that;
                                              at 10 it is 8.4x; at 5 it is 1.7x."
```

The comment justifying the shipped cap argues its third requirement — *coherence with the precision
the solver reports* — at `tolerance = 1e-3`, which **is not the declared default**. One of the two
has to change.

**CORRECTION, recorded because it was committed.** `audit_C3.md` §4, `dry_run_A4.md` §3 and the
commit messages for `38b3789`, `a0a93a0` and `940050c` describe `tolerance = 1e-4` as **"the shipped
configuration"** understating its error by 40x / 24x / 18x. That overstates it:

```text
src/app/ProjectRunner.cpp        ZERO PremixedFlame references -- the production path never
                                 reaches this solver at all
case-file parsers                expose no `tolerance` key; the value is unreachable from a case
every construction in the repo   51 sites, and ALL of them assign .tolerance before the solve
```

So `1.0e-4` is **dead-written**: no caller reads it. The `17.988` is what a *library-API user
relying on defaults* would get — real, and worth fixing — but **not "what ships"**, because nothing
ships through this path. This is the repository's own capability-vs-reachability rule applied to my
own finding, and it is the reason this gate claims a consistency fix rather than a behavioural one.

*(A second correction: I first reported "only two constructions exist". That came from a grep over
`results/*/tools`, which does not expand to `results/technical_debt/TD0xx_*/tools`. The real count
is 51 — 2 in tests, 49 in probes. All still assign tolerance, so the conclusion is unchanged.)*

## 2. THE CHANGE, and why this value

```text
FROM   Real tolerance{1.0e-4};
TO     Real tolerance{1.0e-3};
```

**Derived from what the value is for, not from what makes a gate pass.** The bisection locates the
eigenvalue of the *discrete* problem; refining that bracket far below the *discretisation* error
buys no accuracy and publishes a precision the answer cannot honour. At the shipped cap the
measured temporal error is `0.152210 %` (A4 dry-run), while `1.0e-4` claims `0.0085 %` — eighteen
times finer than the answer's own error. `1.0e-3` claims `0.0677 %`, the same order as the error.

`1.0e-3` is additionally the value **every caller in the repository already uses**, the value the
cap's own justification argues at, and the value all recorded flame evidence was measured at. So it
removes the inconsistency without inventing a case-specific number.

**What this deliberately does NOT do.** It does not make `C3-A4` pass: at `1.0e-3` the ratio is
`2.248`, still above 2. Full coherence at this cap would need `tolerance` in
`[1.3547e-3, 2.7113e-3)` — the window whose terminal bracket is `1.3547e-3` — giving a ratio of
`1.124`. **That value is NOT chosen here**, because selecting a production default to make a
criterion pass is fitting production to the gate. It belongs to TD-027 C3, with the cap, and is
recorded here only so the option is not lost.

## 3. CRITERIA. Execution stops at the first failure.

| id | criterion | quantity | threshold |
| --- | --- | --- | --- |
| **T1** | **no caller reads the default**, so the change cannot alter behaviour | every `PremixedFlameSettings` object construction in the repository | **every one assigns `.tolerance` before its solve**, except the two deliberate omissions in `tools/tol_probe.cpp`, which exist to prove T1 can fail |
| **T2** | **the default is what the header says** | `PremixedFlameSettings{}.tolerance`, as compiled | `1.0e-3` after the change, `1.0e-4` before |
| **T3** | **the cap's justification cites the declared default** | the `at tolerance <x>` string in the cap comment vs the `Real tolerance{<y>}` declaration | `x == y` |
| **T4** | **nothing else in the header changed** | `git diff` on `PremixedFlame.hpp` | exactly the default line plus the comment lines that cite it; no other production file touched |
| **T5** | **format stays clean** | `clang-format-18 --dry-run --Werror` over the §7 scope | exit 0 |

## 4. DRY-RUN ON THE UNCHANGED TREE (logs/TOL_01_dryrun.log)

```text
T1  51 constructions enumerated; all assign .tolerance except tol_probe.cpp's two
    deliberate omissions                                                            PASSES
T2  declared default reads 1.000000e-04                                             PASSES (pre-state)
T3  declared 1.0e-4 vs cited 1e-3                                                   *** FAILS ***
T4  n/a before the change
T5  clean as of 2026-10-12 (742 files, exit 0)                                      PASSES
```

**T3 fails on the baseline and T1/T2 pass, so the gate discriminates rather than rubber-stamping.**

## 5. NON-VACUITY

```text
T1 must be able to FAIL.  tools/tol_probe.cpp constructs settings WITHOUT assigning tolerance and
   shows the default reaching the caller unchanged:
       declared default tolerance        1.000000e-04
       after a caller sets other fields  1.000000e-04   <- the default IS live
       after a caller assigns 1.0e-3     1.000000e-03   <- and is dead-written when assigned
   So "no caller reads it" is a statement about the 51 call sites, not about the field being inert.

T2 must be able to FAIL.  It reads the compiled value, so a header edit that did not take effect,
   or a stale build, makes it fail.

T3 must be able to FAIL.  It does, on the baseline.
```

## 6. SCOPE LIMIT, stated rather than hidden

**The premixed-flame suite is NOT re-run for this change, and no flame result is re-measured.** The
justification is T1: a dead-written default cannot alter a computed value, the enumeration behind
T1 is exhaustive and machine-generated, and T1's non-vacuity control proves the enumeration is
load-bearing. Re-running the suite would cost hours — the A4 ladder alone took 9.5 h — to confirm a
value that no code path reads.

**This is a labelled proportionality judgement, not a proof by absence.** If the enumeration in T1
is incomplete, T1 is wrong and so is this limit. That is why T1 enumerates rather than samples, and
why its output is committed in full.

## 7. FREEZE

Recorded in `logs/TOL_00_freeze.log`: the sha256 of this file, of
`include/cfd/species/PremixedFlame.hpp` and `src/species/PremixedFlame.cpp` before the change, and
of `tools/tol_probe.cpp`.
