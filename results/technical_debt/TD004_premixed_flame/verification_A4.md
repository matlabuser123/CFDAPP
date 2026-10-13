# TD-004 A4 — C5 PASSES on the frozen amendment. The gate advances to C6. TD-004 does NOT close.

```text
STATUS:      C5 SATISFIED under GATE_A3. C5a, C5b, C5c, C5d all PASS on a fresh post-freeze run.
             TD-004 remains OPEN / STOPPED -- see §4.
SCOPE:       C5 only. No production change was made for this amendment; the locator fix verified
             in A2 is unchanged and remains the only production change in TD-004.
GATE:        GATE_A3.md  8a11ad39a147830ce5e2d1e91310c85212a881222ba143bde177a2771e6fb113
             frozen in logs/A3_00_freeze.log, RECOMPUTED at the start of this run and matched.
             It amends C5 of GATE.md 12ccc5dd…, which is otherwise unchanged.
RESULT:      C5a PASS · C5b PASS · C5c PASS · C5d PASS
EVIDENCE:    logs/A3_04_verification.log  (fresh; reuses no earlier result, per §5.3 step 6)
REGRESSION:  NOT RUN. No production code changed, and TD-035 blocks the §8 ladder regardless.
COMMIT:      none — not authorized.
```

---

## 1. The three gates that ran before any measurement

```text
GATE 1  GATE_A3.md hashes to 8a11ad39…, matching logs/A3_00_freeze.log        OK
GATE 2  PremixedFlame.cpp is fa815c97…, and :662 reads 1.0e-9                 OK
GATE 3  libcfdcore.a is 852555cf…, the uncorrupted post-fix archive           OK
```

GATE 1 exists because **this repo's frozen-hash check runs only at phase end**, so an edit made
after freezing would hide for the whole phase. It is recomputed at the start of the step that
depends on it.

GATE 2's `:662` check is not decoration: the M5 control raised `residualAcceptanceFraction` to
`1.0e-6`, and a control patch surviving into the verification run is exactly the contamination that
would make a PASS meaningless. It reads `1.0e-9`.

## 2. The result

```text
                              measured                  threshold            verdict
C5a  |T_ad - T_peak|          1.025553e-03 K            1.800000e-03 K       PASS
C5b  |min Y_i|                1.889798e-11              5.000000e-11         PASS
C5c  |x_front - x_anchor|     7.807782e-07 m            8.000000e-05 m       PASS
C5d  |sum Y - 1|              8.822866e-07              1.200000e-06         PASS

finalMarchSteps 15283 · S_L 4.955332027 cm/s · front 3.499219221840 mm (-0.039 cells)
fuelBalanceResidual 6.302972231e-03 · recentrings 223 (final march 3) · wall 693 s
```

**Every value is identical to the dry-run's REF leg to the last digit**, on a separately built
binary. The solve is deterministic at this fixture, which is reported rather than assumed.

## 3. What this is worth, stated carefully

The amendment replaced three clauses that **failed on the baseline** — `verification_A3.md` measured
all three on the pre-fix library — with three derived from first principles:

```text
C5a  (T_ad - T_u) * 1.2e-06     the energy closure's gain on C5's ALREADY-FROZEN sum-Y band
C5b  1e-09 * Y_F,u              the linear acceptance fraction at :662 times the fuel scale
C5c  (deadBand + 1) * dx        the recentring controller's designed error band at :928
```

None was taken from an observed value, and **C5b's derivation was independently corroborated**: M5
varied `residualAcceptanceFraction` by 1000x and `|min Y_i|` responded by 651x, with the
measured/bound ratio stable at 0.38 and 0.25 across three orders of magnitude.

So C5 is no longer a set of transcribed print-precision constants. It is four bounds, each tied to a
named quantity in the code or in the frozen gate, each shown able to fail.

## 4. WHY TD-004 STILL DOES NOT CLOSE

```text
C6, C6a, C7   NEVER RUN. The original verification stopped at C2, and A2_06 stopped at C5. The
              gate now stands at C6 -- eigenvalue properties, independence of the initial guess,
              plateau in anchor and domain, convergence under refinement.
TD-040        carries TD-004's registered stop INDEPENDENTLY of C5: S_L moved +6.07 % when the
              locator was fixed, and which eigenvalue is correct (4.955332 vs 4.671937) is
              unverified. A passing C5 does not touch that.
```

**C5 passing advances the gate by one criterion. It is not a closure and must not be reported as one.**

## 5. What is NOT established

```text
NOT established  why min Y_i is 2.24x larger post-fix (1.8898e-11 from 8.4530e-12). The fill-count
                 explanation verification_A3.md offered is REFUTED by the dry-run's M2 leg, and
                 nothing replaces it. Both values sit inside C5b, so it does not block the gate --
                 it is an open question carried forward, not a closed one.
NOT established  that C5d's 1.2e-06 band is correctly derived. It is carried over UNCHANGED and is
                 GATE.md §3.2's measured baseline band, attributed to TD-003. M2 fails it by only
                 1.03x -- a thin margin for a clause that had never been dry-run before this
                 amendment. Recorded; amending it was not authorized and was not done.
NOT established  that M5 is a DISCRIMINATING control for C5b. It fails C5a, C5b and C5d together.
                 It proves C5b can fail, which is what §5.2 requires, but it does not isolate it.
NOT measured     C6, C6a, C7.
NOT run          any regression (§8), blocked by TD-035.
NOT changed      any production code for this amendment.
```

## 6. The campaign lesson from this amendment

`GATE.md` §1 shows C5 was never dry-run: the record says the dry-run covered *"C1 and its controls
N2/N3"*. Three of its four clauses were therefore transcribed from a **printed table** — `T_peak` at
two decimals, `x_front` at five, where `x_front` was additionally the anchor compared against itself
through a reporter TD-005 later proved never measured anything.

**A clause that was never executed is not passing, and a threshold read off a print is not derived.**
Both failures were invisible for as long as the gate stopped before reaching C5 — which it did,
twice.
