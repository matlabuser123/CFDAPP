# AMENDMENT — P17.1 Group N: two "must break" lists were over-broad

Recorded 2026-09-26, after running the controls. **This amends a prediction, not a
threshold.** The frozen gate (`acceptance_gate.md`, sha256 `876466e2…`) is unchanged and
its actual requirement — *every N control must fail* — was met by all five.

---

## What the gate predicted, and what happened

```text
control  gate said "must break"                          actually broke
-------------------------------------------------------------------------------------
N1       D1, D2, D3, D6                                  4 tests -- ALL of them   OK
N2       D1, D6 while D2 still passes                    3 tests + D2 passed      OK
N3       D6 only                                         1 test  -- exactly D6    OK
N4       S1, S2, S3, S4, M3                              2 tests                  OVER-BROAD
N5       S1, M3, M4 while C1 still passes                1 test + C1 passed       OVER-BROAD
```

All five **DETECTED**. All five restores **VERIFIED by SHA-256**, 0 markers left, 20/20
at baseline afterwards. Non-vacuity is established. What was imprecise is my forecast of
*which* named tests each control would trip.

## Why N4 broke fewer tests than predicted

N4 replaced the internal-face interpolation with `density[0]` — a global constant taken
from cell 0.

```text
InternalFaceUsesArithmeticMeanDensity   BROKE, as predicted.
                                        density[0] = 1.0 but the correct mean is 2.0,
                                        so the flux became 2.0 instead of 4.0.
RowVaryingDensityConservesMassAnd-      BROKE, as predicted.
  ScalesFluxPerRow
FaceDensityIsRecomputedPerCallNotCached DID NOT BREAK -- and it was wrong of me to
                                        predict it would.
                                        That test mutates density[0] and asserts the
                                        next call's flux changes. N4 still READS
                                        density[0] live, so the flux does still change.
                                        N4 collapses the SPATIAL dependence, not the
                                        FRESHNESS. They are different properties and the
                                        test is correctly insensitive to N4.
```

The prediction conflated "collapsed to a constant" with "stale". A staleness control
would have to cache the face field across calls, which is a different defect.

## Why N5 broke only one test — and the finding underneath it

N5 collapsed **every** face density to `thermodynamics.density(referencePressure,
temperature[0])`, the faithful analogue of using one material density everywhere.

```text
RowVaryingDensityConservesMassAnd-      BROKE. The two rows have densities 2.0 and 1.0,
  ScalesFluxPerRow                      so one constant cannot give both rows their own
                                        flux magnitude.
UniformDensityMatchesIncompressible-    Still passed, as predicted. At a uniform state
  MassFluxExactly                       the constant IS the right answer.
InternalFaceUsesArithmeticMeanDensity   DID NOT BREAK.
OutletBoundaryUsesEosEvaluated-         DID NOT BREAK.
  PressureNotOwnerDensity
```

Both survivals are **numeric coincidences**, verified by reading the tests rather than
guessed:

```text
InternalFaceUsesArithmeticMeanDensity
  uses density = {1.0, 3.0}. Its arithmetic mean is exactly 2.0.
  kReferenceDensity is 2.0, and kReferencePressure is defined as
  kReferenceDensity * R * kReferenceTemperature, so
  EOS(kReferencePressure, kReferenceTemperature) = 2.0 as well.
  N5's constant therefore EQUALS the correct mean, and the flux is unchanged at 4.0.

OutletBoundaryUsesEosEvaluatedPressureNotOwnerDensity
  pins the outlet with FixedValue(0.0) GAUGE pressure, so that face's absolute pressure
  is exactly referencePressure by construction, and the temperature field is uniform.
  The correct boundary density there IS EOS(referencePressure, kReferenceTemperature) --
  precisely N5's constant. The test is doing its real job (proving the owner cell's
  deliberately wrong density of 999.0 is ignored) and is simply blind to this particular
  collapse.
```

## The finding that matters

```text
InternalFaceUsesArithmeticMeanDensity cannot distinguish "the arithmetic mean of the two
owning cells" from "the reference constant", because its chosen density pair has a mean
equal to the reference density. Its name promises discrimination it does not deliver.
```

Classified per `CLAUDE.md` §5: a **contaminated instrument**, not an obsolete constant.
The property it checks still holds and its assertion is still correct — it just cannot
fail for the reason its name implies. It is **not** modified here: touching a passing
P12-COMP-001 test is outside P17.1 scope, and the fix is a one-line change of the density
pair that a future item can make deliberately. Recorded as technical debt.

### Consequence, which is the point of running controls at all

```text
RowVaryingDensityConservesMassAndScalesFluxPerRow is the ONLY test in the suite that
detects a collapse of the face density to a single constant. It caught BOTH N4 and N5.
```

Its two-row construction — densities in an exact 2:1 ratio, with each row's own flux
magnitude asserted — is what gives it that power, and the per-row clause is load-bearing
rather than decorative. **Do not delete it as redundant with
`InternalFaceUsesArithmeticMeanDensity`; it is strictly stronger, and that test is the
weaker one.**

## What was NOT done

```text
NOT DONE   editing acceptance_gate.md to match the observed outcome
NOT DONE   weakening any criterion
NOT DONE   modifying InternalFaceUsesArithmeticMeanDensity to make N5 break
NOT DONE   claiming the predictions were right
DONE       recording the over-broad forecast, the exact numeric reason for each
           survival, and the debt item it exposed
```

Had any control failed to break **anything**, that would have been a vacuous criterion
and a genuine gate failure. None did.
