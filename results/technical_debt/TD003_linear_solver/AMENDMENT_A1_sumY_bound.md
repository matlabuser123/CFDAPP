> **WITHDRAWN (2026-10-12) -- this amendment FAILED ITS OWN DRY-RUN.** D1 required the derived
> bound to hold on the unchanged tree; it is short by **6.2e+06** (bound `3.719907e-13`, measured
> `2.301527e-06`), and by `5.9e+06` in the NV1 control. §4 of this file pre-registered the
> response -- *"the amendment is wrong and the failure is preserved; `T` is NOT adjusted"* -- and
> `T` is unchanged at 24. The scale factor was the error: the assembly is in FLUX units with
> coefficients ~1e-4, so `||A||_inf = 2.7e-04`, not the ~1e0..1e4 the derivation assumed.
> **The 1e-8 threshold is UNTOUCHED and the test still fails.** Retained unaltered below per
> CLAUDE.md §4, because a rejected amendment is evidence. See
> [verification_amendment_withdrawn.md](verification_amendment_withdrawn.md).

# TD-003 C3 — AMENDMENT A1. The `1e-8` ΣY bound is replaced by a DERIVED relative bound.

```text
AMENDS       the `1e-8` assertion at tests/unit/species/test_premixed_flame.cpp:471 ONLY.
             Nothing else in TD-003's criteria changes. C1, C2, C4, C5 and criteria 1 and 2 are
             untouched.
AUTHORIZED   by the user, 2026-10-12, after the failure was preserved and reported across five
             committed evidence documents (audit.md, verification_kappa.md,
             verification_sigmay_sources.md, verification_instrumented_residuals.md,
             verification_gate_provenance.md, verification_minimum_configuration.md).
FROZEN       before any edit to the test. Hashes in logs/04_amendment_freeze.log.
WHAT CHANGES the THRESHOLD's FORM: an absolute constant becomes a derived relative bound.
WHAT DOES NOT the quantity being bounded (`maxCompositionSumDeviation`), the solver, the
             formulation, and `build_identity`-style provenance. No production file changes.
```

---

## 1. WHY THE ORIGINAL IS BEING REPLACED RATHER THAN MET

Established across the committed evidence, and not re-derived here:

```text
the 1e-8 is in NO frozen gate      P18_GATES.md has no such bound; it is a bare test assertion
                                   in a file untracked until 1a70f0a
it was NEVER met                   0 of 81 recorded measurements; range 9.55e-08 .. 4.01e-06
it was NEVER derived               the only ABSOLUTE constant among 4*eps*N (G2(a)),
                                   8*eps*N_cells (G3) and 8*eps*N_s*max|J^0| (section 30); the
                                   SAME constant passes with 529x margin on minMassFraction
                                   while failing by 88x here
the mechanism is closed            max |A^-1 (sum_i b_i - A*1)| = max_c |sum_i y_EXACT - 1| to
                                   SEVEN digits; the RHS closes to 3.162982e-11 = 3.5x
                                   eps*||b||, and ||A^-1||_inf = 1.2e+05 amplifies it
it asks for the impossible         reaching 1e-8 needs RHS closure at 1.374e-13, which is 65x
                                   BELOW double precision at this ||b||
Option B is refuted                the one datapoint suggesting a better-conditioned state is a
                                   125-cell flame whose consumption integral is off by 51 %
```

## 2. WHY THIS DOES NOT CONTRADICT P18.2's AMENDMENT A1 — the precedent, confronted

`results/p18/species_transport/diffusion_closure_A1.md` states: **"THRESHOLDS CHANGED NONE. Not
one. The criterion this is meant to fix -- the frozen G2(a) bound 4 eps N_species -- is the SAME
bound the explicit split failed against."** When ΣY closure failed there at `1.577e-06`, P18 fixed
the IMPLEMENTATION — assembling the cross-species correction implicitly so
`sum_i K(Y_i) = K(1)` cancels exactly — and left the bound alone.

**That precedent cuts against this amendment, so it has to be answered rather than ignored.**

```text
G2(a)'s 4*eps*N_species is enforced by MassFractionState, and MassFractionState.hpp:139 calls it
"The frozen G2(a) tolerance". SpeciesTransport.hpp:121-126 states where it applies:

  "a deferred-correction iterate legitimately violates sum(Y_i) = 1 until it converges and
   MassFractionState correctly refuses such a composition. The invariant is enforced at STEP
   BOUNDARIES, where it must hold; an iterate is a WORKING VECTOR."

and ProjectRunner.hpp says the rest outright:

  "An iteratively transported composition cannot meet that bound -- ITS CLOSURE IS THE LINEAR
   SOLVE'S"
```

`maxCompositionSumDeviation` is computed on the final transport solve's output
(`PremixedFlame.cpp:1616`-`:1629`) — **an iterate, not a validated state**. So G2(a) does not
govern it, by the repository's own statement, and A1's fix is not available: A1 removed the one
residue that had an avoidable discrete cause, and what remains is the linear solve's own closure.

**The amended bound therefore has to carry the linear solve's conditioning, which is exactly what
the measurement shows and what no absolute constant can express.**

