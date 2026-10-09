# P16 — Driver & Seegmiller / NASA TMR 2D BFS: reference specification audit

Written for: whoever has to judge whether the CFDApp setup reproduces the
reference, and which parts of it rest on data I could not obtain.

Recorded **before** any change to the case or to production turbulence physics.

---

## 1. Confidence, stated first

This environment has no network access, so the NASA TMR grids and tabulated
profiles cannot be fetched. Everything below is marked with what it rests on.
**Nothing marked "not available" is reconstructed from memory and used as if it
were data** — that is the single most important line in this document.

| Marking | Meaning |
|---|---|
| **[established]** | A value I am confident in; universally quoted in the BFS literature |
| **[derived]** | Computed here from an [established] value, with the derivation shown |
| **[not available]** | Reference data exists but I cannot access it — must not be invented |

---

## 2. Geometry

| Quantity | Value | Basis |
|---|---|---|
| Step height H | reference length (1 inch in the experiment) | **[established]** |
| Upstream channel height | **8H** | **[established]** |
| Downstream channel height | **9H** | **[established]** |
| Expansion ratio | **1.125** | **[established]** |
| Top wall | straight (0° deflection case) | **[established]** — the tunnel could deflect; the standard TMR case is undeflected |
| Span | nominally 2-D at the centreline | **[established]** |
| Downstream extent | ≥ 20H needed past reattachment | **[derived]** — x_r/H = 6.26 plus recovery |

## 3. Upstream extent and the reference station

| Quantity | Value | Basis |
|---|---|---|
| **Inflow station used by TMR** | **x/H = −4** | **[established]** — the TMR case prescribes inflow there |
| Physical tunnel development length upstream | long; the layer is developed *as a tunnel boundary layer*, not as developed channel flow | **[established]** |
| **δ/H at the step** | **≈ 1.5** | **[established]** |
| Re_θ at the reference station | ~5000 | **[established]**, approximate |

**The key structural fact, and the one my previous setup got wrong:** the
upstream flow is **NOT fully developed channel flow**. In an 8H channel, fully
developed means δ = 4H. The reference has δ/H ≈ 1.5, so the boundary layers
occupy under 40 % of the duct and the core is still effectively potential flow.

A precursor run "until developed" would therefore produce the **wrong** inlet —
δ/H ≈ 4 instead of 1.5. The precursor must be stopped at the station where
δ/H = 1.5, not run to equilibrium. This is recorded now because it is exactly
the kind of thing that would otherwise be discovered after a second failure.

## 4. Flow conditions

| Quantity | Value | Basis |
|---|---|---|
| **Re_H = U_ref H / ν** | **36,000** | **[established]** |
| U_ref | reference free-stream velocity at the inflow station | **[established]** |
| Mach | 0.128 | **[established]** — routinely computed as incompressible |
| ν (CFDApp, U_ref = H = ρ = 1) | 1/36000 | **[derived]** |

## 5. Inflow state

| Quantity | Availability |
|---|---|
| u(y) profile at x/H = −4 | **[not available]** — tabulated by TMR |
| v(y) | **[not available]** |
| k(y) | **[not available]** |
| ω(y) or μ_t/μ | **[not available]** |
| Turbulent viscosity ratio | **[not available]** |

**Consequence for the approach choice.** The brief's Approach B —
*"if the reference provides measured/tabulated inlet profiles, implement those
directly"* — is the more faithful option **and it is unavailable here**. Not
because it is worse, but because the data cannot be obtained in this
environment. **Approach A (developed precursor) is therefore chosen**, and the
reason is availability, not preference.

## 6. Wall and outlet conditions

| Boundary | Reference | CFDApp |
|---|---|---|
| Bottom wall (downstream of step) | smooth no-slip | Wall + wall function |
| Step face | smooth no-slip | Wall + wall function |
| Top wall | smooth no-slip | Wall + wall function |
| Upstream walls | smooth no-slip | Wall + wall function (precursor) |
| Outlet | far downstream, pressure | Outlet + fixed pressure |

## 7. Comparison stations

| Quantity | Reference stations | Availability |
|---|---|---|
| **x_r/H = 6.26** | reattachment | **[established]** — the gated quantity |
| C_f(x) | along the bottom wall | **[not available]** — recorded, structurally checked |
| C_p(x) | along both walls | **[not available]** — same |
| u/U_ref profiles | x/H = 1, 4, 6, 10 (approximately) | **[not available]** — same |
| Reynolds shear stress | several stations | **[not available]** — not attempted |

---

## 8. What the previous setup got wrong

| Aspect | Previous | Reference | Effect |
|---|---|---|---|
| Upstream domain | **none** — domain started at the step | 8H duct, inflow at x/H = −4 | shear-layer initial state prescribed, not developed |
| Inlet u(y) | 1/7 power law, δ/H = 1.5 imposed | developed tunnel layer | profile shape approximately right, *history* absent |
| Inlet k, ω | **uniform** free-stream estimate | profiles with a near-wall peak | **the largest error** — no turbulence in the incoming layer where the shear layer forms |
| Inlet v | 0 | small but non-zero | minor |

The third row is the sharpest version of the root-cause hypothesis. A 1/7 power
law gets the *velocity* roughly right, but imposing a uniform free-stream k
across a 1.5H boundary layer gives the incoming layer essentially **no**
turbulence. A shear layer starting from an under-turbulent boundary layer mixes
too slowly and reattaches too far downstream — the direction and rough magnitude
observed (8.50 against 6.26).

## 9. The plan, and the test that can refute it

1. **Precursor**: an 8H-tall duct at Re_H = 36,000, uniform inflow, SST + wall
   functions, run long enough that the bottom-wall δ reaches **1.5H** — then
   stopped, **not** run to full development (§3).
2. **Verify the inflow before using it**: δ/H, C_f, mass flow, and the k/ω
   profile shape, checked at the extraction station *before* any BFS result is
   examined.
3. **A/B on the medium grid**: old undeveloped inlet vs mapped precursor inlet,
   everything else fixed.
4. **The refutation condition, fixed now**: if the mapped inlet does **not**
   move x_r/H materially toward 6.26 while the old setup stays near 8.5, the
   inlet hypothesis is **wrong** and the investigation moves to wall-function
   applicability, SST near-wall coupling, the production limiter, convection
   discretisation, outlet influence, step-corner wall distance, and
   steady-vs-unsteady behaviour — in that order.

**SST constants will not be tuned to fit the experiment** under any outcome.

---

## 10. Unavoidable deviations that remain even after the precursor

| Deviation | Why | Expected effect |
|---|---|---|
| Inflow developed by CFDApp rather than taken from TMR tables | data not available (§5) | The incoming layer is *this solver's* equilibrium layer, not the experiment's. Reduces but does not eliminate the inflow error. |
| Precursor uses the same model being validated | no independent source | A circularity worth naming: if SST's attached-layer prediction is wrong, the inflow inherits it. The attached-wall channel gates (Re_τ = 590, E_Reτ = 0.0003) are the evidence that this is a small risk. |
| Incompressible | CFDApp's SIMPLE path | negligible at M = 0.128 |
| 2-D | CFDApp turbulence is 2-D only | reference is nominally 2-D |
| C_f/C_p/profile comparison qualitative only | data not available | cannot be improved here |

The second row is a real limitation and is stated rather than buried: validating
a model against an inflow the same model produced is weaker than validating
against measured inflow. It is the best available, and the attached-wall
evidence bounds how much it can mislead.
