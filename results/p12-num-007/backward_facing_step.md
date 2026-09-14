### Validation: backward_facing_step

Laminar backward-facing step (Gartling 1990, ER = 2): Re = 800 on 20/30/40/50 cells per H, L = 15H, QUICK; Re = 100 on 20 cells per H

Reference: Re = 800: published 2D numerical x_r/h in [11.48, 12.20] and (x_rs - x_s)/h in [10.60, 11.52] (ten studies, compiled in arXiv:2507.16509 Table 2; Gartling (1990) Int. J. Numer. Meth. Fluids 11, 953-967: x_r/h = 12.20). Re = 100: x_r/h = 3.00 (arXiv:2507.16509 Table 1, single source, context only)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| backward_facing_step_re800/300x20/quick | 800 | 300x20 | quick | Converged | 4739 | 520468 / 2291284 | 1.07e-13 | 938.3 | yes |
| backward_facing_step_re800/450x30/quick | 800 | 450x30 | quick | Converged | 3840 | 460967 / 2977681 | 5.43e-13 | 1954.7 | yes |
| backward_facing_step_re800/600x40/quick | 800 | 600x40 | quick | Converged | 10711 | 1111429 / 11188433 | 8.36e-13 | 8808.8 | yes |
| backward_facing_step_re800/750x50/quick | 800 | 750x50 | quick | Converged | 5410 | 498226 / 7419652 | 6.9e-13 | 8596.2 | yes |
| backward_facing_step_re100/300x20/quick | 100 | 300x20 | quick | Converged | 2373 | 66985 / 761960 | 2.68e-13 | 153.1 | yes |

### Grid convergence: backward_facing_step_re800_20_30_40

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

**upper_separation_over_h** -- x_s/h, upper-wall separation (reported)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 7.6699365 | -- | -- | -- |
| medium | 8.9256145 | -- | -- | -- |
| fine | 9.2992827 | -- | -- | -- |
| extrapolated | 9.6646015 | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | 2.4489 | 2 | 9.6646015 | 0.4566 | 0.04911 | 0.1035 | 1.1761 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.44892 (formal 2); asymptotic ratio 1.17614 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**upper_reattachment_over_h** -- x_rs/h, upper-wall reattachment (reported)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 19.812391 | -- | -- | -- |
| medium | 20.788978 | -- | -- | -- |
| fine | 20.925164 | -- | -- | -- |
| extrapolated | 20.976506 | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | 4.5029 | 2 | 20.976506 | 0.06418 | 0.003067 | 0.01128 | 2.5098 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 4.50295 (formal 2); asymptotic ratio 2.50984 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic


### Grid convergence: backward_facing_step_re800_30_40_50

Re = 800, QUICK, 30/40/50 cells per H

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 450x30 | 450 x 30 | 13500 | 0.0333333 | 1954.7 | Converged (3840 it) | yes |
| 600x40 | 600 x 40 | 24000 | 0.025 | 8808.8 | Converged (10711 it) | yes |
| 750x50 | 750 x 50 | 37500 | 0.02 | 8596.2 | Converged (5410 it) | yes |

**reattachment_length_over_h** -- x_r/h, lower-wall primary eddy (published range [11.48, 12.20])

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 11.404549 | 11.84 (benchmark) | -0.4355 | 0.03678 |
| medium | 11.779801 | 11.84 (benchmark) | -0.0602 | 0.005084 |
| fine | 11.932304 | 11.84 (benchmark) | 0.0923 | 0.007796 |
| extrapolated | 12.136759 | | 0.2968 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.25 | 1.333 | 2.4974 | 2 | 12.136759 | 0.2556 | 0.02142 | 0.03788 | 1.1389 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.49737 (formal 2); asymptotic ratio 1.13891 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic
- warning: refinement ratio below 1.3 (Celik et al. recommend r > 1.3)

**upper_bubble_length_over_h** -- (x_rs - x_s)/h, upper-wall bubble (published range [10.60, 11.52])

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 11.863363 | 11.06 (benchmark) | 0.8034 | 0.07264 |
| medium | 11.625881 | 11.06 (benchmark) | 0.5659 | 0.05116 |
| fine | 11.50582 | 11.06 (benchmark) | 0.4458 | 0.04031 |
| extrapolated | 11.238247 | | 0.1782 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.25 | 1.333 | 1.6611 | 2 | 11.238247 | 0.3345 | 0.02907 | 0.04168 | 0.9155 | asymptotic | no |

- monotonic convergence, observed order 1.66113 (formal 2); asymptotic ratio 0.915537 within 1 +/- 0.1
- grid independence: GCI21 0.0290693 > threshold 0.01
- warning: refinement ratio below 1.3 (Celik et al. recommend r > 1.3)

**upper_separation_over_h** -- x_s/h, upper-wall separation (reported)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 8.9256145 | -- | -- | -- |
| medium | 9.2992827 | -- | -- | -- |
| fine | 9.4511058 | -- | -- | -- |
| extrapolated | 9.6545503 | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.25 | 1.333 | 2.4983 | 2 | 9.6545503 | 0.2543 | 0.02691 | 0.04775 | 1.1392 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.49829 (formal 2); asymptotic ratio 1.13919 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic
- warning: refinement ratio below 1.3 (Celik et al. recommend r > 1.3)

**upper_reattachment_over_h** -- x_rs/h, upper-wall reattachment (reported)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 20.788978 | -- | -- | -- |
| medium | 20.925164 | -- | -- | -- |
| fine | 20.956926 | -- | -- | -- |
| extrapolated | 20.974696 | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.25 | 1.333 | 4.5940 | 2 | 20.974696 | 0.02221 | 0.00106 | 0.002959 | 1.9846 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 4.59401 (formal 2); asymptotic ratio 1.98459 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic
- warning: refinement ratio below 1.3 (Celik et al. recommend r > 1.3)


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
- [x] backward_facing_step_re800/750x50/quick: solve_accepted: Converged
- [x] backward_facing_step_re800/750x50/quick: mass_flow: max |Q - 0.5| over inflow, outflow and 4 sections 8.47e-13 (bound 1e-6)
- [x] backward_facing_step_re800/750x50/quick: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] backward_facing_step_re800/750x50/quick: reattachment_detected: x_r/h = 11.9323
- [x] backward_facing_step_re100/300x20/quick: solve_accepted: Converged
- [x] backward_facing_step_re100/300x20/quick: mass_flow: max |Q - 0.5| over inflow, outflow and 4 sections 2.68e-13 (bound 1e-6)
- [x] backward_facing_step_re100/300x20/quick: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] backward_facing_step_re100/300x20/quick: reattachment_detected: x_r/h = 3.1887
- [x] reattachment_length_over_h_within_published_range: finest-grid (50 cells/H) 11.9323 vs published [11.48, 12.20] (30/40/50 status monotonic_not_asymptotic)
- [x] upper_bubble_length_over_h_within_published_range: Richardson-extrapolated 11.2382 vs published [10.60, 11.52] (30/40/50 status asymptotic)

Limitations:

- Re = 100 (20 cells/H): x_r/h = 3.1887 vs 3.00 in arXiv:2507.16509 Table 1 (single source with an upstream inlet section; reported, not gated)
- Uniform Cartesian grids only (no clustering at the step corner or walls); the finest grid here has 50 cells per H.
- Collocated SIMPLE without Rhie-Chow interpolation; the pressure field is not validated.
- Reattachment from the wall-adjacent cell velocity (first-order one-sided wall shear): the zero crossing itself is independent of that approximation's magnitude error.
- Runtimes are wall-clock and not deterministic.

Overall: PASSED
