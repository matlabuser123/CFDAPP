# PRESERVED FAILURE — P17.4 gate attempt 1: two tests were VACUOUS

Run 2026-09-26T21:17Z. **Preserved, not deleted**, per `CLAUDE.md` §4 and §5.3.

## What failed

```text
momentum suite            88% passed, 2 FAILED of 17
compressible suite        99% passed, 2 FAILED of 170
  MatchesMinusTwoThirdsMuGradDivergenceTimesVolume   FAILED
  PressureForceMatchesMinusVolumeTimesAnalyticalGradient  FAILED
```

Both failures are the **same assertion**, and it is one I wrote deliberately:

```text
test_compressible_momentum.cpp:288: Failure
  Expected: (checked) > (0), actual: 0 vs 0
  no deep-interior cells -- the mesh is too small to verify anything
test_compressible_momentum.cpp:435: Failure
  Expected: (checked) > (0), actual: 0 vs 0
```

## Diagnosis — a vacuous test, caught by its own non-vacuity guard

Both tests verify an analytical value at **deep-interior** cells only (at least two cells
from any boundary), because `acceptance_gate.md` §2 records that `grad(div U)` and the
Green-Gauss pressure gradient are exact only there.

The helper:

```cpp
return c.centroid().x > 2.0 * h && c.centroid().x < lx - 2.0 * h &&
       c.centroid().y > 2.0 * h && c.centroid().y < ly - 2.0 * h;
```

Both tests used an **8 x 4 mesh on 8.0 x 4.0**, so `h = 1.0` and the y condition becomes

```text
y > 2.0   AND   y < 4.0 - 2.0 = 2.0        <- impossible
```

Zero cells qualified. The loop body never ran, **every EXPECT_NEAR inside it was skipped**,
and the tests would have reported PASS while verifying nothing at all — if not for the
guard:

```cpp
ASSERT_GT(checked, 0) << "no deep-interior cells -- the mesh is too small to verify anything";
```

## Classification

```text
NOT a code defect       the dilatational operator and the pressure assembler are untouched
                        by this; the other 7 P17.4 rows passed, including the
                        divergence-free, uniform-flow, viscosity-scaling, invalid-input and
                        determinism checks
NOT a criterion problem the deep-interior restriction is correct and is KEPT verbatim --
                        asserting exactness at boundary-adjacent cells would fail a correct
                        implementation, which is exactly why §2 of the gate says so
A TEST-DESIGN ERROR     my mesh was too thin in y to contain a single deep-interior cell
```

This is the third time in P17 that a guard written to prevent a vacuous pass has actually
fired (P17.2's N6 control, P17.3's `rho*U=const` non-zero assertion, and now this). The
guard is doing the job it was added for.

## The fix — the MESH, not the criterion and not the code

```text
before   8 x 4 on 8.0 x 4.0    h = 1.0   deep-interior cells: 0
after   10 x 10 on 10.0 x 10.0 h = 1.0   deep-interior cells: 6 x 6 = 36
```

The deep-interior definition, the tolerances, and the production code are **unchanged**.
Only the mesh dimensions change, so that the region the criterion is stated over actually
exists.

## What was salvaged, and is valid

```text
build                                   exit 0, 173 s
clang-format on the changed files       CLEAN
dilatational option defaults to false   verified
7 of 9 P17.4 rows                       Passed, including:
    IsExactlyZeroForADivergenceFreeField          the safety property
    UniformVelocityContributesNothing
    ScalesLinearlyWithViscosityAndVanishesAtZeroViscosity
    RejectsInvalidInputs
    RepeatedAssemblyIsBitwiseIdentical
    BodyForceScalesLinearlyWithDensity
    TransientTermDistinguishesDRhoUDtFromRhoDUDt
low-Mach + MMS                          10 / 10   UNCHANGED -- the default-off path
P14 / P15 / P16                         68 / 161 / 315   all unchanged
G8 GPU compressible momentum            0 hits, NOT PRESENT
```

The low-Mach and MMS suites passing is the significant one: it confirms the default-off
wiring leaves the validated compressible path alone.
