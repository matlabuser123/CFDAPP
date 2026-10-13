# TD-004 C6a-ord — AUDIT. The resolution precondition is counted in a unit the probe itself chooses, and the remedy GATE_A6 called a production change is one line in a probe.

```text
STATUS:      AUDIT ONLY. No production file, no test, no gate, no threshold changed. Nothing was
             run: the machine is committed to TD-027's GATE_A4 fresh verification for ~9.5 h and
             the hardware policy forbids launching anything heavy beside it. This audit needed no
             solver.
METHOD:      read GATE_A6's criterion and C6a's own probe, then re-derive the recorded verdict.
             The derivation reproduces GATE_A6's recorded uncertainty interval (2.00, 3.81)
             -- computed here as (1.993, 3.801), agreeing to its printed precision -- which is what makes the claims below measurements rather than opinions.
VERDICT:     C6a-ord's UNRESOLVED state is removable at a cost of ~30-45 min, NOT by a production
             change. Doing so is predicted to convert UNRESOLVED into a DEFINITE FAIL, which is a
             real result and not an unblock.
REQUESTED:   authorization for a §5.3 amendment to GATE_A6's C6a-ord (§5). Not taken.
```

---

## 1. WHERE C6a-ord STANDS, AND WHY

```text
triplet {125, 250, 500}   S_L 4.90527 / 4.94863 / 4.95533      wall 17 + 88 + 713 = 818 s
differences in quanta     n1 = 12.94   n2 = 2.00   floor = 4   -> UNRESOLVED
```

GATE_A6 §3(a) requires both successive differences to exceed **4 lattice quanta**, "where one
quantum is `(2^(1/1024) - 1)` of `S_L` at the middle level". `n2 = 2.00` is below it, so the order
is recorded UNRESOLVED — a stop, explicitly not a pass.

§3(b)'s interval, re-derived here to confirm I am reading the criterion correctly:

```text
p_lo = log2((n1-1)/(n2+1)) = log2(12/3)  = 2.000
p_hi = log2((n1+1)/(n2-1)) = log2(14/1)  = 3.807      -> (2.00, 3.81), as GATE_A6 records
```

## 2. THE UNIT IN THE PRECONDITION IS NOT A CONSTANT

`2^(1/1024) - 1` is not an independent property of the solver. TD-027's A4 work established — from
`PremixedFlame.cpp:1832`, and confirmed by measurement — that the eigenvalue lattice **is the
terminal bisection bracket**: phase 1 brackets to `high/low = 2` exactly, phase 2 takes the
geometric midpoint, so after `k` bisections the bracket is `2^(1/2^k) - 1`, and the loop stops at
the first `k` with that `<= settings.tolerance`.

```text
tolerance 1e-3   ->  k = 10   q = 6.771307e-04      <- this is C6a-ord's hardcoded quantum
tolerance 1e-4   ->  k = 13   q = 8.461627e-05      <- 8.002370x finer (NOT exactly 8)
```

**So the unit in which C6a-ord counts its resolution is set by the tolerance the probe itself
chooses.** `tools/c6a_amended_probe.cpp:153` sets `s.tolerance = 1.0e-3`.

## 3. A CORRECTION TO FROZEN EVIDENCE, carried forward

`GATE_A6.md` §4 records:

> "does NOT settle whether `p ~ 2.69` is the family's true order. Resolving that needs a finer
> `settings.tolerance` (TD-043), **which is a production change outside this scope**"

**The first half is right and the second half is wrong.** A finer tolerance *is* the remedy, but it
is not a production change: C6a's triplet is produced by a standalone probe that sets
`s.tolerance` itself, exactly as every other probe in `results/` does. Changing it is one line in
`tools/c6a_amended_probe.cpp`, and no file under `src/` or `include/` is involved.

The claim appears to conflate *"the probe must request a finer tolerance"* — free — with *"the
shipped default must change"* — production. Only the former is needed, and the remedy has been
available since A6 was written. `GATE_A6.md` and `verification_A6.md` are frozen and are **not
edited**; this is the correction forward (§4, §10).

*(Separately, and not relevant to C6a: the shipped default did change, from `1.0e-4` to `1.0e-3`,
under its own gate GATE_TOL in commit `7c2056a`. That moved the default **looser**, not finer, and
it does not affect this probe, which sets its own value.)*

## 4. PRE-REGISTERED PREDICTION, stated before any run

The differences are physical; what changes with tolerance is the **resolution they are known to**.
Counting them in the finer quantum:

