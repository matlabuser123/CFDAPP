# P18.1 — Multi-Species State — SUMMARY

```text
P18.1 STATUS: COMPLETE

GATE              results/p18/gates/P18_GATES.md
                  sha256 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3
                  re-verified UNCHANGED at P18.1 start and at close. No threshold was derived,
                  chosen or adjusted by this phase -- every one is G2's, inherited verbatim.
G2                PASS, all four criteria (a) (b) (c) (d).
G1 regression     PASS. Release+CUDA and CPU-only reproduce the baseline EXACTLY. Debug+GUI has
                  one FEWER inherited failure, attributed to a marginal pre-existing test that
                  P18.1 provably cannot reach. NO NEW FAILURE IN ANY CONFIGURATION.
G10               state-layer determinism only. The solver-level claim is NOT made; it is P18.8/9's.
NOT CLAIMED       G3 G4 G5 G6 G7 G8 G9 G11 -- none is measurable from a state-only change (§36).

TREE AT START     P18_START_SOURCE_FINGERPRINT
                  60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318
                  identical to the P18.0 freeze -- P18.1 began from the qualified baseline.
```

---

## 1. State architecture

```text
species state type   cfd::species::MassFractionState      -- values, invariants enforced
species identity     cfd::species::SpeciesRegistry        -- ordered names, bijective name<->index
                     Split deliberately: P18.3 needs identity with no field in sight; P18.2 needs
                     the fields. MassFractionState holds the registry BY VALUE, so a field set can
                     never be read against the wrong ordering.
storage layout       SPECIES-MAJOR, std::vector<cfd::fields::ScalarField>, Y_i[cell] == field(i)[cell]
                     Chosen to match the repository's established field-per-species convention, so
                     the state holds exactly the objects the existing stack already holds and the
                     compatibility claim is a bitwise identity rather than a reinterpretation.
                     Cell-major is NOT pre-empted -- P18.8 owns GPU layout (§32).
number of species    N >= 1, unbounded. Verified at N = 1, 2, 3, 64.
number of cells      >= 1, fixed at construction, identical for every species.
mutation policy      VALID BY CONSTRUCTION, PER-CELL ATOMIC.
                     Every constructor validates and throws; there is no way to obtain an invalid
                     state and no default constructor. The ONLY mutator takes a COMPLETE cell
                     composition and validates it before writing anything, so a rejected call
                     leaves the state bitwise unchanged.
                     setMassFraction(species, cell, value) is DELIBERATELY ABSENT: changing one
                     Y_i necessarily breaks sum(Y)=1, so such a setter could only leave the object
                     invalid or silently repair the remainder. Both are forbidden (§8, §18, §24).
```

## 2. Invariants

```text
Y_i lower bound      0, inclusive          boolean, no tolerance (G2(b))
Y_i upper bound      1, inclusive          boolean, no tolerance
sum criterion        |sum_i Y_i - 1| <= 4 * eps * N_species, PER CELL   (G2(a), inherited)
sum tolerance        eps = 2.2204460492503131e-16
                     N=1  8.8817841970012523e-16     N=3  2.6645352591003757e-15
                     N=2  1.7763568394002505e-15     N=64 5.6843418860808015e-14
finiteness           std::isfinite required; NaN and Inf rejected
summation order      ascending species index, left to right, one Real. No unordered iteration,
                     no pairwise or compensated sum, no parallel reduction.
normalization policy NEVER IMPLICIT. normalizeMassFractions() is a separate named free function,
                     called by no constructor and no mutator. {0.8, 0.8} is REJECTED by the state
                     and becomes {0.5, 0.5} only when a caller asks for that.
remainder species    NOT IMPLEMENTED (§24). Every species carries its own stored value, so the sum
                     criterion is a genuine check rather than an identity that cannot fail.
```

## 3. Validation measurements

```text
max valid sum error             1.7763568394002505e-15   =  8.000 eps   ACCEPTED
                                (exactly the N=2 bound -- the criterion is tight, not slack)
largest accepted composition    8.000 eps deviation
smallest rejected composition   9.000 eps deviation      =  1.9984014443252818e-15
discrimination width            1 eps

largest accepted Y_i            1        smallest accepted Y_i   0
NaN detections                  rejected      Inf detections     rejected (both signs)
negative-Y detections           4 (incl. -1e-12 with an exactly-1 sum)
Y>1 detections                  included in the above
invalid sums detected           17
```

