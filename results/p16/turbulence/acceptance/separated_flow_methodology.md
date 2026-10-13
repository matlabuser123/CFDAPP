# P16 — separated-flow validation methodology (FROZEN before results)

Written for: a reviewer checking that the SST separation gate was defined
before the run that has to pass it.

**Frozen 2026-09-23, before the case exists.** No Driver–Seegmiller run has been
performed in CFDApp at the time of writing.

---

## 1. The reference case

**Driver, D. M. & Seegmiller, H. L. (1985),** *"Features of a reattaching
turbulent shear layer in divergent channel flow"*, AIAA Journal 23(2), 163–171.
This is the case the **NASA Turbulence Modeling Resource 2D Backward Facing
Step** is built on, which is one of the two options named in the brief.

| Parameter | Reference value |
|---|---|
| Step height | H |
| Expansion ratio | 1.125 (inlet 8H, outlet 9H) |
| Re_H = U_ref H / ν | 36,000 |
| Mach | 0.128 |
| Inlet boundary-layer thickness at the step | δ/H ≈ 1.5 |
| **Reattachment** | **x_r/H = 6.26** |

---

## 2. What reference data I actually have — stated before use

This determines what can be *gated* and what can only be *recorded*, and it is
the most important section of this document.

| Quantity | Available to me | Use |
|---|---|---|
| **x_r/H = 6.26** | **Yes** — a single, universally quoted value I am confident in | **GATED** |
| Geometry, ER, Re_H | Yes | Case definition |
| C_f(x) distribution | **No** — I cannot access the tabulated dataset | Recorded + **structural** checks only |
| C_p(x) distribution | **No** — same | Recorded + **structural** checks only |
| u/U_ref profiles | **No** — same | Recorded + **structural** checks only |
| Reynolds shear stress | **No** | Not attempted |

**I will not compare against numbers I cannot verify.** Writing a plausible
C_f(x) table from memory and gating against it would manufacture agreement, and
it is precisely the kind of thing a reviewer cannot detect afterwards. So
C_f, C_p and the velocity profiles are **exported as evidence** and checked for
*structure a correct separated flow must have* — not for numerical agreement
with a dataset this environment cannot reach.

If the tabulated data is supplied later, the recorded distributions can be
compared against it without re-running anything.

---

## 3. Gated quantity

$$
E_{x_r}=\frac{\left|x_{r,\mathrm{CFD}}-6.26\right|}{6.26}
$$

**The threshold is not set here.** Per the same procedure used for the channel
gates: run the mesh sequence first, identify mesh-stable results, measure
repeatability, then derive the threshold from evidence. It is recorded after
the sweep in `separated_flow_results.md`.

What *is* frozen now is the **upper bound**: a published RANS study of this case
that could not place reattachment within ~25 % would not be considered credible,
so any derived threshold above `E_x_r = 0.25` means the model has failed rather
than that the threshold needs raising.

---

## 4. Reattachment detection — frozen NOW

Detected by the **existing** `cfd::validation::step::separationTopology`,
unchanged: the downstream-most negative→positive sign change of the bottom-wall
shear, located by linear interpolation between the two straddling samples.

This detector already exists, already has its own unit tests
(`BackwardFacingStep.ReattachmentDetection`), and was written for the laminar
case long before P16. Reusing it unchanged is deliberate — **the detector must
not be chosen or tuned after seeing the answer**, and the surest way to
guarantee that is to use one that predates the question.

---

## 5. Structural checks (not quantitative gates)

A correct separated flow must show all of these. They are cheap, they are
falsifiable, and none of them requires the reference tables:

| Check | Why it is not vacuous |
|---|---|
| C_f < 0 somewhere in the bubble | a flow that never reverses has not separated |
| C_f crosses zero exactly once downstream of the bubble | a detector finding many crossings is reading noise |
| C_f recovers to positive and stays positive to the outlet | reattached flow does not re-separate here |
| C_p rises monotonically through reattachment | the pressure recovery is the defining feature of the bubble |
| u/U_ref at the outlet is everywhere positive | the flow has recovered |
| max μ_t finite and > 10 μ | the turbulence model is actually active |

---

## 6. Mesh refinement

At least coarse / medium / fine, each recording:

```
cell count, near-wall y+, x_r/H, E_x_r, C_f and C_p distributions,
outlet velocity profile, iterations, final residuals, continuity,
max mu_t, floored wall-viscosity faces, deterministic repeat
```

**Mesh stability** is required: `x_r/H` must change by less than half the
derived threshold between the two finest meshes. A single-mesh result is not
evidence, which is the brief's own requirement.

---

## 7. Deviations from the reference — declared before running

CFDApp cannot reproduce every aspect of the experiment. Each deviation is
stated now so none can be discovered retrospectively and explained away:

| Deviation | Reason | Expected effect |
|---|---|---|
| **No upstream development section.** The domain starts at the step. | The existing mesh capability is a rectangle with a split left boundary; an L-shaped domain needs multiblock, which is out of this milestone's scope. | The inlet boundary layer is *prescribed* rather than developed. This is the most significant deviation and directly affects x_r/H. |
| **Prescribed 1/7-power-law turbulent inlet profile**, scaled to δ/H ≈ 1.5. | Best available approximation to the reference's developed boundary layer without an upstream section. | Closer than the laminar parabola the existing harness uses; still an approximation. |
| **Incompressible.** | CFDApp's SIMPLE path. M = 0.128 is routinely computed as incompressible for this case. | Negligible. |
| **Straight top wall.** | The reference tunnel has a slight top-wall divergence in some configurations. | Small. |
| **Uniform Cartesian mesh.** | No graded generator for this geometry. | Wall-normal resolution is tied to the core resolution; y⁺ is what it is and is recorded, with G1 applied. |
| **2-D.** | CFDApp turbulence is 2-D only (`requireTwoDimensional`). | The reference is nominally 2-D at the centreline. |

The no-upstream-section deviation is serious enough that **a failure to match
x_r/H must be attributed to it before it is attributed to the model**, and the
evidence write-up has to say which it concluded and why.

---

## 8. What would invalidate this gate

- **Changing the detector after seeing x_r/H** — §4 exists to prevent it.
- **Setting the threshold to whatever the model achieved** — §3 defers it to
  measurement and caps it at 0.25.
- **Gating on C_f/C_p against remembered numbers** — §2 forbids it explicitly.
- **Validating on one mesh** — §6 requires demonstrated mesh stability.
- **Declaring a deviation only after it is needed as an excuse** — §7 is why
  they are listed now.

---

## 9. Relationship to the Re_h = 5000 case

The existing internal step case is **preserved and not retrofitted with a
threshold**. It was never calibrated to a named dataset, so it stays what it
is: a **qualitative mechanism-discrimination benchmark**, and a good one — it
detects limiter, cross-diffusion and blending failure. The two cases answer
different questions and both are kept.
