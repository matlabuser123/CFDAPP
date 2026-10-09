# P16 — Driver & Seegmiller separated-flow validation: **FAILED**

Written for: the person deciding whether `k-omega SST` can close. It cannot.

Methodology frozen before this case existed:
`../acceptance/separated_flow_methodology.md`. Nothing below was changed after
seeing a result except the iteration budget, and §3 explains exactly why that
was a measurement fix rather than tuning.

---

## Result

Driver & Seegmiller (1985) / NASA TMR 2D BFS. Re_H = 36,000, ER = 1.125,
reference **x_r/H = 6.26**. All three meshes **converged** (continuity ~2e-11).

| mesh | cells | x_r/H | **E_x_r** | mean y⁺ | faces outside band | floored | max μ_t | iters |
|---|---|---|---|---|---|---|---|---|
| coarse | 1080 | 6.509 | **0.0398** | 251.4 | **45** | 0 | 6.284e-3 | 7562 |
| medium | 2430 | 8.502 | **0.3582** | 181.6 | 2 | 0 | 7.994e-3 | 7988 |
| fine | 4320 | 9.438 | **0.5076** | 141.1 | 2 | 0 | 8.136e-3 | 6219 |

**Verdict: FAIL.** The frozen methodology capped any derivable threshold at
`E_x_r = 0.25`; the two better-resolved meshes are at 0.36 and 0.51.

Two failures, either of which alone is disqualifying:

1. **Accuracy.** The converged medium and fine meshes over-predict reattachment
   by 36 % and 51 %.
2. **Mesh stability.** §6 requires x_r/H to settle. It does not — 6.51 → 8.50 →
   9.44 moves **monotonically away** from the reference, with a medium→fine
   shift of 0.94 step heights.

### The coarse mesh is not a passing result

`E_x_r = 0.0398` on the coarse mesh looks excellent and **must not be reported
as a pass**:

- it has **45 wall faces outside the G1 band**, so it fails the precondition;
- it is the least resolved mesh in a sequence that has not converged, so its
  agreement is a coincidence of under-resolution, not accuracy;
- selecting it would be choosing the mesh that flatters the model after seeing
  the results — the exact move the frozen methodology exists to prevent.

---

## The convergence question, settled before interpreting anything

The first run capped every mesh at 3000 iterations and **none converged**, so
none of those numbers could be interpreted at all. A diagnostic with a 12000
budget settled it:

```
budget 12000, medium mesh:
  converged=1  iters=7988  u=9.999e-06  p=4.943e-04  cont=1.991e-11  x_r/H=8.5021
```

So the case needs ~8000 outer iterations, and **convergence changes the answer
by 0.09 step heights** (8.594 unconverged → 8.502 converged). The budget was
raised to 12000 and the sequence re-run. That made the measurement *valid*; it
did not make the model *pass*, and it was not tuning — the diagnostic
established the conclusion was unchanged before the budget was touched.

---

## Cause: attributed to the declared deviation, per the frozen §7

§7 required a miss to be attributed to the declared deviations **before** it is
attributed to the model, and to say which it concluded and why.

**Prime cause: no upstream development section** — flagged in §7 as the most
significant deviation before any run. The domain starts at the step, so the
shear layer's initial state is *prescribed* (a 1/7 power law plus a free-stream
k/ω estimate) rather than developed over an upstream wall. Reattachment length
is strongly sensitive to the shear layer's initial momentum thickness and
turbulence level; too little turbulence at the lip gives a layer that mixes too
slowly and reattaches too far downstream — the direction **and** the magnitude
seen here.

**The refinement trend supports that reading.** A discretisation error converges
to a value as the mesh improves. This moves monotonically *away* from the
reference, which is the signature of a boundary-condition error being resolved
more faithfully: the coarse mesh's numerical diffusion was accidentally
supplying the mixing the inlet condition does not.

**What this does not establish.** A setup deviation this large makes the case
unable to test the model. It is equally consistent with a correct SST and with
an SST that over-predicts reattachment, and **no claim is made either way.**
Saying "the deviation explains it" would be as unfounded as saying "the model is
wrong". The case cannot currently distinguish them, and that is the finding.

Secondary contributors, not separately quantified:
- uniform Cartesian mesh — resolving the shear layer forces the whole 9H channel
  to be refined, so no mesh here is well resolved where it matters;
- inlet k/ω from a free-stream intensity estimate, not measured profiles.

---

## Consequence for P16

**`k-omega SST` does NOT close.**

| Evidence | Required | State |
|---|---|---|
| Attached wall (Re_τ = 590) | E_Reτ ≤ 0.059, E_u⁺ ≤ 1.4, G1 valid | **PASS** — 0.0003 / 0.478 |
| Attached wall (Re_τ = 180 stress) | E_Reτ ≤ 0.22, E_u⁺ ≤ 3.9 | **PASS** |
| Mechanism discrimination | controls detected | **PASS** — internal Re_h = 5000 step |
| **Separated flow, named reference** | credible + mesh-converged | **FAIL** |

No SST separated-flow threshold is derived. Deriving one from data that fails
its own pre-declared cap would be manufacturing a gate to fit the result.

---

## What would fix the case

In the order I would attempt them:

1. **Add an upstream development section** — an L-shaped domain: `[-8H, 0] × [H, 9H]`
   joined to `[0, 30H] × [0, 9H]`. CFDApp has multiblock
   (`cases/*_multiblock`), so this is feasible, but it is real work, not a
   parameter change. This directly removes the prime suspect.
2. **Graded wall-normal mesh** so the shear layer can be resolved without
   refining the entire channel height.
3. **Only then** re-derive the threshold. If the case still misses by ~35 %
   with a developed inlet, that is evidence about the *model*, and worth having.

---

## What was preserved

- The internal **Re_h = 5000** step, unchanged, as the qualitative
  mechanism-discrimination benchmark. No threshold was retrofitted onto it.
- Both channel sweeps and their gates.
- The pre-P16 baseline.
- This failure, with its evidence, rather than a tuned pass.

---

## Evidence

| Path | Contents |
|---|---|
| `../acceptance/separated_flow_methodology.md` | Frozen before the case existed |
| `driver_seegmiller/sweep.csv` | The three converged meshes |
| `driver_seegmiller/<mesh>/wall.csv` | C_f(x), C_p(x) per mesh |
| `driver_seegmiller/<mesh>/outlet_profile.csv` | u/U_ref at the outlet |
| `../logs/ds2.log`, `../logs/ds3.log`, `../logs/dsdiag2.log` | Raw runs incl. the convergence diagnostic |
