# TD-006 audit — G8 stays BLOCKED, but its PRECISION PRECONDITION is discharged, and the second G8 failure is explained

```text
STATUS:      **TD-006 REMAINS BLOCKED. No fix attempted, correctly.** 8 of its 11 declared
             dependencies are not closed. This is the AUDIT step of the campaign protocol and it
             stops there, per CLAUDE.md §1.3.
             THREE results, none of which needed a solver run:
               1. the PRECISION PRECONDITION -- the one TD-006 says must hold "before any
                  measurement" -- is DISCHARGED: 4.17 % -> 0.97 %, a 4.3x improvement, crossing
                  from "cannot resolve the band at all" to 2.86x inside it.
               2. the second G8 failure, previously recorded as UNEXPLAINED, is explained
                  EXACTLY: the test dies in its own setup assertion at 6 ms and HAS NEVER RUN
                  ITS SOLVES. Registered as TD-046.
               3. preconditions 2 and 3 (cp/lambda(T) citations; the Westbrook & Dryer mechanism)
                  are NOT met, and precondition 2's capability is the one TD-046 leaves untested.
SCOPE:       audit only. No production change, no test change, no gate change. G8's gate text and
             g8_reference.md are untouched and are NOT to be altered (TD-006's own instruction).
INSTRUMENT:  no new instrument and NO SOLVER RUN. Sources read; TD-005's frozen C3 probe
             (94710a235895ec02, frozen before TD-037) re-read; ctest's own LastTest.log from the
             2026-10-11 Release regression preserved as logs/01.
EVIDENCE:    this file · logs/01_lasttest_release_2026-10-11.log
COMMIT:      none sought for this audit beyond carrying it as evidence.
```

---

## 1. WHY IT IS BLOCKED — measured, not asserted

TD-006's row declares `TD-001…TD-005, TD-027…TD-031` plus TD-020. Parsed from the register today:

```text
TD-001  P0  CLOSED    RESOLVED
TD-002  P0  CLOSED    RESOLVED
TD-028  P0  CLOSED    RESOLVED 2026-10-10
TD-003  P0  BLOCKING  OPEN                      <- untouched; BiCGSTAB
TD-004  P1  BLOCKING  OPEN / STOPPED on one blocker (C6a-ord)
TD-005  P1  BLOCKING  OPEN / PARTIAL -- C3 passes, C6's premise obsolete
TD-020  P2  BLOCKING  OPEN  (but see §1.1 -- its CONTROL is available)
TD-027  P0  BLOCKING  REOPENED -- C3 FAILS at 4.06x
TD-029  P0  BLOCKING  RE-MEASURED, qualified then reinstated
TD-030  P1  BLOCKING  OPEN / AUDITED
TD-031  P1  BLOCKING  RE-MEASURED, confirmed and strengthened

8 of 11 NOT closed. Three are P0 and two of those (TD-027, TD-029) are items this campaign
has already touched and stopped on.
```

**So TD-006 cannot be worked, and nothing in this audit tries to.** The campaign's own rule is one
item at a time in dependency order; G8 is the last link of that chain, not a thing to attempt early.

### 1.1 One dependency is softer than the register makes it look

TD-006's row says *"TD-020's control is now AVAILABLE"*, while TD-020's own row says `OPEN`. Both
are true and they are not in conflict: TD-020 is *"no early-out when a mixture cannot sustain a
flame"*, and TD-006 needs it only as **negative control 1** (reactions disabled must fail). TD-005's
C5 dry-run established that control 1 as originally written is **unreachable** — an
`anchorTemperatureFraction` of 1.5 is refused — so the no-flame control exists only via
`reactionsEnabled = false`, which is control 3. **TD-020 therefore blocks TD-006's CONTROL, not its
measurement**, and the register's two rows should not be read as a contradiction.

## 2. PRECONDITION 1 — DISCHARGED. The method can now resolve the band.

TD-006 states the precondition itself:

> *"The extraction method's own reproducibility is 4.3 % (drift window 32 to 4.50 cm/s; 64 to 4.32
> cm/s) against a band of ±2.8 %. A method that cannot resolve the band cannot decide the gate near
> its edges. So: method reproducibility much less than 2.8 % first."*

```text
the band        35.0 <= S_L <= 37.0 cm/s   =>  half-width 2.7778 %   (Hermanns 36 +/- 1)
as recorded     driftWindow 32 vs 64       =>  4.167 %   (TD-006 rounds to 4.3 %)
                4.167 > 2.778  ->  THE METHOD COULD NOT RESOLVE THE BAND AT ALL
```

