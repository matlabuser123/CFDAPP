# TD-040 — the premise is REFUTED and no production fix is warranted. The operator is correct.

```text
STATUS:      §2's central claim is REFUTED by this item's own evidence, which was never written
             back into the audit. All three candidate fixes in §4 are now wrong or unnecessary.
             PROPOSED DISPOSITION: N/A AS A DEFECT -- a correctly-implemented frame change with a
             correctly-reporting ledger. Reclassification is a user decision and is NOT applied.
CONSEQUENCE: TD-004's blocker 1 ("TD-040 carries the S_L stop") is FALSE and the stop is
             dischargeable. TD-004 still has blocker 2 (C6a-ord UNRESOLVED), so it does not close.
SCOPE:       TD-040's premise and its fix options. NO production change was made or is proposed.
EVIDENCE:    logs/01_deadband_ladder.log · logs/03_dt_ladder_fixed_tree.log
             src/species/PremixedFlame.cpp:1715-1741 (read, not modified)
COMMIT:      none — not authorized.
```

---

## 1. §2's CENTRAL CLAIM IS REFUTED, by evidence in this item's own logs

`audit.md` §2 states:

> *"`S_L` **is** the mass flux the bisection converges on, so an uncounted flux biases it directly.
> That is why repairing TD-004's locator — which only made the operator fire as often as it was
> always supposed to — moved the eigenvalue."*

`logs/01_deadband_ladder.log`, run to test exactly that:

```text
deadBand   S_L cm/s   recentrings  final   ledgerRate      as % fuelT   front off   balance
       1   4.955332          666     10   -8.183467e-05      -2.8037      -0.961   6.206e-03
       3   4.955332          223      3   -7.364218e-05      -2.5230      -0.039   6.303e-03
      10   4.955332           63      1   -8.168112e-05      -2.7985      -1.020   6.201e-03
      20   4.955332           27      0    0.000000e+00       0.0000       8.335   7.066e-03
    1000   4.955332            0      0    0.000000e+00       0.0000       8.335   7.066e-03
```

**`S_L` is identical to six decimals at every dead band — including recentring entirely OFF, with
zero translations and a ledger of exactly zero — while the ledger rate swings from −2.80 % to 0.**

```text
=> THE TRANSLATION'S NON-CONSERVATION DOES NOT BIAS THE EIGENVALUE. Not partially. Not at all.
```

`audit.md` §2.1 had already flagged that the arithmetic did not close — *"a factor of 3.5 apart…
'The ledger explains the shift' is NOT established"*. The ladder resolves it: the flux bias
contributes **zero**, not one part in 3.5.

### 1.1 Why the eigenvalue is insensitive, which §2 did not consider

`S_L` is produced by bisection on the **sign of the trials' front drift**. Translation moves the
frame; it does not change the front's drift rate *relative to that frame*. So the sign the bisection
reads is unaffected, and the eigenvalue it converges on is unaffected. The quantity §2 assumed
`mdot` must account for is one the bisection never consults.

### 1.2 So what did move `S_L` by +6.07 %?

The locator fix, via the trials' drift measurement. Pre-fix, the first recentring in each trial
blinded `locateFront`, which then refused the true front for the rest of the march, so `history` —
which is appended only on a successful locate — recorded a corrupted drift. TD-040's own dead-band
ladder is what eliminated translation as the cause, and `logs/03_dt_ladder_fixed_tree.log` then
**excluded the pre-fix eigenvalue** (all four dt levels within 0.136 % near 4.96; 4.671937 is 6.0 %
away and the sequence moves away from it).

**So the move was toward the truth, and translation was not its cause.**

## 2. ALL THREE CANDIDATE FIXES ARE NOW WRONG OR UNNECESSARY

### 2.1 §4.1 "count the translated inventory as a boundary flux" — WRONG, it would corrupt C3

This looked like the minimum honest fix. It is a **category error**, and reading the balance settles
it. `PremixedFlame.cpp:1727-1740`:

```cpp
const Real fuelInflux = gammaInlet * (composition[fuelIndex][0] - yFuelUnburnt) / halfCell;
const Real convected  = massFlux * (composition[fuelIndex][last] - yFuelUnburnt);
result.fuelBalanceResidual = std::abs(convected - fuelIntegral - fuelInflux) / fuelThrough;
```

and the comment at `:1718`, written by the prior work:

> *"These are integral identities that hold for a converged steady flame and nothing else."*

**The balance is three INSTANTANEOUS fluxes evaluated on the FINAL field.** The ledger is a
**CUMULATIVE mass per area accumulated over the march**. They are different quantities with
different accounting dimensions, which is exactly why `−2.52 %` and `0.63 %` never reconciled —
§2 read their mismatch as an unexplained residue when it is a units mismatch.

