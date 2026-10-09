> **SUPERSEDED, AND PRESERVED DELIBERATELY.**
>
> This file records the state of P17.9 at the moment it was BLOCKED, and the three gate defects it
> reported were subsequently AUTHORISED for amendment by the user on 2026-09-28. It is kept verbatim
> because the amendment records depend on it: A1, A2 and A3 each cite the original failing result,
> and an amendment whose "before" state had been edited away would not be auditable.
>
> Current state of each finding:
>
> | Finding | Outcome |
> | --- | --- |
> | 1. Group V vacuous (HLL passed everything) | **AMENDMENT A1** — stationary-contact width must be exactly 0. V1 and V2 now both FAIL. |
> | 2. X3 rarefaction-head bound mis-derived | **AMENDMENT A2** — replaced by the derived diffusive scaling law, no free constant. |
> | 3. S6 density plateau contaminated | **AMENDMENT A3** — replaced by the flattest-window estimator; contamination proven objectively. |
>
> See `amendments/coverage_map.md` for the full audit trail, and `summary.md` for the closing state.
> Nothing below this line has been altered.

---

# P17.9 — BLOCKED: three frozen-gate criteria need authorization to amend

**The solver is not the problem.** Eleven of thirteen benchmarks pass, most of them by three to
twelve orders of magnitude, including the normal shock, both contacts, compression, expansion,
supersonic advection, conservation and determinism. What is blocked is the **gate**: three of its
criteria turn out to measure something other than what they name.

`acceptance_gate.md` is **UNMOVED** — sha256 `b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40`.
Nothing has been amended. CLAUDE.md §5.3 requires the failure preserved, the cause investigated
independently, the replacement derived from first principles rather than from the observed number,
and **authorization requested** before anything is changed. This file is that request.

---

## Finding 1 — Group V is PARTIALLY VACUOUS. This is the serious one.

The frozen G7 says, in its own words:

> *"a first-order upwind scheme WITHOUT the Riemann flux must FAIL this gate. If it passes, the gate
> is not measuring shock capturing. A second, discriminating case is required beyond Sod: Sod alone
> can be passed by an over-diffusive scheme that smears everything to roughly the right place."*

Both controls were run (`logs/07_controls.log`). The result:

```text
control                                   contact test   Sod plateau test   conservation
-------------------------------------------------------------------------------------------
V1  Rusanov / local Lax-Friedrichs        PASSED         FAILED (detected)  PASSED
    -- no Riemann solver at all
V2  HLL -- HLLC with the CONTACT WAVE     PASSED         PASSED             --
    DELETED, nothing else changed          ^^^^^^^^^^^^^^^^^^^^^^
```

