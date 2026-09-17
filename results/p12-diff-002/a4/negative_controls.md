# P12-DIFF-002 A4-5 — negative and positive controls

A4 requires that every amended validation carry evidence it can detect the defect it protects
against, and explicitly forbids freezing a criterion merely because production passes it. Because A4
stopped at A4-2 before freezing a gate (`inventory.md` §9), **no criterion was frozen and no test was
amended**. What follows is the control evidence that does exist, and what is still missing for the
scope that has not been authorized.

## 1. Controls already established and reusable

### Harness freshness (A4-1) — both directions demonstrated

`harness_validation.md`. The new harness rebuilds rather than assuming an existing executable is
current:

```text
step 1 baseline         binary bc698f840a6eb6b9   tests 169   lib 58be6b751328c9bb
step 3 after a temporary always-passing TEST is appended to a test source
                        binary f44e2670f6f134cb   tests 170   lib 58be6b751328c9bb
step 4 RESPONDS         binary and test count both changed                      PASS
step 7 RESTORED         source and binary back to baseline exactly              PASS
```

Negative direction: the *old* harness (`results/p12-diff-002/tools/run_w7.sh`) rebuilt only when the
binary was absent, and demonstrably reported `CFDThermalTests 92/92` where a forced rebuild of the
same sources against the same library gives **79/92** — the defect that invalidated A1's and A2's W7
counts. The new harness also fails closed: a target whose build returns non-zero, or whose executable
is missing after a successful build, is reported `BUILD FAILED`, never executed, and the harness
exits non-zero.

### A3-1 activation instrument — negative and positive control

Frozen in `acceptance_gate_A3.md` §1 and recorded in `a3/logs/02_dryrun.log`:

| control | result |
| --- | --- |
| correction forced off while the case asks for N = 1 (a path that ignored the setting) | interior rows differ on **0** rows → instrument **FAILS**, as required |
| real production path, sheared 16×16 / 32×32 | **196/196** and **900/900** interior rows differ → **PASSES** |
| orthogonal Cartesian 16×16 (the correction is bit-identical there by construction) | **0** rows differ → instrument correctly reports "not applied" |

Both directions plus a third, independent discrimination check.

### A3-2 conservation instrument — negative and positive control

Frozen in `acceptance_gate_A3.md` §2, recorded in `a3/logs/02_dryrun.log`, at **all three**
resolutions the test uses:

| control | per-cell imbalance | cross-section spread | verdict |
| --- | --- | --- | --- |
| legacy (inconsistent) estimator, nt = 20 / 30 / 45 | 3.264e-05 / 1.003e-05 / 3.047e-06 | 8.567e-05 / 3.810e-05 / 1.694e-05 | **FAIL** (3–4 orders over) |
| consistent, independently summed estimator | 1.505e-12 / 1.516e-13 / 9.171e-12 | 8.116e-14 / 1.897e-12 / 1.237e-12 | **PASS** (≥160× margin) |

Derived bound `1e-8 · Q ≈ 1.471e-09`, from the case's own linear relative tolerance times the flux
scale — 100× tighter than the assertion it replaced.

## 2. Controls that would be required, per sub-class, and are NOT yet established

| sub-class | required negative control | required positive control | status |
| --- | --- | --- | --- |
| A — hand-derived coefficients (18) | inject a deliberately wrong coefficient (e.g. keep `Γ\|S\|/d` where a stencil exists, or drop the far-cell entry) → the amended test must FAIL | the independently hand-derived DIFF-002 values → PASS | derivable (`derivations.md` §1–§3 works three of them end to end); **not built**, since no amendment was authorized |
| B — flux estimators (2) | the legacy `(..., nullptr, false)` estimator → FAIL | the consistent operator with independent summation → PASS | pattern already proven for the sector test in A3-2; **not yet applied** to `ThermalBoundaryConsistency` |
| C — superseded closed form (3) | the old `ny²/(ny²+2)` reference → FAIL against the new scheme | a newly derived three-point discrete-exact channel reference → PASS | **the reference does not exist yet**; deriving it is new work, not a constant swap |
| D — order bands (4) | an error sequence at first order → FAIL the lower bound; an artificially super-converging sequence → FAIL the upper bound | the measured monotone sequences → PASS | **blocked on a policy decision**: the upper band legitimately guards against error-cancellation artifacts, so raising it is a numerical judgement, not a derivation |
| E — recorded baselines (2) | a deliberately perturbed iteration baseline → FAIL | the re-recorded baseline → PASS | not investigated; A4 stopped first |
| F — conservation / benchmark (4) | **not applicable** — these are not instrument defects | — | `PRODUCTION_DEFECT_SUSPECTED`; A4-3 forbids amending them |

## 3. The standing rule this phase is applying

Three times in this phase a gate result was produced by a defective instrument rather than by the
code: W3b froze a criterion that passed **vacuously** on the baseline; A1/A2's W7 counts were taken
from **stale binaries**; and A2's first thermal W5 reading compared two **configurations** rather than
before/after. The controls above exist because of those three lessons, and the reason sub-classes C,
D and F are reported rather than amended is the same discipline: an instrument may only be replaced
once it is shown to distinguish the behaviour it claims to protect, and a classification never waives
a failing test.
