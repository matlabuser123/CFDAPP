# TD-029 — the cancellation survives in FORM and is REFUTED in substance. The temporal error collapsed 62x.

```text
STATUS:      RE-MEASURED on the TD-032-fixed march. TD-029's qualitative claim (opposite signs)
             HOLDS. Its quantitative claim ("not converged in time, by 4.24 %") is REFUTED:
             the temporal error is now 0.068 %.
SCOPE:       the dt/dx separation only. No production change.
RESULT:      spatial -0.679 % · temporal +0.068 % · ratio 0.10 (was 5.23) · BOTH SIGNS FLIPPED
EVIDENCE:    logs/01_dt_dx_separation.log
INSTRUMENT:  tools/dt_dx_separation_probe.cpp
TREE:        PremixedFlame.cpp fa815c97… · libcfdcore.a 852555cf… (the A3-verified pair)
COMMIT:      none — not authorized.
```

---

## 1. What TD-029 recorded, and what is measured now

```text
                              RECORDED (pre-TD-032)      MEASURED (fixed march)
refining dx, 250 -> 500            +0.81 %                    -0.679 %
refining dt, dt  -> dt/2           -4.24 %                    +0.068 %
|temporal| / |spatial|              5.23                       0.10
```

```text
leg  cells   dt              S_L cm/s     steps    fuel bal     front mm
A      500   9.424300e-06    4.955332     15283    6.3028e-03   3.499220
B      250   9.424300e-06    4.988989     15078    6.3390e-03   3.559875
C      500   4.712150e-06    4.958687     30526    2.0261e-03   3.497621
```

Both fronts sit inside C5c's derived `0.08 mm` bound (worst `|3.559875 - 3.5| = 0.0599 mm`), so no
leg is reporting a runaway front.

## 2. The signs are still opposite — and that is the WEAKEST reading of this data

`refine dx` lowers `S_L`, `refine dt` raises it. Opposite directions, so under a coupled refinement
they still partially offset, and TD-029's structural point stands: **a family that ties `dt` to `dx`
is not self-similar, and an observed order measured on one mixes two convergences.**

**But the probe's own verdict was too narrow, and I am recording that.** It compared the two signs
*to each other* and stopped. It did not compare either sign to the RECORDED one — and both have
**flipped**:

```text
pre-fix   refining dx RAISED  S_L (+0.81 %)      now it LOWERS it (-0.679 %)
pre-fix   refining dt LOWERED S_L (-4.24 %)      now it RAISES it (+0.068 %)
```

A probe that answers the question it was asked while missing a reversal in its own reference data is
the fourth auto-verdict in this campaign to be too narrow. The verdict line is not wrong; it is
incomplete, and the incompleteness is the interesting part.

## 3. THE REFUTATION — the temporal error collapsed by 62x

TD-029's headline is quoted in `dry_run.md` §L.3 as *"it is not converged in time, by 4.24 %"*.

```text
temporal error      4.24 %   ->   0.068 %        62x smaller
```

**The march is now very nearly converged in time at this fixture.** The quantitative claim that gave
TD-029 its severity does not survive the fixed tree.

### 3.1 Why that is exactly what should have happened

**TD-032's defect was a TEMPORAL one.** The transient term read a time level one step stale, so the
march was split into two interleaved sub-sequences. A measurement of "how much does `S_L` move when
I halve `dt`" taken on that march was measuring **the defect's response to `dt`**, not the
discretisation's. Removing the stale level removes most of the apparent temporal error.

So the `4.24 %` was never temporal *discretisation* error in the ordinary sense. This is a causal
story that fits every number here, and it is **not independently verified** — the discriminating
test would be the same dt ladder on a reconstructed pre-TD-032 library, which is not run.

### 3.2 The cancellation is now badly asymmetric, so "cancellation" oversells it

```text
pre-fix    temporal 4.24 %  vs spatial 0.81 %   ratio 5.23   TEMPORAL dominated
now        temporal 0.068 % vs spatial 0.679 %  ratio 0.10   SPATIAL dominates by 10x
```

Two errors of opposite sign cancel to the extent of the SMALLER one. At a ratio of `0.10`, coupled
refinement can offset at most about a tenth of the spatial error. **Calling that "they cancel" would
be repeating the pre-fix picture, where the two were within 5x and the offset was substantial.**

## 4. What this does to C6a — it WEAKENS my own candidate explanation

`verification_A5.md` §2.2 offered the dt/dx coupling as a candidate for C6a's order leaving the
frozen band (`p` 2.057 -> 2.693), explicitly as not established. This measurement **weakens that
candidate**:

```text
the coupling IS present          the two errors are genuinely opposite in sign
the coupling is SMALL            the temporal component is now 10x smaller than the spatial, so a
                                 combined-refinement order is dominated by the spatial convergence
                                 rather than mixing two comparable contributions
```

So the coupling cannot be assumed to account for a shift from 2.057 to 2.693. **§2.2's candidate is
not refuted, but it is no longer the natural explanation, and C6a's order regression remains
unexplained.** That is a worse position than A5 left it in, and it is the honest one.

## 5. What it EXPLAINS instead: C6a's balance clause

Leg C halves `dt` at fixed `dx` and the fuel balance improves **3.1x**, from `6.3028e-03` to
`2.0261e-03`, while `S_L` barely moves. So:

```text
S_L            is nearly converged in TIME (0.068 %)
the fuel balance is NOT -- it is strongly dt-limited
```

Different quantities have different temporal convergence, and the balance is the dt-limited one.
That supplies a mechanism for C6a's balance clause failing at the coarse levels: in that family
`dt ~ dx^2`, so at 125 cells `dt` is roughly **16x** larger than at 500, and a dt-limited quantity
degrades accordingly:

```text
C6a fuel balance   125 cells 0.1505    250 cells 0.0283    500 cells 0.0063
```

**This is a mechanism, not a measurement of that family.** It was derived at 500 cells and applied
to 125 and 250 by argument. Confirming it would need the balance measured at fixed `dt` across those
levels, which is not run here.

## 6. What is NOT established

```text
NOT established  that the 4.24 % was caused by TD-032's stale time level. §3.1 fits every number
                 but the discriminator -- the same dt ladder on a reconstructed pre-fix library --
                 was NOT run.
NOT established  that the coupling explains ANY of C6a's order shift. §4 shows the mechanism is
                 present and small. C6a's regression stays unexplained.
NOT established  the dt-limited explanation for C6a's balance clause. §5 derives it at 500 cells
                 and extends it by argument.
NOT comparable   the magnitudes against TD-029's record. Its base dt of 3.770e-05 is cell diffusion
                 number 20, the cap TD-027 REMOVED; this uses 9.4243e-06 at the shipped cap of 5.
                 Only the SIGNS were ever comparable, and those are reported as flipped.
NOT measured     whether two points suffice for either direction. Each leg is a SINGLE halving, so
                 neither error has a demonstrated order -- and this campaign has already recorded
                 that an order from two points can be wrong in sign. No order is claimed here, only
                 a direction and a magnitude at one refinement step.
NOT re-measured  TD-031, the other item in this step.
```

**§6's fifth item bounds everything above.** One halving gives a direction and a size; it does not
give a convergence order, and nothing here should be read as one.
