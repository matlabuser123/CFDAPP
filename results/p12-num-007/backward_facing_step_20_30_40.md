### Validation: backward_facing_step

Laminar backward-facing step (Gartling 1990, ER = 2): Re = 800 on 20/30/40 cells per H, L = 15H, QUICK; Re = 100 on 20 cells per H

Reference: Re = 800: published 2D numerical x_r/h in [11.48, 12.20] and (x_rs - x_s)/h in [10.60, 11.52] (ten studies, compiled in arXiv:2507.16509 Table 2; Gartling (1990) Int. J. Numer. Meth. Fluids 11, 953-967: x_r/h = 12.20). Re = 100: x_r/h = 3.00 (arXiv:2507.16509 Table 1, single source, context only)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| backward_facing_step_re800/300x20/quick | 800 | 300x20 | quick | Converged | 4739 | 520468 / 2291284 | 1.07e-13 | 938.3 | yes |
| backward_facing_step_re800/450x30/quick | 800 | 450x30 | quick | Converged | 3840 | 460967 / 2977681 | 5.43e-13 | 1954.7 | yes |
| backward_facing_step_re800/600x40/quick | 800 | 600x40 | quick | Converged | 10711 | 1111429 / 11188433 | 8.36e-13 | 8808.8 | yes |
| backward_facing_step_re100/300x20/quick | 100 | 300x20 | quick | Converged | 2373 | 66985 / 761960 | 2.68e-13 | 153.1 | yes |

### Grid convergence: backward_facing_step_re800

Re = 800, QUICK, 20/30/40 cells per H

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 300x20 | 300 x 20 | 6000 | 0.05 | 938.3 | Converged (4739 it) | yes |
| 450x30 | 450 x 30 | 13500 | 0.0333333 | 1954.7 | Converged (3840 it) | yes |
| 600x40 | 600 x 40 | 24000 | 0.025 | 8808.8 | Converged (10711 it) | yes |

**reattachment_length_over_h** -- x_r/h, lower-wall primary eddy (published range [11.48, 12.20])

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 10.085191 | 11.84 (benchmark) | -1.755 | 0.1482 |
| medium | 11.404549 | 11.84 (benchmark) | -0.4355 | 0.03678 |
| fine | 11.779801 | 11.84 (benchmark) | -0.0602 | 0.005084 |
| extrapolated | 12.121979 | | 0.282 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | 2.5735 | 2 | 12.121979 | 0.4277 | 0.03631 | 0.07863 | 1.2306 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.57347 (formal 2); asymptotic ratio 1.23058 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**upper_bubble_length_over_h** -- (x_rs - x_s)/h, upper-wall bubble (published range [10.60, 11.52])

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 12.142455 | 11.06 (benchmark) | 1.082 | 0.09787 |
| medium | 11.863363 | 11.06 (benchmark) | 0.8034 | 0.07264 |
| fine | 11.625881 | 11.06 (benchmark) | 0.5659 | 0.05116 |
| extrapolated | -- | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | -- | 2 | -- | -- | -- | -- | -- | divergent | no |

- monotonic but the differences do not shrink fast enough for a positive order (epsilon32/epsilon21 = 1.17521 <= ln r32 / ln r21 = 1.40942)
- grid independence: divergent sequence: monotonic but the differences do not shrink fast enough for a positive order (epsilon32/epsilon21 = 1.17521 <= ln r32 / ln r21 = 1.40942)


- [x] backward_facing_step_re800/300x20/quick: solve_accepted: Converged
- [x] backward_facing_step_re800/300x20/quick: mass_flow: max |Q - 0.5| over inflow, outflow and 4 sections 2.31e-13 (bound 1e-6)
- [x] backward_facing_step_re800/300x20/quick: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] backward_facing_step_re800/300x20/quick: reattachment_detected: x_r/h = 10.0852
- [x] backward_facing_step_re800/450x30/quick: solve_accepted: Converged
- [x] backward_facing_step_re800/450x30/quick: mass_flow: max |Q - 0.5| over inflow, outflow and 4 sections 1.08e-12 (bound 1e-6)
- [x] backward_facing_step_re800/450x30/quick: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] backward_facing_step_re800/450x30/quick: reattachment_detected: x_r/h = 11.4045
- [x] backward_facing_step_re800/600x40/quick: solve_accepted: Converged
- [x] backward_facing_step_re800/600x40/quick: mass_flow: max |Q - 0.5| over inflow, outflow and 4 sections 8.36e-13 (bound 1e-6)
- [x] backward_facing_step_re800/600x40/quick: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] backward_facing_step_re800/600x40/quick: reattachment_detected: x_r/h = 11.7798
- [x] backward_facing_step_re100/300x20/quick: solve_accepted: Converged
- [x] backward_facing_step_re100/300x20/quick: mass_flow: max |Q - 0.5| over inflow, outflow and 4 sections 2.68e-13 (bound 1e-6)
- [x] backward_facing_step_re100/300x20/quick: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] backward_facing_step_re100/300x20/quick: reattachment_detected: x_r/h = 3.1887
- [x] reattachment_length_over_h_within_published_range: finest-grid 11.7798 vs published [11.48, 12.20] (status monotonic_not_asymptotic)
- [ ] upper_bubble_length_over_h_within_published_range: finest-grid 11.6259 vs published [10.60, 11.52] (status divergent)

Limitations:

- Re = 100 (20 cells/H): x_r/h = 3.1887 vs 3.00 in arXiv:2507.16509 Table 1 (single source with an upstream inlet section; reported, not gated)
- Uniform Cartesian grids only (no clustering at the step corner or walls); the finest grid here has 40 cells per H.
- Collocated SIMPLE without Rhie-Chow interpolation; the pressure field is not validated.
- Reattachment from the wall-adjacent cell velocity (first-order one-sided wall shear): the zero crossing itself is independent of that approximation's magnitude error.
- Runtimes are wall-clock and not deterministic.

Overall: FAILED