Re-measured on the fixed tree by **TD-005's C3 probe, frozen before TD-037 existed** — so this is an
independent instrument, not one built to answer this question:

```text
sweep                          spread     last increment
A driftWindow 16/32/64         0.0000 %      0.0000 %   <- the axis that GAVE the 4.17 %
B anchorFrac 0.30/0.40/0.50    0.4070 %      0.0000 %   <- 0.40 and 0.50 agree EXACTLY
C relaxT 1.5/3.0/6.0           1.4316 %      0.2712 %
D tolerance 1e-2/1e-3/1e-4     0.1905 %      0.0212 %
```

**The axis that produced the 4.167 % is now exactly zero** — bit-identical S_L and bit-identical fuel
balance at all three windows. That is TD-037's fix, confirmed by an instrument that predates it.

### 2.1 The residual budget, each knob read for what it actually is

A spread is only irreproducibility if the knob *should not* move the answer. Two of the four are
convergence parameters, where the honest residual is the **last increment**, not the full spread:

```text
driftWindow            must not matter                     0.0000 %
anchor ON plateau      0.40 vs 0.50, exactly equal         0.0000 %
anchor incl. 0.30      off-plateau level retained          0.4070 %
relaxT last increment  3.0 -> 6.0                          0.2712 %
tolerance last incr.   1e-3 -> 1e-4                        0.0212 %
dt refinement          TD-027 C2-A2, measured              0.2030 %
lattice quantum        TD-043, 2^(1/1024) - 1              0.0677 %

conservative LINEAR sum   0.9701 %   ->  2.86x inside the band
RSS, if independent       0.5343 %   ->  5.20x inside the band
improvement over record   4.3x
```

### 2.2 So the precondition is met — with a stated undecidable zone, which is the point

```text
VERDICT   the method resolves the +/- 2.78 % band with 2.86x margin, conservatively.
          It could not resolve it at all before.
LIMIT     a +/- 0.970 % zone at each edge stays undecidable:
            35.000 .. 35.340 cm/s   and   36.641 .. 37.000 cm/s
```

**This is exactly the distinction TD-006 drew** — *"a gross failure is still decisive at 4.3 %; a
near-miss is not"* — now quantified instead of estimated. A measurement landing mid-band, or far
outside, is decisive. One landing within 1 % of an edge is not, and would have to be reported as
undecided rather than as a pass or a fail.

### 2.3 What this does NOT establish, and it is a real limit

```text
NOT established   that the reproducibility TRANSFERS to the gated measurement. Every number in
                  §2 is the SYNTHETIC fuel at S_L ~ 4.955 cm/s with CONSTANT cp and lambda.
                  G8 measures methane at ~36 cm/s with CITED cp and lambda(T) -- 7x the flame
                  speed, a different mechanism with fractional orders, and a temperature-
                  dependent conductivity. Relative reproducibility is PLAUSIBLY similar and is
                  NOT measured.
NOT established   that 0.970 % is the right way to combine the residuals. The linear sum assumes
                  worst-case alignment and the RSS assumes independence; neither is proven. The
                  conservative figure is used throughout.
NOT re-measured   the 4.167 % baseline itself. It is TD-006's recorded value, carried forward.
```

## 3. PRECONDITION 2 — NOT MET, and the capability underneath it is UNTESTED

TD-006: *"then freeze `cp` and `lambda(T)` with citations, then measure once."*

```text
cp        no citation found in g8_reference.md. The synthetic fixture uses 1200 J/(kg K), which
          is a test constant, not a cited methane value.
lambda(T) no citation found. AND -- see below -- the only test that checks lambda(T) is
          consumed at all has never executed its own solves.
```

### 3.1 lambda(T) IS consumed. That part is sound, and was verified by reading the source.

```text
PremixedFlame.cpp:1052  conductivityField = evaluatePropertyField(mesh, temperature, *conductivity)
PremixedFlame.cpp:1061  gamma = conductivityField[c] / (cp * lewisNumber)
```

Per-cell, from the temperature field. So lambda is **not** frozen at the inlet, and the defect the
failing test is named for does not exist. Lines `:478`, `:589` and `:718` separately evaluate lambda
at `T_adiabatic` to set `dt` and the seed width, which is deliberate and documented in place.

### 3.2 But its sole test never runs. That is TD-046.

