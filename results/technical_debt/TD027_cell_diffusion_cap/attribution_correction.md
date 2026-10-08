# TD-027 — ATTRIBUTION CORRECTION — the cap does NOT govern the limit cycle

```text
recorded    2026-10-06, after TD-031's convergence-point measurement (TD031 logs/01)
corrects    an explanatory claim made in TD-027's audit.md §2/§3, GATE.md §4.4, and TD-005's
            dry_run.md §M.4 / §N
affects     the MECHANISM attributed to the fix. It does NOT affect any criterion or verdict.
```

---

## 1. The claim, and the measurement that refutes it

**Claimed** (TD-027 `GATE.md` §4.4, and in the same words in `audit.md` and TD-005 `dry_run.md` §N):

> *"TD-028's limit cycle is governed by this cap … at `D = 0.833` in the old convention — `D = 5` in
> the code's — four march lengths over a 1.67x range agreed to 2 %."*

**Measured** (TD-031 `logs/01`, which swept the convergence point across a mesh pair and a cap pair
precisely because a default derived from one configuration is what TD-027 exists to prevent):

```text
cells  cap   D    dt            limit cycle
 250    5    5   3.7699e-05    PRESENT       <- SAME D as the row below
 500    5    5   9.4246e-06    suppressed
 500   10   10   1.8849e-05    PRESENT
 500   20   20   3.7699e-05    PRESENT
```

**Same `D = 5` at two meshes: suppressed at 500 cells, present at 250.** So `D` is not the governing
parameter, and TD-027's C4 passing at `D = 5` did not establish that it is.

The pattern instead follows **absolute `dt`**:

```text
dt 9.4246e-06   no cycle
dt 1.8849e-05   cycle
dt 3.7699e-05   cycle   -- and at BOTH (cap 5, 250 cells) and (cap 20, 500 cells), which is the
                           decisive pair: identical dt, different D, same behaviour
```

That last line is the cleanest statement available: two configurations with the **same `dt` and
different `D`** behave the same, while two with the **same `D` and different `dt`** do not.

## 2. What survives, and what does not

```text
SURVIVES   TD-027's C4 verdict. "The march converges at the gated fixture" was measured at
           500 cells / cap 5 and PASSED: max-min 8.07e-04 against mean 8.48e-02, by 105x, and
           TD-005 logs/19 measured the same configuration independently. The fix WORKS where it
           was verified.
SURVIVES   C1, C2, C3, C5, C6 -- none of them concerns the cycle.
SURVIVES   the reason cap 5 was CHOSEN: the +3.99 % S_L error, the asymptotic range, and the
           coherence with the reported bracket. None of those rests on the cycle attribution.

DOES NOT   the claim that the cap GOVERNS the cycle. Refuted.
DOES NOT   "TD-028 resolves as a consequence of TD-027" (GATE.md §5.1). TD-028 is SUPPRESSED AT
           ONE CONFIGURATION, not resolved. It remains open and its register entry is corrected.
```

**The criterion was about the fixture; the prose was about the mechanism.** C4 asked whether the
march converges at the gated fixture and it does. I then explained *why* in terms of `D`, and that
explanation was wrong. The distinction matters because the verdict is still usable and the
explanation is not.

## 3. Where the claim appears, and why two files are NOT edited

```text
TD-027 GATE.md §4.4, §5.1     FROZEN, sha256 56080f7f... recorded in logs/00_freeze.log and still
                              matching. NOT EDITED.
TD-027 audit.md §2, §3        FROZEN, sha256 abb4a620... recorded in the same log and still
                              matching. NOT EDITED.
TD-027 regression.md          a running record. Corrected in place, pointing here.
TD-005 dry_run.md §M.4, §N    a running record, appended to throughout that item. Corrected in
                              place, pointing here.
TECHNICAL_DEBT.md TD-027/028  corrected in place.
```

**Why the frozen pair is left alone.** Their hashes are the freeze record: editing them would break
the only evidence of what the gate committed to before production changed, which is the entire point
of recording a hash. CLAUDE.md §4 requires an invalid result to be marked in place, and §10 requires
release evidence to be immutable with *"corrections recorded going forward"*. For an artefact whose
hash IS the commitment, going forward is the only option that preserves both — so this file is the
correction, and the frozen files are left bit-identical to what the freeze log attests.

**Anyone reading `GATE.md` §4.4 or `audit.md` §2–§3 should read this file with them.** Both remain
accurate about every criterion and threshold; both contain a refuted mechanism.

## 4. Consequences

```text
TD-028   NOT resolved by TD-027. Suppressed at 500 cells / cap 5 only. The governing parameter is
         dt in absolute terms, not D, and no threshold on dt has been derived -- the three
         measured points bracket it between 9.42e-06 and 1.88e-05 and nothing finer is known.
TD-031   its premise is also refuted: there is no convergence point to set a default from at two
         of three configurations, because the cycle is still running there. Its fix therefore
         CANNOT be a default value -- it has to be a convergence criterion the march tests for
         itself. That was the outcome pre-registered in TD031's probe header as the consequential
         one, and it is what occurred.
TD-027   still not closeable, and now for two reasons: the gate-design gap in regression.md §6.1
         (two unnamed PASS -> FAIL transitions), and this correction, which removes the claim that
         TD-028 resolves alongside it.
```

## 5. The pattern this makes three of

```text
D was "a cap"                       -- turned out to be an IDENTITY: dt sits exactly at it
the cycle period was "in the flame's own time unit"  -- MOVED with the mesh anyway
the cap "governs the cycle"         -- the cycle follows absolute dt, not D
```

Three times in this campaign a quantity that is nominally dimensionless or nominally physical has
failed to govern what I attributed to it. The practical lesson is narrow: **a parameter being
expressed in natural units is not evidence that the behaviour scales with it**, and the only thing
that establishes it is varying the other parameters at fixed value — which is what TD-031's mesh
pair and cap pair did, and what neither TD-027's gate nor its audit had done.
