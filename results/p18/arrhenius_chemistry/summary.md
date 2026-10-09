# P18.5 — Arrhenius Chemistry — summary

```text
STATUS      P18.5 COMPLETE
SCOPE       LIBRARY/API ONLY. k(T) = A T^n exp(-Ea/(R_u T)) and elementary mass-action progress
            rates. No heat release, no equilibrium constant, no case-file reachability.
GATE        results/p18/gates/P18_GATES.md
            sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
            re-verified UNCHANGED at open, mid-phase (logs/07) and at close (logs/11)
AMENDMENT   A1 -- formulation_A1.md. The EVALUATION of k only; the model and every threshold
            unchanged. formulation.md itself NOT edited, sha256 re-verified at close.
RESULT      G6 kinetics half CLOSED. 38 new tests, all passing in all three configurations.
```

---

## 1. The primary gate

G6's `measured` clause, verbatim: *"omega_i for a known mechanism against an INDEPENDENT
arbitrary-precision implementation (Python/mpmath, no CFDApp code), over a matrix of T, p and
composition spanning the intended range"*, at *"relative error <= 32 * eps"*.

| quantity | worst measured | allowance | margin |
|---|---:|---:|---:|
| `k(T)`, 8 points 300–3000 K | **11.05 eps** | 32 eps | 2.9× |
| `q`, 27 non-zero matrix rows | **10.24 eps** | 32 eps | 3.1× |
| `omega_H2` / `omega_O2` / `omega_H2O` | **10.09 / 10.12 / 10.04 eps** | 32 eps | 3.2× |
| `rho` through P18.3's EOS | **1.04 eps** | 32 eps | 31× |
| `\|sum_i omega_i\| / scale` | **0.96 eps** | 32 eps | 33× |
| `q`, 9 zero-reactant rows | **exactly 0** | exact | — |

```text
THE MARGIN IS ~3x, NOT 1000x, and that is worth stating rather than burying. The threshold was
INHERITED from P17.7 for the same kind of pointwise closed-form comparison, not fitted to anything
measured here. Anyone tightening 32 eps later should know it would bite almost immediately.

WHERE THE ~10 eps COMES FROM, and it is not a defect: q composes pow(C_H2, 2.0), two multiplies,
and k's own pow(T, n) and exp(x) -- about five correctly-rounded operations. omega_i adds one
multiply and tracks q to within 0.2 eps rather than drifting from it.
```

G6's own two controls, quantified: exponent sign flipped **5.08e8×**; `Ea = 0` **2.25e4×** *and*
the Arrhenius slope collapses to exactly 0, which is the signature G6 names.

## 2. The coverage gap found and closed

```text
THE FIRST VERSION OF THE G6 MATRIX TEST COVERED ONE COMPOSITION OUT OF FOUR.
It carried 9 hand-typed rows -- 3 pressures x 3 temperatures -- from a reference that had computed
36. G6 says "a matrix of T, p AND composition". Three temperatures and three pressures are a
matrix; one composition is not a dimension.

It passed. It compared against a genuinely independent arbitrary-precision reference. And it did
not measure the dimension the criterion names third.

CLOSED by tools/emit_cpp_cases.py, which imports the same mpmath module and emits all 36 rows as a
C++ initializer. Nothing is transcribed by hand -- 36 rows of 17-significant-digit literals is
exactly where a wrong digit hides, and it is the one error a reference comparison cannot catch,
because the literal IS the reference.

AND THE GENERATOR REPRODUCED THE 9 HAND-TYPED ROWS EXACTLY, which is how the original
transcription was confirmed before being replaced.
```

A second, inverted comparison route was added for the same reason: the probe prints CFDApp's own
doubles at 17 figures and `tools/compare_probe.py` recomputes the reference in mpmath. Literals-in
and values-out close each other's blind spot.

The 9 zero-reactant rows get **exact equality with 0** instead of a relative bound, and both the
test and the comparator refuse a run in which either branch is empty — otherwise an implementation
returning 0 everywhere would take the exact branch for all 36 rows and never evaluate a single
relative bound.

## 3. Amendment A1

