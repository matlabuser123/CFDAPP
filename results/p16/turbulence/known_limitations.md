# P16 turbulence -- known limitations

Written at the close of the automatic-wall investigation. Every claim below is
backed by preserved evidence under `results/p16/turbulence/`; nothing here is
an expectation or a plan.

## Validated

* **Menter SST equations**, term by term: F1/F2 blending, cross diffusion
  `D_omega = 2(1-F1) rho sigma_w2 grad(k).grad(omega)/omega`, the eddy-viscosity
  limiter `mu_t = rho a1 k / max(a1 omega, S F2)`, the production limiter, and
  `alpha = beta/beta* - sigma_w kappa^2/sqrt(beta*)`.
* **HighRe wall-function path** (Launder-Spalding) for SST and k-epsilon, with
  `omega`/`epsilon` imposed in the wall cell, equilibrium production replacing
  `mu_t S^2` there, `k` left zero-gradient, and the wall shear reaching momentum
  as a per-face viscosity.
* **Attached-wall channel flow** at Re_tau = 180 and Re_tau = 590, against the
  adopted gates. Re_tau = 590 SST: `E_Re_tau <= 0.059`, `E_u+,Linf <= 1.4` on
  the G1-valid subset {ny06, ny08, ny12, ny16}.
* **Determinism**: identical inputs give bit-identical iterations, wall shear,
  Re_tau and profile metrics.
* **Mechanism discrimination**: disabling the limiter collapses SST onto
  k-epsilon's reattachment length and eddy viscosity on the Re_h = 5000 step, so
  the SST-specific terms are demonstrably active rather than inert.

## NOT validated

* **Low-y+ wall-resolved SST.** The untouched low-Re path (`wall_functions`
  off: `k = 0` and `omega = 60 nu/(beta1 y^2)` as wall-FACE Dirichlet
  conditions, full SST PDE solved in the first cell) was measured on the whole
  Re_tau = 590 family and fails on every mesh, `E_Re_tau` = 0.46 to 0.67. That
  is not a defect in the path: it needs `y+ <~ 1` at the first cell, and the
  finest mesh in this family is `y+ = 18.4`. **No y+ ~ 1 mesh family exists in
  this repository**, so the low-Re branch has never been exercised where it is
  valid.
* **Mesh-insensitive automatic SST wall treatment.** Five formulations were
  implemented and measured; none satisfies both adopted attached-flow gates.
  See `automatic_wall/SUMMARY.md` for the full table and the structural reason.
* **Quantitative separated-flow SST closure.** The Driver & Seegmiller
  (NASA TMR) backward-facing step, ER 1.125, Re_H = 36000, reference
  `x_r/H = 6.26`, was run with an analytic inlet and again with a verified
  mapped developed-precursor inlet. Both sequences diverge under refinement.
  Preserved in `separated_flow/`.

## The structural limitation, stated precisely

The HighRe wall function is valid only for `y+` in [30, 300]. At a separation or
reattachment point the wall shear is **zero by definition**, so `y+ -> 0` there
however the mesh is built. A treatment whose validity band excludes `y+ = 0` can
therefore never be valid at the exact points a reattachment benchmark measures.
This was confirmed rather than assumed: over the corrected Driver & Seegmiller
sequence the minimum wall `y+` fell 30.34 -> 22.73 -> 5.93 as the mesh refined,
and the violating faces were those around separation and reattachment.

The converse limitation applies to every automatic treatment tried. Any
treatment continuous into the sublayer must add a viscous contribution wherever
`y+` is finite; in a 4th-power blend that contribution is `(u_vis/u_log)^4`. At
`ny16` (`y+ = 37`) the ratio is ~0.72, so **any** such blend raises `u_tau` by
~6% and `tau_w` by ~12% purely by construction. The adopted gates were derived
from the HighRe path, which uses the pure log law and is near-exact on these
meshes (`E_Re_tau = 0.0003` at ny12). On a family sitting at `y+ = 18-95`,
"continuous to the wall" and "reproduces the pure log law" are in direct
conflict.

**Consequence:** neither available treatment can close `k-omega SST` on its own.
HighRe passes the channel and is structurally inapplicable at separation;
automatic, in every variant tried, regresses canonical attached channel flow.

## What this means for a user of the solver today

SST with `"wall_functions": true` is suitable for **attached, wall-bounded flow
with the first cell deliberately placed in the log layer**, and the model
reports `y+` per face plus a count of faces outside [30, 300] so a case can be
checked rather than trusted. It is **not** suitable for flows whose answer
depends on separation or reattachment location, and it is not mesh-insensitive:
refining the near-wall mesh past `y+ = 30` moves the first cell outside the
treatment's validity, which the diagnostics report but the model cannot repair.

`wall_treatment` accepts `high_re` (default), `automatic`, `automatic_spalding`
and `automatic_momentum_flux`. **Only `high_re` is validated.** The other three
are preserved, reachable and tested for their own mathematical properties, but
each regresses the attached-flow gates and none should be used for production
results.

## Deferred, not abandoned

Moved to a later milestone (see TODO.md):

* a `y+ ~ 1` low-Re SST mesh family, without which the low-Re branch cannot be
  assessed at all;
* wall-resolved SST validation on that family;
* automatic low/high-`y+` treatment, reassessed on an acceptance basis that does
  not presuppose the pure log law at `y+ = 37`;
* separation / reattachment validation and the Driver & Seegmiller quantitative
  benchmark;
* an adverse-pressure-gradient benchmark;
* GPU equivalence for all of the above.

## Running the deferred-mode evidence

The three deferred wall treatments keep their channel tests. They are NOT
deleted and NOT disabled -- they are excluded from the green P16 regression by
an explicit filter, and run deliberately by a separate command:

```bash
results/p16/turbulence/tools/run_p20_known_failures.sh
```

or, through ctest:

```bash
ctest --test-dir build/final \
  -R 'SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel' \
  --output-on-failure
```

**Expected outcome: RED.** These are recorded as KNOWN EXPECTED FAILURES in P20
scope. A green result is itself a finding and should be compared against
`automatic_wall/SUMMARY.md` before anything is concluded from it.

The P16 regression (`tools/run_p14_regression.sh`) excludes exactly these
suites, by the matching filter. The two lists must be kept in step.