`tests/unit/species/test_premixed_flame.cpp:627`, from the 2026-10-11 Release regression:

```text
Expected: (risingConductivity.value(kExpectedAdiabatic)) > (3.0 * kConductivity),
  actual: 0.11000000000000001 vs 0.15000000000000002
[  FAILED  ] ... (6 ms)
```

```text
LinearProperty(0.05, 300, 4.0e-5)  ->  lambda(1800) = 0.05 + 4.0e-5 * 1500 = 0.11   = 2.2x
the assertion demands                                            > 3.0 * 0.05 = 0.15   = 3.0x
```

**The fixture and its own precondition disagree arithmetically, so the test fails in setup at 6 ms
and everything after line 627 has never executed** — both solves, the 1.3x flame-speed comparison
that is the actual property, and the thickness comparison. The test's comment says *"a conductivity
that triples across the front"*; the slope needed for that is `6.667e-5`, not `4.0e-5`.

```text
CONSEQUENCE   "lambda(T) is really consumed, not silently frozen" is UNTESTED, not failing.
              This test is the SOLE coverage -- no other test passes a non-constant conductivity
              to solvePremixedFlame. §3.1 establishes the property by INSPECTION only.
```

**And this corrects my own TD-007 classification.** `TD007_regression/verification.md` §5 recorded
this failure as *"not explained by anything measured today"* and listed it as an open known failure.
It is now explained completely, and it is not a solver defect at all.

### 3.3 I predicted the wrong cause first, and the recorded duration is what caught it

My first hypothesis was a real mechanism and it was wrong. `:718` makes `dt` inversely proportional
to `lambda(T_ad)`, `:1419` makes the required step count proportional to it, and `:1423` caps that
count at an **unscaled** `maximumStepsPerTrial{200000}` — so a rising lambda should shrink `dt`,
inflate the requirement, hit the cap and report a *slower* flame. That is TD-027's mechanism
exactly, and it predicted this failure.

**It is not the cause: the test never reaches a solve.** What refuted it was the cheapest possible
check — the recorded duration, `6 ms`. A hypothesis about truncating a 200 000-step march cannot
explain a 6 ms failure. Reading the duration before running anything avoided a roughly 2 h two-solve
run that would have measured nothing.

```text
The chain at :718/:1419/:1423 is REAL and is NOT refuted -- it is simply not what this test hit.
It remains a live concern for any lambda(T) G8 run, and it is TD-027's scope, not TD-006's.
```

## 4. PRECONDITION 3 — the mechanism is NOT wired, and that is partly by design

```text
Westbrook & Dryer appears ONLY in comments, in two headers:
  include/cfd/species/ReactionMechanism.hpp:82,110-112
  include/cfd/io/case/PhysicsConfig.hpp:197-198
documenting why the API accepts FRACTIONAL reaction orders -- which WD Table II Set 3 needs.
No built-in methane mechanism exists; a case file supplies orders and Arrhenius parameters.
```

**The API supports what the mechanism requires.** That is the part that matters, and it is a
deliberate choice recorded in `ReactionMechanism.hpp:110-112`: the API *refuses* mechanisms whose
rate grows without bound as the fuel vanishes, quoting Westbrook & Dryer against their own best-fit
set. So wiring Set 3 is a case-authoring task, not a code task — but it is not done.

**The synthetic fuel is also deliberate and must stay.** `g8_reference.md` §2: debugging against
methane would mean every decision was taken with the gated answer visible. So the methane run is
one-shot, last, after the dependencies close.

## 5. WHAT IS NOT ESTABLISHED

```text
NOT established  that G8 would pass. Nothing here measures a methane flame; no solver was run.
NOT established  that the 8 open dependencies all genuinely gate G8. They are TD-006's own
                 declared list, audited for STATUS here, not re-derived for NECESSITY.
NOT established  that cp = 1200 J/(kg K) or any lambda(T) law is appropriate for methane. Both
                 need citing before a measurement, and neither is cited.
NOT measured     whether the :1423 unscaled cap would bind for a real lambda(T) methane case.
                 §3.3 shows the mechanism exists; its magnitude for that case is unknown.
NOT done         TD-046's fix. It is a TEST change inside blocked TD-006's scope, so it is
                 registered and reported, not made (CLAUDE.md §2).
NOT run          G8 itself, TD-007's eleven-gate re-qualification, and any new regression. The
                 only run referenced is the 2026-10-11 Release regression already on record.
```
