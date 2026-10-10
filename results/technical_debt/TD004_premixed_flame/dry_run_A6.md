# TD-004 GATE_A6 — DRY-RUN AND NON-VACUITY. Both clauses can pass and can fail. Qualified for freezing.

```text
STATUS:    DRY-RUN COMPLETE. Both clauses executed against the UNCHANGED baseline and against the
           fixed tree, on TWO triplets, from TWO libraries. All four verdict states demonstrated.
           GATE_A6.md is qualified for freezing; it is NOT yet frozen when this file is written.
METHOD:    the clauses are arithmetic on measured S_L and fuel-balance values, so the dry-run
           evaluates them against the recorded two-library measurements. No new solve is required
           for the DRY-RUN -- but §5.3 step 6 still requires a FRESH verification run afterwards,
           and the post-fix rows below do NOT serve as that.
SOURCES:   logs/08_fixture_selection.log   PRE-FIX,  library 3b94eb84… (dac29920 source)
           logs/A5_01_c6_c6a.log           POST-FIX, library 852555cf… (fa815c97 source)
           TD043_eigenvalue_lattice/audit.md   the lattice: ratio 2^(1/1024), spacing 0.067713 %
```

---

## 1. The measurements the clauses are evaluated against

```text
            S_L cm/s                                   fuel balance
cells       PRE-FIX     POST-FIX                       PRE-FIX   POST-FIX
   63       3.88104     4.51340                         0.8021     0.5197
  125       4.40773     4.90527                         0.4703     0.1505
  250       4.77424     4.94863                         0.5457     0.0283
  500       4.86230     4.95533                         0.0433     0.0063
```

Two libraries, four levels each, measured by the SAME frozen instrument
(`fixture_probe.cpp`, `e9d83b3a…`, hash-verified in both runs).

## 2. C6a-bal — the balance clause

```text
bound = 0.02 * (500/cells)^2 :    125 -> 0.3200     250 -> 0.0800     500 -> 0.0200

                125               250               500            verdict
PRE-FIX     0.4703  FAIL      0.5457  FAIL      0.0433  FAIL        FAIL
POST-FIX    0.1505  PASS      0.0283  PASS      0.0063  PASS        PASS
```

**The baseline fails at every level and the fixed tree passes at every level.** The clause is
non-vacuous and it still detects the pre-fix defect — which is the property the original flat `0.02`
had and must not be lost. Note the baseline fails the 500-cell level too, at the bound that did not
change (`0.0433` against `0.0200`), so the correction is not what rescues the fixed tree there.

## 3. C6a-ord — the order clause, on both triplets

One quantum is `(2^(1/1024) - 1)` of `S_L` at the middle level.

```text
PRE-FIX  {125, 250, 500}
  diffs  +3.6651e-01  +8.8060e-02 cm/s   =  113.4 and  27.2 quanta   both >= 4 -> RESOLVED
  p = 2.057   interval (1.99, 2.12)   width 0.13   -> inside [1.5, 2.5]        PASS

POST-FIX {125, 250, 500}
  diffs  +4.3360e-02  +6.7000e-03 cm/s   =   12.9 and   2.0 quanta   n2 < 4 -> UNRESOLVED
  the criterion cannot be evaluated; recorded as UNRESOLVED, which is a STOP   UNRESOLVED

PRE-FIX  {63, 125, 250}
  diffs  +5.2669e-01  +3.6651e-01 cm/s   =  176.5 and 122.8 quanta   both >= 4 -> RESOLVED
  p = 0.523   interval (0.50, 0.54)   width 0.04   -> OUTSIDE [1.5, 2.5]        FAIL

POST-FIX {63, 125, 250}
  diffs  +3.9187e-01  +4.3360e-02 cm/s   =  118.0 and  13.1 quanta   both >= 4 -> RESOLVED
  p = 3.176   interval (3.06, 3.30)   width 0.24   -> OUTSIDE [1.5, 2.5]        FAIL
```

## 4. NON-VACUITY — all four verdict states occur on real data

```text
PASS        pre-fix  {125,250,500}   resolved, interval inside the band
FAIL        pre-fix  {63,125,250}    resolved, p ~ 0.52, far BELOW the band
FAIL        post-fix {63,125,250}    resolved, p ~ 3.18, ABOVE the band
UNRESOLVED  post-fix {125,250,500}   a difference below the 4-quantum floor
```

**The order clause is non-vacuous in both directions.** It fails below the band on one library and
above it on the other, which is stronger than a single negative control: the two failures bracket
the band from opposite sides, so the clause is not merely able to fail but able to distinguish *how*
it fails.

### 4.1 The 4-quantum floor is itself non-vacuous

Three of the four combinations are RESOLVED and one is not, so the precondition is neither always
true nor always false on real data. It is not a formality that passes by construction.

### 4.2 What the dry-run reveals that the amendment did not anticipate

`{63, 125, 250}` **fails on BOTH libraries**, at `p ~ 0.52` pre-fix and `p ~ 3.18` post-fix. The
63-cell level is admissible by C6a's own resolution guard (5.07 cells of front, > 4) yet produces an
order nowhere near 2 on either tree.

That is independent evidence for what `fixture_probe`'s own header warned about — *"a coarser
triplet is LESS likely to be asymptotic"* — and it means the asymptotic range for this family starts
**above 63 cells**. It is recorded here because it bears on any future attempt to cheapen the
fixture further, and because it was not predicted by GATE_A6.md.

## 5. What this dry-run does NOT establish

```text
NOT a verification  §5.3 step 6 requires a FRESH run that reuses no earlier result. The post-fix
                    rows above come from logs/A5_01 and are the DRY-RUN, not the verification.
NOT established     that p ~ 2.69 is the post-fix family's true order. The whole point of the
                    resolution precondition is that this measurement cannot say.
NOT established     that h^2 is the balance's true convergence rate. Measured 2.17-2.41 on the
                    fixed tree over two intervals; h^2 is used as a conservative formal-order
                    bound, and two intervals do not establish a rate.
NOT re-measured     the 63-cell level's admissibility. §4.2 reads its order from recorded values;
                    whether 63 cells belongs in any family is a question for the fixture, not for
                    this amendment.
NOT changed         any production file or any other criterion.
```
