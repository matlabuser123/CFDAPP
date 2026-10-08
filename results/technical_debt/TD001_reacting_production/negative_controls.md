# TD-001 — NEGATIVE CONTROLS

```text
gate        GATE.md     section 3, controls N1-N5
amendment   GATE_A1.md  section 3, controls A1-N1 / A1-N2 / A1-N3
binary      build/release/apps/cli/cfdapp, rebuilt per variant, staleness checked each time
```

Each control must be **demonstrated to fail** the criterion it guards. One does not, and that is
recorded as a failure of the control rather than explained away — see N3.

Every case-file control is a COPY of `cases/reacting_plug_flow` under the scratchpad, so the
committed case is never mutated. Every source control is applied to `src/app/ProjectRunner.cpp`,
built, measured, and reverted with the revert **proved by sha256** against the production hash
`f14aa151d743190777d242e101cef6b19fb7ed77005bb9c65ead090292d8bc9a` recorded before the first variant.
The harness fails closed: a build failure or a hash mismatch stops it rather than leaving a variant in
the tree. One N1 patch anchor failed mid-run and the harness did exactly that, restoring and exiting.

---

## GATE.md controls

### N1 — the reacting dispatch removed (`if (false && setup.reacting...)`)

The pre-fix state, and the primary reproducer.

```text
fields.csv header   cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,pressure,temperature
metadata species    []
```

No `mass_fraction_*`, no `temperature_reacting`, no `heat_release`, no species metadata — on a case
file that declares a full mechanism and parses without complaint.

```text
C5 FAILS   no reacting result
C6 FAILS   no composition, so no composition change and no omega
C7 FAILS   no reacting temperature field
C8 FAILS   no Y_i, no reaction quantity
```

**All four fail, as required.**

### N2 — every `pre_exponential` set to 0

```text
max S_E                      0.0            (production 2.710138e+04)
max abs(dY)                  5.146406e-08   (production 1.355075e-03)
C1-C5                        still pass -- the case parses, builds and dispatches
```

C6 is a conjunction of `max abs(omega_i) > 0` **and** `max abs(Y - Y0) > 1e-12`. The first clause
fails at exactly `0.0`, so **C6 FAILS, as required**, and it proves C6 measures chemistry and not
parsing.

**But the second clause PASSES on this control**, at `5.15e-08`, for a reason that has nothing to do
with chemistry: it is the inherited transport error root-caused in `verification_A1.md` §3. On its own
that clause measures nothing, because the transport half moves the composition by more than `1e-12`
whether or not any reaction exists. Reported rather than left implicit — C6 survives on its first
clause alone.

### N3 — every `formation_enthalpy` set to 0 — **THIS CONTROL DOES NOT FAIL C7**

```text
max abs(dY)                  1.355075e-03   -- C6 still passes, as the control intends
max S_E                      0.0            -- no heat release, as the control intends
max abs(dT)                  5.688378e-05 K -- C7 requires only > 1e-9 K, so C7 PASSES
min_cell T                   999.999943 K   -- below T0, i.e. a cell COOLED
max_cell T                   1000.000056 K
```

C7 as frozen is `max_cell abs(T - T0) > 1e-9 K`, with *"the sign matching the mechanism's
`standardReactionEnthalpy` (exothermic => T rises)"*. With every formation enthalpy zeroed the
reaction enthalpy is **zero**: there is no sign to match, so the sign clause is undefined rather than
violated, and the magnitude clause passes by four orders of magnitude.

**So N3 does not fail C7, and gate §5 item 2 is not satisfied.** Gate §3 names N2 and N3 as "the two
that matter", precisely because C6 and C7 are the criteria most easily satisfied vacuously.

The `5.69e-05 K` is not a temperature response. It is the temperature transport half's own inherited
error — the same effect as `verification_A1.md` §3, in the energy equation — and the distribution
proves it: symmetric about 1000 K, `999.999943 .. 1000.000056`, i.e. some cells warmer and some
cooler. A heat release cannot cool a cell.

The signal-to-control ratio is nonetheless decisive: `2.710194 K` against `5.688e-05 K`, a factor of
**47 646**. The instrument discriminates; the frozen threshold is simply placed below the noise floor
of an implementation that runs a transport solve on the temperature whether or not chemistry heated
it. A derived replacement is proposed in `summary.md` and is **not** applied here.

