# P17.9 — acceptance gate

**FROZEN before any benchmark was run.** Every threshold below is either exact (boolean/bitwise),
derived from the first-order scheme's own smearing behaviour and scaled with `dx`, inherited
verbatim from an already-frozen gate, or **explicitly labelled empirical** with the reasoning shown.
None is fitted to an observed value, because at the time of freezing no benchmark had produced one.

The frozen `results/p17/gates/P17_GATES.md` assigns P17.9: **G1 G3 G4 G6 G7 G9**.
G7 is this phase's own gate and its text governs everything below.

Configuration: `configuration.md`, frozen with this file.
Reference: `tools/riemann_reference.py`, 60 digits, no CFDApp code, cross-checked against P17.7's
independent derivation and self-checked against eight defining physical properties.

`eps = 2.2204460492503131e-16`.

---

## 0. The exact Sod solution, at the project's gamma — frozen here so it cannot drift

```text
gamma = 1.3998189288947698308   (cp/cv with R = 287.05, cp = 1005)

p_star            0.30313762666080176
u_star            0.92751993507257016      (= the contact speed)
rho_star_left     0.42627989804573955
rho_star_right    0.26560611841121122
a_left            1.1831394376381720
a_right           1.0582320837679303
a_star_left       0.99771942415354125

wave speeds (x/t)                 positions at t = 0.2, x0 = 0.5
  rarefaction head  -1.1831394376     0.26337211247
  rarefaction tail  -0.0701994891     0.48596010218
  contact           +0.9275199351     0.68550398702
  shock             +1.7520928142     0.85041856284
```

These are REPORTED BY THE SCRIPT, not typed from a textbook. A textbook Sod table is computed at
gamma = 1.4 exactly and differs in the fourth digit, which would sit on top of the very errors this
gate measures.

---

## 1. Group W — wave structure must be IDENTIFIED BY MEASUREMENT (G7)

G7: *"The three structures must be identified by MEASUREMENT (locating each wave and comparing its
speed and jump to the exact solution), never by eye and never from a plot."*

| id | criterion | threshold |
| --- | --- | --- |
| W1 | all THREE structures are detected in the numerical Sod profile by the detectors of §2, in the order rarefaction → contact → shock | boolean |
| W2 | the **rarefaction** is a smooth fan: `rho` decreases monotonically across it, and it spans `≥ 3` cells | boolean, DERIVED below |
| W3 | the **contact** carries a density jump but not a pressure or velocity jump | `|Δp|/p_star ≤ 0.05` AND `|Δu|/u_star ≤ 0.05` AND `|Δrho| ≥ 0.5 ×` the exact contact jump |
| W4 | the **shock** jumps in all three of `rho`, `u`, `p`, in the correct directions | boolean |
| W5 | no fourth structure: outside the three detected waves the profile is flat to the plateau tolerance of §3 | boolean |

```text
DERIVATION OF W2's "at least 3 cells". The exact fan spans x/t from -1.18314 to -0.07020, i.e.
0.22259 in x at t = 0.2 -- 44.5 cells at nx = 200. A first-order scheme cannot make a genuine fan
NARROWER than the exact one, so any count below a small fraction of 44 means the fan was not
resolved at all (or was mis-detected as a discontinuity). 3 is a floor on the DETECTOR, not on the
physics, and is deliberately far below 44 so that it tests "a fan exists" rather than "the fan is
accurate" -- accuracy is Group R's job.

W3's 0.05 IS DERIVED FROM THE CONTACT ITSELF, NOT PICKED. A contact is an exact solution with
CONSTANT p and u; the only thing that puts a pressure or velocity signal on it is scheme error. P17.8
measured that error at 7.181e-16 (pressure) and 6.496e-16 (velocity) for an isolated contact -- 14
orders below 0.05. Here the contact sits between a fan and a shock, so it is not isolated and the
bound must allow for the neighbouring waves' tails; 0.05 is a DETECTION threshold for a scheme that
puts an O(1) spurious jump at a contact, which is the failure mode HLLC exists to prevent. Control
V2 (HLL, contact deleted) must break it.
```

