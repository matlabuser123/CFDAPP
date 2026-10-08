# TD-001 — VERIFICATION UNDER AMENDMENT A1 — **STOPPED AT C4b**

```text
item        TD-001  Reacting-species production reachability
gate        GATE.md     sha256 0749efbf1120db1fa6689a5b86be2e5045a20722413e6c2770e4605d5c7c319b
amendment   GATE_A1.md  sha256 53aee7afa7b38193fee1d045d43f35720d038fb8b0b6d7851809c0b9969f43b3
            (both recomputed at the start of every step below -- unchanged throughout)
verdict     STOPPED. C4b FAILS: measured ratio 1.0000, threshold <= 0.25.
            The implementation is NOT the cause. C4b's premise is wrong, and the
            measurements below identify what the quantity it bounds actually is.
```

`verification.md` holds the ORIGINAL C4 failure and is not touched. This file is the A1 run.

---

## 1. Criteria measured, in frozen order

| # | criterion | measured | verdict |
| --- | --- | --- | --- |
| C1 | a case file selects reacting species | `cases/reacting_plug_flow` parses; `G8ReactingCaseTest` 12/12 | **PASS** |
| C2 | the parser reaches the configuration | every species, reaction, order, Arrhenius triple and `time_step` asserted value-for-value | **PASS** |
| C3 | the mechanism is instantiated | `reactionCount` 1, `speciesCount` 3/4, net stoichiometry and explicit orders asserted | **PASS** |
| C4a | the CONSTRUCTED state keeps `4*eps*N` | measured in-test over every cell, `<= 3.55e-15` | **PASS** |
| **C4b** | **the SOLVED composition SCALES with the solve's tolerance** | **ratio 1.0000** (ladder below) | **FAIL** |
| C4c | the SOLVED composition is non-negative, exactly | `min_cell min_i Y_i = +1.230741e-03` | PASS (measured) |
| C4d | the absolute figure, reported not gated | `max_cell abs(sum Y - 1) = 5.718198e-08` at cell 37 | reported |
| C5 | dispatch through the production `ProjectRunner` | `grep -c 'setup.reacting'` = **3**; `cfdapp --case` returns a reacting result | PASS (measured) |
| C6 | chemistry actually executes | `max S_E = 2.710138e+04 W/m^3 > 0`; `max abs(dY) = 1.355075e-03 > 1e-12` | PASS (measured) |
| C7 | energy coupling actually executes | `max abs(dT) = 2.710194 K > 1e-9`; T rose in every cell (1002.45 .. 1002.71 from 1000) | PASS (measured, see §5) |
| C8 | outputs expose species / T / reaction quantities | `mass_fraction_A/B/N2`, `temperature_reacting`, `heat_release` in `fields.csv`; `metadata.json` `species` now lists A/B/N2, each `Converged` | PASS (measured) |
| C9 | an invalid mechanism fails clearly | 6 malformed variants, each exit 2 with the offending key/species/reaction named, no `results/` written | PASS (measured) |
| C10 | a chemistry-off case is BITWISE preserved | 3 cases, 9 artefact hashes, **all identical** to `baseline/c10_prefix_hashes.txt` | PASS (measured) |
| C11 | patch compositions are valid | a patch summing to 0.9 refused at construction, with the measured sum | PASS (measured) |
| C12 | no unchecked linear solve added | 4 solves added, each `converged()` checked; status, iterations and residual exported | PASS (measured) |

**The gate's execution rule is "stop at the first failure" and that is C4b.** The criteria after it are
recorded as measured, not as a basis for closing the item. Nothing is marked resolved.

## 2. C4b — the measurement, and the instrument correction it required

### 2.1 First attempt, and why it measured nothing

A1 says *"solve the same case at relative tolerances tol and tol/10"*. Scaling
`relativeTolerance` from `1e-10` to `1e-11` produced a deviation **identical to seventeen digits**:

```text
rel 1e-10   max|sum Y - 1| = 5.718197704229766e-08
rel 1e-11   max|sum Y - 1| = 5.718197704229766e-08
```

The exported metadata says why: the per-species final residuals are `2.26e-13`, `1.78e-13`,
`6.85e-13`, all **below `LinearSolverSettings::absoluteTolerance = 1e-12`**. The absolute tolerance
is what stops these solves, so the relative one never bound and scaling it could not move the
answer. This is exactly the dry-run obligation A1 §5 states, and the identical number is its
finding. The instrument — not the criterion — was corrected: *the solve's tolerance* scaled by ten
means **both** tolerances.

### 2.2 The corrected measurement: a four-point ladder