```text
                         at tol 1e-3        at tol 1e-4 (8.002370x finer)
n1                          12.94              103.6
n2                           2.00               16.0
floor                        4                  4
precondition             n2 FAILS           both PASS  -> RESOLVED

p_lo = log2((103.6-1)/(16.0+1)) = log2( 6.0353) = 2.593
p_hi = log2((103.6+1)/(16.0-1)) = log2( 6.9733) = 2.801
interval                                     [2.593, 2.801]
band                                         [1.5, 2.5]      -> ENTIRELY ABOVE
```

**C6a-ord is therefore predicted to go from UNRESOLVED to a definite FAIL.** That is not a
disappointment to be engineered around: `verification_A6.md` §6 already predicted it —
*"the likelier outcome is a DEFINITE failure. Tightening tolerance is a remedy for the UNRESOLVED
state, not for the order."* This audit agrees, and now quantifies it.

What it would **establish**, which §6 lists as explicitly NOT established today: *"that `p ~ 2.69`
is the family's true order, or that it violates the band."*

## 5. THE AMENDMENT THIS REQUESTS

Running at a finer tolerance while still counting in `2^(1/1024) - 1` would be incoherent: the
criterion's `(N-1, N+1)` bound treats one quantum as the measurement's own uncertainty, so the unit
must be the resolution actually achieved. That makes this an **instrument amendment to a frozen
gate**, hence §5.3 and hence authorization.

```text
C6a-ord-A7   unchanged in logic and threshold. One substitution:
             "one quantum is (2^(1/1024) - 1) of S_L at the middle level"
          -> "one quantum is the TERMINAL BISECTION BRACKET at the tolerance the triplet was run
              at, 2^(1/2^k) - 1 for the first k with that <= tolerance -- which is
              (2^(1/1024) - 1) at tolerance 1e-3 and (2^(1/8192) - 1) at 1e-4"
             The 4-quantum floor is UNCHANGED. The band [1.5, 2.5] is UNCHANGED. §3(b)'s formula
             is UNCHANGED. The triplet {125, 250, 500} is UNCHANGED.
             The probe sets tolerance 1e-4 instead of 1e-3. No production file changes.
```

**Non-vacuity it must show:** the amended criterion must still report UNRESOLVED when run at
`1e-3` (reproducing `n2 = 2.00` and today's verdict), and RESOLVED when run at `1e-4`. A criterion
that resolves at both tolerances is not measuring resolution.

**Cost:** the triplet is 818 s at `1e-3`. A4 measured the same `k = 10 -> 13` change costing about
2x, because the extra bisections sit nearer the eigenvalue where drift takes longer to settle, so
**~1600-2500 s, call it 30-45 minutes**. It must wait for GATE_A4's run to finish; the hardware
policy is not negotiable and the probe's own guard would refuse to start.

## 6. WHAT THIS WOULD *NOT* SETTLE — and it is the more important blocker

A resolved `p ≈ 2.7` admits **two** readings, and a finer tolerance cannot separate them:

```text
(i)  the scheme's observed order on this family really is ~2.7, so the frozen [1.5, 2.5] band is
     violated and that is a finding about the discretisation;
(ii) {125, 250, 500} is not yet in the asymptotic range, so the measured order is a
     pre-asymptotic artifact and the band is being applied outside its domain.
```

`verification_A6.md` §6 already shows the family is fragile at its coarse end: `{63, 125, 250}`
gives `p = 3.176` post-fix and `0.523` pre-fix, failing on **both** libraries, so 63 cells is
outside the asymptotic range. Whether 125 is inside is **not established**.

Separating (i) from (ii) needs a **finer mesh triplet**, not a finer tolerance — and
`{250, 500, 1000}` is unaffordable: the 1000-cell level was tested against the real
`ctest --timeout 7200` cap and **killed at exit 124**, which is why C6a's cheap triplet is forced.

**So the honest summary is that the tolerance amendment buys a definite verdict, and the verdict
will not be interpretable as a band violation until the mesh-family question is settled.** That is
worth saying before spending the 30-45 minutes, not after.

## 7. WHAT IS NOT ESTABLISHED

```text
NOT established  that n1 and n2 scale by exactly 8.002370. The ratio is NOT 8: q(1e-3)/q(1e-4)
                 = 8.002370, which is why 103.6 appears above and not 103.5. The differences are
                 physical and the quantum shrinks by that ratio, so the counts should; but they are also subject to the
                 +/-0.5-quantum centring of each level, so 103.6 and 16.0 are PREDICTIONS and the
                 run may land a quantum either side. The precondition has margin for that; the
                 interval does not, which is why §4's interval is pre-registered rather than relied on.
NOT established  that p ~ 2.7 violates the band. See §6.
NOT re-measured  C6a-bal (PASSES), the admissibility clauses, C6b, C7. The gate stops at C6a-ord.
NOT changed      anything. No file under src/, include/, tests/ or any frozen gate.
```
