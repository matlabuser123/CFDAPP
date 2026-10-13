# P17.8 AMENDMENT — two Group N predictions that algebra, not the implementation, decides

`acceptance_gate.md` is **unchanged** (sha256 `a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e`).
No threshold, criterion or prediction in it has been edited. Its §15 requires post-freeze
corrections to be recorded here instead, and this is that record. Both original predictions stand
on the page exactly as they were frozen.

The controls themselves are in `tools/controls.sh`; the run is `logs/10_controls.log`.

---

## 1. What happened

Group N lists, for each planted defect, the criteria it **must break** and the ones it **must still
pass**. Two of those predictions turned out not to be statements about the implementation at all.
They are settled by the algebra of the cases the criteria run on, and no correct or incorrect solver
could change either outcome.

```text
control  gate prediction              measured        why
-------------------------------------------------------------------------------------------------
N2       must break C1                C1 PASSES       C1's residual is identically zero
N2       must break G1                G1 PASSES       G1's mesh has uniform cell volumes
N3       must still pass K3           K3 FAILS        with no acoustic term a cell at rest has no
                                                      signal speed at all, so no dt exists
```

## 2. N2 versus C1 — undetectable in principle

N2 deletes the `1/V` from the update, so `Q_new = Q - dt R` instead of `Q - (dt/V) R`.

C1 is uniform flow. A uniform state's residual is **identically zero** — that is criterion R4, and
it is measured at exactly `0.000e+00`. The update is therefore

```text
  Q_new = Q - scale * 0 = Q          for ANY scale whatsoever
```

so C1 returns the same answer under every possible `1/V` error, including deleting it entirely.
**No implementation of C1 can detect N2.** This is not a coverage gap that a better test would
close; it is the case being orthogonal to the defect. The prediction was mis-derived at freeze time.

## 3. N2 versus G1 — a REAL coverage gap, and it is now closed

This one is different, and it is the finding worth having.

G1 is closed-domain conservation on `createCartesian2D(10, 8, 1.0, 0.8)`, where **every cell has the
same volume `V`**. The conservation statement is

```text
  correct:   sum_P (Q_new - Q) V_P = -dt sum_P (R_P / V_P) V_P = -dt sum_P R_P       = 0
  with N2:   sum_P (Q_new - Q) V_P = -dt sum_P  R_P      V_P   = -dt V sum_P R_P     = 0
```

`sum_P R_P` is zero for a closed box because every internal face contributes `+F` and `-F`. With
uniform volumes the constant `V` factors straight out of the second line, so **both expressions
vanish** and G1 passes either way. The uniform mesh made the two algebraically identical.

This is the same shape as P17.7's star-energy defect, recorded in
`results/p17/hllc/AMENDMENT_star_energy_sign.md` §8: *a criterion's non-vacuity is not the same as
its coverage*. G1 is genuinely non-vacuous — N1 and N4 both break it — but its case could not reach
the term N2 corrupts.

```text
CLOSED BY A NEW TEST, not by relaxing anything:
  DensityBasedConservationTest.G1_ClosedDomainConservesOnAGradedMeshToo
A GRADED mesh (volume ratio > 2 across the domain, asserted) makes sum_P R_P V_P no longer
proportional to sum_P R_P, so the cancellation is real rather than algebraic. It DETECTS N2, and it
passes on the correct implementation: mass 1.132e-16, energy 4.303e-16 relative.
```

`U1` was blind to N2 for a closely related reason and has been **strengthened in place**, not
replaced: it built its mesh as `createCartesian2D(3, 2, 3.0, 2.0)`, giving `dx = dy = 1` and
therefore a cell volume of **exactly 1.0**, so `dt/V` was bit-identical to `dt` in a test whose own
name is `U1_UpdateIsExactlyQMinusDtOverVTimesR`. It now runs on a graded mesh, asserts that no cell
has unit volume and that the volume spread exceeds 1.5, and adds the decisive statement — the change
in `Q` scales as `1/V` between two meshes, measured against their actual volumes. It detects N2.

## 4. N3 versus K3 — extra detection, not a weakened criterion

N3 deletes the `+ a_f` acoustic term, leaving `dt_P = CFL V_P / sum_f |u.n| A_f`.

K3/K6 includes an explicit **zero-velocity** edge case. With the acoustic term deleted, that cell's
face-speed sum is exactly `0`, there is no finite positive `dt`, and `localTimeSteps` throws. So K6
fails.

That is the solver behaving correctly: a cell at rest in a medium with no sound speed has no signal
speed, and `dt = CFL V / 0` is not a number. The gate predicted K3 would still pass, which assumed
the advective term alone keeps `dt` finite — true for every case except the one K6 exists to cover.

```text
NOTHING IS WEAKENED BY THIS. A control breaking MORE than predicted strengthens the instrument: it
shows the acoustic term is load-bearing in two independent ways (the CFL/Co consistency relation K4,
and the existence of dt at rest), rather than one. The required detection -- K4 -- fired.
```

## 5. What is NOT claimed

```text
* No threshold was changed, relaxed or rescaled. acceptance_gate.md's sha256 is unmoved.
* No criterion was removed from Group N. All seven controls still run, and all seven still have to
  break what they can break.
* C1/N2 and G1/N2 are reported as REPORT ONLY in controls.sh with this file named, so the original
  prediction and the measured outcome are both visible in the log rather than one replacing the
  other.
* The G1 gap was closed by ADDING a case, and the U1 gap by STRENGTHENING an assertion. Neither
  existing test lost an assertion.
```