A ladder rather than the single pair the criterion needs, because a ladder separates solver error
(falls with tolerance) from a fixed defect (does not) without resting on one ratio.

```text
abs/rel          max|sum Y - 1|     ratio      per-species final residual
1e-10 / 1e-8     5.701531280e-08    0.9971     1.24e-11  1.24e-11  2.75e-11
1e-11 / 1e-9     5.718197649e-08    1.0000     2.05e-12  1.50e-12  6.85e-13
1e-12 / 1e-10    5.718197704e-08    1.0000     2.26e-13  1.78e-13  6.85e-13   <- PRODUCTION
1e-13 / 1e-11    5.717923712e-08    0.99995    2.89e-14  3.16e-14  6.84e-14
```

The residuals fall by a factor of **430** across the ladder. The deviation does not move.

```text
C4b requires   deviation(tol/10) / deviation(tol) <= 0.25
measured       1.0000
                                                          *** FAIL ***
```

## 3. What the 5.718e-08 actually is — root-caused, not classified

A1 offered two readings: accumulated linear-solve error, or a conservation defect in the
discretisation. **It is neither.** Three measurements locate it.

### 3.1 It is not chemistry

With `pre_exponential = 0` — no reaction at all — the deviation is `5.718229e-08`, the same number.

### 3.2 It is not cross-species conservation: ONE species reproduces it exactly

A variant with a single carrier, `Y_N2 = 1.0` uniformly, inlet `fixed_value 1.0`, the other two
species identically zero, zero chemistry:

```text
mass_fraction_A    Y0=0    max|dY| = +0.000000e+00
mass_fraction_B    Y0=0    max|dY| = +0.000000e+00
mass_fraction_N2   Y0=1    max|dY| = -5.718229e-08      <- the WHOLE deviation, in one species
```

So the quantity C4b bounds is not a species-coupling error. It is a **single scalar's transport
failing to preserve a uniform field equal to its own Dirichlet boundary value.**

### 3.3 It is the FLOW SOLVER's residual continuity error, scaled by dt

A uniform `Y` is an exact solution of the transport equation only if the convective operator
annihilates a constant:

```text
sum_f mdot_f Y_f  =  Y * sum_f mdot_f  =  Y * (that cell's mass imbalance)
```

SIMPLE delivers discrete continuity only to its own tolerance — this run reports
`global_mass_imbalance = 6.443302e-10`, `continuity residual = 1.099100e-11`. The uniform field
therefore leaves a residual equal to `Y` times the cell imbalance, and the resulting error in `Y` is
that residual over the diagonal, which for this transient solve is `a_P = rho V / dt`:

```text
|dY|  ~  dt * |imbalance_cell| / (rho V)
```

**Prediction, written down before the runs: the deviation is proportional to dt.** Measured, with
chemistry off so only transport is in play:

```text
dt       max|sum Y - 1|      ratio to dt=0.1     dt / 0.1
0.01     7.078734154e-09          0.1238           0.1
0.1      5.718228724e-08          1.0000           1.0
1.0      2.253972294e-07          3.9417          10.0
```

Tenfold smaller `dt` gives an **8.1-fold** smaller deviation — the predicted scaling. The `dt = 1.0`
point saturates, and that is predicted too: `rho V / dt = 0.0025` there is no longer large against
the convective coefficient `mdot_f = rho u A = 0.0025`, so `a_P` stops behaving as `1/dt`. At
`dt = 0.1`, `rho V / dt = 0.025` is tenfold the convective coefficient, the transient term dominates
the diagonal, and the `1/dt` scaling holds — which is the pair that confirms the mechanism.

The spatial profile agrees: the deviation grows monotonically downstream from `4.35e-09` in the first
cell column to `5.72e-08` at `x = 0.875`, then falls at the outlet — an advected, accumulating error,
not a local one.

### 3.4 Where A1's derivation went wrong, exactly

A1 §2 reads: *"`sum_i b_i` is the right-hand side of the equation whose exact solution is the
constant 1, because the inlet composition sums to 1 and the zero-gradient patches contribute
nothing. So the exact discrete solution has `sum_i Y_i = 1` identically."*

That step requires the convective operator to annihilate a constant, i.e. **exact discrete
continuity**. It holds for the differential operator and for a divergence-free discrete flux; it does
not hold for a SIMPLE flux field converged to a finite tolerance. With that term restored, the exact
discrete solution is `1 + O(dt * imbalance / (rho V))`, and the deviation is a property of the FLOW
solution the composition inherits — which scales with the flow's continuity residual and with `dt`,
and not with the species solver's tolerance.

So C4b asks the implementation to make a quantity scale with a knob that does not control it.

## 4. Negative controls — all three A1 controls behave as A1 requires