```text
THE ORIGINAL FROZE LOG SPACE ALWAYS, correctly justified: A T^n can overflow while k is
representable. THE COST, measured on the unchanged tree:

    n = 0, Ea = 0  =>  k should be EXACTLY A
    measured       150000000.00000015  against A = 150000000     ~4.5 eps

because exp(log(A)) does not round-trip. Section 11 calls k = A "one of the strongest simple
analytical regression tests", and under log space it held only to a few eps.

PRESERVED VERBATIM at logs/01_first_failures_PRESERVED.log.

A1: direct when A T^n is finite, log space as the overflow fallback. pow(T, 0.0) and exp(0.0) are
both exactly 1.0, so k == A BITWISE. NO THRESHOLD MOVED -- G6's 32 eps is untouched, and the
amendment makes the implementation more accurate rather than the criterion looser.

THE DRY-RUN IS THE FAILURE ITSELF: a criterion the unchanged baseline measurably fails and the
corrected implementation passes.
```

A1's two non-vacuity controls, and the second is the load-bearing one:

| control | measured |
|---|---|
| **NV1** the two forms agree where both are safe | worst **18.04 eps** at T = 2700 K over 300–3000 K, inside G6's 32 |
| **NV2** the log-space fallback is still **live** | `A T^n = inf` while `k = 1.9913135012679248e+263` is finite |

```text
WITHOUT NV2, A1 COULD HAVE SILENTLY DELETED the overflow protection the original formulation
existed for, and every other criterion would still have passed.
```

## 4. Everything measured

| § | what | measured |
|---|---|---|
| 13 | the rate's range | spans **1.22e20** over 300–3000 K |
| 14 | `dk/dT` closed form vs central difference | worst **6.73 × eps^(2/3)**, bound 8 — derived from the dimensional floor, not fitted |
| 35 | Arrhenius slope / intercept | slope 1.09 eps, intercept **bitwise** |
| 36 | determinism | **204 800 comparisons, 0 bitwise differences**, mechanism and kinetics rebuilt each repeat |
| 17 | zero reactant | `q = 0` exactly |
| N7 | `rho -> 2 rho` | `q × 8` **exactly** (`2^3`, total order 3) |
| 43–46 | predecessor preservation | 60 + 34 + 34 + 38 + 38 + 37 = **241** = the whole binary |
| 42 | focused suite | **241/241 pass** (203 + 38) |
| 47 | three-configuration regression | see `regression.md` |

The negative controls N1–N10 are all permanent tests, each reporting its margin: **38.7×** to
**5.08e8×**. N8 and N9 are marked *unrepresentable* rather than *detected* — there is no reachable
defective path, not merely a large margin.

## 5. Regression

| configuration | predicted | measured | FAILED | runtime |
|---|---|---|---:|---:|
| Release+CUDA | 2967 / 2916 | **2967 / 2916** ✓ | **0** | 2293 s |
| CPU-only | 2744 / 2697 | **2744 / 2697** ✓ | **0** | 1525 s |
| Debug+GUI | 3029 / 2975 **or** 2974 | **3029 / 2974** ✓ | **4** | 8617 s |

All three match the prediction declared before the run, Debug+GUI on its second declared branch.
`DriverSeegmiller.MeshSequence` now has **five** measurements against the unchanged 7200 s cap —
7123.03 PASS, 7201.94 TIMEOUT, 6764.75 PASS, 7034.57 PASS, **7200.12 TIMEOUT** — all within 6% of
the limit. The cap was not raised.

## 6. Three instrument defects, all mine, all recorded

```text
These are recorded rather than quietly fixed, because each failed in the direction of a clean
result and each is a pattern that will recur.

(1) THE INTEGRITY SCRIPT FAILED OPEN. `fail=1` was written inside a $( ) command substitution --
    a subshell -- so the assignment was discarded. It printed "*** NOT 0 ***" three times AND THEN
    "INTEGRITY: PASS", exit 0. The markers were in the log and the verdict contradicted them.
    FIXED, and the FAIL path is now proven against three injected corruptions
    (logs/09_integrity_failpath.log). After a fail-open defect, a PASS proves nothing until the
    fail path is exercised.

(2) THE EXCLUSION GREPS MATCHED PROSE. "equilibrium", "omega" and "clamp" were each reported
    present in the P18.5 files. Every occurrence was a comment or an error-message string stating
    that P18.5 does NOT do that thing -- "Nothing is clamped.", "-> omega_i" in a layering
    diagram, "NOT derived from an equilibrium constant". The better the comments, the more false
    findings. FIXED: tools/strip_code.py removes comments and literals first, and positive
    controls confirm the stripped copies still contain what they should.

(3) THE VALIDATION-NOISE MAGNITUDE WAS MIS-MEASURED, in the alarming direction. It reported
    "max relative |delta l2| = 4.632e-02" against KNOWN_LIMITATIONS L36's recorded <= 4.1e-07 --
    an apparent five-order contradiction. The measurement paired diff lines with `paste - -`,
    which mis-pairs a grouped hunk and compares grid 1 with grid 2, reporting the REFINEMENT RATIO
    as drift. Proven on a synthetic diff: true answer 1.0e-09, reported 3.0e+00.
    L36 IS NOT CONTRADICTED. The figure is marked INVALID AS AUTHORITATIVE in place at
    logs/12_validation_noise.log and superseded by logs/13_noise_measurement_CORRECTED.log.
    Replaced by tools/compare_validation_json.py, which compares by key path; run live against
    Debug-regenerated reports it found 810 keys compared and NON-TIMING KEYS UNCHANGED.
```

