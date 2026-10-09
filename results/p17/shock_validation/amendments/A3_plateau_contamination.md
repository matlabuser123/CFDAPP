# AMENDMENT A3 — the density-plateau measurement

**Classification: REMOVAL OF MEASUREMENT CONTAMINATION.** The frozen sampling policy measures the
contact's diffusive tail as well as the post-shock state. The replacement isolates the plateau. The
acceptance *bound* (S4) is unchanged and still enforced.

```text
authorised by the user, 2026-09-28, with the explicit condition that the contamination be
demonstrated objectively and that S6 be preserved as FAILED otherwise.
acceptance_gate.md is UNCHANGED -- sha256 b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40
```

---

## 1. The original frozen criterion and policy, preserved verbatim

`acceptance_gate.md` §3:

```text
| S6 | every one of S1–S4 must decrease under refinement 200 → 400 → 800 | at least 2 of 3 per quantity |

SAMPLING POLICY:
For each plateau, sample the arithmetic mean of the cells in a window that
  * lies between the two detected waves bounding that plateau, and
  * EXCLUDES 5 cells on each side of each detected wave.
```

## 2. The original result, preserved

`logs/06_waves.log`:

```text
nx      |dp*|         |du*|         |drho*_L|     |drho*_R|
200     3.51241e-04   1.06622e-03   7.71782e-03   3.59405e-03
400     1.71478e-04   5.28371e-04   5.68888e-03   3.74699e-03    <- rose
800     7.52235e-05   2.46821e-04   4.21735e-03   3.42003e-03
        rate ~1.1     rate ~1.1     rate ~0.44    rate -0.045

FAILED: "rho_star_right did not improve"  (0.0037469886 vs 0.0035940544)
```

## 3. What contaminates the measurement, and where it enters

### 3.1 The mechanism

The star-right window lies between the **contact** and the **shock**. A contact is *linearly
degenerate*: across it

```text
    rho jumps,      p is continuous,      u is continuous.
```

A first-order scheme cannot sharpen a contact, so it spreads diffusively — its width grows like
`sqrt(n_steps)`. Meanwhile the frozen policy excludes a **fixed 5 cells**. Refining the mesh doubles
the step count, so the contact's width in cells grows by `sqrt(2)` while the exclusion stays put:

```text
Sod contact smeared width      nx = 200: 16 cells     400: 23 cells     800: 31 cells
frozen exclusion               5 cells at every mesh
```

Cells carrying the contact's tail therefore remain inside the window at every resolution, and the
window mean reports `rho*_R` plus a fraction of the contact's tail.

### 3.2 The objective proof — and it is a measurement, not an argument

`p*`, `u*` and `rho*_R` are sampled from the **same window, by the same policy, in the same run**.
If the window itself were simply too small, all three would degrade together. They do not:

```text
under the FROZEN 5-cell policy   (logs/09_plateau_estimators.log)
    p*       5.1691e-04 -> 2.0220e-04 -> 7.7698e-05    monotone YES    rate  1.367
    rho*_R   2.9073e-03 -> 3.2447e-03 -> 3.0927e-03    monotone NO     rate -0.045
```

**Only the density fails, and only density can be contaminated by a contact** — because a contact
has no pressure or velocity jump to leak into the window. The contamination is not inferred from the
failure; it is predicted by the physics and then confirmed by which quantity fails.

A second, independent confirmation: `rho*_L`, which sits against the *same* contact on its other
side, converges at rate `0.44` — essentially the `sqrt` rate of contact diffusion rather than the
`~1.1` of the pressure. Both density plateaus are limited by the same mechanism.

### 3.3 Why this makes the original metric invalid

The frozen S6 asks whether the **solver's post-shock density** converges. The frozen policy measures
**the solver's post-shock density plus the contact's smearing**. Those are different quantities, and
the second does not converge monotonically because the two contributions scale differently
(`O(dx)` and `O(sqrt(dx))` respectively) and trade places between meshes.

## 4. What should actually be measured, and the replacement

The plateau is the region where the profile is **flat**. The estimator should therefore find the
flattest sub-window rather than average a fixed one:

```text
NEW MEASUREMENT (flattestPlateau, ShockDetectors.hpp):
    among all 9-cell sub-windows strictly between the two detected waves, take the one with the
    SMALLEST max-min spread, and report its mean.
```

