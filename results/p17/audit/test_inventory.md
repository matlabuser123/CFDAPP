# P17.0 — compressible test inventory

Measured 2026-09-26. Two different counts are reported because they answer two
different questions, and conflating them is how the earlier "90 tests" figure arose.
Each is defined exactly and reproducible.

---

## 1. Source-level count — `TEST` / `TEST_F` macros

Definition: every `TEST(` or `TEST_F(` at line start, in every `.cpp` whose path
contains `compressible` (case-insensitive), plus the two adjacent files that exercise
the compressible path by name.

```text
file                                                            cases
-------------------------------------------------------------------------
unit/compressible/test_ideal_gas_eos.cpp                          21   <- 12 + 9 (P17.2)
unit/compressible/test_thermodynamic_properties.cpp               13
unit/compressible/test_compressible_mass_flux.cpp                 12
unit/compressible/test_compressible_momentum.cpp                   7
unit/compressible/test_compressible_continuity.cpp                 6
unit/io/test_compressible_case.cpp                                15
solver/compressible_simple/test_compressible_simple.cpp           10
solver/compressible_simple/test_compressible_pressure_correction.cpp 6
solver/compressible_simple/test_compressible_relaxed_momentum.cpp  5
solver/compressible_simple/test_compressible_non_orthogonal.cpp    4
solver/compressible_simple/test_compressible_robustness.cpp        3
integration/case/test_compressible_production_case.cpp             8
integration/case/test_compressible_coupled_production_case.cpp     6
integration/compressible/test_low_mach_regression.cpp              7
integration/mms/test_mms_compressible.cpp                          1
-------------------------------------------------------------------------
                                          15 files      124 cases
                                          minus P17.2:  115 before P17.2
```

## 2. Registry-level count — what ctest actually runs

Definition: `ctest -N -R 'Compressible|LowMach|IdealGas|Thermodynamic'` in
`build/qual-final`.

```text
registered and runnable                                          129
```

The 129 exceeds the 124 above because the regex also catches compressible-named cases
living in files whose *path* does not contain "compressible" — for example
`ManufacturedFieldsTest.CompressibleForcingExact` and the two
`assembleCompressibleMomentumComponent` cases in
`tests/unit/discretization/test_operators3d.cpp`. Neither count is wrong; they are
different sets. 129 is the one to quote for "tests that run", and it is the figure the
P17.2 gate recorded.

### Per-suite registry counts

```text
suite regex                            registered
-------------------------------------------------
IdealGasEOSTest                            21
CompressibleCaseTest                       15
CompressibleMassFluxTest                   12
ThermodynamicPropertiesTest                 7
CompressibleSimpleTest                      7
LowMachRegressionTest                       7
CompressibleContinuityTest                  6
CompressiblePressureCorrection              6
CompressibleCoupledProductionCase           6
CompressibleRelaxedMomentum                 5
CompressibleProductionCase                  8
CompressibleSimpleMMS                       1
```

`CompressibleMomentumTest`, `CompressibleRobustness` and `CompressibleNonOrthogonal`
return 0 for those exact strings: the gtest suite names differ from the file names.
Recorded rather than silently dropped — the aggregate of 129 is the authoritative
figure, and a per-suite zero here means "my regex missed it", not "no tests".

## 3. The established preservation instrument

P17.2 defined these regexes and they are reused, verbatim, as P17's preservation
instrument. They are deliberately **narrower** than a naive keyword sweep.

```text
label            regex                                                      count
------------------------------------------------------------------------------------
P14_transport    Transport|LegacyMatrix|ScalarTransport                        68
P15_thermal      EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent
                 |FixedTemperature|HeatFlux|ConvectiveBoundary|Boussinesq
                 |TurbulentConductivity                                       161
P16_turbulence   Turbulence|EffectiveViscosity|WallFunction|WallTreatment
                 |KEpsilon|KOmega                                             315
```

For contrast, a broad keyword sweep over the same tree gives 158 / 250 / 473. **The
two must not be mixed.** A future gate that quotes 161 for P15 but measures with the
broad regex would appear to gain 89 tests it never had. The narrow set above is the
instrument; the broad numbers are recorded here only so the difference is visible.

## 4. What the existing tests actually verify

### Low-Mach validation — thorough, and its criteria are explicit

`tests/integration/compressible/test_low_mach_regression.cpp`, 7 cases:

