# P18.8 §57 — the three-configuration regression

**Attempt 2.** Attempt 1 is marked `logs/regression_INVALID_AS_AUTHORITATIVE.md` and produced no
evidence; its raw output is preserved at `logs/regression_attempt1_INVALID/`.

Execution policy, inherited verbatim from P18.7 ← P17.10 ← P13's G10 closeout:

```
OMP_NUM_THREADS=1   ctest -j 8   --timeout 7200   -E <5 frozen exclusions>
```

Prediction frozen before the run: `regression_prediction.md` (`ab941cf5…`) plus
`regression_prediction_addendum.md` and `regression_prediction_addendum_2.md` (`610f388a…`).

## 1. Reconciliation against the frozen prediction

| | | registered | ran | passed | failed | skipped | disabled |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `release_cuda` | predicted | 3081 | 3016 | 3014 | 0 | 2 | 52 |
| | **actual** | **3081** | **3016** | **3014** | **0** | **2** | **52** |
| | | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `cpu_only` | predicted | 2835 | 2775 | 2775 | 0 | 0 | 47 |
| | **actual** | **2835** | **2775** | **2775** | **0** | **0** | **47** |
| | | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `debug_gui` | predicted | 3143 | 3078 | 3073 | **3** | 2 | 52 |
| | **actual** | **3143** | **3078** | **3072** | **4** | **2** | **52** |
| | | ✅ | ✅ | **−1** | **+1** | ✅ | ✅ |

**Seventeen of eighteen figures are exact.** The single discrepancy is one test in `debug_gui`, and
it is the subject of §3.

Configuration assertions passed for all three (`CMAKE_BUILD_TYPE`, `CFDAPP_BUILD_GUI`,
`CFDAPP_ENABLE_CUDA`, `CFDAPP_ENABLE_OPENMP`), each read from its own cache **after** configuring.
Every test binary was clean-first rebuilt and verified newer than the newest production source.

| config | build | ctest | ctest exit | P18.7 ctest |
| --- | --- | --- | --- | --- |
| `release_cuda` | 253 s | 2226 s | 0 | 2083 s |
| `cpu_only` | 247 s | 1495 s | 0 | 1441 s |
| `debug_gui` | 477 s | 8478 s | 8 (expected) | 8156 s |

## 2. P18.8's own rows

| config | passed | disabled | failed |
| --- | --- | --- | --- |
| `release_cuda` | **37** | 3 | 0 |
| `cpu_only` | **17** | 0 | 0 |
| `debug_gui` | **37** | 3 | 0 |

**Zero P18.8 failures in any configuration.** The 3 `DISABLED_` sanitizer controls report
`Not Run (Disabled)` in both CUDA configurations and never appear in a passed count — which matters,
because they perform a deliberate out-of-bounds access and a deliberate uninitialised read.

`cpu_only` is the only configuration that compiles the shared arithmetic header with CUDA **OFF**,
and link one's 17 rows pass there — so the "one shared source" claim is exercised by a build that
has no CUDA at all.

## 3. THE ONE FINDING: `DriverSeegmiller.MeshSequence` timed out in `debug_gui`

The failure sets differ by **exactly one row**. Nothing dropped; the three known timeouts all
reproduce.

| | P18.7 baseline | attempt 2 |
| --- | --- | --- |
| `DriverSeegmillerInlet.MediumGridAB` | Timeout 7201.93 s | Timeout 7200.06 s |
| `DriverSeegmiller.CorrectedSequenceIsDeterministic` | Timeout 7201.92 s | Timeout 7200.05 s |
| `DriverSeegmillerInlet.MappedInletPreservesTheMassFlow` | Timeout 7201.92 s | Timeout 7200.02 s |
| **`DriverSeegmiller.MeshSequence`** | **Passed 6963.18 s** | **Timeout 7200.08 s** |

Under CLAUDE.md §8 a known failure stays known only while it reproduces its recorded values, and
**any new or changed failure is a regression**. So this is reported as one, and P18.8 stops here for
a decision rather than classifying its way past it.

### What the evidence says about the cause

**It is a Debug-only marginal test.** The same test in Release:

| | P18.7 | attempt 2 |
| --- | --- | --- |
| `release_cuda` | Passed 1219.90 s | Passed 1323.84 s |

1220–1324 s against a 7200 s cap — 17–18 % of it. In Debug it ran at **6963.18 s, which is 96.7 % of
the cap, a margin of 3.3 %.**

**It has a documented history of straddling that cap.** Eight measurements are now on record:
7123.03 P, 7201.94 T, 6764.75 P, 7034.57 P, 7200.12 T, 7168.06 P, 6963.18 P, and now **7200.08 T** —
**5 pass, 3 timeout, every one within 6 % of the cap.**

**Attribution — and this is the part that constrains it.** CLAUDE.md §6 requires two libraries before
blaming a change. Here the stronger form is available:

* `MeshSequence` is a **CPU** driver test (incompressible + turbulence).
* P18.8 modified **no CPU production file**. All 12 CPU species sources are **byte-identical** to the
  hashes recorded in P18.8's own baseline, verified by `tools/check_integrity.sh` check 6.
* P18.8's additions are GPU kernels, a host/device header included by no CPU production file, and
  test files. None is reachable from this test.