### N4 — a patch whose per-species values sum to 0.9

```text
cli exit 2
boundaries.json: field "boundaries.json patch "left".species" must satisfy have fixed_value mass
fractions summing to 1 across all 3 reacting species (a patch composition is a mixture; this
codebase refuses an out-of-bounds composition and never normalises one); received 0.900000
no results/ written
```

**C11 fails to construct, as required** — refused before any solve consumes it, with the measured sum
in the message, and no partial build.

### N5 — `initial_mass_fraction` summing to 0.9

```text
cli exit 2
physics.json: field "physics.json reacting.species" must satisfy have initial_mass_fraction summing
to 1 (this codebase refuses an out-of-bounds composition and never normalises one); received 0.900000
no results/ written
```

**C4 refused by the parser with the measured sum, as required** — the carrier policy is enforced, not
merely documented.

---

## GATE_A1.md controls

Both steady variants are reconstructed in place: no transient term, no chemistry advance, and the
reaction source carried inside the transport solve. They differ only in the source treatment. The
temperature half is left as the production one, because it cannot affect the composition and C4b/C4c
are composition criteria — stated so the reconstruction is not mistaken for a full revert.

Both reproduce the numbers recorded before the amendment, which is what makes them faithful
reconstructions rather than new variants: `3.00e-01` and `4.92e-07`.

### A1-N1 — the Patankar-split steady pass; must FAIL C4b

Run at three rungs of the same tolerance ladder used on production:

```text
abs/rel          max|sum Y - 1|      min_i Y_i        iterations
1e-11 / 1e-9     3.002618400e-01     +2.838846e-03    30 37 23
1e-12 / 1e-10    3.002618357e-01     +2.838846e-03    32 38 24
1e-13 / 1e-11    3.002618352e-01     +2.838846e-03    33 40 28
```

Flat to eight significant figures while the residuals fall by two orders. **C4b FAILS on A1-N1, as
required.**

**And this is where the amendment runs into trouble.** Production fails C4b the same way — ratio
`1.0000` — so the criterion no longer separates a closure of `5.7e-08` from one of `3.0e-01`, five
million times worse. A1 §3 says of this control: *"If A1-N1 passed C4b, the criterion would be
measuring nothing."* The measured situation is the mirror image: A1-N1 fails it, and so does the
correct implementation, which leaves the criterion equally uninformative. Recorded as a defect of
C4b, not repaired.

### A1-N2 — the fully explicit steady pass; must FAIL C4c

```text
min_cell min_i Y_i   = -2.974231e-01
max|sum Y - 1|       =  4.919333e-07     <- the BEST sum closure of any variant measured
```

**C4c FAILS on A1-N2, as required**, and it proves exactly what A1 added C4c for: the variant with
the best sum closure is the one that drives a mass fraction to −0.297. A criterion set gating only
the sum would have preferred it.

### A1-N3 — the original `4*eps*N` applied to the SOLVED field

```text
bound     4 * eps * N_species = 2.66e-15
measured  5.70e-08 .. 5.72e-08 at every rung of the ladder
```

**Fails at every tolerance, as required** — recorded to show the amendment was necessary rather than
convenient. Note that the §3 root cause makes this stronger than A1 assumed: the gap is not the
solver's tolerance, so no reachable tolerance closes it.

---

## Summary

| control | guards | required | result |
| --- | --- | --- | --- |
| N1 | C5, C6, C7, C8 | all fail | **fails all four** |
| N2 | C6 | fails | **fails** (on the `omega > 0` clause; the `dY > 1e-12` clause passes — reported) |
| N3 | C7 | fails | **DOES NOT FAIL** — gate §5 item 2 unsatisfied |
| N4 | C11 | fails to construct | **fails to construct** |
| N5 | C4 | refused with the sum | **refused with the sum** |
| A1-N1 | C4b | fails | **fails** — but so does production, so C4b no longer discriminates |
| A1-N2 | C4c | fails | **fails** |
| A1-N3 | the original C4 | fails | **fails at every tolerance** |