## 3. THE DERIVATION — from the assembler sequence, NOT from observed output

### 3.1 The identity

The species share one matrix at `Le = 1` (`PremixedFlame.cpp:1061`-`:1064` assigns the same
`gamma` to every species), and the instrumented run measured
`max |A_i - A_0| = 0.000000e+00` — **bit-identical**. So `sum_i (A y_i) = A (sum_i y_i)`, and the
exact solution obeys

```text
A (sum_i y_i) = sum_i b_i        =>        sum_i y_i - 1 = A^-1 (sum_i b_i - A*1)
```

so with `r = sum_i b_i - A*1`:

```text
|sum Y - 1|_inf  <=  ||A^-1||_inf * ||r||_inf                                            (I)
```

### 3.2 Bounding `r` by counting the assembly's own additions

`r[c]` is the difference of two accumulated sums. `src/species/SpeciesTransport.cpp:300`-`:345`
runs the assemblers in a fixed, load-bearing order, and for the flame's options
(`convection{true}`, `diffusion{true}`, `closureCorrection{true}`, transient on, source an
explicit zero) **A = 5** of them write into `rhs` and the bands:

```text
1 assembleScalarDiffusionContribution
2 assembleScalarConvectionContribution   (closure->rawSum -- the A1 implicit correction)
3 assembleScalarConvectionContribution   (massFlux)
4 applyImplicitScalarSource              (zero for the flame, but it executes)
5 the transient term
```

Counting the floating-point ADDITIONS whose rounding can enter `r[c]`:

```text
each b_i[c] accumulated over A assemblers        N_s (A - 1)
sum_i b_i[c]                                     N_s - 1
each of 3 bands accumulated over A assemblers    3 (A - 1)
(aW + aP + aE)[c]                                2
the final subtraction                            1
                                                 ------------------------------
T(N_s, A) = (N_s + 3)(A - 1) + N_s + 2
```

For this fixture, `N_s = 2`, `A = 5`:   **T = 5 x 4 + 4 = 24.**

The first-order bound for a sum of `T` rounded additions is `T * eps * (sum of |terms|)`, and the
magnitudes entering row `c` are the `N_s` right-hand sides and the row's own absolute sum:

```text
||r||_inf  <=  T * eps * ( N_s * ||b||_inf + ||A||_inf )                                 (II)
```

### 3.3 The amended criterion

Substituting (II) into (I):

```text
  |sum Y - 1|  <=  T * eps * ||A^-1||_inf * ( N_s * ||b||_inf + ||A||_inf )

  T = (N_s + 3)(A - 1) + N_s + 2      from the assembler count, section 3.2
  eps = DBL_EPSILON = 2.220446049250313e-16
  N_s, ||b||_inf, ||A||_inf, ||A^-1||_inf   MEASURED per state by the instrument
```

**Every factor is either a count taken from the source or a norm measured at the state being
judged. No factor is read off a previous `|sum Y - 1|` measurement**, which is what section 5.3
forbids. `T` is an integer derived from the assembler sequence; if a future configuration enables
or disables an assembler, `A` changes and `T` changes with it.

## 4. DRY-RUN AND NON-VACUITY — section 5.2, run BEFORE adoption

```text
D1  the amended bound must HOLD on the unchanged tree, with the measured norms substituted.
D2  the ORIGINAL 1e-8 must still FAIL on that same run -- so the amendment is shown to change the
    verdict for the stated reason and not because the tree moved.
D3  minMassFraction's own -1e-8 assertion must still PASS untouched, confirming the amendment is
    scoped to one assertion.

NV1 NON-VACUITY: `closureCorrection = false` must make the amended bound FAIL.
    This control is PRECEDENTED and needs no production edit --
    SpeciesTransport.hpp:168-171 says the flag "Exists ONLY so the N5 negative control can break
    the closure without editing production source", and A1's own NV3 used exactly it:
    "disabling the correction must make sum(Y) = 1 FAIL".
    Without NV1 the amended bound could be satisfied by any state at all, which is the vacuity
    section 5.2 exists to catch.
NV2 the bound must also fail if ||A^-1||_inf is replaced by 1 -- i.e. the amplification factor is
    load-bearing, not decoration.
```

**If D1 fails, the amendment is wrong and the failure is preserved; `T` is NOT adjusted to make it
pass.** That is pre-registered here, before the dry-run is executed.

## 5. WHAT THIS AMENDMENT DOES NOT DO

```text
NOT changed   G2(a)'s 4*eps*N_species. It still governs MassFractionState at step boundaries,
              where it must hold, and this amendment does not touch it.
NOT changed   G3, section 30's closure bound, or any production file.
NOT changed   the -1e-8 on minMassFraction two lines above. Different quantity, different floor,
              and it passes with 529x margin.
NOT claimed   that 2.3e-06 is good. It is what double precision permits for this operator; the
              amplification of 1.2e+05 remains a defect worth its own item, recorded separately.
NOT closing   TD-003. Criteria 1 and 2 are unaffected; this amends C3's threshold only.
```
