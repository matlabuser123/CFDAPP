# TD-002 — REPRODUCER

```text
gate      GATE.md sha256 287c7027bc7db99f0d590eaad1b35388803ffec9dfdd182a2af584bf00e581e5
library   build/release/src/libcfdcore.a sha256 0a29fd9821bc84c0b107e52bf4c50c9ee9947a7046c6efbd529d8fc6c443a7ce
probes    tools/drift_audit.cpp, tools/dryrun.cpp
logs      logs/01_drift_audit.log, logs/02_dryrun_PRE_FREEZE.log
```

Three failure modes, all on the unchanged library, all reached through real entry points.

---

## V1 — tightening the accuracy control makes the integrator THROW

The shortest statement of the defect. One settings value changed, nothing else.

```cpp
ChemistryIntegratorSettings s;
s.maximumFractionalChange = 0.0125;   // the accuracy knob, tightened
s.maximumSubSteps = 2000000;          // not the limit -- the budget is never reached
integrateChemistry(mechanism, mixture, formation, kinetics, start, /*dt=*/20.0, s);
```

```text
cfd::InvalidArgumentError
  mass fractions at cell 0 sum to 1.000000000000002; must be within
  1.7763568394002505e-15 of 1 (deviation 1.9984014443252818e-15)
```

The whole ladder, same `dt`, same problem, only the control varying:

```text
   control   sub-steps   deviation       in eps
  0.400000          64   0.000000e+00      0.00
  0.200000         136   4.440892e-16      2.00
  0.100000         284   4.440892e-16      2.00
  0.050000         584   1.110223e-15      5.00
  0.012500   THREW
  0.003125   THREW
  0.000781   THREW
```

**An accuracy control that cannot be tightened is not an accuracy control.** The drift rises with the
sub-step count, crosses `4*eps*2 = 1.7764e-15`, and `MassFractionState` then refuses the state.

## V2 — a long march is impossible, and the failure is a refusal rather than a wrong number

On a drift-exercising problem (reversible `A <-> B`, both species `O(1)` indefinitely):

```text
  variant        steps    worst deviation    in eps   min Y
  plain           1000    1.332268e-15        6.00    +2.710e-04
  plain          30000    THREW -- sum to 0.99999999999999811, outside 4*eps*2
```

`MassFractionState` validates every sub-step, so the march does not produce a drifted answer — it
**stops**. There is no "slightly wrong" result to inspect, which is why the defect has survived: a
suite that only checks returned values never sees it.

Measured plain drift rate: **0.006 eps/step**, crossing the bound at **~1 333 steps**.

## V3 — the production path cannot start a march from a transported composition

TD-001 measured the transport half's closure at `5.718198e-08`. Handing that to the state class:

```text
cfd::InvalidArgumentError
  mass fractions at cell 0 sum to 1.0000000571819769; must be within
  1.7763568394002505e-15 of 1 (deviation 5.7181976931275358e-08)
```

So there is **no baseline path at all** by which a second chemistry step could consume the first
step's transported composition. This is the limitation TD-001 recorded in `ProjectRunner.hpp` and
handed to TD-002, and it is independent of V1/V2: fixing the drift does not fix this, and fixing this
does not fix the drift.

---

## Why the existing suite is green while all three are true

| | why it misses the defect |
| --- | --- |
| `test_chemistry_integrator.cpp:203` | asserts `EXPECT_NEAR(a + b, 1.0, 1.0e-14)` — **5.6x looser** than the `4*eps*2 = 1.7764e-15` the state class enforces. Loosening the `sum Y` tolerance is on TD-002's own forbidden list; here it is in the test rather than the code. |
| `TighteningTheStepControlReducesTheErrorAgainstTheReference` | uses controls `0.2 / 0.05 / 0.0125` — and `0.0125` is exactly the rung that throws in V1. It passes only because it marches `dt = 1.0` instead of `20.0`. |
| every other chemistry test | the reference problem is **degenerate for round-off**: `Y_A` decays to `2.47e-323` and `Y_B` rises to ~1, so one addition is `tiny ± tiny` and the other `1 ± tiny`, both exact. Measured: **7 521 sub-steps at 0.00 eps**, and **8 797** with an inert carrier. |

The suite is not wrong about what it asserts; it asserts things the defect does not touch.
