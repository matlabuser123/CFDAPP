# TD-027 C3 — AUDIT. C3's numerator and denominator are COUPLED through `settings.tolerance`, so the criterion counts lattice steps instead of measuring coherence. §12 STOP.

```text
STATUS:      **§12 STOP — a frozen criterion is structurally unable to express its own threshold.**
             No production change, no test change, no gate change, no threshold change. C3 still
             FAILS at 4.06x and that failure stands exactly as recorded in verification_A3.md.
SCOPE:       AUDIT ONLY, by derivation from the source and the already-recorded A3 measurements.
             Nothing was run: the machine was held by an unrelated ctest throughout, and this audit
             needed no solver (CLAUDE.md hardware policy).
METHOD:      read `PremixedFlame.cpp`'s bracketing and bisection, then re-derive C3's recorded
             value from first principles. The derivation REPRODUCES the recorded 4.06 exactly
             (4.0638 against a recorded 4.06, E = 0.203139 % against a recorded 0.2030 %), which
             is what makes the structural claims below measurements rather than opinions.
VERDICT:     C3 is a **contaminated instrument**, not an obsolete constant (CLAUDE.md §5.3). The
             property it names is real and still untested; its threshold of 2 is unreachable as a
             boundary; and its most comfortable PASS is its least informative measurement.
REQUESTED:   authorization for a §5.3 amendment (§6). Not taken, and not to be taken implicitly.
```

---

## 1. FIRST: C3's INSTRUMENT IS SOUND IN KIND, so this is not that complaint

`verification_A3.md` §5 left open whether `massFluxResidual` is the right reported-precision proxy.
It is. Verified in the source:

```text
PremixedFlame.cpp:1857   result.massFluxResidual = (high - low) / low;
PremixedFlame.hpp:636    "(high - low)/low: a genuine BRACKET on mdot, not a step size"
PremixedFlame.cpp:1862   status = (massFluxResidual <= settings.tolerance) ? Converged : MaxIterations
```

It is a real, measured bracket on the eigenvalue, it is what the solver publishes, and it is what a
reader would take as the precision. **C3 names the right quantity.** The defect is elsewhere.

## 2. WHERE TD-043's LATTICE COMES FROM — it is not an independent constant

Derived from the search, not assumed:

```text
PHASE 1  bracket   massFlux *= 2.0 or *= 0.5 until bracketed  ->  high/low = 2 EXACTLY
PHASE 2  bisect    massFlux = sqrt(low*high), the GEOMETRIC midpoint
                   while (high - low)/low > settings.tolerance
```

A geometric midpoint splits a ratio `r` into two intervals of ratio `r^(1/2)`. Starting from
`r = 2`, after `k` bisections the bracket is

```text
massFluxResidual(k) = 2^(1/2^k) - 1
```

and the loop stops at the first `k` with that `<= tolerance`. At `tolerance = 1e-3` the first such
`k` is **10**, giving `2^(1/1024) - 1 = 6.771307e-04`.

**That is TD-043's lattice quantum, to every digit.** The lattice is not a separate phenomenon: it
is the terminal bisection bracket, and its value is a function of `settings.tolerance` alone.

## 3. THE DEFECT: NUMERATOR AND DENOMINATOR ARE THE SAME KNOB

```text
C3   threshold:  E / B  <=  2
     E = C2's measured temporal error  =  (a difference of S_L values across a dt ladder)
     B = the published precision       =  sqrt(1 + tolerance) - 1
```

Every `S_L` in `E` is a bracket centre produced by the search above, so `E` is resolvable only in
units of the terminal bracket `q`. And `B` is set by the same `tolerance` that sets `q`. So:

```text
q  lies in (tolerance/2, tolerance]   -- consecutive brackets halve, and the previous k FAILED
B  ~ tolerance/2                      -- sqrt(1+t)-1 = t/2 - t^2/8 + ...
=> q / B  lies in (1, 2]              for EVERY tolerance
```

Measured across a range, to show this is not an asymptotic hand-wave:

