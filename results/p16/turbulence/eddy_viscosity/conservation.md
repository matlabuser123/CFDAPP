# P16 turbulent thermal conservation -- FROZEN METHODOLOGY

**Written and frozen BEFORE any balance was computed.** Nothing below was
chosen after seeing a residual. The threshold section is deliberately left
open at freeze time and is derived from measurement in the section after it,
following this repository's evidence-first gate workflow.

---

## Case

`cases/turbulent_heated_channel`, unmodified, at the default
`Pr_t = kDefaultTurbulentPrandtl = 0.85`.

* air, `rho = 1.225`, `mu = 1.8e-5`, `k_mol = 0.0257`, `cp = 1005`
* 2 m x 0.1 m, 40 x 10 structured Cartesian
* `Re_Dh = rho U D_h / mu = 1.225 * 10 * 0.2 / 1.8e-5 = 1.361e5`
* k-epsilon with wall functions
* left: inlet `u = 10 m/s`, `T = 300 K` (Dirichlet)
* right: outlet, `T` adiabatic (zero gradient)
* bottom/top: walls, `T = 400 K` (Dirichlet)
* **no volumetric heat source**, so `Q_source = 0` and it does not appear below

## Why the balance is computed on BOUNDARY faces only

The discrete energy equation is a per-cell balance. Summing the assembled
equation over every cell makes the internal face fluxes cancel exactly -- each
internal face contributes equal and opposite amounts to its two cells, which is
what "conservative finite volume" means -- leaving only boundary faces and the
(here absent) volumetric source. So the global steady balance is a statement
about boundary faces alone, and computing it that way tests the discretisation
rather than re-deriving a different one.

**The face formulas below are the assembler's own**, not an independent
quadrature. Using a different-but-equivalent-looking formula would produce a
non-zero residual that looks like non-conservation and is not.

## Sign convention -- fixed here, once

For every boundary face `f` with owner cell `P`, define the energy flux
**INTO the domain**:

```
q_f = Q_diffusive_in(f) - Q_advective_out(f)
```

* `massFlux[f]` is positive in the face's OUTWARD normal direction (the mesh's
  own convention), so a positive value at a boundary face is OUTFLOW.

* **Advective, out of the domain:**
  ```
  Q_advective_out(f) = massFlux[f] * cp * T_upwind(f)
  T_upwind(f) = T_P          if massFlux[f] > 0   (leaving)
              = T_boundary   otherwise            (entering)
  ```
  Upwind, because that is the scheme the convection assembler uses. For
  constant `cp` the transported enthalpy is `h = cp T`; a variable-`cp` case
  would need `h = integral(cp dT)` and is out of scope here (this case is
  constant-`cp`).

* **Diffusive, into the domain, at a face whose temperature is PRESCRIBED:**
  ```
  Q_diffusive_in(f) = k_eff_P * A_f / d_f * (T_boundary - T_P)
  d_f = |x_f - x_P|
  ```
  Positive when the boundary is hotter than the owner cell, i.e. heat entering.
  `A_f / d_f` with the owner-cell conductivity is exactly what
  `boundaryFaceDiffusionTerms` builds for a prescribed value on an orthogonal
  mesh, and this mesh is Cartesian, so the non-orthogonal correction is
  identically zero and no explicit-flux term arises.

* **Diffusive at a zero-gradient (adiabatic) face:** `Q_diffusive_in(f) = 0`,
  by definition of the condition.

* **`k_eff` is the SAME quantity the solve used:**
  ```
  k_eff_P = k_mol + mu_t[P] * cp / Pr_t
  ```
  evaluated with the converged `mu_t` and the resolved `Pr_t`, through the same
  `cfd::thermal::turbulentConductivity` the solver calls. Computing this balance
  with `k_mol` alone is explicitly forbidden: it would omit most of the wall
  heat flux in a turbulent case and the residual would be meaningless.

## The balance

```
Q_wall = sum over bottom+top faces of q_f          (heat entering from the walls)
Q_in   = sum over left faces of q_f                (net entering at the inlet)
Q_out  = - sum over right faces of q_f             (net leaving at the outlet)

imbalance = Q_wall + Q_in - Q_out
```

Equivalently `imbalance = sum over ALL boundary faces of q_f`, which is how it
is computed; the three-way split is reported for interpretability.

## Normalised residual

```
eps_E = |Q_wall + Q_in - Q_out| / max(|Q_wall|, |Q_in|, |Q_out|)
```

Normalised by the LARGEST of the three, so a near-cancellation between two big
terms cannot flatter the result.

## Threshold

**Not frozen at methodology-freeze time, deliberately.** Guessing a round number
here is the error this repository has made before. It is derived in
`conservation_result.md` from the measured value together with the two error
sources that bound it:

1. the flow solve's own mass imbalance, which enters `Q_advective` directly;
2. the thermal solve's outer tolerance.

## Non-vacuity control -- REQUIRED before the threshold means anything

A balance that closes is only evidence if the same instrument would FAIL on a
broken one. Two controls, both computed with the identical routine:

* **C1 -- molecular conductivity only.** Recompute the wall flux with `k_mol`
  instead of `k_eff`. In a turbulent case `k_t >> k_mol`, so this must produce a
  large `eps_E`. If it does not, the balance is insensitive to the turbulent
  term and proves nothing about it.
* **C2 -- perturbed Pr_t.** Compute the balance for the converged field of one
  `Pr_t` while evaluating `k_eff` at a different `Pr_t`. Must also fail.

Both controls exist to show the residual is small because the scheme conserves,
not because the instrument cannot see anything.