So a code-level slowdown is ruled out by construction, not by argument.

**What remains is scheduling and machine variance, and P18.8's contribution to it is bounded.** With
`-j 8`, the four Seegmiller tests held four slots for the full 7200 s while the other four cycled
through the rest. P18.8 adds 40 rows to that stream, costing ~40 s of Debug execution in total —
roughly **0.5 % of one of four slots**, against a **3.3 %** margin. That is an order of magnitude too
small to account for the flip, but it is not zero, so P18.8 cannot be excluded as a *contributing*
factor on arithmetic alone.

### What would settle it, and why it was not done here

Running `MeshSequence` alone in `debug_gui` would separate contention from a genuine slowdown: in
isolation it should land near its single-test time, well inside the cap. That is a ~2-hour
measurement and it is **not** authorized under P18.8's scope — it would also not change the gate
verdict, since the test is not P18.8's and its cap is not P18.8's threshold to move.

**`P18_GATES.md`'s G10 is the gate that must predict a branch for this test, and G10 belongs to
P18.9.** This run supplies its 8th data point.

### What was explicitly not done

The 7200 s timeout was **not raised**. The test was **not** excluded, re-run until it passed, or
reclassified. The authorization forbids raising a timeout to obtain a pass, and the frozen exclusion
list is P18.7's five patterns, unchanged.

## 4. Runtime noise, classified then restored

A full `ctest` rewrites tracked `results/validation/**`. 50 files changed. Classified with P18.7's
fail-closed instrument (`logs/11_validation_noise_classified.log`):

| check | result |
| --- | --- |
| distinct changed keys | 37, **all** in the allow-list of timing and iteratively-converged quantities |
| 10 physics fields that must not move (`wall_shear`, `u_tau`, `y_plus`, `nusselt`, `reattachment`, `cf_`, `pressure_coefficient`, `mass_flow`, `max_divergence`, `mass_imbalance`) | **all 0** |
| positive control — did the classifier read a diff at all | **1 074 changed lines inspected** |
| max relative change, non-timing | **3.513e-06** (`extrapolated_error`, distorted-mesh MMS) |
| keys at the convergence floor | 810, max **absolute** change 1.328e-08 |
| keys absent from the new file (a structural change) | **0** |
| restored | 50 → **0** |

Established by experiment in P17.11 and recorded as `KNOWN_LIMITATIONS` L36: the tracked files are
**Release-generated**, and the `debug_gui` pass overwrites them with Debug numbers because an
iterative solve converges to a slightly different point under different optimisation. L36's recorded
bounds are ≤ 4.1e-07 for raw norms and ≤ 1.1e-05 for Richardson-extrapolated quantities; the measured
3.513e-06 is a Richardson quantity and sits inside that.

`results/validation` is back to its tracked state.

## 5. Verdict

* **`release_cuda`: PASS** — exact on all six figures, 0 failures.
* **`cpu_only`: PASS** — exact on all six figures, 0 failures.
* **`debug_gui`: one changed row** — `DriverSeegmiller.MeshSequence`, a Debug-only test with a
  3.3 % margin and a documented 5-pass/3-timeout history, whose code path P18.8 provably did not
  touch. **Reported as a regression per CLAUDE.md §8; awaiting a decision.**
* **P18.8's own 40 rows: 0 failures in any configuration.**

## 6. Resolution — added 2026-10-02 after the decision

§3 above is kept exactly as written, including its verdict that the row be reported as a regression.
That was the right procedure on the baseline I had used. **The conclusion was wrong**, and the
correction is in `decision.md`:

`results/p18/gates/P18_GATES.md` — G11's acceptance clause, in the frozen file whose hash this phase
recomputed at every step — states that **THE 4 DEBUG+GUI TIMEOUTS ARE INHERITED, NOT P18's**, that
they are pre-P17 DriverSeegmiller cases **passing in both Release configurations**, and that the
7200 s timeout is not to be raised to clear them.

This run measured exactly that: **4** timeouts, all DriverSeegmiller, all four passing in **both**
Release configurations, timeout unchanged.

| test | Release+CUDA | CPU-only | Debug+GUI |
| --- | --- | --- | --- |
| `DriverSeegmiller.MeshSequence` | Passed 1323.84 s | Passed 1012.19 s | Timeout 7200.08 s |
| `DriverSeegmiller.CorrectedSequenceIsDeterministic` | Passed 2001.74 s | Passed 1471.38 s | Timeout 7200.05 s |
| `DriverSeegmillerInlet.MediumGridAB` | Passed 2011.30 s | Passed 1493.78 s | Timeout 7200.06 s |
| `DriverSeegmillerInlet.MappedInletPreservesTheMassFlow` | Passed 1587.17 s | Passed 1206.40 s | Timeout 7200.02 s |

**So `0 new failures` is satisfied and there was no regression.** My prediction of 3 came from
P18.7's *observed run*, not from the gate's *stated contract*; P18.7's run is the deviation, having
had one of the four squeeze under the cap at 6963.18 s.

§3's mechanism analysis stands and is what makes the gate's "inherited, not P18's" claim verifiable
rather than asserted: Debug-only, 3.3 % margin, a CPU test, and no CPU production file modified.

**Verdict, corrected: all three configurations conform to the frozen gate. 0 new failures.**
