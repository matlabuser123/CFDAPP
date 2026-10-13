# P18.8 — Amendment A2, ERRATUM 1: criterion labels

`acceptance_gate_A2.md` is **not edited**. It stands at sha256
`f0973e34068930d119aed6a485646e05e3947945ec898c8d1346181f454d11fe`, and every threshold,
measurement, control and verdict in it is unchanged and correct. This erratum corrects **criterion
labels only**, in three places where I mapped A2's clauses onto the wrong numbers from the original
gate (`acceptance_gate.md`, sha256 `3fc7a020…`).

Found by building `coverage_map.md` — mapping each frozen criterion to the test that measures it,
which is the step that also exposed two genuine coverage gaps (both now closed, §3 below).

## 1. The corrections

| A2 says | The original gate says | Correct label |
| --- | --- | --- |
| "A2 … replaces criteria **E3, E4, E6, E8 and E9**" | — | replaces **E3, E4, E5, E6, E8, E9** |
| "**E5** — concentrations (unchanged, restated for completeness)" | **E5** is *stoichiometric sources* — `ω_i` per species, per reaction, per cell, with `Σᵢ ωᵢ` vanishing to `32·ε·max|ω|` | concentrations are an **input** to E4/E5, not a numbered criterion. A2's clause is a restatement of part of **E4**. |
| "**E6′** — species sources and heat release (replaces E6)" | **E5** is `ω_i`; **E6** is heat release | replaces **E5 and E6**, since the clause covers both |
| "**E7** — mixture properties and equation of state (unchanged, and now load-bearing)" | **E1** is mixture properties; **E2** is the EOS. **E7** is the *energy update and temperature recovery* | should read **E1 and E2** |
| "Condition (4) is therefore policed by **E7′**, the BITWISE mixture-property criterion" | — | policed by **E1** |

## 2. What this does and does not change

**Does not change:** the derived bound of 8, the `q` scale `Σ|terms|`, the measured values (`k` 3 ULP,
`q` 3.13865, FMA on `q` 0.843356 against libm 3.13865), the allocation of G9 condition (4) to the
zero-tolerance mixture-property criterion, the argument that it cannot live on a bounded criterion,
the control table, or any verdict. The relabelling moves no threshold and reclassifies no result.

**Does change** one substantive statement, in A2's favour: the original **E7** — *energy update and
temperature recovery*, with "EVERY INTERMEDIATE compared, not only the final T", controls N7 and N9 —
is **not amended by A2 and passes unchanged and BITWISE**. A2 should have said so explicitly instead
of occupying the E7 label. Measured: the sensible and chemical energy pools and the recovered `T` are
bitwise over 1024 cells, and bitwise over a 50-step march in which `T` moved 28.6389 K.

So A2 relaxes **E3, E4 and the `q`-dependent part of E5/E6**, and nothing else. Of the thirteen
frozen criteria, **ten remain at zero tolerance**.

## 3. Two coverage gaps found by the same mapping exercise, both now closed

1. **E1's own case list was four fifths unmeasured on device.** E1 names "single-species, binary,
   ternary, near-pure (Y → 1−ε), spatially varying, × 5 temperatures". The device suite had only the
   spatially-varying ternary case; link one covered binary and ternary but on the **host**. Closed by
   `E1_EveryCompositionCaseInTheFrozenListIsBitwiseOnDevice` — 1 280 comparisons over 4 compositions
   × 5 temperatures, all bitwise, plus an independent check that a single-species mixture reproduces
   that species' own `M` and `cp` exactly.

   **Recorded limitation:** the "× 5 temperatures" clause is **inert for this mechanism family**.
   `DeviceSpecies` carries a single constant `cp` per species, so `M_mix`, `R_mix` and `cp_mix`
   cannot depend on `T`. Running five temperatures satisfies the letter of the frozen list and
   measures nothing new. The limitation behind it is a capability gap, recorded in
   `kernel_inventory.md` §4: **the device restatement supports constant `cp` only, while the CPU
   production path supports ranged `cp`.**

2. **Control N10 was never implemented.** The frozen control table lists "N10 — different
   timestep/control → E9". Closed by `N10_ADifferentTimestepProducesADifferentTrajectory`: the same
   total time marched as `dt = 1e-5 × 50` and `dt = 5e-6 × 100` differs by `max |ΔT| = 0.0124043` K
   and `max |ΔY| = 3.26e-06` — a first-order truncation difference, bounded away from both zero and
   a blow-up. Without it, E9's "host and device agree bitwise" could have been agreement about an
   inert computation.

   This is the fourth control in this phase found pointing at the wrong criterion or at none —
   after N1 and N2 (aimed at the reaction sources, which never consult them) and N8 (classified as
   a control for the bounded `q` criterion, which it does not touch). All four were found by the
   mapping, none by the tests failing.

## 4. Status

All twelve controls in the frozen table are now implemented and measured, except **N3**, which the
frozen gate itself marks **N/A — transport not implemented** (`audit.md` §5).
