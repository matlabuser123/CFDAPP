# P16 — acceptance methodology (FROZEN before implementation)

Written for: a reviewer checking that the P16 wall-treatment gate was designed
before the implementation that has to pass it.

**Frozen 2026-09-23, before any wall-function production code exists.** What is
frozen here is the *methodology* — the quantities, the mesh requirement, the
derivation procedure and the non-vacuity obligation. The numeric thresholds are
deliberately **not** frozen yet: the brief requires them to be derived from
measurement, and picking them now would be picking them to pass.

The order matters and is the standing rule for this repository: freeze the
gate, then implement, then derive the numbers from evidence, then re-check that
the gate still detects what it was built to detect.

---

## 1. What is being gated

Fully developed turbulent plane channel flow at **Re_τ = 180**, the standard
low-Re DNS benchmark (Kim, Moin & Moser 1987), which the existing harness
already targets.

Three metrics, because Re_τ alone is not enough — a model can land on the right
wall shear with a wrong profile, and a right profile with a wrong wall shear.

### G1 — wall-resolution validity (a precondition, not a score)

$$
y_1^+ \in [30,\,300]
$$

for the high-Re wall-function path, where `y_1^+` is the first-cell-centre
value at every wall face.

This is a **precondition**: outside that band the standard wall function is
being used where its own derivation does not hold, and any agreement would be
accidental. A run violating it is rejected, not scored. (A low-Re path, if one
is adopted, carries the converse requirement `y_1^+ ≲ 1`; the gate records
which path a run claims and applies that path's band.)

### G2 — friction Reynolds number

$$
E_{Re_\tau}=\frac{\left|Re_{\tau,\mathrm{CFD}}-Re_{\tau,\mathrm{ref}}\right|}{Re_{\tau,\mathrm{ref}}},
\qquad
Re_{\tau}=\frac{\rho u_\tau \delta}{\mu},
\qquad
u_\tau=\sqrt{\tau_w/\rho}
$$

with `Re_{τ,ref} = 180` and `δ` the channel half-height.

### G3 — velocity profile in wall units

$$
E_{u^+,L_\infty}=\max_i\left|u_i^+-u_{\mathrm{ref},i}^+\right|,
\qquad
u^+=\frac{U}{u_\tau}
$$

evaluated over the cells whose `y^+` lies in the log layer, against

$$
u^+_{\mathrm{ref}}=\frac{1}{\kappa}\ln y^+ + B .
$$

`κ` and `B` are taken from the constants the adopted wall function itself uses,
not chosen independently — a gate that compares a model against a different
log law than the one it was built from measures the disagreement between two
conventions rather than the quality of the solution.

`L_∞` rather than `L_2`: the existing harness reports both, and `L_2` over eight
samples dilutes a single bad point. The worst point is what a profile plot shows.

### Supporting quantities — recorded, not gated

Wall shear `τ_w` (and its top/bottom asymmetry), friction factor
`f_D = 8τ_w/(ρU_b²)`, `k`, `ε`/`ω` ranges, iteration count, and the continuity
residual. Recorded at every resolution so a later regression can be diagnosed,
but not themselves acceptance criteria — adding a criterion whose correct value
is not independently known would be adding a number to freeze, not a gate.

---

## 2. How the thresholds will be derived

In this order, after the implementation works:

1. **Run a wall-normal refinement sweep** at several first-cell spacings
   spanning the valid band, plus at least one deliberately outside it to confirm
   G1 rejects.
2. **Identify the valid range** — the subset of resolutions where `y_1^+` lands
   in `[30, 300]`.
3. **Compare against the reference** over that subset, computing G2 and G3.
4. **Measure repeatability and numerical noise**: repeat a configuration and
   record the spread across runs, the same way P13-007's benchmark threshold was
   derived from measured cross-session noise rather than from a single
   measurement.
5. **Set each threshold** from the worst value observed over the valid range,
   plus the measured noise margin. The threshold is a derived quantity with its
   derivation recorded, not a preference.

**Explicitly prohibited:** choosing a threshold because the implementation
passes it. If the implementation cannot meet a threshold the evidence supports,
the finding is that the implementation is not good enough — not that the
threshold was too strict.

---

## 3. Non-vacuity — the dry-run, performed now

A gate the pre-P16 baseline already passes would prove nothing. This gate is a
**detection** gate: it must fail against the unchanged models. Dry-run against
the recorded baseline (`../pre_p16_baseline/channel_flow_verbatim/`, preserved
verbatim), 64×16, Re_bulk 5600:

| Criterion | k-ε baseline | SST baseline | Detects? |
|---|---|---|---|
| **G1** `y_1^+ ∈ [30,300]` | **7.50** | **8.21** | **YES** — both far below 30 |
| **G2** `E_{Re_τ}` | **0.333** | **0.270** | **YES** for any threshold < 0.270 |
| **G3** `E_{u^+,L_∞}` | **14.56** | **9.22** | **YES** for any threshold < 9.22 |

All three criteria fail on the baseline before any threshold is chosen, and G1
fails on a pure mesh-geometry fact that no threshold choice can soften. So the
gate cannot be satisfied vacuously by the current implementation, whatever
numbers step 2 produces.

This also bounds the derivation from above: **G2 must end below 0.270 and G3
below 9.22**, or the new gate would be weaker than the historical one it
replaces on the models it is meant to improve.

---

## 4. Relationship to the historical thresholds

The pre-P16 gate is preserved, not deleted:

```
kOuterL2ThresholdCoarse  = 18.0    kReTauErrorThresholdCoarse = 0.45
kOuterL2ThresholdMedium  = 15.0    kReTauErrorThresholdMedium = 0.40
kOuterL2ThresholdFine    = 12.0    kReTauErrorThresholdFine   = 0.32
```

Those gated **regression** on a 64×16 mesh — they answered "did this get worse",
which is a real question, and they answered it honestly (their own comment says
so). They did not gate physics: a 40 % Re_τ error passed.

The new gate answers a different question on a different mesh, so it **replaces**
rather than loosens. The historical numbers and the runs that produced them stay
in `pre_p16_baseline/` verbatim as the record of what the models did before P16.

---

## 5. What would invalidate this gate

Recorded so a later reader can check rather than trust:

- **a criterion the baseline passes** — none do, per §3;
- **a threshold set after seeing the implementation's result**, rather than
  derived per §2;
- **a reference profile taken from the implementation itself** — G3 compares
  against the analytical log law with the model's own κ and B, never against a
  stored CFD result;
- **G1 applied as a score instead of a precondition** — a run outside the band
  must be rejected, not given partial credit;
- **the sweep run at a single resolution** — step 1 requires several, including
  one outside the band.

---

## 6. Status

| Item | State |
|---|---|
| Metrics G1–G3 | **FROZEN** |
| Mesh-validity precondition | **FROZEN** |
| Derivation procedure | **FROZEN** |
| Non-vacuity dry-run | **DONE** — all three detect, §3 |
| Numeric thresholds | **NOT YET SET** — derived after implementation, per §2 |
| Upper bounds on those thresholds | G2 < 0.270, G3 < 9.22 (from §3) |