Every "must be 0" check in `final_integrity.sh` is now paired with a positive control that must be
non-zero on the same file. The comparison instrument has its own five-corruption non-vacuity suite
(`logs/06_comparator_nonvacuity.log`), including an empty log and a log with no zero-rate row.

## 7. results/validation

```text
50 tracked reports were rewritten by the regression. CLASSIFIED before restoring:
    37 distinct changed keys, EVERY ONE in the timing / iteratively-converged allow-list
    10 physics fields checked (wall_shear, u_tau, y_plus, nusselt, reattachment, cf_,
       pressure_coefficient, mass_flow, max_divergence, mass_imbalance) -- each 0 changed lines
    1076 changed lines inspected -- the positive control against a blind verdict
    restored: 50 modified before, 0 after

The tool FAILS CLOSED on any key outside the allow-list or any physics field that moved. The
full-set magnitude is recorded as NOT MEASURED for this run, because the only measurement of it
came from the broken instrument described above and the regenerated files were restored before it
could be re-measured. The restore decision rests on the classification, which does not depend on it.
```

## 8. Limitations — stated, not deferred

| limitation | why |
|---|---|
| **No case file can reach any P18.5 code** | library/API scope. Per CLAUDE.md §9 this is **not production capability**. The same limit applies to P18.2, P18.3 and P18.4 — the species reacting-flow stack is now complete as a library and reachable from nothing |
| no equilibrium-constant consistency | a caller can specify a thermodynamically inconsistent forward/reverse pair and nothing detects it. §20 forbids inventing a `K_eq`; it needs the Gibbs route and full thermodynamic polynomials |
| `A`'s units cannot be validated | they depend on the reaction's total order, so there is no single dimensional form to check. A user supplying `A` for a different order gets a plausible wrong rate |
| no heat release | P18.6. P18.5 **reads** `T` and never writes it; the `reaction -> heat -> T` loop does not exist yet |
| no third-body / pressure-dependent kinetics | not in the frozen gate |
| no stiff-chemistry integration | instantaneous rates only; no sub-stepping or operator splitting |
| sanitizers NOT RUN | deferred to G11 at P18.9, as in P18.1–P18.4. Recorded as NOT RUN, not as a pass |
| Debug+GUI failure count unpredictable | 3 reliable timeouts + 1 marginal test, 3 PASS / 2 TIMEOUT over five runs. P18.9 should predict the branch, not a number |
| 259 whitespace + 28 clang-format violations | all pre-existing, in files no P18 phase authored. A push blocker in `TODO.md`. P18.5 fixed the one `TODO.md` warning it found |

## 9. Changeset

```text
PRODUCTION (new)
  include/cfd/species/ArrheniusKinetics.hpp   44d6561f14bf23c1d81a4013cb641e281c65464da0c880bf39db517ffc7e5e58
  src/species/ArrheniusKinetics.cpp           ba095f0e5ec785bf89f70834bf9359455bd3a901600372890ceceae6722af795
PRODUCTION (modified) -- build lists and one comment only
  src/CMakeLists.txt                          one target_sources entry
TESTS (new)
  tests/unit/species/test_arrhenius_kinetics.cpp
TESTS (modified)
  tests/unit/species/CMakeLists.txt           one source line
DOCS
  TODO.md, ROADMAP.md, results/p18/arrhenius_chemistry/**

NO PRE-EXISTING PRODUCTION SOURCE WAS MODIFIED. Preservation of P18.1-P18.4 is therefore
structural, and it is measured anyway -- "it cannot have changed" is the claim most worth checking.
```

Nothing was committed, pushed, reset, cleaned or stashed. `HEAD` is unchanged.
