# TD-004 — ACCEPTANCE GATE, AMENDMENT A1 — replacing criterion C2

```text
item          TD-004   1D premixed-flame capability · P1 · route (a)
amends        GATE.md sha256 12ccc5dd5e3ec01358d3677d98be5c6c6906dcd8f39ebf155217d9dc851f2e73
              criterion C2 ONLY. Every other criterion in GATE.md stands unchanged.
authorized    2026-10-05, "authorize the C2 amendment"
status        DRAFT until §5's dry-run is complete and §6 records the freeze hash.
```

**CLAUDE.md §5.3 sequence, and where this file sits in it:**

```text
1. preserve the original failure verbatim        DONE -- verification.md, summary.md §1, §4.1
2. investigate independently, no production      DONE -- under TD-005, 12 probe runs, logs/10-21
3. derive the replacement from first principles  THIS FILE, §3
4. request authorization                         GRANTED 2026-10-05
5. freeze as a separate file, own dry-run        THIS FILE, §5 and §6
6. rerun fresh, reuse no earlier result          §7
```

---

## 1. Why C2 is being replaced rather than re-thresholded

C2 as frozen:

> **the balance no longer degrades with march length** — fuel balance residual at march `3968` vs
> `35717`; `ratio <= 2.0`; measured **14.6577** on the baseline.

**Its premise is false.** The quantity does not degrade monotonically with march length: it is a
**limit cycle** of period 14 relaxation times and essentially constant amplitude. TD-005 measured it
across three independent ladders — 17 verdicts, all predicted by that one period — with the
amplitude undiminished at 8 periods over an 11.6× span of march length
(`TD005_flame_speed_extraction/dry_run.md` §G–§N).

```text
the same ladder, at the DEFAULT dt (TD005 logs/14):
relaxT      10        14        18        22        26        30
balance  1.531e-02 1.061e-02 6.751e-01 1.045e-02 1.093e-02 6.022e-01
```

C2 sampled `relaxT 10` and `90` and formed their ratio. It caught one good phase and one bad one.
**There is no ratio to bound**, so no value of the threshold would have been correct, and **no fix to
the recentring operator could ever have satisfied it.** Re-thresholding would preserve the error;
the criterion has to be re-derived.

**The measured `46.9362` stands as a real measurement** and TD-004's stop at C2 was correct under its
own protocol. What is wrong is the criterion, not the result.

**Cross-check that licenses this amendment.** TD-005's probe reproduced TD-004's own C2 datum
exactly, from different code on a different day:

```text
TD-004 C2       fuel balance 6.022411e-01 at 11905 march steps (relaxT 30)
TD-005 logs/13  fuel balance 6.022e-01    at 11905 march steps (relaxT 30)
```

## 2. What C2 was actually trying to guarantee

Stated plainly, because the replacement must encode this and not the mechanism C2 assumed:

> **The reported flame state must not depend on an arbitrary choice of when to stop marching.**

That is a well-posedness and reproducibility requirement. C2 expressed it as "no monotone
degradation", which is one particular failure mode of it. A limit cycle violates the requirement
without producing any monotone trend at all, which is exactly how it slipped past.

## 3. The replacement criterion, derived

**C2-A1 — THE REPORTED STATE MUST BE INDEPENDENT OF THE MARCH LENGTH, ONCE THE MARCH IS LONG
ENOUGH.** Three clauses, over a family of march lengths:

```text
(a) REACHES      at least one level of the family has fuelBalanceResidual < 0.05
(b) STAYS        every level at or beyond that first admissible level is also admissible
(c) SETTLES      over the levels at or beyond it, (max - min) <= mean
```

### 3.1 Clause (a) and (b): the threshold's provenance

`0.05` is **the tree's own admissibility bound**, at `test_premixed_flame.cpp:447`:

> `ASSERT_LT(flame.fuelBalanceResidual, 0.05) << "the state does not conserve fuel well enough for
> the consumption identity to mean anything, so the mismatch below would be measuring the wrong
> state"`

It is **neither weakened nor tightened**, and it is the same bound TD-005's C3 uses for the same
judgement. Nothing is invented here: (a) and (b) change *where* the existing bound is applied —
from two arbitrary sample points to a family — which is precisely the defect.

**Why (a) and (b) are separate.** (a) alone would pass a march that is admissible once and then
leaves — which is the limit cycle. (b) alone would be vacuously satisfiable by a family in which
nothing is ever admissible, since "every level at or beyond the first admissible one" is an empty
condition. Each closes the other's loophole.

### 3.2 Clause (c): a settling test with NO fitted constant

```text
(max - min) <= mean,   over the levels at or beyond the first admissible one
```

**Derivation.** A quantity whose variation across the family exceeds its own mean has not settled to
a value; it is being sampled from a distribution rather than measured. This is the weakest possible
statement of "it converged", and it carries **no constant at all** — not a tolerance, not a ratio,
not an empirical factor. It is scale-free and dimensionless by construction.

It is also the clause that a limit cycle fails even if its amplitude were somehow admissible: an
oscillation between two states has `max - min` of order its own mean or larger.

### 3.3 The family, and the one place a MEASURED quantity legitimately enters

