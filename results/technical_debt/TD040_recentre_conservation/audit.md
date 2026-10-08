# TD-040 — `recentre()` discards reactant and fabricates product, once per translation

```text
opened      2026-10-08, carved out of TD-004
authorized  2026-10-08, in full
status      AUDIT. No production change. No gate frozen.
severity    P0. It carries TD-004's stop, it biases the eigenvalue, and any fix that makes
            recentring fire more often moves S_L until it is addressed.
evidence    TD004_premixed_flame/logs/A2_05_ledger.log  (two libraries)
            TD004_premixed_flame/verification_A2.md §4
reproducer  already recorded; the mechanism below is DERIVED, not inferred from a run.
```

---

## 1. The mechanism, derived exactly

`shiftField` is the whole of it:

```text
source = c + shift
  source <  0    -> shifted[c] = upstreamFill          (tUnburnt / unburntComposition)
  source >= nc   -> shifted[c] = field[nc - 1]         (the outlet value, fully burnt)
  otherwise      -> shifted[c] = field[source]
```

With `shift > 0` — the front downstream of the anchor, content moved upstream — the **upstream
`|shift|` cells are discarded**, and they are full of fresh reactant. The downstream end is extended
by duplicating `field[nc-1]`, which is fully burnt product.

**Every translation therefore throws away reactant and fabricates product.** Both ledger quantities
follow from that in closed form:

```text
                                                        DERIVED        MEASURED       agreement
fuel    shift * rho_u * Y_F,u * dx                  -3.534113e-06  -3.533634e-06      0.014 %
energy  shift * rho_b * cp * (T_ad - T_u) * dx      +21.2047       +21.20180          0.014 %
```

(`rho_u = 1.178038`, `rho_b = 0.196340` kg/m^3 from `P W / R T`; `shift = 3`, `dx = 2e-5` m.)

The sensible term is a **pure gain** because the discarded upstream cells sit exactly at `tUnburnt`,
which is the reference `sensibleContent()` measures from, so they contribute nothing to the loss
side. Only the fabricated burnt cells register.

**This is arithmetic, not hypothesis.** No measurement was needed to establish the mechanism; the
two-library run only confirmed it and showed the per-event cost is identical across libraries to
6e-05 relative, which is what rules out a mis-reporting ledger.

## 2. It is NOT a coding slip, and that decides the fix

The fills are **correct for their boundaries**: fresh unburnt entering upstream, zero-gradient
outflow downstream. Translating the field while the domain boundaries stay fixed is a **frame
change**, and a frame moving with the front genuinely does admit reactant and expel product.

The defect is that the mass this moves is **never counted in `mdot`**:

```text
effective extra mass flux, as a fraction of mdot:
    pre-fix  (1 translation  per final march, 120 across the trials)    0.79 %
    post-fix (3 translations per final march, 220 across the trials)    2.52 %
```

`S_L` *is* the mass flux the bisection converges on, so an uncounted flux biases it directly. That
is why repairing TD-004's locator — which only made the operator fire as often as it was always
supposed to — moved the eigenvalue.

### 2.1 The arithmetic does NOT close, and that is stated rather than smoothed

```text
change in effective flux bias   0.79 % -> 2.52 %   =  1.73 percentage points
change in S_L                   4.671937 -> 4.955332 cm/s  =  +6.07 %
```

Same order, same sign, **a factor of 3.5 apart.** So the flux bias is not the whole story: either the
response is non-linear (a 1 % flux bias need not give a 1 % eigenvalue shift), or a second
contribution exists. **"The ledger explains the shift" is NOT established and must not be written
down as though it were.** Closing it is §5.

## 3. What the existing "fix" did, so it is not redone

TD-004's register entry records recentring non-conservation as **fixed**. That work added the ledger
and the refusal branch — it made the cost *measurable* and bounded the worst single shift — and it
reduced the damage from a 164-cell shift moving 84 % of the domain's fuel inventory to a 3-cell
shift moving 0.79 % of `fuelThrough`. **It did not make the operator conservative**, and the ledger
exists precisely to report the residue. This item owns that residue; it is not a re-run of the
earlier work.

## 4. Candidate fixes — they differ in kind, not just in cost

### 4.1 Count the translated inventory as a boundary flux — BOOKKEEPING ONLY, insufficient alone

The ledger already measures exactly what crossed. Adding it to the fuel balance would make the
balance self-consistent.

**But it does not undo the perturbation.** The solution really has lost that reactant; the flame
really is burning in a domain that has been fed extra mass. Accounting for it would make the
*balance* right while leaving the *eigenvalue* biased. Necessary for honest reporting, not
sufficient for TD-004.

### 4.2 Compensate the translation with an opposing inflow correction — PLAUSIBLE, needs derivation

Subtract the translated mass from the inflow over the following steps, so the net mass admitted
equals `mdot * dt` as intended. Keeps the moving frame and removes the bias at source.

**Risks that must be gated:** it introduces a correction term with its own timescale, and a
correction applied over the wrong number of steps would trade a steady bias for a transient one.
It also interacts with the dead band — a flame that recentres rarely gets a large correction at long
intervals.

### 4.3 Do not translate; measure in the lab frame — CLEANEST, largest blast radius

The front drifts, the domain stays put, and the drift is measured directly instead of being
cancelled. No fills, no discarded cells, no ledger.

**Why it is not obviously right:** the domain must then be long enough to contain the front's whole
excursion, and TD-030 already measured that the upstream room is **2.96 preheat lengths against the
5.8 the fixture assumes**. Removing recentring without lengthening the domain would put the front
into the inlet. This is an architecture change and would need `cfdapp-architect`, not a patch.

### 4.4 Reduce the shift to one cell at a time — REJECTED

Smaller shifts, more often: the per-event cost falls but the *rate* does not, since the same total
displacement is applied. The ledger rate is what biases the eigenvalue, so this moves nothing.

## 5. What the audit needs next, before any fix

```text
a  CLOSE THE ARITHMETIC. Is the eigenvalue shift explained by the flux bias? The discriminator is
   a ladder in recentring FREQUENCY at fixed physics -- vary recentreDeadBandCells, which changes
   how often the operator fires without changing anything else, and see whether S_L moves with the
   ledger rate. If it does, §2 is established and 4.2 is the fix. If it does not, something else
   moved the eigenvalue and the diagnosis is incomplete.
b  WHICH EIGENVALUE IS CORRECT? Neither 4.671937 nor 4.955332 is verified. TD-027's Richardson
   limit was extrapolated on the blind-locator solver and may have inherited the defect. The dt
   ladder re-measured on the fixed tree would give a limit that has not.
```

**(a) is cheap and decisive and should come first.** `recentreDeadBandCells` is a setting, so the
ladder needs no production change — the same reason TD-032's budget-ceiling trace worked.

## 6. What this audit does NOT establish

```text
NOT established  that the flux bias accounts for the 6.07 % shift. §2.1.
NOT established  which eigenvalue is correct. Neither is verified.
NOT established  that 4.2 is implementable without introducing a transient. §4.2.
NOT measured     the trials' ledger. It is RESET before the final march, so the 220 trial
                 translations are unmeasured -- and they are the ones that set the eigenvalue.
                 This is the most important gap in the current evidence.
```

**That last one deserves emphasis.** Every ledger number in this audit is the *final march's*, which
is 3 translations. The eigenvalue is set by the trials, which perform 220. The quantity that matters
most to TD-004's stop has not been measured at all, because the instrument is reset before it.
