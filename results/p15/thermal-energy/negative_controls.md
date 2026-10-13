# P15 — non-vacuity controls

Written for: a reviewer deciding whether the P15 gates actually detect anything.

A gate that passes under its own negative control proves nothing. This project
has hit that trap repeatedly — P13-002 M2, P13-003 E8, P13-005 N1, and three of
P14's first control round — so every P15 gate is paired with a deliberate break
and required to **fail**.

**Result: 8 controls, 8 DETECTED, 0 vacuous.**

Full log: `logs/final/negative_controls.log`.

---

## The controls

| # | What is broken | Gate that must fail | Result |
|---|---|---|---|
| NC1 | Robin conductance sign reversed | `ThermalValidation.CaseERobin*` | **DETECTED** |
| NC2 | Volumetric heat source zeroed | `ThermalValidation.CaseB*` | **DETECTED** |
| NC3 | k(T) collapsed to a constant | variable-conductivity case | **DETECTED** |
| NC4 | A cp(T) coefficient corrupted | enthalpy tests | **DETECTED** |
| NC5 | Enthalpy integral replaced by `cp·T` | naive-form guard | **DETECTED** |
| NC6 | Enthalpy inversion broken | round-trip | **DETECTED** |
| NC7 | Transient uses the **new** density on the RHS | conservation trap | **DETECTED** |
| NC8 | Conservation accounting perturbed by 1e-6 relative | `ThermalConservation.*` | **DETECTED** |

NC5 and NC7 are the two worth singling out. NC5 breaks the thing that
distinguishes a real enthalpy from the `cp·T` shortcut — an implementation using
the shortcut would pass every test that only checks self-consistency. NC7
breaks discrete conservation in a way that still converges to something
plausible and is invisible at constant density.

---

## How the harness avoids fooling itself

Three properties, each of which exists because of a specific past failure.

### Restores are verified by sha256, not assumed

Hashes are recorded at run time, before any mutation, and `sha256sum -c` is
checked after every restore. A failed restore **stops the run** rather than
continuing on a mutated tree. P13-002 left production source modified by a
control; this is the fix for that class.

### Each control asserts the mechanism, not merely a nonzero exit

A test that fails for an unrelated reason is a false DETECTED. P14's first
control round produced three of them: the baseline predated a field the
environment check required, so the comparison refused every pair and every
control "detected" nothing real. Each control here names the specific filtered
gate its break should affect.

### A control that does not build is inconclusive, not detected

`rebuild()` deletes the target binaries first and verifies they came back. Without
that, a mutation that does not compile leaves the previous binary in place, the
gate runs it, it passes, and the control is recorded UNDETECTED — a gate
reported vacuous when the control never ran. The `RC=$?`-after-a-pipeline family
of errors is the same class, hit three times in P13.

A build failure routes to an explicit **inconclusive** verdict, which counts as
a failure. It is never silently read as a detection.

---

## What the run's own self-check found

After the eighth restore the harness rebuilds the restored tree and re-runs the
full thermal gates, to confirm the controls left nothing behind. It reported:

```
*** restored tree does NOT pass -- investigate before trusting any control above ***
src/thermal/EnergyEquation.cpp: OK
src/thermal/Enthalpy.cpp: OK
src/thermal/TemperatureDependentProperty.cpp: OK
src/boundary/ConvectiveHeatTransfer.cpp: OK
tests/integration/thermal/test_thermal_validation.cpp: OK
```

A contradiction worth resolving rather than waving away: sha256 says every file
was restored byte-exactly, yet a gate fails.

**Cause:** a pre-existing failure with nothing to do with the controls. The unit
test `ConvectiveBoundary.BoundaryValueThrowsRatherThanGuessing` still pinned the
`boundaryValue` contract that was deliberately reversed earlier in P15 (see
`implementation.md` §6). The header and implementation were updated at the time;
the test was not, and it had been failing since.

**Does this invalidate the controls?** No. Every control targets a specific
filtered gate — `ThermalValidation.CaseE*`, `CaseB*`, the enthalpy tests,
`ThermalConservation.*`. None of those filters includes `ConvectiveBoundary.*`.
The eight detections stand on their own filters.

**Resolution.** The stale test was not deleted — deleting it would have quietly
removed the protection it existed to provide. It was re-pointed at where that
guarantee moved to (`cfd::transport` refusing a Robin wall, with the message
required to name the runtime-diffusivity cause), and a second test now pins the
new `boundaryValue` contract. Thermal unit: **126/126**. Thermal validation:
**16/16**.

This is what that self-check is for, and it earned its place.
