# GATE_TOL VERIFICATION — the production tolerance default is `1.0e-3`. All five criteria PASS.

```text
STATUS:      **ALL FIVE CRITERIA PASS.** The change is one line.
GATE:        GATE_TOL.md 1185af1d993fef11dba14cd60267473ead80ec7ab6a0ce03d62c2e8f0aebbc7c
             frozen in logs/TOL_00_freeze.log, RECOMPUTED at the start of the run and matched.
RESULT:      T1 PASS (51 constructions, 0 unassigned) · T2 PASS (compiled default 1.000000e-03)
             T3 PASS (declared == cited) · T4 PASS (1 file, 1+1 lines) · T5 PASS (742 files, exit 0)
PRODUCTION:  include/cfd/species/PremixedFlame.hpp
               8fcc1553617bdaa36658805855edcb0d6ba34142ef23e96d3f28cd5d2d5ec937  before
               71429f0649889442b81604c4ee8fb74b41705e7d70ebde7966d0f2ee4385cd40  after
             src/species/PremixedFlame.cpp UNCHANGED at 19db4b6b...
EVIDENCE:    logs/TOL_00_freeze.log · logs/TOL_01_dryrun.log · logs/TOL_02_verification.log
             tools/tol_probe.cpp
REGRESSION:  NOT RUN, by a labelled proportionality judgement -- see §4. TD-035 also blocks the §8
             ladder for this suite.
```

---

## 1. THE CHANGE

```diff
-  Real tolerance{1.0e-4};
+  Real tolerance{1.0e-3};
```

One line, in `include/cfd/species/PremixedFlame.hpp:196`. No other production or test file is
touched, and `src/species/PremixedFlame.cpp` is byte-identical.

## 2. WHAT IT FIXES, AND WHAT IT DOES NOT

**Fixed — an internal inconsistency.** The comment at `:256-258` justifies the shipped
`maximumCellDiffusionNumber = 5` partly on *coherence with the precision the solver reports*, and
argues it "at tolerance 1e-3". The declared default was `1.0e-4`. The justification now evaluates
the value the header actually ships (T3).

**Fixed — a false-precision default.** At `1.0e-4` the solver publishes a bracket of `0.0085 %`
while its own temporal error at the shipped cap is `0.152210 %`. A library-API caller relying on
defaults received a precision claim **eighteen times finer than the answer's error**:

```text
                      published bracket      E / bracket
tolerance 1.0e-4        8.461627e-05            17.988
tolerance 1.0e-3        6.771307e-04             2.248     <- an 8x improvement in coherence
```

**NOT fixed, deliberately: `C3-A4` still fails.** `2.248` is still above its threshold of 2. Full
coherence at this cap needs `tolerance` in `[1.3547e-3, 2.7113e-3)`, giving `1.124` — and that value
was **not** chosen, because selecting a production default to make a criterion pass is fitting
production to the gate. It stays with TD-027 C3, alongside the cap.

**NOT fixed: the comment's numeric figures.** `:257` still reads "0.05 % at tolerance 1e-3", which
is the *formula* bound `sqrt(1+tol)-1`. A4 established that the solver publishes the *terminal
bracket*, `0.0677 %`, and that the cap-5 ratio is `2.248` rather than the "1.7x" the comment states.
Correcting those figures would make the comment assert that the cap fails its own third requirement
— which is true, and is exactly TD-027 C3's open question. It is therefore corrected **forward
here** rather than rewritten in production inside this gate's scope.

## 3. A CORRECTION TO EVIDENCE I ALREADY COMMITTED

`audit_C3.md` §4, `dry_run_A4.md` §3 and the commit messages for `38b3789`, `a0a93a0` and `940050c`
call `tolerance = 1e-4` **"the shipped configuration"** and say it understates its temporal error by
40x / 24x / 18x. **The ratio is right; "shipped" is wrong.**

```text
src/app/ProjectRunner.cpp      ZERO PremixedFlame references -- the production path never reaches
                               this solver
case-file parsers              expose no `tolerance`; the value is unreachable from a case file
all 51 constructions           every one assigns .tolerance before solving (T1)
```

So the default was **dead-written**, and `17.988` is what a library-API user relying on defaults
would have got — real, and the reason this change is worth making — not what any shipped path
produced. This is the repository's own capability-vs-reachability rule turned on my own finding.

**A second correction, same area.** I first reported "only two constructions exist in the repo".
That came from a grep over `results/*/tools`, which does not expand to
`results/technical_debt/TD0xx_*/tools`. The true count is **51** — 2 in tests, 49 in probes. All of
them still assign `.tolerance`, so the dead-write conclusion is unchanged, but the figure was wrong
by a factor of 25 and had already been stated.

## 4. THE REGRESSION THAT WAS NOT RUN, and why

The premixed-flame suite was **not** re-run and no flame result was re-measured. The argument is
T1: a dead-written default cannot change a computed value; the enumeration behind T1 is exhaustive
and machine-generated over `src include tests apps results`; and T1's non-vacuity control shows the
default IS live when a caller omits the assignment, so the enumeration is load-bearing rather than
decorative.

```text
after a caller sets other fields      1.000000e-03   <- the default IS live
after a caller assigns 1.0e-3         1.000000e-03   <- and is dead-written when assigned
```

**This is a labelled proportionality judgement, not a proof by absence.** If T1's enumeration is
incomplete then T1 is wrong and so is this limit, which is why T1 enumerates all 51 sites and its
full output is committed in `logs/TOL_02_verification.log`. The A4 ladder alone cost 9.5 h, and
TD-035 independently blocks the §8 ladder for this suite.

## 5. WHAT IS NOT ESTABLISHED

```text
NOT established  that 1.0e-3 is right for an arbitrary flame problem. It is derived from the
                 temporal error AT THE SHIPPED CAP for the gated fixture (0.152210 %). A default
                 cannot be correct for every case, and the general statement -- that the bracket
                 should be commensurate with the discretisation error rather than far below it --
                 is what justifies the value, not a universal optimum.
NOT established  that C3-A4 can be satisfied by any cap. That needs the temporal order, which
                 TD-043's lattice still prevents measuring (dry_run_A4.md §7).
NOT re-measured  any flame result. See §4.
NOT changed      the cap, C3, C3-A4, any threshold, or the comment's numeric figures.
```
