# TD-046 — frozen gate. The FIXTURE is corrected to meet its own UNTOUCHED assertions.

```text
SCOPE        one constant in tests/unit/species/test_premixed_flame.cpp: the LinearProperty
             slope at line 625, 4.0e-5 -> 1.0e-4, plus the stale comment that describes it.
             NO ASSERTION IS CHANGED. No production file is touched.
WHY          the fixture and its own precondition disagree arithmetically, so the test dies at
             line 627 in 6 ms and has NEVER executed its solves. Established in
             technical_debt/TD006_g8_closure/audit.md section 3.2 and
             logs/01_lasttest_release_2026-10-11.log.
FROZEN       before the edit. Production source PremixedFlame.cpp
             fa815c97216584485e1c94842669fbf71976b9fb06863096868972d88448e3a2 (must not change).
```

---

## 1. THE DERIVATION — the slope follows from the assertions, not the reverse

`LinearProperty::value(T) = referenceValue + slope * (T - referenceTemperature)`
(`src/physics/TemperatureProperty.cpp:42`-`:45`), with `referenceValue = kConductivity = 0.05`,
`referenceTemperature = kUnburntT = 300`, and `kExpectedAdiabatic = 1800`.

### 1.1 The binding constraint is the precondition, which stays as written

```text
ASSERT_GT(risingConductivity.value(1800), 3.0 * 0.05)
  =>  0.05 + 1500 s > 0.15  =>  s > 6.666667e-05
current s = 4.0e-5  ->  lambda(1800) = 0.1100 = 2.20x   FAILS, by construction
```

### 1.2 The main assertion's own requirement is LOOSER, and is derived

`S_L ~ sqrt(alpha)` with `alpha = lambda/(rho cp)`. The front spans `T_u..T_ad`, so the
representative conductivity is near the midpoint, `lambda(1050) = 0.05 + 750 s`:

```text
EXPECT_GT(flameRising.flameSpeed, 1.3 * flameConstant.flameSpeed)
  =>  sqrt((0.05 + 750 s)/0.05) > 1.3  =>  s > 4.600000e-05
```

So `6.667e-05` governs. **The fixture was below BOTH requirements.**

### 1.3 Why 1.0e-4 and not the 6.6667e-5 minimum

```text
s           lambda(1800)   ratio@T_ad   expected S_L ratio   margin over 1.3
6.6667e-05      0.1500         3.000           1.414              8.8 %
1.0000e-04      0.2000         4.000           1.581             21.6 %
```

`6.6667e-05` makes `lambda(1800)` **exactly** `3x` against a **strict** `>`. A strict inequality on
an exactly-equal value is not a test, it is a coin toss on rounding. `1.0e-4` clears the
precondition by 33 % and the main threshold by 22 % on the estimate above.

**And it is more discriminating, which is the point of the test.** A frozen-lambda defect returns
an S_L ratio of exactly `1.000`, which is 23 % BELOW the 1.3 threshold, while a correct solver is
expected near `1.58`, 22 % above. The threshold sits between the two with margin on both sides.

## 2. CRITERIA — frozen in this order, stop at the first failure

```text
C1  the precondition executes and PASSES: value(1800) = 0.200000 > 0.15, and the printed
    value must be exactly 0.2 (the slope is exact in binary: 1.0e-4 * 1500 = 0.15).
C2  the reference point is unmoved: ASSERT_NEAR(value(300), 0.05, 1e-15) still passes.
    The slope cannot affect it -- 300 K IS the reference temperature -- so a failure here
    means the edit hit the wrong argument.
C3  both solves return PremixedFlameStatus::Converged.
C4  EXPECT_GT(flameRising.flameSpeed, 1.3 * flameConstant.flameSpeed).
C5  EXPECT_GT(flameRising.flameThickness, flameConstant.flameThickness).
C6  NON-VACUITY: the test must still be able to FAIL. The frozen-lambda defect it is named
    for would give a ratio of exactly 1.000 against a 1.3 threshold. This is argued, not run
    -- see section 4.
C7  REGRESSION: CFDSpeciesTests, and no other test's value changes. The edit touches one
    fixture inside one test, so nothing else can see it; C7 verifies that claim.
```

### 2.1 C4 AND C5 ARE GENUINELY UNKNOWN, AND THAT IS PRE-REGISTERED

**This test has never executed a single solve.** C3, C4 and C5 have no recorded outcome on any
tree. The estimate in section 1.3 says C4 should pass near `1.58`, but that estimate uses a
midpoint-average `lambda_eff` that is a modelling choice, not a measurement.

```text
I COMMIT NOW, BEFORE RUNNING:
  * if C4 fails, I will NOT raise the slope further and I will NOT lower the 1.3 threshold.
    Either would be tuning the instrument until it agrees, which is what section 5.3 and
    section 7 forbid. A C4 failure is a REAL FINDING about lambda(T)'s effect on S_L and gets
    preserved and reported.
  * if C5 fails while C4 passes, that is also a finding: thickness and speed would be
    disagreeing about the same diffusivity change.
  * if C3 fails, the rising-lambda case does not converge, and that is a solver finding --
    note that a 4x lambda shrinks dt by 4x through PremixedFlame.cpp:718, against the
    unscaled maximumStepsPerTrial = 20000 in this fixture, which is TD-027's mechanism and a
    plausible C3 risk.
```

## 3. WHAT IS NOT BEING CHANGED, EXPLICITLY

```text
NOT CHANGED  ASSERT_GT(value(1800), 3.0 * kConductivity)   -- the precondition
NOT CHANGED  ASSERT_NEAR(value(300), kConductivity, 1e-15)
NOT CHANGED  the 1.3 factor in the main assertion
NOT CHANGED  the thickness assertion
NOT CHANGED  any production file
```

**This is a fixture repair, not a threshold change.** The assertions stay exactly as the author
wrote them; the fixture is corrected so it delivers what they already demand. Nothing is loosened,
so CLAUDE.md section 7's prohibition is not engaged.

## 4. NON-VACUITY, AND ITS LIMIT

C6 is **argued, not demonstrated**. Demonstrating it properly would mean building a mutant solver
that freezes `lambda` at the inlet and showing this test fails against it — a production mutation,
out of scope here.

What IS established without a mutant: `PremixedFlame.cpp:1052` evaluates `lambda` as a per-cell
field from the temperature field, so the defect the test is named for does not currently exist.
That makes C6 a statement about the test's power against a hypothetical regression, and it is
recorded as argued so a later reader does not mistake it for measured.

## 5. COST

Two 500-cell solves. The fixture's converged solve took `2735 s` in the 2026-10-11 Release run,
and the rising-lambda case has a 4x smaller `dt`, so it may take longer or hit
`maximumStepsPerTrial`. Budget: up to the repository cap of 7200 s per gtest case.
