# P18.5 — formulation, AMENDMENT A1

```text
AMENDS      formulation.md section 2 (the evaluation form) ONLY.
            The original is NOT edited. Its sha256 is recorded here and re-verified at close.
            formulation.md  sha256 recorded at logs/02_amendment_hashes.log

WHAT CHANGES        the EVALUATION of k: log-space-always becomes direct-when-safe, log-space
                    as the overflow fallback.
WHAT DOES NOT       the MODEL. k = A T^n exp(-Ea/(R_u T)) is unchanged, as are the units, the
                    validity rules, the mass-action form and every threshold.
THRESHOLDS CHANGED  NONE. G6's 32 eps is untouched. The amendment makes the implementation MORE
                    accurate, not the criterion looser.
AUTHORIZED          by the user's standing P18.5 authorization; this is an implementation
                    correction inside the authorized scope, recorded as an amendment because it
                    changes a decision that was frozen before measurement.
```

---

## 1. What the original froze, and what it cost

```text
formulation.md section 2 froze LOG SPACE ALWAYS:
    ln k = ln A + n ln T - Ea/(R_u T),  then  k = exp(ln k)

and justified it correctly: A T^n can overflow while k itself is representable.

THE COST, MEASURED (logs/01_first_failures_PRESERVED.log):
    n = 0, Ea = 0  =>  k should be EXACTLY A
    measured       150000000.00000015   against A = 150000000
    error          ~4.5 eps relative

because exp(log(A)) does not round-trip. Section 11 calls k = A "one of the strongest simple
analytical regression tests", and under log space it holds only to a few eps rather than exactly.
```

## 2. The amendment

```text
    k = 0                                            if A == 0                    (unchanged)
    k = (A T^n) exp(-Ea/(R_u T))                     if A T^n is FINITE           <-- DIRECT
    k = exp( ln A + n ln T - Ea/(R_u T) )            otherwise                    <-- fallback
```

```text
WHY THE DIRECT FORM IS BETTER WHERE IT IS SAFE, and both reasons are measurable:

(a) FEWER ROUNDINGS. Direct: pow, exp, two multiplies. Log space: log, multiply, add, exp -- one
    more transcendental, each contributing ~1 eps.

(b) IT IS EXACT IN THE REDUCED CASES, which log space is not:
        std::pow(T, 0.0) == 1.0   EXACTLY, for every T
        std::exp(0.0)    == 1.0   EXACTLY
    so  n = 0, Ea = 0  gives  A * 1.0 * 1.0 == A  BITWISE.
    Section 11's criterion is then exact rather than approximate, and sections 9 and 10 each carry
    one rounding instead of three.

WHY LOG SPACE IS KEPT, rather than removed: the original's justification stands. A = 1e300, n = 10,
T = 1000 makes A T^n = 1e330 = +inf while k may be finite, and inf * 0 is NaN. The fallback is
selected by TESTING isfinite(A T^n) -- the actual condition, not a parameter-range guess.
```

## 3. Dry-run against the unchanged baseline (§40)

Run before implementing, so the criteria are known to be able to fail.

```text
CRITERION                                   BASELINE (log space always)      CAN IT FAIL?
k = A for n = 0, Ea = 0, BITWISE            FAILS at 4.5 eps                 YES -- it does fail
k(T) against mpmath, 32 eps                 PASSES                           see NV1
the G6 omega matrix, 32 eps                 PASSES                           see NV1
overflow refused by name                    PASSES                           see NV2
underflow to exactly 0                       PASSES                           see NV2
```

```text
THE FIRST CRITERION IS THE DRY-RUN: it is measured, reproducible, and it FAILS on the unchanged
tree. That is the non-vacuity the amendment needs -- a criterion the baseline fails and the
corrected implementation must pass.
```

## 4. Non-vacuity controls for A1 specifically

```text
NV1  the mpmath comparison must still pass, and must still be TIGHT. If the direct form were
     wrong rather than merely different, the 32-eps comparison over the T sweep and the
     9-point G6 matrix would break. Retained unchanged.
NV2  THE FALLBACK MUST STILL BE REACHED. A1 is worthless if the log-space branch is dead code, so
     a test drives A T^n to overflow while k stays representable and asserts a finite, correct k --
     proving the fallback executes. WITHOUT THIS, A1 could silently delete the robustness the
     original froze.
NV3  the overflow refusal and the underflow-to-zero behaviour must be unchanged, since both are
     decided on ln k and A1 does not move that decision.
```

## 5. What could go wrong, stated before measuring

```text
RISK 1  the two branches disagree beyond 32 eps somewhere in the valid range, which would mean the
        selection is observable as a discontinuity in k. A test compares the two forms directly
        across a T sweep in the region where both are safe.
RISK 2  isfinite(A T^n) is the wrong predicate -- A T^n could be finite while the product with exp
        overflows. It cannot: exp(-Ea/(R_u T)) <= 1 for Ea >= 0 and T > 0, so multiplying by it
        never increases the magnitude. That is why Ea >= 0 being enforced matters here.
RISK 3  pow(T, n) for large |n| loses accuracy relative to exp(n ln T). Measured by the mpmath
        comparison, which spans n = -3.5 to 2 in the sensitivity tests.
```

## 6. Verification at close

```text
formulation.md unchanged:  sha256 re-verified and recorded in summary.md
```
