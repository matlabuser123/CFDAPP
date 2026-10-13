# TD-004 — VERIFICATION OF AMENDMENT A1 — **C2-A1 FAILS, AS PREDICTED**

```text
item        TD-004   1D premixed-flame capability · P1
amendment   GATE_A1.md  sha256 e57bc758ca5610eb7431438537de3e3211a884a9ed8a33adcefe0d3694eed42e
            frozen 2026-10-05, logs/A1_00_freeze.log
gate        GATE.md     sha256 12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73
            UNCHANGED. A1 supersedes its C2 and nothing else.
run         logs/A1_01_c2a1_verification.log, fresh, per CLAUDE.md §5.3
verdict     C2-A1 FAILS at (b) and (c). TD-004 remains OPEN / STOPPED.
```

---

## 1. The determinism guard passed first

```text
pattern GGBGGB      logs/14 measured GGBGGB      determinism confirmed
```

The probe refuses to evaluate the criterion unless the phase pattern reproduces TD-005's
independent measurement, because a criterion must not be judged against a non-reproducible signal.
It reproduces exactly — a third independent confirmation of the limit cycle, from a third probe.

## 2. The measurement

```text
relaxT   marchStp     fuel bal    admiss    S_L cm/s   delta mm
    10       3968    1.531e-02       yes     4.86230     1.2289
    14       5556    1.061e-02       yes     4.86230     1.2238
    18       7143    6.751e-01        NO     4.86230     0.4111
    22       8730    1.045e-02       yes     4.86230     1.2231
    26      10318    1.093e-02       yes     4.86230     1.2231
    30      11905    6.022e-01        NO     4.86230     0.4188
```

`delta` alternates with the phase — `1.223–1.229 mm` admissible, `0.411–0.419 mm` not — so the
whole flame structure participates, not just the balance. And `S_L = 4.86230` at **every** level:
the eigenvalue is again untouched by the cycle, now in a sixth independent family.

## 3. C2-A1, clause by clause

| clause | measured | verdict |
| --- | --- | --- |
| (a) REACHES | first admissible level `relaxT 10`, `1.531e-02 < 0.05` | **PASS** |
| (b) STAYS | `relaxT 18` leaves at `6.751e-01`; `relaxT 30` leaves at `6.022e-01` | **FAIL** |
| (c) SETTLES | `max 6.751e-01`, `min 1.045e-02`, `mean 2.208e-01`; `max−min = 6.647e-01 > mean` | **FAIL** |

**Predicted in `GATE_A1.md` §7 before the run: FAIL at (b).** Recorded there so this would be a
test rather than a confirmation. It failed at (b) *and* (c), which §5.1's dry-run also anticipated.

## 4. What the amendment achieved, and what it did not

**It did not change TD-004's outcome.** The item still fails its second criterion and remains
OPEN / STOPPED. Anyone looking only at the verdict would see no difference.

**What changed is that the criterion is now answerable.**

```text
BEFORE  C2 bounded the ratio of a MONOTONE degradation with march length. The signal is a
        constant-amplitude limit cycle, so there was no ratio to bound. NO FIX TO THE RECENTRING
        OPERATOR COULD EVER HAVE SATISFIED IT -- the item was stopped at an impossible criterion
        and the 46.9362 was a phase difference between two arbitrary samples.

AFTER   C2-A1 fails for two NAMED, OPEN defects, each with its own register item:
          TD-028  the numerical limit cycle -- suppressed at D <~ 1, so (b) becomes reachable
          TD-030  the 8.48e-02 reaction-integral residual -- what (a) then rests on
        and it is SATISFIABLE: TD-005's logs/19 sub-family satisfies (b) and (c) by 47x.
```

So the amendment converted a stop with no path forward into a stop with two specific prerequisites.
That is the whole purpose of CLAUDE.md §5.3, and it is the only thing an amendment is permitted to
do — **no threshold was weakened**: `0.05` is the tree's own bound, unchanged, and clause (c) carries
no constant at all.

## 5. The original C2 result is preserved, not superseded

```text
C2 as frozen      ratio <= 2.0,  measured 46.9362      PRESERVED in verification.md §1 and
                                                       summary.md §1. Still a real measurement of
                                                       a real quantity; the CRITERION was wrong,
                                                       not the number.
```

`GATE.md` is **unmodified** — its sha256 is unchanged at `12ccc5dd…` — and A1 lives in its own file
with its own hash, its own dry-run and its own verification, exactly as §5.3 requires.

## 6. What TD-004 now needs

```text
1. TD-028 resolved (or TD-027, which governs it: maximumCellDiffusionNumber = 20.0 admits the
   D = 3.33 at which the cycle runs). Then clause (b) becomes reachable.
2. TD-030's candidate resolved -- the 8.48e-02 steady reaction-integral excess, which is what
   clause (a) rests on once the cycle is gone.
3. then C2-A1 rerun, followed by the rest of GATE.md in frozen order from C3.
```

**None of that is authorized**, and route (a)'s own fix — the locator correction verified under the
original gate — stands and is not reverted.