```text
MachNumberStaysWellBelowPointOne                  machMax < 0.1 AND > 1.25*inletMach
DensityVariationIsSmallRelativeToReferenceDensity deltaRhoOverRho < 1e-2
EosConsistencyErrorIsAtFloatingPointTolerance     maxEosError < 1e-9
CompressibleMassFluxApproachesTheIncompressible-  maxRelativeMassFluxDifference < 0.15
  Limit
GlobalMassImbalanceIsSmall                        <= a computed bound, not a constant
GridRefinementKeepsAllMetricsSmall                all three, across nx
RepeatedRunIsDeterministic                        EXPECT_EQ, bitwise, on density and
                                                  the derived scalars
```

The `> 1.25 * inletMach` clause in the first case is worth noting: it stops the test
passing vacuously on a flow that never accelerates. That is a non-vacuity guard
already built into the existing suite.

Configuration: isothermal channel, `R = 287.05`, `cp = 1005.0`, `T = 300 K`,
`p_ref = 101325 Pa`, `mu = 0.1`, `H = 1.0`, `L = 8.0`, `U_mean = 1.0`.

### Literature validation — Arkilic isothermal lubrication

`tests/integration/case/test_compressible_coupled_production_case.cpp` (6 cases) drives
`cases/compressible_channel_coupled` through the **real production path**
(`ProjectRunner::run`), and one of its cases is a genuine physical validation rather
than a numerical verification:

```text
MatchesArkilicIsothermalLubricationPressureProfile
  analytical   p(x) = sqrt(p_out^2 + slope*(L - x)),
               slope = 24 mu R T mdot / H^3      the isothermal compressible
                                                 lubrication solution
  measured     L2 and Linf relative error of pressureAbsolute over every cell
  acceptance   both < 0.05
  DERIVATION   the lubrication approximation's OWN leading-order error is
               O(reduced Reynolds) = O(Re*(H/L)) ~ 0.17 for this case, so a few
               percent residual is the physically expected level of agreement, not
               FVM discretization error. 5% bounds the measured ~1.3% L2 / ~2.6%
               Linf while staying tight enough to catch a regression in the
               pressure-density coupling.
```

The tolerance is derived from the **reference model's** own error, not fitted to the
observed result — the standard `CLAUDE.md` §5.1 requires.

The other five cases in that file are worth naming, because together they are a
production-path proof rather than a library test:

```text
DispatchesToCompressibleSimpleAndConverges        the coupled flag genuinely dispatches
MassFlowRateIsConservedAtEveryInternalCrossSection conservation, per cross-section
ExportsCoupledCompressibleFields                  asserts "coupled": true in metadata
RepeatedRunIsDeterministic                        bitwise
NonCoupledCaseStillUsesPostHocPathUnchanged       NEGATIVE CONTROL -- proves the other
                                                  mode is preserved
```

### MMS — system-level, and it documents its own limits

`CompressibleSimpleMMS.SystemConvergence`, 1 case, levels nx = 8/16/32.

```text
manufactured   p = cos(pi x) cos(pi y) + x y/2 (gauge)
               rho = (P_ref + p)/(R T0),  P_ref = R T0 = 20, rho in [0.95, 1.08]
               rho U = curl(psi) so div(rho U) = 0 EXACTLY while div U != 0
forcing        analytical, hand-derived, independently checked by
               ManufacturedFieldsTest.CompressibleForcingExact
order gate     1.0, band [0.55, 1.3], L2, on u and v; plus decrease gates on u, v, p
not covered    energy equation, mu/3 grad(div U) stress term, higher-order
               convection, high Mach          <- the test's own machine-readable field
```

`div(rho U) = 0` exactly while `div U != 0` is a genuinely discriminating design: it
exercises the variable-density mass flux without letting a constant-density
implementation pass.

### What no existing test covers

```text
total-energy conservation          no such equation exists
temperature recovery from energy   no such path exists
Riemann / HLLC flux correctness    no such flux exists
shock position / strength          no Sod, no normal shock, no shock tube
supersonic advection               no case
positivity under a discontinuity   nothing generates one
CPU/GPU compressible equivalence   no GPU compressible implementation
transient compressible continuity  the diagnostic is tested (6 cases) but is not in
                                   any production path, so those 6 cases verify a
                                   library function, not solver behaviour
3D compressible                    refused by requireTwoDimensional
```

## 5. Consequence for P17

The compressible **verification** suite is genuinely strong for the low-Mach, steady,
2D, isothermal regime it targets — 129 running tests, an MMS study with an honest
order gate, a determinism check and a non-vacuity guard.

It contains **no instrument at all** for the regime P17.5 through P17.9 are about.
Every gate for the new work therefore has to be built, not extended, and the existing
suite's role in P17 is as a **preservation** instrument rather than a starting point.