Adding a cumulative term to a steady-state identity would make the residual **worse by roughly the
ledger's size**: `0.63 %` plus `2.52 %` lands near `3 %`, which would **FAIL C3's `< 0.02` bound**
and C4 and C6a-bal with it. A "fix" that breaks three passing criteria by mixing dimensions is not a
fix.

### 2.2 §4.2 "compensate the translation with an opposing inflow correction" — UNNECESSARY

Its entire purpose was to remove an eigenvalue bias. §1 shows there is none. It would introduce a
correction term with its own timescale — the audit's own stated risk — to cancel an effect measured
at zero.

### 2.3 §4.3 "do not translate; measure in the lab frame" — UNNECESSARY, and the ladder prices it

Also aimed at a bias that does not exist. The ladder additionally shows what removing recentring
costs: at `deadBand` 20 and 1000 the front sits **8.335 cells** off the anchor instead of 0.039, and
the balance **degrades** from `6.303e-03` to `7.066e-03`. **Recentring makes the balance better**, by
keeping the front away from the inlet — the opposite of what an item about its non-conservation
would predict.

### 2.4 §4.4 "one cell at a time" — already rejected, and now doubly so

## 3. THE PROPOSED DISPOSITION

`audit.md` §2 itself establishes the operator is correct:

> *"The fills are **correct for their boundaries**: fresh unburnt entering upstream, zero-gradient
> outflow downstream. Translating the field while the domain boundaries stay fixed is a **frame
> change**, and a frame moving with the front genuinely does admit reactant and expel product."*

That was right. What followed it — that the moved mass must be counted in `mdot` — is what §1
refutes. Putting the two together:

```text
the fills                   correct for a moving frame
the ledger                  correctly measures what the frame change moves, to 6e-05 relative
                            agreement across two libraries (A2_05)
the eigenvalue              properly INSENSITIVE to it (§1)
the steady-state balance    properly EXCLUDES it (§2.1) -- it is an instantaneous identity
```

```text
PROPOSED: TD-040 is N/A AS A DEFECT. There is nothing to fix, the behaviour is correct for what
          it is, and the ledger is the right instrument for reporting it.
```

**This is proposed, not applied.** Reclassifying a P0 is a user decision, and §4 of this document
says what it would change.

## 4. WHAT THE RECLASSIFICATION WOULD DISCHARGE

```text
TD-004 blocker 1   "TD-040 carries TD-004's stop" is FALSE. The registered stop was: "if S_L
                   moves, the fix changed the eigenvalue, which it must not: the locator feeds
                   recentring, and recentring is supposed to be conservation-neutral."
                   Its chain breaks at the third link -- recentring's non-conservation does not
                   bias S_L -- and the dt ladder shows the move was TOWARD the verified limit.
                   The stop fired correctly and did its job: it forced this investigation. Whether
                   a fired stop is DISCHARGED by refuting its premise is a user decision.
TD-004 blocker 2   UNAFFECTED. C6a-ord is UNRESOLVED at settings.tolerance = 1e-3 (GATE_A6,
                   verification_A6.md). TD-004 does NOT close either way.
TD-027 C2          UNAFFECTED by this document, though the same dt ladder already showed its
                   Richardson reference is contaminated.
```

## 5. What is NOT established

```text
NOT established  that the discarded fuel is harmless to quantities NOT currently gated. The
                 eigenvalue and the steady-state balance are shown insensitive. A TIME-INTEGRATED
                 conservation claim over the march would see the ledger's 7.9e-04 kg/m^2 (223
                 translations) and would be right to -- no such claim is made anywhere today, and
                 if one is ever made this item becomes live again.
NOT established  that the frame change is correct for a NON-steady application. Everything here is
                 gated on a converged steady flame, which is what :1718 says the identities
                 require. A transient or unsteady use of this solver would need its own analysis.
NOT established  that the ledger is correct in absolute terms. A2_05 showed the per-event cost
                 agrees to 6e-05 relative ACROSS TWO LIBRARIES, which rules out a mis-reporting
                 instrument but is not an independent derivation of the value. The audit's §1
                 closed-form derivation agrees to 0.014 %, which is strong -- but both are the same
                 arithmetic, not two independent paths.
NOT measured     whether the front's 8.335-cell offset with recentring OFF breaks anything else.
                 It is outside C5c's 0.08 mm bound by a factor of ~2.8, so a recentring-free
                 configuration would fail C5c -- which is a reason not to remove the operator, and
                 is recorded rather than explored.
NOT applied      any reclassification, and NO production change.
```

**§5's first item is the live boundary.** TD-040 is "not a defect" *with respect to every quantity
the project currently gates*. That is a conditional disposition, not an absolution, and the
condition is written down so a future time-integrated claim reopens it rather than inheriting it.