**V2 passes every criterion in the gate.** By G7's own standard, the gate does not measure the wave
HLLC exists to restore. V1 is caught, so the primary requirement ("a scheme without the Riemann flux
must fail") *is* met by the plateau criteria — but the "second, discriminating case" is not.

### Why, and the quantity that does discriminate

`logs/08_contact_width.log` measures the contact's smeared width in cells for all three fluxes:

```text
scheme     Sod contact   STATIONARY contact   moving contact
-----------------------------------------------------------
HLLC       16 cells       0 cells              22 cells
HLL        18 cells      21 cells              22 cells
Rusanov    26 cells      21 cells              27 cells
                         ^^^^^^^^^^
```

**A stationary contact is preserved EXACTLY by HLLC — zero smeared cells — and smeared over 21 cells
by both controls.** That is the discrimination, and it is not a tolerance: a stationary contact is an
exact steady solution of the Euler equations, and P17.7 already proved the flux carries exactly zero
mass and energy across one, bitwise.

Two things are worth noting because they are not obvious:

* the **moving** contact does not discriminate at all (22 cells for both HLLC and HLL). The
  frozen gate's contact criteria are written around position and spurious p/u signals, and a
  symmetric over-diffusive scheme gets all of those right;
* the existing test **already measures** the width and prints `0 cells` — it simply never asserts on
  it, because the authorization's §23 says smearing "is not necessarily a pass/fail metric unless
  P17 gates define one". The gate did not define one.

### Proposed amendment 1 — exact, boolean, not a tolerance

```text
NEW CRITERION V5: a STATIONARY contact must be preserved with ZERO smeared cells.
    measured: transitionWidthCells(rho, rho_L, rho_R) == 0
    HLLC: 0        HLL: 21        Rusanov: 21
```

One assertion on a number already being computed. It is exact rather than empirical, it follows from
the contact being an exact steady solution, and it makes both V1 and V2 fail as G7 requires.

---

## Finding 2 — criterion X3 (rarefaction head ≤ 2 dx) is mis-derived

```text
FAILED, preserved in logs/PRESERVED_03_sod_first_run.log and logs/06_waves.log:
    rarefaction head position error, nx = 200:  5.8921e-02 = 11.78 dx   (limit 2 dx)
```

The gate derived `2 dx` from "half the smear width of a jump". **The rarefaction head is not a
jump.** Density is *continuous* there; only its gradient is discontinuous. A first-order scheme
smears a weak wave diffusively, so the position at which the profile has departed from the plateau
by a fixed *fraction of amplitude* moves like the diffusion length `sqrt(D t)` with `D ~ a dx / 2` —
i.e. like `sqrt(dx)`, not like `dx`.

The measurement confirms the derivation rather than the reverse:

```text
nx     dx          head err      head/dx      head err / sqrt(dx)
200    5.000e-03   5.8921e-02    11.78        0.83326
400    2.500e-03   3.8596e-02    15.44        0.77192
800    1.250e-03   2.4860e-02    19.89        0.70315
                                 ^^^^^^       ^^^^^^^
                                 GROWS        nearly CONSTANT
```

The error **decreases in absolute terms** at every refinement (observed rate `dx^0.62`, between the
`sqrt(dx)` of a purely diffusive weak wave and the `dx` of a sharp one). A bound written as `C·dx`
is dimensionally wrong for this feature and would fail a converging solver at every mesh, more
badly the finer the mesh.

For contrast, the criteria whose derivation *was* right:

```text
shock   err/dx = 0.82, 0.85, 0.86   -> flat. O(dx), exactly as derived. Passes 2 dx everywhere.
contact err/dx = 1.15, 1.29, 1.44   -> slowly growing (contact diffusion ~ sqrt(steps)). Passes.
tail    err/dx = 2.71, 2.70, 2.07   -> flat-ish. Passes its 4 dx.
```

### Proposed amendment 2 — derived from the diffusion length, magnitude not fitted

```text
REPLACE X3 with:
  (a) the head position error must DECREASE in absolute terms under refinement   [the substance]
  (b) err / sqrt(a_L · dx · t) must be bounded by a declared constant and must not GROW
  (c) err/dx is REPORTED, and the evidence must state that it grows by construction
```

---

## Finding 3 — criterion S6 applied to `rho*_R` is instrument-limited, not solver-limited

```text
FAILED: |rho*_R - exact| did not decrease from nx = 400 to nx = 800
    nx=200  3.59405e-03      nx=400  3.74699e-03      nx=800  3.42003e-03
```

The diagnosis is decisive and does not depend on judgement. `p*`, `u*` and `rho*_R` are sampled from
**the same window, by the same policy, at the same time**:

```text
nx      |dp*|         |du*|         |drho*_L|     |drho*_R|
200     3.51241e-04   1.06622e-03   7.71782e-03   3.59405e-03
400     1.71478e-04   5.28371e-04   5.68888e-03   3.74699e-03
800     7.52235e-05   2.46821e-04   4.21735e-03   3.42003e-03
        rate ~1.1     rate ~1.1     rate ~0.44    does not converge
```

**A contact carries a density jump but no pressure or velocity jump.** Its smeared tail therefore
contaminates the density measurement in that window and leaves `p` and `u` untouched — which is
exactly the pattern observed. `rho*_L`, sitting against the same contact, converges only at ~0.44
(the `sqrt` of contact diffusion) for the same reason.

`logs/04_diagnose_plateau.log` confirms it directly by re-measuring `rho*_R` from the saved profiles
with wider exclusions:

```text
exclusion    nx=200        nx=400        nx=800
5 (frozen)   2.90729e-03   3.24468e-03   3.09269e-03     contaminated
20           1.15950e-03   1.76682e-04   2.25597e-04     an order of magnitude better
```

The criterion S4 that *bounds* `rho*_R` (`≤ 0.10 × the shock jump = 1.4e-2`) passes at every mesh
with a factor of four to spare. Only the **monotone-improvement** criterion S6 fails, and it fails
because the measurement floor is set by contact smearing rather than by the solver.

### Proposed amendment 3

```text
S6 continues to apply UNCHANGED to p* and u*, which converge cleanly at ~1.1 and are the quantities
the star state is actually defined by.
For the two DENSITY plateaus, replace monotone improvement with:
  (a) the bound S3/S4 must hold at every mesh                                   [unchanged]
  (b) the sampling exclusion must SCALE with the measured contact width, not sit at a fixed 5 cells
  (c) with that exclusion, rho*_L and rho*_R must decrease under refinement
```

---

## What passes, so the scale of the above is clear

```text
NORMAL SHOCK, M1 = 2, 2D mesh 400x1          all five ratios within 1.1e-4 of analytical
  rho2/rho1 2.6667103 vs 2.6670021           rel 1.09e-04
  p2/p1     4.4995544 vs 4.4998114           rel 5.71e-05
  T2/T1     1.6873053 vs 1.6872171           rel 5.23e-05
  u2/u1     0.3749263 vs 0.3749528           rel 7.07e-05
  M2        0.5772702 vs 0.5773261           rel 9.68e-05
  Rankine-Hugoniot residuals: mass 1.80e-04, momentum 1.19e-04, total enthalpy 4.02e-05
  shock drift 0.0178 dx over 1389 steps; 0 retries, 0 rejected steps

SOD, the three structures                     rarefaction -> contact -> shock, in order, measured
  shock position     0.8545118 vs 0.8504186   0.82 dx
  contact position   0.6797523 vs 0.6855040   1.15 dx
  contact carries    |dp|/p* 1.64e-03, |du|/u* 3.63e-03, and a real density jump
  L1 errors converge at rho 0.66, u 0.83, p 0.75 -- in the band the gate predicted in advance

STATIONARY CONTACT    position error EXACTLY 0, p and u variation EXACTLY 0, width 0 cells
MOVING CONTACT Ma 3.4 position 0.141 dx, p variation 4.09e-14, u variation 3.00e-15
COMPRESSION           p* rel 1.77e-03, rho* rel 3.98e-03, u* exactly 0, both rise
EXPANSION             p* rel 3.97e-03, rho* rel 2.91e-03, u* exactly 0, both fall, min rho 0.688
SUPERSONIC ADVECTION  centroid position error 2.0e-13, p and u variation ~1e-14
CONSERVATION          mass 2.31e-15 / 1.51e-14, energy 6.31e-15 / 4.29e-15 (bound 3.6e-13/7.1e-13)
DETERMINISM           dt, time, residual, retry, full state, shock and contact positions BITWISE
CFL SENSITIVITY       CFL 0.4 vs 0.2: shock err 4.09e-03 vs 4.35e-03, L1 1.278e-02 vs 1.304e-02
POSITIVITY            every case rho > 0, p > 0, T > 0; 0 retries, 0 rejections, 0 non-finite
```

---

## The decision requested

```text
1. Authorize AMENDMENT 1 (the stationary-contact width criterion)?  -- this one matters most:
   without it the gate does not measure the contact wave, and G7 says so itself.
2. Authorize AMENDMENT 2 (the rarefaction-head scaling law)?
3. Authorize AMENDMENT 3 (density-plateau convergence under a smearing-aware exclusion)?
```

Each would be frozen as a separate `AMENDMENT_*.md` with its own dry-run and non-vacuity check, and
`acceptance_gate.md` would be left untouched, as its own §13 requires. **No criterion would be
loosened**: amendment 1 ADDS a criterion and makes the gate strictly stronger; amendments 2 and 3
replace a wrongly-scaled bound with a correctly-scaled one plus a convergence requirement.

Nothing has been changed pending that decision. The tree is verified clean:
`DensityBasedSolver.cpp` `53e84ac7…` and `HllcFlux.hpp` `fc4a8db5…`, both equal to their freeze
values, and every control restored with its hash re-checked.