Measured by stepping one entry of `{0.5, 0.5}` across the bound in **ULPs**, so nothing depends on
a hand-picked magnitude. `9 eps ~ 2e-15` is already rejected -- some twelve orders of magnitude
tighter than the `1e-3` §30 warns against.

```text
PER-CELL, NOT AGGREGATE   one bad cell planted in 100000; neither field's global min or max moves.
                          Detected: "mass fractions at cell 99999 sum to 1.0000000010000001".
                          The pre-P18 global {min,max} diagnostic cannot see this at all.
DETERMINISM               compositionSum() bitwise identical over 100 repeats and across a copy.
```

## 4. Compatibility

```text
existing passive behaviour   PRESERVED BY CONSTRUCTION. P18.1 is strictly additive: two new
                             production TUs, one new test file, two CMakeLists lines. No
                             pre-existing production or test file was modified.
                             Measured: 37/37 pre-existing species unit tests, plus the full
                             three-configuration regression.
serialization                UNCHANGED -- no case key added, removed or renamed; the 14 parser
                             tests are untouched. A configuration->state->serialization round trip
                             is NOT claimed: the state is not yet case-reachable. Recorded as a
                             LIMIT, not a pass.
boundaries                   NOT TOUCHED. The species boundary lag is a P18.0 finding for P18.2,
                             and CLAUDE.md §2 forbids repairing it inside this phase.
export                       UNCHANGED and COMPATIBLE -- field(name) returns the same ScalarField
                             type the existing exporter already consumes. No new export subsystem.
GPU                          NO CUDA CODE ADDED (§33). P18.8 owns equivalence.
```

### The one real gap, recorded rather than faked (§31)

Tested against the **actual committed output** of a shipped case —
`cases/species_diffusion/results/fields.csv`, column `concentration_CO2`, 80 cells:

```text
(a) all 80 shipped values lie in [0, 1]          range 2.4999968e-02 .. 9.7499997e-01
(b) as a ONE-species mixture they are REJECTED   correct: a passive TRACER is not a composition,
                                                 and the only valid one-species composition is Y=1
(c) with an explicit carrier (N2 = 1 - Y_CO2)    VALID, and all 80 values come back BITWISE
```

**P18.2 must decide how a case declares a carrier / complementary species.** P18.1 deliberately
does not invent one: §24 forbids introducing a remainder rule no frozen design chose. The three
tests above are what will change when that decision is made.

## 5. Negative controls — 5 of 5 DETECTED

```text
C1 bounds check removed        DETECTED  4 tests
C2 sum check removed           DETECTED  5 named tests (8 failing in all)
C3 finiteness check removed    DETECTED  RejectsNaN -- and RejectsInf correctly STILL PASSES,
                                         as predicted in the harness before the run, because Inf
                                         is caught by the bounds check. The finiteness check
                                         guards NaN specifically and is not redundant.
C4 species identity weakened   DETECTED  2 tests
C5 frozen threshold widened    DETECTED  2 tests   (4*eps*N -> 1e-3 is caught)

production source afterwards   BYTE-IDENTICAL to pre-control, sha256 verified
```

**C4 first reported VACUOUS, and that is preserved** (`logs/04_`, diagnosed in `logs/05_`). The
control had worked; the harness grepped for the test under the wrong suite name. It **failed
closed** — reporting vacuity rather than a detection it had not observed — which is the right
direction for an instrument to fail. The identity invariant was then given a second guard.

## 6. Tests

