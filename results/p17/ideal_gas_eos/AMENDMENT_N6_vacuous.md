# AMENDMENT — P17.2 control N6 was VACUOUS; replacement derived and frozen

Recorded 2026-09-26, immediately after the controls ran. The frozen gate
(`acceptance_gate.md`, sha256 `1a0aefe2…`) is **unchanged**.

**This is a genuine gate failure, not a pass.** `CLAUDE.md` §5.2 requires that every
negative control fail; a control that does not fail means the criterion it targets is not
demonstrably detecting. N6 did not fail, so criterion **T1 was unproven** until the
replacement below ran.

---

## 1. The failure, preserved verbatim

```text
=== N6 -- cv stored as a rounded independent value, not cp - R ===
  PLANTED DEFECT     yes (compiled)
  CRITERION          *** DID NOT FAIL -- the criterion is VACUOUS ***
  MUST STILL PASS    -> 100% tests passed, 0 tests failed out of 1
  RESTORE            VERIFIED by SHA-256 (both files)
```

Full log: `logs/07_controls.log`. Not deleted, not amended in place.

## 2. Diagnosis — the control was a NO-OP, not a weak perturbation

N6 replaced

```cpp
specificHeatVolume_ = specificHeatPressure_ - gasConstant;
```

with

```cpp
specificHeatVolume_ = std::round((specificHeatPressure_ - gasConstant) * 1.0e10) / 1.0e10;
```

Measured directly (`tools/n6_diagnosis.cpp`):

```text
cv = cp - R              = 717.95000000000005
cv * 1e10                = 7179500000000          <- ALREADY AN EXACT INTEGER
round(cv * 1e10)         = 7179500000000          <- round() is the IDENTITY here
/ 1e10                   = 717.95000000000005
identical to cv?           YES, BIT FOR BIT
```

For air, `cp - R = 717.95`, whose double representation multiplied by `1e10` lands exactly
on an integer. `std::round` therefore changes nothing and the round trip is the identity.
**No defect was planted at all**, so there was nothing for T1 to detect.

This is my error in designing the control: I chose a rounding grid (`1e-10`) that the
actual value sits precisely on. It is the same class of mistake as P17.1's N2/N5
coincidences — a control whose *arithmetic* happens to agree with the correct answer — but
worse, because here the perturbation was exactly zero rather than merely undetectable.

## 3. The replacement, derived from the bound

Not chosen, and not fitted to an observed result. T1 bounds `(cp - cv) - R` at
`4 * eps * R`. Perturbing `cv` by a relative amount `delta` shifts that residual by
`delta * cv`, so the minimum relative perturbation that must exceed the bound is:

```text
bound      = 4 * eps * R      = 2.5495161537492096e-13   (absolute)
delta_min  = bound / cv       = 3.5511054443195341e-16   = 1.60 eps
```

**N6b:** `cv *= (1 + 1e-12)`.

```text
delta chosen             1e-12
margin over delta_min    2816.0x
cv perturbed             717.95000000071809
resulting residual       -7.1810291046858765e-10
exceeds the bound by     2816.6x
```

`1e-12` is picked as a round figure three orders above `delta_min`, so the control cannot
fail to trip T1 for arithmetic reasons — which is precisely what N6 did fail to do.

### It must still demonstrate the T1/T2 asymmetry

That was N6's actual purpose: to show T1's tolerance is load-bearing while T2's equality
survives. Verified before running:

```text
gamma stored = cp / cv_perturbed   = 1.3998189288933698
gamma - cp/cv_perturbed            = 0        <- T2 STILL PASSES, exactly
gamma > 1                          = yes      <- T3/T4 still pass
```

So N6b perturbs `cv` enough to break `cp - cv = R` while leaving `gamma == cp/cv` exact,
because `gamma` is stored as the quotient *of the perturbed cv*. That is the asymmetry the
dry-run exposed and that T1 and T2 are gated differently for.

## 4. Two further findings from N2 and N3 — recorded, not amended

Both controls DETECTED, so neither is a gate failure. But each broke fewer tests than the
per-test mapping suggested, for reasons worth recording.

### N2 — `temperature()` returns a constant: broke 2 of 4

```text
BROKE      ThreeWayClosureHoldsAcrossGasesAndStateRanges
           TemperatureIsReachableThroughTheAbstraction   (evaluates at T = 450)
DID NOT    TemperatureMatchesPOverRhoR
BREAK      TemperatureToPressureRoundTripRecoversPressure
```

Both survivors evaluate at **T = 288.15**, and the constant planted was **288.15**. A
numeric coincidence — the same trap as P17.1's `InternalFaceUsesArithmeticMeanDensity`,
whose density pair had a mean equal to the reference density.

The frozen gate's wording was already correct here: it required N2 to break "C3, C6, C7
**at every non-default state**". The 9-state `ThreeWayClosure` test is what carries the
detection.

```text
CONSEQUENCE, recorded so it is not lost: TemperatureMatchesPOverRhoR and
TemperatureToPressureRoundTripRecoversPressure are SINGLE-STATE tests and cannot detect a
constant-return defect. ThreeWayClosureHoldsAcrossGasesAndStateRanges is the discriminator.
Do not delete it as redundant with the two simpler tests; it is strictly stronger.
```

### N3 — `temperature()` drops input validation: broke 1 of 3

```text
BROKE      TemperatureMessageNamesTheOffendingField
DID NOT    TemperatureRejectsNonPositivePressureOrDensity
BREAK      TemperatureRejectsNonFinitePressureOrDensity
```

Because removing the *input* validation still leaves the *result* check
`requirePositiveFinite(T, "temperature")`, and it catches every invalid case on its own:

```text
p = 0            -> T = 0        rejected by the result check
p < 0            -> T < 0        rejected
rho = 0          -> T = inf      rejected
rho < 0          -> T < 0        rejected
NaN in either    -> T = NaN      rejected
```

So for `temperature()` the defence is **fully layered**: the input validation's unique
contribution is not rejection but **diagnostic quality** — which field is named. That is
exactly why `TemperatureMessageNamesTheOffendingField` was written, and it is the only
test that distinguishes the two layers.

The earlier P17.2 found a weaker form of this for `pressure()` ("N3 broke one test, not
two… the non-finite path is defended twice"). Here the result check defends *every* path,
which is a stronger statement and is recorded as such.

## 5. What was NOT done

```text
NOT DONE   editing acceptance_gate.md to match the observed outcome
NOT DONE   weakening T1's 4 eps bound, or any other criterion
NOT DONE   deleting or rewriting the vacuous N6 result
NOT DONE   declaring T1 proven on the strength of N6
NOT DONE   modifying the two single-state tests to make N2 break
DONE       preserved the failure, diagnosed it to the exact arithmetic, derived the
           replacement from the bound with a stated 2816x margin, verified in advance
           that it preserves the T1/T2 asymmetry, and re-ran the control unchanged in
           every other respect
```

## 6. Outcome

Recorded in `logs/09_control_N6b.log`. T1 is proven only if N6b fails; if it does not,
P17.2 is **BLOCKED** and T1 is reported as `NOT VERIFIED`.