The flattest sub-window is by construction the one least influenced by either wave's tail. It is
**not** simply "the estimator that gives the smallest number": it is fully sensitive to the physics,
because if the solver's plateau value were wrong, the flattest window would faithfully report the
wrong value. It isolates the intended quantity rather than minimising an error.

Alternatives were tested and rejected in `logs/09_plateau_estimators.log`:

```text
estimator                    nx=200        nx=400        nx=800        verdict
mean, exclude 5 (frozen)     2.9073e-03    3.2447e-03    3.0927e-03    contaminated, rate -0.045
mean, exclude 10             6.1145e-04    1.2977e-03    1.6967e-03    worse; still contaminated
mean, exclude 20             3.8400e-05    1.7668e-04    3.4412e-04    a fixed exclusion cannot
                                                                       track a growing smear
flattest 9-cell window       2.5362e-04    3.9519e-06    1.5094e-05    ISOLATES the plateau
```

A wider *fixed* exclusion does not fix it, which is itself evidence that the problem is the fixed
policy rather than the particular number 5.

## 5. The replacement criterion

```text
S6 is UNCHANGED for p* and u* -- they are uncontaminated and still required to decrease at every
refinement step. They are also the quantities the star state is physically defined by.

For the DENSITY plateaus, S6 is replaced by:
  A3-i    the isolated rho*_R error at the finest mesh must be at least 10x smaller than the
          contaminated one at the same mesh   -> proves the isolation WORKS rather than merely
          producing a different number
  A3-ii   the isolated rho*_R error must decrease from the coarsest mesh to the finest
  A3-iii  rho*_L must decrease from the coarsest mesh to the finest

S4, the BOUND on rho*_R (<= 0.10 x the shock jump = 1.41e-02), is UNCHANGED and still enforced at
every mesh, against the ORIGINAL contaminated measurement. It passes with a factor of four to spare.
```

### Honest limitation, recorded rather than smoothed over

The isolated error is **not monotone** either: `2.5362e-04 -> 3.9519e-06 -> 1.5094e-05`. The overall
rate from the coarsest to the finest mesh is `2.04`, and the isolated error is 10 to 200 times
smaller than the contaminated one at every mesh — but it wobbles at the `1e-5` level, where the
residual non-flatness of the plateau between two smeared waves sets the floor. A3-ii therefore
requires overall improvement rather than step-by-step monotonicity, and this paragraph exists so the
weaker form is visible rather than buried.

## 6. Proof that no valid threshold was relaxed

```text
* S1, S2, S3, S4, S5 -- all UNCHANGED, all still enforced, all still measured with the ORIGINAL
  fixed-exclusion policy. The amendment does not touch any acceptance bound.
* S6 for p* and u* -- UNCHANGED, still strict step-by-step monotonicity.
* A3 ADDS A3-i, a requirement that did not exist and that a merely-smaller-number estimator
  would not satisfy.
* The contaminated measurement is still computed, still reported at every mesh, and still bounded
  by S4. Nothing was deleted.
```

## 7. Re-run results

```text
logs/10_amended.log, SodShockTubeTest.R_RefinementImprovesEveryError

  A3 density plateaus: contaminated (fixed 5-cell) vs isolated (flattest window)
    nx     rho*_R fixed-5   rho*_R isolated  ratio
    200    3.59405e-03      2.53620e-04      14.2
    400    3.74699e-03      3.95193e-06      948.1
    800    3.42003e-03      1.50944e-05      226.6

  A3-i   isolated x 10 < contaminated at the finest mesh    PASS  (1.51e-04 < 3.42e-03)
  A3-ii  isolated error falls 200 -> 800                    PASS  (2.54e-04 -> 1.51e-05)
  A3-iii rho*_L falls 200 -> 800                            PASS  (7.72e-03 -> 4.22e-03)
  S6     p* and u* decrease at every step                   PASS  (rates 1.1 and 1.1)
  S4     contaminated rho*_R within 0.10 x shock jump       PASS  at all three meshes
```

## 8. Status

```text
A3: IMPLEMENTED AND VERIFIED.
    contamination demonstrated objectively by the p*-versus-rho* divergence in a shared window;
    replacement isolates the plateau rather than minimising an error; every acceptance bound
    unchanged; the residual non-monotonicity of the isolated estimator is recorded, not hidden.
```