| id | control | required | measured | verdict |
| --- | --- | --- | --- | --- |
| A1-N1 | the Patankar-split STEADY pass | **C4b FAILS** — deviation does not fall with tolerance | `3.002618400e-01`, `3.002618357e-01`, `3.002618352e-01` across the same ladder — flat. Reproduces the originally recorded `3.00e-01`. | **fails C4b, as required** |
| A1-N2 | the fully explicit STEADY pass | **C4c FAILS** | `min_i Y_i = -2.974231e-01`; its sum closure `4.919333e-07` reproduces the recorded `4.92e-07` | **fails C4c, as required** |
| A1-N3 | the original `4*eps*N` on the SOLVED field | still fails at any tolerance | `5.718e-08` vs `2.66e-15` at every rung of the ladder | **fails, as required** |

**A1-N1 is the control A1 called "the one that matters", and it exposes the residual problem.** It
does fail C4b — but so does production, for the same reason: neither deviation scales. C4b therefore
no longer *discriminates* between an implementation whose closure is `5.7e-08` and one whose closure
is `3.0e-01`, five million times worse. A criterion both the correct and the broken implementation
fail is not measuring the thing it was written to measure.

## 5. A second, independent finding: C6 and C7 have thresholds below this implementation's noise floor

Found while running gate §3's controls N2 and N3, and reported here because it is the same class of
defect as C4b's.

```text
                             production        N2 (A=0)        N3 (h_f=0)
max S_E            [W/m^3]   2.710138e+04      0.0             0.0
max abs(dY)                  1.355075e-03      5.146406e-08    1.355075e-03
max abs(dT)            [K]   2.710194e+00      5.688378e-05    5.688378e-05
min_cell T             [K]   1002.452434       999.999943      999.999943
```

* **N2 does fail C6**, because C6 is a conjunction and its `max abs(omega_i) > 0` clause fails. The
  control is valid. But C6's *other* clause, `max abs(Y - Y0) > 1e-12`, **passes on N2** at
  `5.15e-08` — it is satisfied by the transport inconsistency of §3 alone. On its own that clause
  measures nothing.
* **N3 does NOT fail C7.** C7 requires `max abs(T - T0) > 1e-9 K`; N3 gives `5.69e-05 K`, four orders
  above it. The sign clause is stated as *"the sign matching the mechanism's
  `standardReactionEnthalpy` (exothermic => T rises)"*, and with every formation enthalpy zeroed the
  reaction enthalpy is **zero**, so there is no sign to match and the clause is undefined rather than
  violated. **Gate §5's definition of done item 2 — "N1–N5 each demonstrated to FAIL the criterion it
  guards" — is therefore not met for N3.** Gate §3 names N2 and N3 as "the two that matter".

The `5.69e-05 K` is the temperature transport solve's own inherited error, the same §3 effect in the
energy equation: symmetric about 1000 K (999.999943 .. 1000.000056), i.e. noise, not a response.

The existence thresholds `> 1e-12` and `> 1e-9 K` were derived when the dispatch was a steady solve,
where zeroing the source left the field at its boundary value and the noise floor was effectively
zero. In a split time step the transport half runs regardless, so both thresholds now sit **below**
the floor.

## 6. Gate §5's other items, for completeness

| item | state |
| --- | --- |
| 1. C1–C12 all PASS | **NO** — C4b FAILS |
| 2. N1–N5 each FAIL what they guard | **NO** — N1, N2, N4, N5 do; **N3 does not** (§5) |
| 3. the three regression baselines unchanged | see `regression.md`; `CFDIoTests` is 328, not 325, by three enumerated additions |
| 4. `git diff --check` adds no warning beyond the three CRLF files | **YES** — the only files flagged are `cuda/CMakeLists.txt`, `tests/unit/compressible/CMakeLists.txt`, `tests/unit/gpu/CMakeLists.txt` (TD-018) |
| 5. evidence written | this file, `implementation.md`, `negative_controls.md`, `regression.md`, `summary.md` |
| 6. `DEBT_REGISTER.md` OPEN -> RESOLVED | **NOT DONE, and must not be** |

## 7. What is NOT being done about any of this

No threshold is raised, lowered, re-derived in place or reinterpreted. No criterion is dropped. The
composition is **not** renormalised, **not** clipped, and the carrier is **not** solved as
`1 - sum of the others` — each of which would make C4b pass by hiding the inherited error that §3
measured. `GATE_A1.md` is unchanged, and its sha256 is the same at the end of this run as at the
start.

A derived replacement for C4b, and for C6/C7's thresholds, is proposed in `summary.md` for
authorization. Nothing is frozen until that authorization exists.
