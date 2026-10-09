# P17.5 amendment — two frozen thresholds were mis-derived (T7, T8)

Raised 2026-09-27 by gate attempt 1. Both failures were **my thresholds being wrong, not the code**.
Each is corrected with a derivation, and each correction is shown to still discriminate.

The failures are preserved in `logs/PRESERVED_gate_attempt1/`.

---

## 1. What went wrong, in one sentence

I derived both thresholds from the dry-run's measurements at a handful of states, where the rounding
happened to vanish, instead of from **the dimensional floor of the operation being performed**. That
is the exact error my own recorded lesson names, and I made it anyway.

## 2. T7 — `rho hTotal = rhoE + p`

```text
FROZEN:   <= 4 eps * |rho hTotal|
MEASURED: difference 5.9117155615240335e-12
          bound      2.0482282536704587e-12      FAILED by 2.9x
          lhs        2306.0999999999999
          rhoE + p   2306.1000000000058
```

### Why the frozen bound was wrong

Under the shifted reference (`T_ref = 298.15`) the two operands are large and of opposite sign while
their sum is small:

```text
rhoE = -101031.9      p = 103338      rhoE + p = 2306.1
```

This is a **cancellation**. The absolute error floor of a subtraction is `eps` times the *larger
operand*, not `eps` times the (much smaller) result. Bounding by `|rho hTotal| = 2306` asked for an
accuracy the arithmetic cannot deliver — about 45x tighter than the floor.

The dry-run measured exactly 0 for this identity, which is what misled me: it used the **zero**
reference, where `e` and `h` are both positive and no cancellation occurs. One measurement, one
regime, over-generalised.

### The corrected bound, derived

```text
operand = max(|rhoE|, |p|)
bound   = 4 eps * operand = 4 * 2.22e-16 * 1.03e5 = 9.2e-11
measured 5.91e-12  ->  15x inside
```

### It still discriminates

The relevant defect is control **N1**, `e = h + R T` (sign flip on the defining relation). That makes
the identity fail by `2 rho R T ~ 2 * 1.2 * 287 * 300 ~ 2.1e5` — **sixteen orders of magnitude**
above the corrected bound. The bound is loose against round-off and still tight against any real
error.

## 3. T8 — `e = cv T` under the zero reference

```text
FROZEN:   EXACTLY 0
MEASURED at T = 700:   e  = 502565
                       cv*T = 502565.00000000006      FAILED, 1 ulp apart
```

### Why the frozen bound was wrong

`e` is computed as `cp*T - R*T` (the defining relation `h - R T`), while `cv` is the **rounded
difference** `cp - R = 717.95000000000005`. In floating point

```text
(cp - R) * T   is not   cp*T - R*T
```

because `cp - R` is rounded once before multiplying, and `R*T` is rounded before subtracting. The
discrepancy is about one ulp — measured 5.8e-11 absolute, 1.16e-16 relative, **0.52 eps**.

Again the dry-run misled me: it measured `|e - cv T| = 0` at `T = {300, 500, 1000, 2000}`, where the
roundings happened to coincide. `T = 700` is where they do not. **Exactness held at four sampled
points and I wrote it into the gate as a property.**

### The corrected bound, derived

```text
h = cp T      ONE product, no rounded intermediate  ->  stays EXACTLY 0, and still passes
e = cv T      cv is a rounded difference            ->  4 eps * |e|
measured 0.52 eps  ->  7.7x inside
```

Note the two halves of T8 are now gated differently, because they are different operations. Lumping
them under one "exactly 0" was the mistake.

### It still discriminates

If `e` used `cp` instead of `cv` — the single likeliest implementation error here — it would be wrong
by `R*T = 287.05 * 700 = 200935`, which is **fifteen orders** above the corrected bound.

## 4. What was NOT done

```text
NOT DONE   editing acceptance_gate.md so the original thresholds appear to have passed
NOT DONE   relaxing the bound until the test went green, then declaring it derived. Each new bound
           is the floor of a named operation, and the margin (15x, 7.7x) is stated so a reader can
           see it is not fitted to the measurement
NOT DONE   deleting or rewriting the failing runs
DONE       preserved both failures, named the operation whose floor each bound now comes from, and
           checked that the replacement still catches the defect the criterion exists to catch
```

## 5. The lesson, which is a repeat

My `frozen-gate-derivation-errors` note already says: *"name the exact quantity each threshold bounds
and derive its floor dimensionally."* Both of these thresholds were instead read off a dry-run table.
The specific trap worth adding: **a dry-run that samples one regime (here, the zero reference) can
make a threshold look exact when it is not.** For an identity that can be evaluated in more than one
thermodynamic convention, the dry-run has to sample every convention the code will accept — and this
dry-run did do that for §C, which is why T5 was correctly gated at exactly 0, but not for §H, which
is where T7 came from.
