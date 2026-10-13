# AMENDMENT A1 — the stationary-contact width criterion

**Classification: GATE-STRENGTHENING.** It ADDS a criterion. Nothing is relaxed, no existing bound
is touched, and the amended gate rejects strictly more implementations than the frozen one.

```text
authorised by the user, 2026-09-28, as a documented correction to a demonstrated vacuity
acceptance_gate.md is UNCHANGED -- sha256 b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40
```

---

## 1. The original frozen gate, preserved verbatim

`acceptance_gate.md` §9, Group V:

```text
| id | planted control | must break | must still pass |
| V1 | Rusanov / local Lax-Friedrichs -- the wave structure replaced by a single maximum-speed
       dissipation, i.e. NO Riemann solver                     | X2, S3, W3 | C1 |
| V2 | HLL -- hllFluxForControlOnly, HLLC with the contact wave deleted | W3, X2, S3 | X1 |
```

and the frozen `P17_GATES.md` G7, whose words define what a failure here means:

> *"a first-order upwind scheme WITHOUT the Riemann flux must FAIL this gate. If it passes, the gate
> is not measuring shock capturing. A second, discriminating case is required beyond Sod: Sod alone
> can be passed by an over-diffusive scheme that smears everything to roughly the right place."*

## 2. The original result, preserved

`logs/07_controls.log`, run before any amendment:

```text
--- V1: Rusanov / local Lax-Friedrichs -- NO Riemann solver
    MUST BREAK  ContactDiscontinuityTest   PASS   *** NOT DETECTED ***
    MUST BREAK  SodShockTubeTest.S         FAIL   detected
    must pass   ShockConservationTest      PASS
--- V2: HLL -- HLLC with the contact wave DELETED
    MUST BREAK  ContactDiscontinuityTest   PASS   *** NOT DETECTED ***
    MUST BREAK  SodShockTubeTest.S         PASS   *** NOT DETECTED ***
    must pass   NormalShockTest            PASS
CONTROLS WITH AN UNEXPECTED RESULT: 2
```

**V2 passed the entire shock-validation gate.** By G7's own standard that means the gate was not
measuring the wave HLLC exists to restore.

## 3. Why the original criteria are defective — the mathematics

A contact discontinuity is **linearly degenerate**: across it `p` and `u` are continuous and only
`rho` jumps. It is therefore invisible to any criterion written on pressure or velocity, and its
*position* is determined by `u*`, which both HLLC and HLL get right because both satisfy the same
Rankine–Hugoniot conditions for the outer waves.

What HLLC alone provides is the **contact wave itself**, which makes a stationary contact an exact
fixed point of the scheme. Deleting it leaves the intermediate state a single averaged region, so
the contact is transported only by numerical diffusion.

On the **Sod** problem this difference is invisible, because there the contact is already smeared by
both schemes:

```text
Sod contact smeared width, nx = 200, t = 0.2, 677 steps   (logs/08_contact_width.log)
    HLLC     16 cells
    HLL      18 cells        12 % apart
    Rusanov  26 cells
```

and the star states are fixed by the shock and the fan, not by the contact's sharpness:

```text
V2 against every criterion the frozen row named   (logs/12_v2_detail.log)
    X2 Sod contact position   HLL 1.28 dx        limit 2 dx        passes
    W3 |dp|/p* at the contact HLL 1.75e-03       limit 5.00e-02    passes
    S3 rho*_L error           HLL 9.03e-03       limit 1.61e-02    passes
```

So the frozen V2 row named three criteria that **cannot** detect a deleted contact wave. That is a
defect in the row, not in the plant.

### 3.1 What P17.7 already had, and why it did not cover this

P17.7 DID test contact-wave deletion, and the test still passes:

```text
tests/unit/compressible/test_hllc_flux.cpp:621
  HllcContactTest.T4_NonVacuity_DeletingTheContactWaveBreaksTheContact
```