---

## 2. Group X — wave POSITIONS (G7's central measurement)

G7: *"shock position — scale with dx; a position error below one cell width is not measurable and
must not be claimed."*

**The detector, fixed here so it cannot be chosen after seeing the answer:**

```text
SHOCK and CONTACT -- the 50 % crossing, linearly interpolated.
  1. take the plateau values on each side from the sampling windows of §3
  2. find the first cell pair (i, i+1) bracketing the midpoint (q_pre + q_post)/2
  3. linearly interpolate between the two cell CENTRES for the crossing abscissa
  quantity: rho for the shock, rho for the contact (p and u are continuous at a contact, so they
  carry no signal there -- using them would be a detector that cannot see the feature)

RAREFACTION HEAD and TAIL -- the first and last cell where the profile departs from the adjacent
  plateau by more than 1 % of the fan's own total density drop, again linearly interpolated.

Every detector operates on the NUMERICAL result alone and is compared to the analytical position
afterwards. No detector is given the expected location (authorization §28).
```

| id | criterion | threshold |
| --- | --- | --- |
| X1 | shock position error | `|Δx| ≤ 2 dx` |
| X2 | contact position error | `|Δx| ≤ 2 dx` |
| X3 | rarefaction-head position error | `|Δx| ≤ 2 dx` |
| X4 | rarefaction-tail position error | `|Δx| ≤ 4 dx`, DERIVED below |
| X5 | every position error must **decrease in absolute terms** under refinement 200 → 400 → 800 | strict, at least 2 of the 3 refinements per wave |
| X6 | no position error may be **claimed** below `1 dx` | reporting rule |

```text
DERIVATION OF THE 2 dx BOUND. A conservative scheme places a shock correctly in the mean (Lax-
Wendroff), but the profile is smeared over a few cells and any crossing-based estimator is displaced
from the true position by at most about half the smear width. The smear width of a first-order
monotone scheme is a small number of cells; 2 dx therefore allows a smear of up to 4 cells with the
crossing at its edge. THE FACTOR 2 IS EMPIRICAL and is labelled as such. What is NOT empirical is
that the bound SCALES WITH dx, which is what G7 demands and what X5 tests directly: an error that is
a fixed fraction of the mesh is a scheme property, an error that stays fixed in absolute terms as the
mesh refines is a bug.

DERIVATION OF X4's 4 dx. The rarefaction TAIL is where the fan meets the star plateau, i.e. where
the density GRADIENT goes to zero. A 1 %-of-drop threshold on a vanishing gradient is intrinsically
less sharp than a 50 % crossing on a jump: the same 1 % of amplitude corresponds to more cells when
the slope is shallow. The exact fan's density falls from 1.0 to 0.42628 over 44.5 cells, so its mean
slope is 0.0129 per cell; 1 % of the 0.5737 total drop is 0.0057, i.e. 0.44 cells of slope -- but the
slope at the TAIL is shallower than the mean, so the detection point is correspondingly less certain.
4 dx is 2x the bound used for the sharp features, for a feature whose defining gradient is
approximately zero. Labelled empirical; scaled with dx like the others.

X6 EXISTS BECAUSE G7 SAYS SO. If a measured error comes out below one cell width, the evidence must
report it as "below the resolution floor of 1 dx" and must NOT quote a smaller number as an accuracy
claim.
```

---

## 3. Group S — plateau STATES, and how they are sampled (G7, §9, §27)

**The sampling policy, fixed here (authorization §27):**

```text
For each plateau, sample the arithmetic mean of the cells in a window that
  * lies between the two detected waves bounding that plateau, and
  * EXCLUDES 5 cells on each side of each detected wave.
If fewer than 5 cells remain, the window is reported as TOO NARROW and the criterion FAILS --
it is never silently widened.
The windows are computed from the DETECTED wave positions, never from the exact ones, so the
policy cannot be tuned by moving a window onto a better-looking region.
```