```text
tolerance   k    terminal q        B                 q/B
1e-2         7   5.429901e-03      4.987562e-03      1.0887
1e-3        10   6.771307e-04      4.998751e-04      1.3546   <- the gated fixture
5e-4        11   3.385081e-04      2.499688e-04      1.3542
1e-4        13   8.461627e-05      4.999875e-05      1.6924   <- the PRODUCTION default
1e-5        17   5.288307e-06      4.999988e-06      1.0577
```

### 3.1 The consequence: C3's threshold of 2 is in an UNREACHABLE GAP

`E` is an integer number of quanta, so the only values C3 can return are `m * (q/B)` with
`q/B` in `(1, 2]`:

```text
m = 0   ratio 0        PASS      the dt ladder did not move at all
m = 1   ratio 1.3546   PASS      one quantum  (in (1,2], so always a PASS)
m = 2   ratio 2.7092   FAIL      two quanta   (in (2,4], so always a FAIL)
m = 3   ratio 4.0638   FAIL      <- THE RECORDED RESULT: measured 2q + tail 1q
```

**No value strictly between 1.3546 and 2.7092 is reachable, and the threshold 2 sits inside that
gap.** So C3 is not the test its derivation describes. GATE.md §4.3 states the intent as "within 2x
the two are the same order"; what the criterion actually computes is the integer test *"did the dt
ladder plus its tail move by at most one terminal bracket width?"* — for any tolerance.

### 3.2 And C3 scores BEST when the measurement is WORST

