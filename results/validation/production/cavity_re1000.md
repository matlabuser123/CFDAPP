### Validation: cavity_re1000

Lid-driven cavity Re = 1000: QUICK (configuration of record) 40/80/160

Reference: Ghia, Ghia & Shin (1982), J. Comput. Phys. 48, 387-411, Table I (u(0.5, y)) and Table II (v(x, 0.5)), Re = 1000 column, 17 stations each (validation/ghia/ghia_re1000_{u,v}.csv)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| cavity_re1000/40x40/quick | 1000 | 40x40 | quick | Converged | 3582 | 74028 / 99660 | 0 | 41.6 | yes |
| cavity_re1000/80x80/quick | 1000 | 80x80 | quick | Converged | 4289 | 102277 / 114476 | 0 | 208.0 | yes |
| cavity_re1000/160x160/quick | 1000 | 160x160 | quick | Converged | 2748 | 71576 / 149259 | 0 | 740.0 | yes |

**u_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re1000/40x40/quick | 3.7027e-02 | 5.4440e-02 | 1.1275e-01 |
| cavity_re1000/80x80/quick | 6.4485e-03 | 9.5906e-03 | 1.9936e-02 |
| cavity_re1000/160x160/quick | 2.0263e-03 | 2.5192e-03 | 4.0764e-03 |

**v_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re1000/40x40/quick | 3.9242e-02 | 5.1738e-02 | 1.0454e-01 |
| cavity_re1000/80x80/quick | 4.6408e-03 | 7.6264e-03 | 1.6652e-02 |
| cavity_re1000/160x160/quick | 3.9562e-03 | 5.4121e-03 | 1.0725e-02 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u_centerline | l2 | -- | 5.676, 3.807 | 2.665 | monotonic_asymptotic_range_unknown |
| v_centerline | l2 | -- | 6.784, 1.409 | 4.316 | monotonic_asymptotic_range_unknown |

### Grid convergence: cavity_re1000_quick_40_80_160

Re = 1000, QUICK, grids 40/80/160

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 40x40 | 40 x 40 | 1600 | 0.025 | 41.6 | Converged (3582 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 208.0 | Converged (4289 it) | yes |
| 160x160 | 160 x 160 | 25600 | 0.00625 | 740.0 | Converged (2748 it) | yes |

**u_center** -- u(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.081408592 | -0.0608 (benchmark) | -0.02061 | 0.339 |
| medium | -0.064468519 | -0.0608 (benchmark) | -0.003669 | 0.06034 |
| fine | -0.061566823 | -0.0608 (benchmark) | -0.0007668 | 0.01261 |
| extrapolated | -0.06096705 | | -0.0001671 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.5455 | 2 | -0.06096705 | 0.0007497 | 0.01218 | 0.06789 | 1.4595 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.54547 (formal 2); asymptotic ratio 1.4595 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_center** -- v(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.020914317 | 0.02526 (benchmark) | -0.004346 | 0.172 |
| medium | 0.023704896 | 0.02526 (benchmark) | -0.001555 | 0.06156 |
| fine | 0.02514988 | 0.02526 (benchmark) | -0.0001101 | 0.004359 |
| extrapolated | 0.026701591 | | 0.001442 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 0.9495 | 2 | 0.026701591 | 0.00194 | 0.07712 | 0.158 | 0.4828 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 0.949512 (formal 2); asymptotic ratio 0.482805 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**u_y0.1719** -- u(0.5, 0.1719), Ghia station near u_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.32091201 | -0.38289 (benchmark) | 0.06198 | 0.1619 |
| medium | -0.37998334 | -0.38289 (benchmark) | 0.002907 | 0.007591 |
| fine | -0.38598968 | -0.38289 (benchmark) | -0.0031 | 0.008095 |
| extrapolated | -0.38666953 | | -0.00378 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 3.2979 | 2 | -0.38666953 | 0.0008498 | 0.002202 | 0.02199 | 2.4587 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 3.2979 (formal 2); asymptotic ratio 2.45871 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_x0.1563** -- v(0.1563, 0.5), Ghia station near v_max

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.33179694 | 0.37095 (benchmark) | -0.03915 | 0.1055 |
| medium | 0.36959733 | 0.37095 (benchmark) | -0.001353 | 0.003647 |
| fine | 0.37381089 | 0.37095 (benchmark) | 0.002861 | 0.007712 |
| extrapolated | 0.37433949 | | 0.003389 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 3.1653 | 2 | 0.37433949 | 0.0006608 | 0.001768 | 0.01604 | 2.2428 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 3.16529 (formal 2); asymptotic ratio 2.24278 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_x0.9063** -- v(0.9063, 0.5), Ghia station near v_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.43934811 | -0.5155 (benchmark) | 0.07615 | 0.1477 |
| medium | -0.51201884 | -0.5155 (benchmark) | 0.003481 | 0.006753 |
| fine | -0.52206203 | -0.5155 (benchmark) | -0.006562 | 0.01273 |
| extrapolated | -0.5236726 | | -0.008173 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.8552 | 2 | -0.5236726 | 0.002013 | 0.003856 | 0.02845 | 1.8090 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.85516 (formal 2); asymptotic ratio 1.80895 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic


- [x] cavity_re1000/40x40/quick: solve_accepted: Converged after 3582 iterations
- [x] cavity_re1000/40x40/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/40x40/quick: ghia_error: u L2 5.4440e-02 (bound 0.07), v L2 5.1738e-02 (bound 0.07)
- [x] cavity_re1000/40x40/quick: bounded: max |U| 0.808984 vs lid speed 1
- [x] cavity_re1000/80x80/quick: solve_accepted: Converged after 4289 iterations
- [x] cavity_re1000/80x80/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/80x80/quick: ghia_error: u L2 9.5906e-03 (bound 0.0125), v L2 7.6264e-03 (bound 0.01)
- [x] cavity_re1000/80x80/quick: bounded: max |U| 0.911642 vs lid speed 1
- [x] cavity_re1000/160x160/quick: solve_accepted: Converged after 2748 iterations
- [x] cavity_re1000/160x160/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/160x160/quick: ghia_error: u L2 2.5192e-03 (bound 0.0035), v L2 5.4121e-03 (bound 0.007)
- [x] cavity_re1000/160x160/quick: bounded: max |U| 0.956270 vs lid speed 1
- [x] monotone_u_centerline: QUICK L2 0.054440 -> 0.009591 -> 0.002519
- [x] monotone_v_centerline: QUICK L2 0.051738 -> 0.007626 -> 0.005412

Limitations:

- Ghia et al. is itself a 129x129 numerical solution; at Re = 1000 its own discretisation error is larger than at Re = 100, so agreement is judged as convergence toward the table, not as an exact target.
- Collocated SIMPLE without Rhie-Chow interpolation; the pressure field is not validated.
- Runtimes are wall-clock and not deterministic.

Overall: PASSED