But it is a **flux-layer** test: it calls `hllFluxForControlOnly` directly, as a control, and
compares its output to `hllcFlux`. It therefore says nothing about what the SOLVER uses. Planting
HLL inside `DensityBasedSolver::assembleResidual` leaves T4 passing untouched, because T4 never
consults the solver.

```text
So the position before A1 was:
  flux layer    deleting the contact wave IS proven to break something   (P17.7 T4)
  solver layer  deleting the contact wave broke NOTHING                  (P17.9 V2, logs/07)
A1 closes the second gap. It does not duplicate T4; it tests a different layer.
```

## 4. The replacement criterion, and its derivation

A **stationary** contact (`u_L = u_R = 0`, `p_L = p_R`, `rho_L ≠ rho_R`) is an exact steady solution
of the Euler equations: every flux component is identically zero except the pressure term, which is
equal on both sides. HLLC reproduces this exactly — P17.7 measured mass and energy flux across a
stationary contact as **exactly zero, bitwise**, and the contact speed `S* = 0` exactly.

Therefore the correct criterion is not a tolerance but an **exact statement**:

```text
NEW CRITERION A1:
    a STATIONARY contact must be preserved with ZERO smeared cells.
    measured by transitionWidthCells(rho, rho_L, rho_R), the count of cells lying strictly
    between 10 % and 90 % of the jump -- a quantity the test ALREADY computed and printed.
```

There is no constant to choose. Zero is the exact answer, and the measurement discriminates by an
unbounded factor rather than by a percentage:

```text
STATIONARY CONTACT smeared width, nx = 200, t = 0.2, 1339 steps
    HLLC      0 cells      <- exact preservation
    HLL      21 cells
    Rusanov  21 cells
```

The **moving** contact, by contrast, does NOT discriminate (22 cells for both HLLC and HLL), which
is why the criterion is placed on the stationary case specifically.

## 5. Proof that no valid threshold was relaxed

```text
criteria removed or loosened by A1 : NONE
criteria added by A1               : 1  (EXPECT_EQ(width, 0) on the stationary contact)
existing assertions in that test   : all four retained unchanged --
      position error <= 2 dx, pressure variation <= 0.05, velocity variation <= 0.05,
      mass error <= 8 eps N
```

The amended gate accepts a strict subset of what the frozen gate accepted. A1 cannot turn a FAIL
into a PASS for any implementation.

## 6. Re-run results

### Positive control — HLLC, the shipped implementation

```text
STATIONARY CONTACT, 2D mesh 200x1, t = 0.20, 1339 steps      (logs/10_amended.log)
    position 0.5000000000 vs 0.5 -> err 0.0000e+00 = 0.0000 dx
    pressure variation 0.0000e+00, velocity variation 0.0000e+00, width 0 cells
    mass 2.812500000000e-03 -> 2.812500000000e-03   relative 0.0000e+00
    A1 width == 0   PASS
```

### Negative control — the contact wave removed

```text
logs/11_controls_amended.log and logs/12_v2_detail.log
    V1 Rusanov  ContactDiscontinuityTest  FAIL  "A1: a stationary contact must be preserved
                                                 EXACTLY; 21 smeared cells means the
                                                 contact-restoring wave is absent or broken"
    V2 HLL      ContactDiscontinuityTest  FAIL  same assertion, same 21 cells
```

**The negative control fails for the intended reason** — the assertion text names the contact wave,
and the failing quantity is the contact width, not a position or a conservation error.

### The full case set, re-run unchanged

```text
logs/10_amended.log:  13 of 13 tests PASS
    Sod contact case          W3 |dp|/p* 1.64e-03, |du|/u* 3.63e-03, drho 0.149362 vs 0.160674
    stationary contact        position 0, p var 0, u var 0, width 0
    moving contact Ma 3.4     position 0.141 dx, p var 4.09e-14, u var 3.00e-15, width 22
```

## 7. Status

```text
A1: IMPLEMENTED AND VERIFIED.
    gate strengthened; Group V is no longer vacuous; both negative controls now fail on it.
```
