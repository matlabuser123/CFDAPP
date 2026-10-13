# P17.0 — baseline regression gate, FROZEN

**Frozen 2026-09-26T04:10Z, before any P17.0 build or test ran.** Every number below
is *predicted* from P13's G10 record plus P17.2's known test delta. Nothing here was
written after seeing a P17.0 result.

Purpose: make the baseline regression an **instrument** rather than a formality. A
run that merely says "PASS" proves almost nothing; a run that lands on a
pre-committed count proves the tree is the one P13 qualified plus exactly P17.2.

---

## 1. Execution policy — frozen, copied verbatim, not re-derived

From `results/p13/final-qualification/2026-09-24/g10-closeout/tools/G10_regress.sh`:

```text
invocation    OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
              Debug+GUI only: --timeout 14400  (AMENDMENT_G10_debug_timeout.md)
PATH          /usr/local/cuda-12.9/bin first
exclusions    SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel
              |WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence
build trees   build/qual-final, build/qual-debug, build/qual-cpuonly
```

`OMP_NUM_THREADS=1`, `-j` and `--timeout` are **execution-environment settings, not
test criteria** — the classification `AMENDMENT_G3.md` established and G10 relied on
twice. They govern how much machine a test gets, never what result is accepted.

The prefix is load-bearing, not an optimisation. G10's first attempt omitted it and
died after 5h38m at 64/2537 with 7 contention timeouts at load 255.94; that invalid
run is preserved under `PRESERVED_invalid_no_omp_limit/`. The exclusion set is
**five patterns, thirteen tests per configuration**, and is not widened by one entry.

## 2. What P17.2 must have added

P17.2 added 9 new `TEST(IdealGasEOSTest, …)` cases to
`tests/unit/compressible/test_ideal_gas_eos.cpp`, taking that file from 12 to 21.
They are plain unit tests: no CUDA, no GUI, no `DISABLED_` prefix, and no name
matching any exclusion pattern. Therefore **+9 registered and +9 executed in all
three configurations**, and +0 excluded, +0 disabled.

## 3. Frozen expected counts

P13 G10's measured numbers, and the arithmetic that closes them exactly:

```text
config          registered  -excluded  -disabled  = executed   (G10, measured)
release_cuda        2550        13         49         2488
debug_gui           2612        13         49         2550
cpu_only            2348        13         47         2288
```

The `Disabled` figures are G10's raw `grep -c '(Disabled)'` counts of 98/98/94
divided by two, because that pattern matches twice per test in ctest output. The
arithmetic closing to G10's executed totals on all three rows is what confirms the
division is right rather than convenient.

**Predicted at the P17.0 freeze:**

| id | configuration | registered | excluded | disabled | executed | failed |
| --- | --- | --- | --- | --- | --- | --- |
| B1 | Release+CUDA `build/qual-final` | **2559** | 13 | 49 | **2497** | **0** |
| B2 | CPU-only `build/qual-cpuonly` | **2357** | 13 | 47 | **2297** | **0** |
| B3 | Debug+GUI `build/qual-debug` | **2621** | 13 | 49 | **2559** | **0** |

B1 and B2 are the two the authorization mandates — the CUDA baseline and the CPU
baseline. B3 is P13's third qualified configuration, run because it is the only one
that exercises the GUI, at a measured cost of ~3.3 h.

## 4. Acceptance conditions

```text
B1..B3    executed count EXACTLY as predicted, failed = 0
B4        the 9 new EOS tests are present BY NAME and Passed in every configuration
          — registration is not execution (G10 learned this: gtest_discover_tests
          populates the list at build time, so a stale tree can omit new rows)
B5        every test binary is newer than the newest production source, per §8's
          authoritative sequence; the harness records it and fails closed
B6        CPU-only links no CUDA: 0 nvcc invocations, 0 libcudart references,
          no cuda/libcfdcuda.a, 0 cudart in ldd of the CLI
B7        the exclusion set is the same 5 patterns; no pattern added or widened
B8        P17_CODE_FINGERPRINT unchanged across the whole regression — running tests
          must not modify code
```

## 5. What a deviation means — decided in advance

An executed count that is not the predicted one is a **finding, not a number to
adjust**. Classification, fixed now so it cannot be chosen to suit the result:

```text
executed = predicted, failed = 0        the tree is P13's qualified state + P17.2
executed = predicted - 9                the test binary is STALE; P17.2's tests were
                                        never compiled in. Invalid as evidence; rebuild.
executed > predicted                    something outside P17.2 added tests. Attribute
                                        before proceeding.
executed < predicted, not -9            registration changed. Attribute before proceeding.
failed > 0, reproduces G10's values     pre-existing; classify and preserve
failed > 0, new or changed values       REGRESSION. Stop. P17.0 is BLOCKED.
timeout only, 0 assertion failures      execution-environment, per AMENDMENT_G3;
                                        record the measured runtime, do not raise a
                                        ceiling inside P17.0 without authorization
```

Per `CLAUDE.md` §1.3 and the authorization's stop rule: **stop at the first genuine
failure**, preserve it, and do not continue because later items would probably pass.

## 6. Forbidden, restated

```text
no threshold, tolerance, iteration limit or sanitizer setting is weakened
no test is disabled, skipped or special-cased
no exclusion is added
no failed run is deleted or overwritten — see §5's classification instead
no commit, push, reset, clean, stash or checkout
```

## 7. Fingerprints at freeze

```text
SOURCE_FINGERPRINT_V2   fbf786656eac2284956a4e29fe2745157a3e59ab339833a9f49ff6e048bd9fd6
P17_CODE_FINGERPRINT    1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
HEAD                    3bd32f918656f471e72b5f6ac7cb6c2cbf75a274 (dirty)
```

V2 is expected to move if the concurrent `ROADMAP.md` work continues; that is a
documentation fact, not a source regression, and B8 is stated against
`P17_CODE_FINGERPRINT` for precisely that reason. See `source_state.md` §3.
