# TD-031 — AUDIT — `finalMarchRelaxationTimes = 10` is an unvalidated convergence default

```text
item        TD-031   the reported state is taken before the march has converged · P1
opened      2026-10-06, "open the march-length item first"
found by    TD-027's regression (logs/03-06) and TD-005 logs/19
status      AUDIT ONLY. No gate frozen, no production change.
```

---

## 1. What the defect is

`PremixedFlameSettings::finalMarchRelaxationTimes` defaults to **10.0**, and the header documents the
final march's purpose plainly:

> *"after the eigenvalue is bracketed, the state is marched again at that mass flux until the STATE
> stops changing, and it is that state whose profiles and balances are reported."*

Measured at the now-validated cap (TD-005 `logs/19`, 500 cells, `D = 5`):

```text
relaxT   front offset   fuel balance   delta mm
   10      -34.25 c      8.602e-02      0.9909    <- THE DEFAULT
   14     -124.70 c      4.395e-01      0.5505
   18       +1.46 c      8.477e-02      1.2781    converged
   22       +2.01 c      8.464e-02      1.2779    converged
   26       +2.56 c      8.454e-02      1.2780    converged
   30       +0.11 c      8.636e-02      1.2778    converged
```

**The march converges by relaxT 18 and the default stops at 10, inside the startup transient.** At
the default the front sits **34 cells** off the anchor and `delta` is **22 % short** of its converged
value. So the reported state — the one every profile, balance and consistency check is computed on —
is a transient, by default.

## 2. Why it was invisible until now

At the old cap the march never converged at all: it executed a limit cycle (TD-028), so there was no
converged state for relaxT 10 to fall short of. *"Converged at relaxT 10"* was a phase of the cycle,
and the phase happened to be a good one — the balance read `1.53e-02`, better than the true converged
`8.47e-02`. **Suppressing the cycle under TD-027 is what made a transient look like a transient.**

This is the same shape as two earlier findings in this campaign: TD-004's 164-cell shifts masked what
its C2 then exposed, and the limit cycle masked TD-030's reaction-integral excess. A defect hidden by
a larger defect, surfacing when the larger one is fixed.

## 3. The evidence from TD-027's regression

Four cases report a transient state, all at the default march length:

| case | measured | the transient accounts for |
| --- | --- | --- |
| `ThePlateau…` anchor 0.30 | front off by **32.4 cells** against a 10-cell tolerance | `logs/19` measures `-34.25` at relaxT 10 |
| `ThePlateau…` anchor 0.40 | front off by **35.6 cells** | same |
| `TheEigenvalueIsIndependent…` | passes on `S_L`, but `S_c` differs **20 %** and `delta` **30 %** between initial guesses | both states are transients, so each retains path memory |
| `ItConverges…`, `TheConsumptionIntegral…` | balance `0.093590` | the **magnitude**: converged is `0.0847` — see §4 |

## 4. WHAT THIS ITEM WILL AND WILL NOT FIX — a correction to my own earlier claim

I reported to the user that TD-027's two `PASS -> FAIL` transitions were *"both explained by
`finalMarchRelaxationTimes = 10`"*. **That is wrong**, and checking it against the converged values
is what shows why:

```text
ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature
   at relaxT 10 (transient)   0.093590   requires < 0.02
   CONVERGED                  0.0847     requires < 0.02   STILL FAILS, by 4.2x
   => the march length explains the MAGNITUDE, not the FAILURE. This is TD-030.

TheFlameSpeedConvergesUnderMeshRefinement
   fails because the refinement differences GROW, 0.00653 -> 0.151
   => TD-029: the temporal error no longer cancels the spatial one. NOT the march length.

TheConsumptionIntegralAgreesWithTheEigenvalue  (was already failing)
   CONVERGED 0.0847   requires < 0.05   STILL FAILS, by 1.7x   => TD-030
```

**So TD-031 does NOT unblock TD-027's closure.** What it does fix:

```text
ThePlateau…                   32.4 and 35.6 cells -> +1.46 to +2.56 at relaxT 18+, inside the
                              10-cell tolerance. A FAIL becomes a PASS.
TheEigenvalueIsIndependent…   its 20 % / 30 % state spread should collapse, since both runs
                              would then report converged states rather than transients.
```

One failing test becomes passing, and one passing test stops passing on two different states. Worth
doing on its own merits — **and it leaves TD-027's two unnamed failures exactly where they are.**

## 5. The candidate fix, and the one question it raises

```text
finalMarchRelaxationTimes{10.0}  ->  a value at or above the measured convergence point (18)
```

Cost is modest, because the final march is a minority of the work. At `D = 5`, relaxT 10 gives
~15 880 march steps out of ~44 000 total, so the trials dominate:

```text
relaxT 10   march 15 880   total ~44 000   1.00x   <- current
relaxT 20   march 31 760   total ~60 000   1.36x
relaxT 24   march 38 112   total ~66 000   1.50x
```

**The derivation question this needs, and it is not settled here.** The convergence point was
measured as 18 **at one mesh, one cap and one fixture**. `relaxT` is a multiple of the flame's own
relaxation time `alpha/S^2`, so it is nominally mesh- and `dt`-independent — but TD-027 established
that `D` is identically the cap and that a quantity being nominally physical is not the same as being
measured to be invariant. **Whether 18 holds across meshes and caps is unmeasured**, and a default
set from a single configuration is precisely the error TD-027 exists to correct. Setting `20.0`
because `18` was measured once would repeat it.

So the gate for this item has to establish the convergence point across at least a mesh pair and a
cap pair before a default is chosen — which is a larger measurement than the fix.

## 6. Scope

```text
IN     the default's value, derived from a convergence point measured across more than one
       configuration; the criteria that the reported state be converged rather than transient
OUT    TD-030's reaction-integral excess (the converged balance is 0.0847 and fails two tests
       regardless of march length)
OUT    TD-029's spatial non-convergence
OUT    TD-027, which is complete on its own six criteria and blocked only by its gate-design gap
```
