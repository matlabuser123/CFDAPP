# AMENDMENT — P17.4 controls N2 and N6 were VACUOUS

Recorded 2026-09-27, immediately after the controls ran. The frozen gate
(`acceptance_gate.md`, sha256 `ded6feaa…`) is **unchanged**.

**Both are genuine gate failures, not passes.** `CLAUDE.md` §5.2 requires every negative
control to fail. Failures preserved verbatim in `logs/PRESERVED_vacuous_N2_N6/`.

```text
N1  drop the -(2/3) factor                DETECTED, 1 test
N2  drop the V_P factor                   *** VACUOUS ***
N3  use div U instead of grad(div U)       DETECTED, 1 test
N5  flip the transient source sign        DETECTED, 3 tests
N6  default the option to ENABLED         *** VACUOUS ***
```

---

## 1. N2 — the control was arithmetically a NO-OP

`DilatationalStressTest.MatchesMinusTwoThirdsMuGradDivergenceTimesVolume` used a **10 x 10
mesh over 10.0 x 10.0**, so `h = 1.0` and every `cell.volume()` is exactly **1.0**.

```text
rhs += -(2/3) * mu * gradComponent * cell.volume()      with V = 1
rhs += -(2/3) * mu * gradComponent                      after N2
```

Multiplying by 1 and not multiplying at all are the same computation. **No defect was
planted**, so there was nothing for the criterion to detect. Identical in kind to P17.2's
N6, where `round(cv*1e10)/1e10` turned out to be the identity.

### Fix — the test mesh, not the criterion and not the code

```text
before   10 x 10 over 10.0 x 10.0    h = 1   V = 1      V_P undetectable
after    10 x 10 over 20.0 x 20.0    h = 2   V = 4      dropping V_P is a 4x error
```

`div U = x` is linear and Green-Gauss is exact for a linear field, so `div U` and
`grad(div U) = 1` remain exact in the deep interior at the coarser spacing; only the
expected magnitude changes, from `-(2/3)(0.1)(1)(1)` to `-(2/3)(0.1)(1)(4)`. Deep-interior
cells: `x, y in (4, 16)` gives 6 x 6 = 36, so the region still exists.

## 2. N6 — and what it exposed, which matters more than the control

Making `includeDilatationalStress` default to `true` disturbed **nothing**: the regex
matched 17 tests (`CompressibleSimpleTest`, `LowMachRegressionTest`, `CompressibleSimpleMMS`)
and all 17 passed.

### Consequence 1: my stated justification for default-off was WRONG on one point

`audit.md` §5 and the gate §0 gave three reasons for default-off. Reason 1 was:

> The MMS test's forcing is hand-derived for `mu lap U` only. Enabling the term
> unconditionally would make a PASSING, VALIDATED test fail.

**Measured: it does not.** The MMS has `div U != 0` (its own header says so), so the term is
genuinely non-zero there — but its order gate is a wide band `[0.55, 1.30]` at
`nx = 8, 16, 32`, and at those resolutions the discretisation error still dominates the
added term. The MMS is simply **not sensitive enough to detect it**.

```text
reason 1   WITHDRAWN as factually wrong -- corrected here rather than left standing
reason 2   STANDS: CLAUDE.md §6 -- prefer a dedicated path, prove existing results
           unchanged. Default-off makes that provable by construction.
reason 3   STANDS: nonOrthogonalCorrections{0} and robustness both default to "exactly
           the previous solver". A default-off numerical option is this codebase's idiom.
```

The decision does not change; one of its three supports was false and is removed.

### Consequence 2: gate criterion D3 was specified but never implemented

`acceptance_gate.md` §4 lists:

```text
D3  Enabled, it CHANGES the answer for a field with div U != 0.     RHS differs
```

**No test implements D3.** Every dilatational test calls
`assembleDilatationalStressContribution` **directly**; none exercises the wired path through
`assembleRelaxedCompressibleMomentumComponent`. So nothing in the suite would fail if the
wiring were wrong, absent, or inverted — and that is exactly why N6 had nothing to break.

```text
This is the "capability vs reachability" trap: the operator is verified, the WIRING is not.
A wiring no test can detect is unverified wiring, in the same way a feature no case file can
reach is not shipped capability.
```

### Fix — implement the missing D3 test against the WIRED path

```text
NEW  DilatationalStressIsWiredIntoTheRelaxedMomentumAssembly
     assembleRelaxedCompressibleMomentumComponent twice on a field with div U != 0,
     once with includeDilatationalStress = false and once with true, and require:
       * the RHS DIFFERS                                     (the wiring is live)
       * the difference EQUALS the standalone operator's contribution, cell by cell
                                                             (the wiring is correct,
                                                              not merely present)
       * with a divergence-free field, off and on are BITWISE identical
                                                             (it cannot disturb that limit)
```

The middle clause is the one that makes N6 detectable and the wiring verified rather than
assumed.

## 3. What was NOT done