The lowest ratio, `0`, is returned when the dt ladder is completely flat across its halvings — that
is, when the refinement resolved nothing. `GATE_A3.md` already identified a zero difference as the
thing that made the ORIGINAL C2 reference arbitrary. So C3's most comfortable PASS is its least
informative state, and its only non-vacuous PASS is a single quantum, where the measurement is at
its own resolution floor. **A criterion that rewards an unresolved measurement is inverted**, and
that is a §12 condition ("a gate criterion looks mis-derived, contradictory or vacuous after
freezing") rather than something to work around.

### 3.3 Why the original derivation was nonetheless sound

C3 was derived on cap 20 / 10 / 5 data, where `E` was `3.99 %`, `0.422 %` and `0.084 %` — that is
59, 6 and 1.2 quanta. At cap 20 the numerator was 59 quanta and the lattice was irrelevant, so the
criterion discriminated honestly and the recorded `80x / 8.4x / 1.7x` ladder is real.

**C3 became lattice-bound only when C2-A2 replaced an extrapolated reference with a measured
difference**, which moved `E` from a (wrongly placed) `0.084 %` to an honest `0.2030 %` — three
quanta. The contamination was always latent; A2 is what made it reachable. This is the same
unmasking pattern recorded three times in this campaign, and the rule still applies: keep the fix,
keep the threshold, stop the item.

## 4. A SECOND, SEPARATE FINDING: the shipped default is judged at a tolerance it does not use

```text
PremixedFlame.hpp:196      Real tolerance{1.0e-4};        <- the PRODUCTION default
test_premixed_flame.cpp:188 settings.tolerance = 1.0e-3;  <- what the GATED fixture sets
PremixedFlame.hpp:256-258  the comment justifying the cap default cites
                           "sqrt(1+tolerance)-1, i.e. 0.05 % at tolerance 1e-3"
```

The production comment that justifies `maximumCellDiffusionNumber = 5` argues its third requirement
— "COHERENT WITH THE PRECISION THE SOLVER REPORTS" — at `tolerance = 1e-3`, which is **not the
default**. At the actual default the published bracket is `5.00e-05`, ten times finer, so the same
physical error of `0.203139 %` reads:

```text
gated fixture   tolerance 1e-3    ratio  4.06
PRODUCTION      tolerance 1e-4    ratio 40.63
```

**The shipped configuration understates its temporal error by ~40x, an order of magnitude worse than
the 4.06x on record**, and the comment asserting coherence for that default evaluates it at a
different tolerance. This is a real finding about the production default, not about the gate. It is
recorded and NOT fixed: changing either the default or the comment is a production change needing
its own authorization (CLAUDE.md §2), and the comment is also load-bearing for the cap's own
justification, so it should be corrected together with whatever C3 is amended to.

## 5. WHAT IS *NOT* ESTABLISHED

```text
NOT established  that E is forced to integer quanta in general. It IS integer quanta on the
                 recorded A3 ladder (increments +1, 0, +1; measured 2q; tail 1q) and the bracket
                 CENTRES lie on a multiplicative grid set by the terminal bracket, so sub-quantum
                 resolution of E is not available from this instrument. Whether some ladder could
                 land between quanta is not proven and does not affect §3.1's conclusion, which
                 only needs the resolution floor.
NOT established  the temporal order p. The lattice forbids it on S_L; this audit does not change
                 that, and TD-043 still reaches into C3 as it does into TD-029 and TD-004 C6.
NOT established  that any particular cap value would pass C3. §3.1 shows the question is
                 ill-posed as C3 is instrumented: "reduce the cap until C3 passes" means "reduce
                 the cap until the ladder moves by at most one quantum", which is a statement
                 about the bisection tolerance, not about the discretisation.
NOT re-measured  C1, C2-A2, C4, C5, C6, C7. Nothing was run. C2-A2's PASS stands.
NOT changed      C3, its threshold, the cap, `tolerance`, or any production file.
```

## 6. THE AMENDMENT THIS AUDIT REQUESTS, derived rather than left blank

§5.3 steps 1-3 are done: the failure is preserved, the investigation changed no production code, and
the replacement below is derived from the structure rather than from the observed value. **Step 4 is
a request for authorization, and this document stops there.**

**The requirement.** `E` must be measured with a resolution that is not set by the same knob as `B`.

**The derivation.** `B` is a property of the *shipped configuration* — the precision that
configuration publishes. `E` is a property of the *discretisation* — how far the discrete answer is
from the continuous one. These are different objects and must be measured at different settings:

```text
B   computed at the PRODUCTION tolerance, unchanged -- it is the claim being audited
E   measured on a ladder run at a RESEARCH tolerance fine enough that the lattice is far below E
```

Requiring the lattice to be at most one tenth of `E` gives, with `E ~ 0.2 %`, a quantum of
`<= 2.0e-04`, hence `tolerance <= ~2.4e-04` (`k = 12`, `q = 1.69e-04`). Resolving `E` to ~1 %
needs `tolerance ~ 3e-05` (`k = 15`, `q = 2.1e-05`). Both are measurement-only settings and
change no default.

> **CORRECTION, 2026-10-12, same day, before the amendment was designed.** The paragraph below
> asserts that the finest ladder level "would breach `ctest --timeout 7200`". **That is WRONG.** The
> C2-A2 ladder is not a gtest case: it is a standalone probe, `tools/c2a2_probe.cpp` built to
> `tools/td027_c2a2` and invoked by a shell script, so no ctest timeout governs it at all — the
> recorded 4 h run already exceeds any per-test cap and was never subject to one. I conflated the
> gtest cap that binds `G8PremixedFlameTest` with a harness that is not under ctest, which is the
> same class of error TD-035 recorded in the other direction. **The consequence: there is no design
> choice to make between "fewer levels / coarser dt / a DISABLED_ run" — the amendment simply runs
> the probe at a finer tolerance and takes longer.** Wall-clock cost remains a real constraint and
> is sized in `amendment_A4_proposal.md` §4. The paragraph is left below as written, per §4 and §10.

**The cost, stated because it constrains the choice.** Each tightening step adds one bisection, so
one trial per ladder level. The A3 ladder cost 788 / 2504 / 3871 / 6496 s at `k = 10`. Going to
`k = 15` is +5 trials per level, and **the finest level would breach `ctest --timeout 7200`** — so
the amendment must either use fewer levels, start from a coarser `dt`, or run outside the gtest cap
as a `DISABLED_` validation run. That is a design decision, not a detail, and it is why this is a
request rather than an edit.

**Non-vacuity the amendment must show, pre-registered here:** the replacement must still return
`80x` at cap 20 and `8.4x` at cap 10 — the two values C3 was derived on and which it correctly
rejected — while returning a resolved, non-quantised value at cap 5. A replacement that cannot
reproduce the cap-20 rejection is not a replacement for C3.

**What is NOT requested:** no change to C3's factor of 2, no change to `maximumCellDiffusionNumber`,
no change to the production `tolerance`, and no re-derivation of a threshold because a measurement
moved — which §5.3 forbids and which this is not: the threshold is untouched and the *instrument* is
what is shown to be unable to express it.