| id | criterion | threshold |
| --- | --- | --- |
| S1 | `p_star` from the star-left and star-right plateaus | each within `0.10 ×` the pressure jump across the adjacent wave |
| S2 | `u_star` from the same two plateaus | each within `0.10 ×` the velocity jump across the adjacent wave |
| S3 | `rho_star_left` | within `0.10 ×` the contact density jump |
| S4 | `rho_star_right` | within `0.10 ×` the shock density jump |
| S5 | the pre-shock plateau reproduces the undisturbed right state | within `0.02 ×` the right-state value |
| S6 | every one of S1–S4 must **decrease** under refinement 200 → 400 → 800 | at least 2 of 3 per quantity |

```text
WHY THE TOLERANCE SCALES WITH THE JUMP AND NOT WITH THE VALUE. The error in a plateau state is
caused by the neighbouring wave's numerical tail leaking into the sampling window. That leakage is a
fraction of the JUMP the wave carries, not a fraction of the plateau's own magnitude. A tolerance
written as "1 % of p_star" would be a different physical requirement on each side of the contact for
no reason; "10 % of the jump" is the same requirement everywhere. THE 0.10 IS EMPIRICAL and is
labelled so. S6 is the criterion that actually carries the weight: a leakage error must shrink when
the mesh shrinks, and that is a scheme property no tolerance choice can fake.

S5's 0.02 is TIGHTER because the pre-shock region is the UNDISTURBED initial state -- no wave has
reached it. Its only error source is whatever the shock's tail leaks upstream, which for a monotone
upwind scheme is small. This is the criterion most likely to catch an over-diffusive control.
```

---

## 4. Group R — field errors and refinement (G7, §5, §22)

| id | criterion | threshold |
| --- | --- | --- |
| R1 | `L1` error in `rho`, `u`, `p` against the exact profile, cell-volume weighted | REPORTED with the observed trend |
| R2 | each `L1` error **decreases** monotonically over 200 → 400 → 800 | strict, all three quantities |
| R3 | the observed `L1` convergence rate | REPORTED, **not gated on a value** |
| R4 | `L_inf` | REPORTED only, and explicitly NOT gated |

```text
R3 AND R4 ARE REPORTED RATHER THAN GATED, FOR TWO DIFFERENT REASONS, AND BOTH ARE G7's OWN.
  * For a first-order monotone scheme the L1 rate on a solution containing a DISCONTINUITY is not
    1: the classical result for a linear discontinuity is O(dx^{1/2}), while a self-sharpening
    shock is captured in O(1) cells and contributes O(dx). Sod contains a fan (smooth, rate ~1), a
    contact (linearly degenerate, rate ~1/2) and a shock, so the composite rate is between those and
    is not a number to be asserted in advance. Authorization §22: "Report the observed trend rather
    than forcing an order."
  * L_inf near a discontinuity is dominated by the single worst cell inside the smeared layer and
    stays O(jump) no matter how good the scheme is. Authorization §5 says so explicitly. Gating on
    it would fail a correct scheme.
R2 IS THE REAL CRITERION: refinement must improve the integral accuracy. That is mesh-independent,
derivable, and is what "converges to the exact solution" means operationally.
```

---

## 5. Group N — the normal shock and the jump relations (§10–§14, §43)

| id | criterion | threshold |
| --- | --- | --- |
| N1 | `rho2/rho1` against `(g+1)M1²/((g−1)M1²+2)` | `≤ 0.02` relative |
| N2 | `p2/p1` against `1 + 2g/(g+1)(M1²−1)` | `≤ 0.02` relative |
| N3 | `T2/T1` against `(p2/p1)(rho1/rho2)` | `≤ 0.02` relative |
| N4 | `u2/u1` against `rho1/rho2` | `≤ 0.02` relative |
| N5 | `M2` against `[(1+(g−1)/2 M1²)/(gM1²−(g−1)/2)]^{1/2}` | `≤ 0.02` relative |
| N6 | Rankine–Hugoniot **mass** residual `rho1(u1−S) − rho2(u2−S)` | `≤ 0.02` of `rho1|u1−S|` |
| N7 | Rankine–Hugoniot **momentum** residual | `≤ 0.02` of `p1 + rho1(u1−S)²` |
| N8 | Rankine–Hugoniot **energy** residual (total enthalpy in the shock frame) | `≤ 0.02` of `h_t1` |
| N9 | the shock does not drift: its position at the end time | `≤ 2 dx` from its initial position |