```text
NOT DONE   editing acceptance_gate.md to match the outcome
NOT DONE   weakening any criterion, tolerance or the deep-interior restriction
NOT DONE   deleting or rewriting the vacuous control results
NOT DONE   declaring D1/D3 proven on the strength of N6
NOT DONE   keeping the withdrawn MMS justification in place
DONE       preserved both failures, diagnosed each to the exact arithmetic or the exact
           missing test, corrected a factually wrong justification, and fixed the test
           mesh and the missing coverage with the criteria untouched
```

## 4. Outcome

Recorded in `logs/08_controls_N2_N6b.log`. D1/D3 are proven only if both re-run controls
fail; if either does not, P17.4 is **BLOCKED** and that criterion is reported
`NOT VERIFIED`.

---

# CORRECTION (2026-09-27, after the second-pass controls ran)

The second pass measured something that **contradicts §2 of this document**, and §2 is wrong on
its central claim. Recording that here rather than editing it away.

## 5. N6 was not vacuous for the reason this amendment gave

Second-pass results, `logs/08_controls_N2_N6b.log`:

```text
N1   drop the -(2/3) factor, on the NEW mesh      DETECTED, 1 test
N2b  drop the V_P factor, V = 4                   DETECTED, 1 test   <- was VACUOUS
N3   div U instead of grad(div U), NEW mesh       DETECTED, 1 test
N5   flip the transient source sign (harness)     DETECTED, 3 tests
N6b  default the option to ENABLED                DETECTED, 2 tests  <- was VACUOUS
```

N6b broke **two** tests, and only one of them is mine:

```text
1704  DilatationalStressTest.DefaultIsDisabledBehaviourally                    (added by P17.4)
2219  CompressibleRelaxedMomentumTest.AlphaOneMatchesUnrelaxedCompressible-
      MomentumComponentExactly                                                (PRE-EXISTING)
```

That second test lives in `tests/solver/compressible_simple/test_compressible_relaxed_momentum.cpp`.
Verified: it is present at `HEAD` and `git diff --numstat HEAD` on that file is **empty**, so
P17.4 neither wrote nor touched it. It detects the default flip because it asserts that the
relaxed assembly at `alpha == 1` matches the *unrelaxed* `assembleCompressibleMomentumComponent`
exactly — and the unrelaxed function has no dilatational option, so any term switched on by
default breaks the equality.

### So what actually went wrong with N6

```text
the original N6 fail-regex:  'CompressibleSimpleMMS|LowMach|CompressibleSimpleTest'
the detector's suite name:   'CompressibleRelaxedMomentumTest'
                              -> does not match. The control never RAN the test that detects it.
```

**N6 was not a coverage gap. It was a too-narrow probe** — the control measured a set of tests
chosen by hand, and the set excluded an existing detector. That is the same class of error as
the defective L5 probe (`AMENDMENT_L5_instrument.md`): an instrument that does not measure what
its criterion says. Two of this phase's four instrument failures are now the same mistake.

### What §2 got wrong, precisely

```text
WRONG   "Making includeDilatationalStress default to true disturbed NOTHING."
        It disturbed a pre-existing test that the control did not run.
WRONG   "every other dilatational test calls the operator DIRECTLY, so no test would have
        failed if the wiring were wrong, absent or inverted"
        Over-general. A wiring active BY DEFAULT was already detectable. What no test checked
        was the wiring's VALUE.
WRONG   the implied conclusion that D1 had no behavioural detector at all.
        It had one; the control just never probed it.
```

### What §2 got right, and still stands

```text
STANDS  D3 was specified in the frozen gate and implemented by NO test. Nothing verified that
        the wired contribution EQUALS the standalone operator cell by cell -- the pre-existing
        test only detects that something changed, not that the change is correct.
STANDS  D1's own probe in the gate script was `grep -c` on a header string, which is not a
        property of the assembly. Replacing it with a behavioural probe was right.
STANDS  N2 was genuinely vacuous: with V_P == 1 no defect was planted at all. That diagnosis
        was arithmetic and is unaffected.
STANDS  The MMS really is insensitive to this term. Under N6b the MMS and low-Mach suites were
        10/10 with the term enabled by default, so the withdrawal of justification reason 1
        stands on a second, independent measurement.
```

### Is `DefaultIsDisabledBehaviourally` still worth keeping?

Yes, and not because it was the only detector. It pins the default **directly and locally**,
next to the code it constrains, and it carries its own non-vacuity clause -- it asserts that the
two branches are distinguishable on that mesh, so it cannot quietly become a tautology. The
pre-existing test detects the default only as a side effect of a different claim about
relaxation, which is fragile coverage to depend on. Both are kept.

## 6. The lesson, stated as a rule

```text
A negative control is TWO things: the planted defect AND the set of tests it is run against.
This phase got the defect right and the set wrong. So:
  * derive a control's probe set from the CRITERION, not from a guess about which suites
    are relevant
  * when a control comes back vacuous, suspect the probe set BEFORE concluding there is a
    coverage gap
  * a control whose probe set is narrower than the suite should say so explicitly
```

Had N6 been run against the whole momentum-and-solver set from the start, it would have come
back DETECTED, and the genuine D3 gap would have been missed. The too-narrow probe produced a
false alarm that happened to surface a real defect — which is luck, not method, and is exactly
why the rule above is written down.
