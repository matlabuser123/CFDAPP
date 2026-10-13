# P17.1 — unit tests

`CompressibleMassFluxTest` went from **12 to 20** tests. All 20 pass. Raw:
`logs/02_mass_flux.log`.

---

## 1. The eight new tests

```text
test                                              gate ids   what would break it
--------------------------------------------------------------------------------------
RejectsNegativeCellDensity                        D1 D4 D5   no positivity check
RejectsZeroCellDensity                            D6         a >= 0 boundary
RejectsNonFiniteCellDensity                       D2         no finiteness check
FaceDensityEvaluationRejectsInvalidCellDensity    D3         validating only in the
                                                             outer entry point
ValidUniformDensityIsUnaffectedByTheNewValidation  -         the new check perturbing
                                                             a valid result
ReversingVelocityReversesMassFluxExactly          M2         a sign or orientation error
FaceDensityIsRecomputedPerCallNotCached           M5         caching the face field
RowVaryingDensityConservesMassAndScalesFluxPerRow S1-S4 K3   a face density collapsed to
                                                  K4 M3      a constant; a conservation
                                                             error
```

## 2. Why each is more than a getter check

### RejectsNegativeCellDensity — asserts the *message*, not just the throw

```cpp
EXPECT_NE(what.find("cell 0"), std::string::npos);
EXPECT_NE(what.find("density"), std::string::npos);
```

A bare `EXPECT_THROW` would pass against an exception thrown for the wrong reason from
the wrong place. Requiring the cell index and the field name pins it to *this* check and
matches the precedent `evaluateDensityField` and `IdealGasEOS` already set.

### RejectsZeroCellDensity — the boundary is at zero, not near it

`rho = 0` is the case the pre-P17.1 tree handled worst: it produced `rho_f = 1` and a
plausible finite flux. Control **N3** (`>= 0` instead of `> 0`) breaks this test and
**only** this test, which is what proves the boundary sits exactly at zero.

### ValidUniformDensityIsUnaffectedByTheNewValidation — asserts the dry-run reference

```cpp
EXPECT_EQ(flux[f], 2.0);   // internal face, exact
```

The value `2.0` was **measured on the unchanged tree before the validation existed**
(`logs/00_dryrun.log` group D). Asserting it exactly is what shows the added precondition
rejects bad input without perturbing good input — a claim that would otherwise rest on
the absence of a failure elsewhere.

### ReversingVelocityReversesMassFluxExactly — exact, not toleranced

Negating the velocity negates `dot(U_f, Sf)` and leaves the face density untouched, so
every flux is the exact IEEE negation of its counterpart. `EXPECT_EQ`, not
`EXPECT_NEAR`. The density is deliberately non-uniform (`density[0] = 1.5`) so the test
is not merely exploiting a symmetric field.

### FaceDensityIsRecomputedPerCallNotCached — a distinct property

Mutates `density[0]` between two calls and requires the flux to change. This is
**freshness**, not spatial dependence: control N4 collapses the spatial dependence but
still reads `density[0]` live, so this test correctly does **not** break under N4. That
distinction is recorded in `AMENDMENT_N_predictions.md`, because the frozen gate
predicted otherwise and the prediction was wrong.

### RowVaryingDensityConservesMassAndScalesFluxPerRow — the strongest test added

Two assertions, and the second is the one that matters:

```text
(1) conservation      per-cell imbalance EXACTLY 0, globalNetFlux EXACTLY 0,
                      totalAbsoluteImbalance EXACTLY 0 -- with a non-uniform density
(2) discrimination    each row's x-normal flux equals that row's OWN rho * U * A
```

The construction makes (1) exact rather than toleranced: temperature varies by row,
density is the EOS value at each row's temperature (so the field is self-consistent
*including* at boundary faces, where the isothermal path takes the owner cell's own
temperature), and the velocity is uniform along +x. A cell's left and right faces lie in
the same row and cancel bit-for-bit; its top and bottom faces have a y-normal against an
x-only velocity and carry exactly zero.

**(2) is why the test exists.** Conservation alone cannot detect a face density collapsed
to one constant — the fluxes would still cancel and (1) would still pass. Controls N4 and
N5 both perform exactly that collapse, and **this is the only test in the suite that
caught either of them.**

## 3. Group T — p/T coupling was already covered

The gate's Group T criteria are satisfied by existing tests, verified by running them
rather than by assuming:

```text
criterion                           existing test                                     status
----------------------------------------------------------------------------------------------
T1  rho = p/(R T)                   MatchesKnownStandardAtmosphereState                PASS
T2  dRho/dp > 0                     DDensityDPressureMatchesAnalyticalFormula,          PASS
                                    DDensityDPressureIsIndependentOfPressure
T3  dRho/dT < 0                     DDensityDTemperatureIsNegative,                     PASS
                                    DDensityDTemperatureMatchesAnalyticalFormula
T4  2p at fixed T doubles rho       DoublingPressureAtFixedTemperatureDoublesDensity    PASS
T5  2T at fixed p halves rho        DoublingTemperatureAtFixedPressureHalvesDensity     PASS
T6  spatial p and T variation       ThermodynamicPropertiesTest (evaluateDensityField)  PASS
```

Measured in the dry-run, at `%.17g`, before any change:

```text
dRho/dp             1.161237879579632e-05      POSITIVE
dRho/dT            -0.0066666666666666671      NEGATIVE
rho(2p,T)/rho(p,T)  2                          exactly
rho(p,2T)/rho(p,T)  0.5                        exactly
```

T4 and T5 are **exact**, not toleranced: a factor of two is dyadic, so `p/(R T)` scaled
by two in numerator or denominator is exactly representable. No new test was added here —
duplicating a passing, adequate test would add maintenance without adding detection.

## 4. Results

```text
CompressibleMassFluxTest                 20 / 20    (12 existing + 8 new)
IdealGasEOSTest + ThermodynamicProperties 28 / 28
whole compressible suite                137 / 137   (129 baseline + 8 new)
```

The `137 = 129 + 8` is a closed prediction: the eight new tests are plain unit tests with
no `DISABLED_` prefix and no name matching an exclusion, so the suite total had to rise
by exactly eight. It did.

## 5. What these tests do NOT cover

```text
transient compressible continuity   evaluateCompressibleContinuity has no production
                                    caller; P17.1 did not give it one
energy conservation                 no energy equation exists (P17.5)
the dilatational viscous stress     P17.4
3D                                  CompressibleSIMPLE is 2D-only
a converged production solve         covered by the existing low-Mach and coupled
                                    production-case suites, which pass unchanged in the
                                    137, and which P17.1 did not modify
an interior-only invalid TEMPERATURE in a cell not adjacent to any boundary face -- see
                                    boundary_density.md section 6
```