```text
THE 0.02 IS EMPIRICAL AND IS THE SAME NUMBER FOR ALL NINE, DELIBERATELY. These are all plateau
measurements of the same structure taken with the same sampling policy, so giving them different
tolerances would imply a physical distinction that does not exist. 2 % is the level at which a
first-order captured shock's plateau states, sampled 5 cells clear of the layer on a 400-cell mesh,
can be read; it is a DETECTION threshold for a wrong jump relation, not an accuracy claim. A scheme
that got the Hugoniot wrong would miss by tens of percent, not by 2.

N6-N8 ARE THE STRONGEST CRITERIA IN THIS GROUP, and authorization §14 says why: they test the jump
conditions the numerical states actually satisfy, rather than comparing two profiles. A scheme can
smear a shock badly and still satisfy Rankine-Hugoniot exactly across the smeared layer, because
conservation forces it to -- so N6-N8 failing means something is wrong with CONSERVATION, which is a
different and more serious defect than inaccuracy.
```

---

## 6. Group C — conservation (G3, G4; §24–§26)

Inherited, not renegotiated.

| id | criterion | threshold |
| --- | --- | --- |
| C1 | closed-domain (wall) cases: `Σ rho V` and `Σ rhoE V` unchanged | `≤ 8 eps × N_cells`, **inherited verbatim from P17.8 G1** |
| C2 | closed-domain momentum against the integrated wall impulse | `≤ 8 eps × N_faces × N_steps ×` the impulse scale, **inherited from P17.8** |
| C3 | open (through-flow) cases: `Δ(Σ Q V) + ∫ boundary flux = 0` for mass, momentum, energy | **inherited verbatim from P17.8 G2** |

```text
G7's own text: "conservation inherits G3's and G4's floors, not a new number." A shock does not
change what conservation means, and inventing a looser bound because a case is harder is exactly
what §35 forbids. If a shock case cannot meet P17.8's conservation floor, that is a FINDING.
```

---

## 7. Group P — positivity through a shock (§20)

| id | criterion | threshold |
| --- | --- | --- |
| P1 | `rho > 0`, `p > 0`, `T > 0` for every accepted state in every benchmark | boolean, every cell, every step |
| P2 | `min rho`, `min p`, `min T`, `min e` are RECORDED for every benchmark | reporting |
| P3 | rejected steps, CFL retries and fallback activations are RECORDED, not hidden | reporting; a nonzero count is a result, not a failure |
| P4 | nothing is clamped | structural: 0 clamp idioms, inherited from P17.8 P4 |

## 8. Group D — determinism (G9; §33)

| id | criterion | threshold |
| --- | --- | --- |
| D1 | Sod, normal shock and moving contact repeated: full field, wave positions, `dt` history, retry counts | **BITWISE**, `EXPECT_EQ` |

```text
Inherited verbatim from P17.8's D1 and from G9, which says BITWISE and "no tolerance". A shock case
has more control flow (retries can fire) than a smooth one, which is precisely why repeating it is
worth doing rather than assuming P17.8's result covers it.
```

## 9. Group V — non-vacuity controls (G7's explicit requirement)

G7: *"a first-order upwind scheme WITHOUT the Riemann flux must FAIL this gate. If it passes, the
gate is not measuring shock capturing. A second, discriminating case is required beyond Sod: Sod
alone can be passed by an over-diffusive scheme that smears everything to roughly the right place."*