```text
focused P18.1        60 new:  SpeciesRegistryTest 12  +  MassFractionStateTest 48   all passed
existing species     37 / 37 passed (unchanged suite, same binary)
CFDSpeciesTests      97 / 97 passed

Release+CUDA         2823 selected · 2772 Passed · 0 FAILED · 2 Skipped · 49 DISABLED · 2060 s
CPU-only             2600 selected · 2553 Passed · 0 FAILED · 0 Skipped · 47 DISABLED · 1442 s
Debug+GUI            2885 selected · 2831 Passed · 3 FAILED · 2 Skipped · 49 DISABLED · 8172 s

P14 / P15 / P17 / SIMPLE   preserved -- carried inside the three full configurations above, not
                           measured separately.
clang-format               the 5 P18.1 files clean; 28 PRE-EXISTING offenders in the tree, none of
                           them a P18.1 file, inherited and NOT fixed here.
sanitizers                 NOT RUN -- G2 does not require them and CLAUDE.md §8 places them in the
                           full regression G11 settles at P18.9.
```

### The one deviation

```text
Debug+GUI predicted 4 FAILED, measured 3. DriverSeegmiller.MeshSequence PASSED at 7123.03 s,
76.97 s under the unchanged 7200 s cap (98.93% of the limit).

NOT P18.1's, on five independent grounds (logs/11_debug_gui_deviation.log):
  1. the cap was not raised -- the other three fired at 7200.6x s
  2. exactly SIX files in the repository name either new type, and ZERO are on this test's path
  3. the same test passes in Release in this same run, at 1213 s -- 5.9x under the cap
  4. the run was 4.0% faster overall (8172 s vs 8514 s)
  5. the arithmetic closes exactly: 2770 P + 4 F = 2774; 2831 P + 3 F = 2834 = 2774 + 60

NO NEW FAILURE APPEARED IN ANY CONFIGURATION.

USEFUL OUTPUT: the inherited set is not "4 timeouts" but THREE RELIABLE TIMEOUTS PLUS ONE
MARGINAL TEST within ~1% of the cap. P18.9 cannot rely on "the identical set, test id for test
id" reproducing. Reported for TODO.md.
```

## 7. Repository changes

```text
production files added     include/cfd/species/SpeciesRegistry.hpp
                           include/cfd/species/MassFractionState.hpp
                           src/species/SpeciesRegistry.cpp
                           src/species/MassFractionState.cpp
production files modified  src/CMakeLists.txt                  (+1 target_sources block)
tests added                tests/unit/species/test_mass_fraction_state.cpp
test files modified        tests/unit/species/CMakeLists.txt   (+1 line)
threshold changes          NONE. No existing threshold was touched; P18.1 derived none of its own.
tests deleted or disabled  NONE.
evidence                   results/p18/multi_species_state/  (design · invariants · compatibility ·
                           validation · negative_controls · coverage_map · review · regression ·
                           summary · logs/ · tools/)
```

## 8. Integrity

```text
transport implemented early      NO -- no convection, diffusion, source or linear solve added
chemistry implemented early      NO
mixture properties early         NO
silent clipping                  NO -- rejection only, proved by C1/C2 and by the {0.8,0.8} test
silent normalization             NO -- proved by ConstructionNeverNormalisesSilently and C2
threshold weakening              NO -- and C5 proves weakening would be caught
deleted tests                    NONE
frozen docs                      P18_GATES.md and P17_GATES.md re-hashed at close, both UNCHANGED
validation runtime noise         50 files rewritten by the Debug run, RESTORED; wall_shear
                                 changes 0, matching the known L36 attribution. 0 modified after.
abandoned run                    one regression launch was stopped and restarted after three
                                 compatibility tests were added; the log is PRESERVED at
                                 logs/10_ABANDONED_regress_superseded.log and the prediction was
                                 re-derived (+57 -> +60) BEFORE relaunching, not fitted afterwards.
commit / push                    NONE. Not authorized.
```

## 9. Next

```text
P18.2 — Species Transport & Diffusion
NOT AUTHORIZED YET
```

Carried forward for whoever authorizes it:

```text
1. the carrier-species decision (§4 above) -- how a case declares the complementary species, which
   is what stands between the shipped cases and a genuine mixture;
2. the species boundary lag -- P18.0's confirmed defect, 335 outer iterations against thermal's 2;
3. the Debug+GUI known-limitation refinement (§6 above);
4. 28 pre-existing clang-format violations, inherited from the uncommitted P14-P17 tree.
```
