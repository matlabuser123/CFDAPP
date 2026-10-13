# P18.7 — the zero limits (sections 14, 15, 34, 35, 36)

---

## 1. Zero reaction, same geometry (sections 14, 34)

```text
The canonical reacting-flow geometry is run with chemistry OFF. With no reaction the inlet
composition simply advects and the inlet temperature is carried through:

    Y_A = 1.0 everywhere        EXACTLY
    T   = T_0 = 1000 K          to 1e-9
    integrated source           EXACTLY 0

This is the geometry-matched zero-reaction limit section 34 asks for: not a different case with
chemistry removed, but THE SAME case.
```

## 2. Reaction-off versus `A = 0` (section 36)

```text
TWO INDEPENDENT ROUTES TO ZERO CHEMISTRY must agree:

    reactions disabled   -- the driver never calls the kinetics
    A = 0                -- P18.5's exact branch, k = 0 for every valid T

MEASURED: BITWISE EQUAL, every cell, for both Y_A and T.

That is a cross-layer check between P18.5's rate evaluation and P18.6's coupling: if A = 0 took a
different path than "no reaction at all", the two would differ in the last bits.
```

## 3. N14 — the non-vacuity control

```text
A zero-reaction limit that matches is worthless if the REACTING case also does nothing. So the
same geometry is run with chemistry ON and asserted to DIFFER:

    outlet Y_A differs
    outlet T differs by MORE THAN 100 K

Without this, "chemistry disabled reproduces the non-reacting result" would pass for a benchmark
that never reacted at all -- which is exactly what control N14 names.
```

## 4. Zero heat release (sections 15, 35)

```text
THE EXPECTED BEHAVIOUR IS DERIVED BEFORE MEASUREMENT, as section 15 requires.

A thermoneutral reaction is built by giving both species ONE SHARED NON-ZERO formation enthalpy
(-750000 J/kg), so Delta H_r^0 = 0 by CANCELLATION rather than by every term being zero -- a
thermoneutral test with h_f = 0 everywhere would pass for an implementation that ignores h_f.

    Delta H_r^0 = 0      EXACTLY (asserted)
    S_E         = 0      EXACTLY

THE TEMPERATURE PREDICTION, derived:
    the species in this case share one CONSTANT cp, so cp_mix is composition-independent. The
    convected quantity rho cp T therefore cannot change when S_E = 0, so T stays at the inlet
    value. REQUIRING CONSTANT T IS CORRECT HERE -- but only because cp is composition-independent.

    With UNEQUAL cp_i it would NOT be: cp_mix would change with composition and T would move. P18.6
    measured exactly that (T_1 = T_0 cp_0/cp_1) and section 15 warns against "incorrectly requiring
    constant temperature". The distinction is why the derivation precedes the measurement.

MEASURED: species change (Y_A -> below 0.95 at the outlet), T unchanged to 1e-8, and conservation
still holds.
```