| id | planted control | must break | must still pass |
| --- | --- | --- | --- |
| V1 | **Rusanov / local Lax–Friedrichs** flux — the wave structure replaced by a single maximum-speed dissipation, i.e. NO Riemann solver | X2 (contact position), S3, W3 | C1 (it is still conservative) |
| V2 | **HLL** — `hllFluxForControlOnly`, HLLC with the contact wave deleted | W3, X2, S3 | X1 (the shock is still roughly placed) |
| V3 | the exact reference shifted by `+3 dx` | X1, X2, X3 | — (proves the DETECTOR measures position, not that it always returns the exact answer) |
| V4 | the shock detector fed a **monotone ramp with no jump** | must report NO SHOCK rather than a position | boolean |

```text
V1 IS G7's REQUIRED CONTROL, and V2 is the "second, discriminating case" its text demands. The
distinction matters: V1 removes the Riemann structure entirely, V2 removes ONLY the contact wave.
If the gate could not tell those two apart from correct HLLC, it would not be measuring what HLLC
is for. P17.7 already measured what V2 does to a stationary contact -- a spurious mass flux of
243.46 kg/(m^2 s) where the true flux is exactly zero -- so it is known to be a real perturbation
and not a no-op.

V3 AND V4 TEST THE INSTRUMENT RATHER THAN THE SOLVER, and both are needed: V3 shows the detector
reports a displacement when there is one, V4 shows it does not invent a wave where there is none.
A detector that always returns "the shock is at 0.85" would pass every criterion in Group X.
```

## 10. Group A — preservation (G1)

| id | criterion | threshold |
| --- | --- | --- |
| A1 | P13-001 SIMPLE fingerprint | **BITWISE IDENTICAL**, 0-byte diff |
| A2 | every P17.1–P17.8 production source UNTOUCHED | SHA-256, 0 TOUCHED |
| A3 | P17.9 adds NO production source and NO case-format key | structural: 0 |
| A4 | full regression, both configurations | 0 failures, exact counts |

```text
A2 IS STATED AS AN EXPECTATION, NOT A HOPE: P17.9 is a VALIDATION phase. The authorization's own
words are "Do not rewrite EOS / conversion / HLLC / CFL / positivity / density-based solver unless a
P17.9 benchmark exposes a genuine bug." If a benchmark does expose one, A2 fails, the failure is
reported, and the fix goes through an amendment with its own re-verification -- exactly as P17.8 did
when it found P17.7's star-energy sign error. What is forbidden is touching them for convenience.
```

## 11. Execution policy — FROZEN, inherited verbatim

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
5 exclusion patterns, 13 tests, unchanged since P13's G10 closeout
build/qual-final (Release+CUDA+OpenMP) and build/qual-cpuonly (CUDA OFF)
build/qual-debug NOT run -- deferred to P17.11 by standing decision
baseline, the P17.8 close:  Release+CUDA 2732 / 2670     CPU-only 2526 / 2466
the delta is pure arithmetic and is declared in the regression harness before it runs
```

## 12. What P17.9 does NOT claim

```text
G8  CPU/GPU solver equivalence   NOT APPLICABLE. P17.8 implemented NO GPU density-based solver, so
                                 §32's "where implemented" is not met. The HLLC FLUX layer's bitwise
                                 CPU/GPU equivalence is P17.7's and is re-run, not re-claimed.
G10 final qualification          P17.11's.
higher-order accuracy            first order only, by §34.
2D/3D shock structure            every case here is 1D on a 2D mesh. G7's constraint note requires
                                 every claim to say 2D, and they do.
viscous shock structure          Euler only. The shock layer here is NUMERICAL, not physical.
```

## 13. Failure policy

```text
STOP at the first failing criterion, in the frozen order W -> X -> S -> R -> N -> C -> P -> D -> V.
Preserve the log. Diagnose. Fix the CODE, never the criterion.
No threshold here may be changed to obtain a PASS. Post-freeze corrections go in an AMENDMENT_*.md
with the reason and the measurement, and this file is left untouched.
A criterion-to-test coverage map with an "asserted?" column is required in coverage_map.md, kept
OUTSIDE this frozen file so completing it cannot move this hash.
```
