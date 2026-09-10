# Turbulent Channel Flow — Reference Data (P2-TURB-007)

## Source

Fully developed, incompressible, plane (two-wall) turbulent channel flow
at nominal friction Reynolds number `Re_tau = u_tau * delta / nu ≈ 180`,
`delta` the channel half-height. This is the canonical low-Re DNS
channel-flow case first computed by:

* Kim, J., Moin, P., & Moser, R. (1987). *Turbulence statistics in
  fully developed channel flow at low Reynolds number.* Journal of
  Fluid Mechanics, 177, 133-166.
* Moser, R. D., Kim, J., & Mansour, N. N. (1999). *Direct numerical
  simulation of turbulent channel flow up to Re_tau=590.* Physics of
  Fluids, 11(4), 943-945. (Re_tau=180 case reproduced/extended from the
  1987 spectral DNS above.)

Both are widely reproduced, extensively cited (thousands of citations)
textbook-standard benchmark datasets for RANS/LES turbulence-model
validation.

## Provenance disclosure (read before using `reference_summary.json`)

The live DNS database mirrors this codebase would normally fetch a
fine-grained `u+(y+)` table from (`turbulence.oden.utexas.edu`,
`turbmodels.larc.nasa.gov`) were **not reachable from this development
sandbox** (DNS resolution failure / the NASA page has since been
retired and redirects to a generic landing page -- both checked at
validation time, 2026-09-09). Rather than fabricate a fine-resolution
table that cannot be independently verified in this environment, this
directory instead stores:

1. `reference_summary.json` -- a small set of **well-established,
   textbook-standard summary statistics** for the Re_tau=180 case
   (friction Reynolds number, bulk Reynolds number, centerline
   Reynolds number, bulk/centerline velocity in wall units, skin
   friction coefficient), reproduced consistently across the secondary
   literature citing Kim/Moin/Moser (1987) and Moser/Kim/Mansour
   (1999). These are the quantities `ChannelFlowValidationUtils`
   actually compares the solver's *achieved* Re_tau/wall-shear against
   (see section "Reynolds-number convention" below).
2. The **analytical law-of-the-wall relations** (viscous sublayer
   `u+ = y+`, log law `u+ = (1/kappa)*ln(y+) + B`, `kappa=0.41`,
   `B=5.0`) as the primary continuous-profile reference in wall units.
   These are not an invented substitute for DNS: they are the
   well-known asymptotic behavior the Re_tau=180 DNS mean-velocity
   profile is itself established to closely follow in the
   corresponding near-wall/log layers (Pope, *Turbulent Flows*, 2000,
   ch. 7; Kim/Moin/Moser 1987's own Figure 3 confirms the fit). Using
   them as the quantitative profile-shape reference is standard
   practice in RANS wall-treatment validation (see this task's own
   section 15/41).

This is a deliberate, documented scope decision, not a silent
substitution (P2-TURB-007 section 8's "do not silently substitute an
easier test with no external comparison" is honored: the comparison
*is* external and independently sourced, just expressed as literature
summary statistics + universal law-of-the-wall relations rather than a
full point-by-point DNS table this sandbox could not retrieve).

## Reynolds-number convention

`Re_tau = u_tau * delta / nu`, `delta` = channel half-height. This is
the convention the reference summary's `re_tau` field uses, and the one
`ChannelFlowValidationUtils::computeAchievedReTau` computes from the
converged numerical solution's own wall shear (never assumed equal to
an input target -- P2-TURB-007 section 5).

`Re_bulk = U_bulk * H / nu`, `H = 2*delta` the *full* channel height,
`U_bulk` the cross-section-averaged streamwise velocity -- this is the
quantity CFDApp's case setup actually controls directly (via the
uniform inlet velocity), so the benchmark's target `Re_bulk = 5600` is
the input parameter; the resulting `Re_tau` is an *output*, read off the
converged solution, and is expected to land near (not exactly at) 180
because of the near-wall-resolution/wall-treatment limitations documented
in `docs/` and the P2-TURB-007 Final Report.

## Data provenance table

| Quantity | Symbol | Value | Source |
|---|---|---|---|
| Friction Reynolds number | `Re_tau` | 180 (178.12 more precisely) | Kim, Moin & Moser (1987) |
| Bulk Reynolds number (full height `2*delta`) | `Re_bulk` | 5600 | Kim, Moin & Moser (1987); widely reproduced (e.g. as the standard "Re=5600" DNS/LES channel setup) |
| Centerline Reynolds number (`delta` basis) | `Re_centerline` | 3300 | Kim, Moin & Moser (1987) |
| Centerline velocity in wall units | `U_centerline+` | 18.2 | Kim, Moin & Moser (1987) |
| Bulk velocity in wall units | `U_bulk+` | 15.6 | Kim, Moin & Moser (1987); consistent with `Re_tau/Re_bulk = u_tau/(2*U_bulk)` |
| Skin friction coefficient | `Cf = 2*(u_tau/U_bulk)^2` | 8.2e-3 | derived from `U_bulk+` above |
| von Karman constant | `kappa` | 0.41 | standard (confirmed via independent web reference at validation time) |
| Log-law additive constant | `B` | 5.0 | standard smooth-wall value |
| Viscous sublayer | `y+ < 5` | `u+ = y+` | standard |
| Buffer layer | `5 <= y+ <= 30` | no closed form | standard |
| Log layer | `y+ > 30` | `u+ = (1/kappa)*ln(y+) + B` | standard |

See `reference_summary.json` for the machine-readable form of this
table, consumed by both the C++ validation utilities
(`tests/integration/turbulence_channel/ChannelFlowValidationUtils.*`)
and the Python analysis module (`python/cfdapp/validation/channel_flow.py`).