```text
relaxT in {10, 14, 18, 22, 26, 30}, at GATE.md's existing fixture
(500 cells, 10 mm, anchor 0.35, tolerance 1e-3), with every other setting at GATE.md's values.
```

**Requirement the family must meet:** it must be able to *resolve* any oscillation it has to detect.
That is a sampling requirement, not a threshold: a family spanning less than one period, or sampling
it too coarsely, cannot see a cycle however tight the threshold is. The measured period is **14
relaxation times**, so this family spans `20 / 14 = 1.43` periods at `3.5` points per period.

**On deriving from observed output (CLAUDE.md §5.3).** The *threshold* is derived from the tree's own
bound and from a constant-free settling condition — not from any measured balance. The *family* is
sized from a measured property of the system, the oscillation period, in the same way a mesh is
sized from a measured flame thickness. Sizing an instrument to resolve the phenomenon is required;
choosing a threshold to fit the observed values is what §5.3 forbids, and is not done here.

**`relaxT 10` is deliberately the first level**, because `finalMarchRelaxationTimes = 10.0` is the
solver's own default — the march length it declares sufficient. If the default is not sufficient,
(a) will say so, and that is a finding rather than a criterion defect.

## 4. What C2-A1 does NOT do

```text
* it does not bound a ratio, a trend, or a degradation -- the signal has none
* it does not relax 0.05, the tree's own admissibility bound
* it does not excuse a short march: clause (a) must be met somewhere in the family
* it does not require `steadyStateReached`, which GATE.md §F13 established is unreachable by
  construction. TD-005 §N explains why -- there was no steady state to reach -- so asserting it
  would fail for a reason unrelated to the criterion
* it does not touch any other criterion in GATE.md. C1, C3, C4, C5, C6, C6a, C6b and C7 stand
  exactly as frozen, and C2-A1 replaces C2 alone
```

## 5. Dry-run against the unchanged baseline

**MANDATORY BEFORE FREEZING** (CLAUDE.md §5.2). Each clause must be shown able to **both** pass and
fail, on real measured data.

### 5.1 The baseline family, from TD-005 `logs/14` (default `dt`, unchanged tree)

```text
relaxT      10        14        18        22        26        30
balance  1.531e-02 1.061e-02 6.751e-01 1.045e-02 1.093e-02 6.022e-01
admiss?    YES       YES        no        YES       YES       no

(a) first admissible level = relaxT 10 (1.531e-02 < 0.05)                       -> PASSES
(b) levels >= 10 all admissible?  relaxT 18 = 6.751e-01                         -> FAILS
(c) over levels >= 10: max 6.751e-01, min 1.045e-02, mean 2.218e-01
    max - min = 6.647e-01  >  mean 2.218e-01                                    -> FAILS

C2-A1 on the unchanged baseline: FAILS at (b), and (c) also fails.
```

### 5.2 Non-vacuity, clause by clause, on real data

| clause | a real PASS case | a real FAIL case |
| --- | --- | --- |
| (a) | baseline `relaxT 10`, `1.531e-02 < 0.05` | TD-005 `logs/19` at `dt/4`: **no** level is admissible (all `>= 8.45e-02`), so (a) fails there |
| (b) | TD-005 `logs/19` sub-family `relaxT {18,22,26,30}`: `8.477e-02, 8.464e-02, 8.454e-02, 8.636e-02` — none leaves | baseline `relaxT 18` leaves after `relaxT 10` was admissible |
| (c) | same `logs/19` sub-family: `max-min = 1.82e-03` against `mean = 8.51e-02` — passes by 47x | baseline: `6.647e-01` against `2.218e-01` — fails by 3x |

**Every clause has both a real pass case and a real fail case, from measured data, with nothing
constructed.** That is the strongest form §5.2 asks for.

### 5.3 A property worth stating: C2-A1 is satisfiable

Three clauses must hold together, and no configuration measured so far satisfies all three. But the
`logs/19` sub-family satisfies (b) and (c) comfortably, and the baseline satisfies (a), so the
criterion is **not** unsatisfiable in principle. It requires both of the defects TD-005 isolated to
be fixed — the limit cycle (TD-028, suppressed at `D <~ 1`) and the `8.48e-02` reaction-integral
residual (TD-030's candidate). **A criterion that currently fails because two known defects are
open is a working criterion**, which is the distinction C2 itself failed.

## 6. Freeze

```text
FROZEN 2026-10-05. The sha256 of this file, of GATE.md (unchanged), and of the production, test
and library artifacts it is frozen against are recorded in logs/A1_00_freeze.log, written in the
same action. GATE.md is NOT modified; this file supersedes its C2 and nothing else.
```

## 7. Rerun

CLAUDE.md §5.3: *"Rerun fresh. Reuse no earlier result."* §5's dry-run uses TD-005's `logs/14` to
establish **non-vacuity**, which is what a dry-run is for. The **verdict** on C2-A1 must come from a
fresh measurement on the unchanged tree, recorded as `logs/A1_01_c2a1_verification.log`, with its own
probe under `tools/`.

**Expected, from §5.1: FAIL at (b).** That is recorded here as a prediction before the run, so the
rerun is a test and not a confirmation. If it comes back otherwise — in particular if the phase
pattern differs from `logs/14`'s — the determinism this amendment rests on is in question and the
amendment stops.
