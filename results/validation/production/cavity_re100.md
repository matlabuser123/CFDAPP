### Validation: cavity_re100

Lid-driven cavity Re = 100: QUICK (configuration of record) 20/40/80/160 and first-order upwind 20/40/80

Reference: Ghia, Ghia & Shin (1982), J. Comput. Phys. 48, 387-411, Table I (u(0.5, y)) and Table II (v(x, 0.5)), Re = 100 column, 17 stations each (validation/ghia/ghia_re100_{u,v}.csv)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| cavity_re100/20x20/quick | 100 | 20x20 | quick | Converged | 3083 | 36967 / 151537 | 0 | 5.6 | yes |
| cavity_re100/40x40/quick | 100 | 40x40 | quick | Converged | 5500 | 59761 / 58120 | 0 | 32.2 | yes |
| cavity_re100/80x80/quick | 100 | 80x80 | quick | Converged | 9258 | 81177 / 50041 | 0 | 206.3 | yes |
| cavity_re100/160x160/quick | 100 | 160x160 | quick | Converged | 5084 | 71849 / 60434 | 0 | 587.2 | yes |
| cavity_re100/20x20/upwind | 100 | 20x20 | upwind | Converged | 3036 | 34136 / 150550 | 0 | 4.9 | yes |
| cavity_re100/40x40/upwind | 100 | 40x40 | upwind | Converged | 5615 | 59881 / 54988 | 0 | 25.3 | yes |
| cavity_re100/80x80/upwind | 100 | 80x80 | upwind | Converged | 9643 | 79024 / 48522 | 0 | 175.5 | yes |

**u_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/quick | 4.8012e-03 | 5.6901e-03 | 1.0462e-02 |
| cavity_re100/40x40/quick | 1.0455e-03 | 1.4519e-03 | 4.6064e-03 |
| cavity_re100/80x80/quick | 1.5255e-03 | 2.0904e-03 | 5.0344e-03 |
| cavity_re100/160x160/quick | 1.5899e-03 | 2.1526e-03 | 4.4610e-03 |
| cavity_re100/20x20/upwind | 1.6212e-02 | 2.1050e-02 | 3.7223e-02 |
| cavity_re100/40x40/upwind | 7.6157e-03 | 1.0169e-02 | 1.8792e-02 |
| cavity_re100/80x80/upwind | 3.2852e-03 | 4.3361e-03 | 8.4948e-03 |

**v_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/quick | 3.4504e-03 | 4.8236e-03 | 1.2624e-02 |
| cavity_re100/40x40/quick | 2.4766e-03 | 2.9903e-03 | 6.5190e-03 |
| cavity_re100/80x80/quick | 3.8586e-03 | 4.5102e-03 | 8.7131e-03 |
| cavity_re100/160x160/quick | 3.6932e-03 | 4.3661e-03 | 8.6666e-03 |
| cavity_re100/20x20/upwind | 1.0872e-02 | 1.4460e-02 | 4.0414e-02 |
| cavity_re100/40x40/upwind | 4.9208e-03 | 6.4477e-03 | 1.6786e-02 |
| cavity_re100/80x80/upwind | 2.7510e-03 | 3.5693e-03 | 6.9850e-03 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u_centerline | l2 | -- | 3.919, 0.695, 0.971 | --, 3.361 | monotonic_asymptotic_range_unknown |
| u_centerline | l2 | 1 | 2.070, 2.345 | 0.900 | asymptotic |
| v_centerline | l2 | -- | 1.613, 0.663, 1.033 | --, -- | oscillatory |
| v_centerline | l2 | 1 | 2.243, 1.806 | 1.477 | monotonic_not_asymptotic |

### Grid convergence: cavity_re100_quick_20_40_80

Re = 100, QUICK, grids 20/40/80

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 20x20 | 20 x 20 | 400 | 0.05 | 5.6 | Converged (3083 it) | yes |
| 40x40 | 40 x 40 | 1600 | 0.025 | 32.2 | Converged (5500 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 206.3 | Converged (9258 it) | yes |

**u_center** -- u(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.20060482 | -0.20581 (benchmark) | 0.005205 | 0.02529 |
| medium | -0.20717103 | -0.20581 (benchmark) | -0.001361 | 0.006613 |
| fine | -0.20867366 | -0.20581 (benchmark) | -0.002864 | 0.01391 |
| extrapolated | -0.20911956 | | -0.00331 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.1276 | 2 | -0.20911956 | 0.0005574 | 0.002671 | 0.01176 | 1.0925 | asymptotic | yes |

- monotonic convergence, observed order 2.12758 (formal 2); asymptotic ratio 1.09246 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00267107 <= threshold 0.01

**v_center** -- v(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.0527948 | 0.05454 (benchmark) | -0.001745 | 0.032 |
| medium | 0.056427914 | 0.05454 (benchmark) | 0.001888 | 0.03462 |
| fine | 0.05736678 | 0.05454 (benchmark) | 0.002827 | 0.05183 |
| extrapolated | 0.057693947 | | 0.003154 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 1.9522 | 2 | 0.057693947 | 0.000409 | 0.007129 | 0.02805 | 0.9674 | asymptotic | yes |

- monotonic convergence, observed order 1.95222 (formal 2); asymptotic ratio 0.967421 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00712885 <= threshold 0.01

**u_y0.4531** -- u(0.5, 0.4531), Ghia station near u_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.20369058 | -0.2109 (benchmark) | 0.007209 | 0.03418 |
| medium | -0.21181989 | -0.2109 (benchmark) | -0.0009199 | 0.004362 |
| fine | -0.21355167 | -0.2109 (benchmark) | -0.002652 | 0.01257 |
| extrapolated | -0.21402046 | | -0.00312 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.2309 | 2 | -0.21402046 | 0.000586 | 0.002744 | 0.01299 | 1.1735 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.23088 (formal 2); asymptotic ratio 1.17355 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_x0.2344** -- v(0.2344, 0.5), Ghia station near v_max

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.16808884 | 0.17527 (benchmark) | -0.007181 | 0.04097 |
| medium | 0.17685157 | 0.17527 (benchmark) | 0.001582 | 0.009024 |
| fine | 0.17912148 | 0.17527 (benchmark) | 0.003851 | 0.02197 |
| extrapolated | 0.17991505 | | 0.004645 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 1.9487 | 2 | 0.17991505 | 0.000992 | 0.005538 | 0.02165 | 0.9651 | asymptotic | yes |

- monotonic convergence, observed order 1.94875 (formal 2); asymptotic ratio 0.965097 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00553792 <= threshold 0.01

**v_x0.8047** -- v(0.8047, 0.5), Ghia station near v_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.23270556 | -0.24533 (benchmark) | 0.01262 | 0.05146 |
| medium | -0.24932676 | -0.24533 (benchmark) | -0.003997 | 0.01629 |
| fine | -0.2528728 | -0.24533 (benchmark) | -0.007543 | 0.03075 |
| extrapolated | -0.2538345 | | -0.008505 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.2287 | 2 | -0.2538345 | 0.001202 | 0.004754 | 0.0226 | 1.1718 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.22874 (formal 2); asymptotic ratio 1.17181 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic


### Grid convergence: cavity_re100_quick_40_80_160

Re = 100, QUICK, grids 40/80/160

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 40x40 | 40 x 40 | 1600 | 0.025 | 32.2 | Converged (5500 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 206.3 | Converged (9258 it) | yes |
| 160x160 | 160 x 160 | 25600 | 0.00625 | 587.2 | Converged (5084 it) | yes |

**u_center** -- u(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.20717103 | -0.20581 (benchmark) | -0.001361 | 0.006613 |
| medium | -0.20867366 | -0.20581 (benchmark) | -0.002864 | 0.01391 |
| fine | -0.20891834 | -0.20581 (benchmark) | -0.003108 | 0.0151 |
| extrapolated | -0.20896594 | | -0.003156 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.6185 | 2 | -0.20896594 | 5.949e-05 | 0.0002848 | 0.001751 | 1.5353 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.61848 (formal 2); asymptotic ratio 1.53526 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_center** -- v(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.056427914 | 0.05454 (benchmark) | 0.001888 | 0.03462 |
| medium | 0.05736678 | 0.05454 (benchmark) | 0.002827 | 0.05183 |
| fine | 0.05758161 | 0.05454 (benchmark) | 0.003042 | 0.05577 |
| extrapolated | 0.057645353 | | 0.003105 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.1277 | 2 | 0.057645353 | 7.968e-05 | 0.001384 | 0.00607 | 1.0926 | asymptotic | yes |

- monotonic convergence, observed order 2.12772 (formal 2); asymptotic ratio 1.09257 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00138375 <= threshold 0.01

**u_y0.4531** -- u(0.5, 0.4531), Ghia station near u_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.21181989 | -0.2109 (benchmark) | -0.0009199 | 0.004362 |
| medium | -0.21355167 | -0.2109 (benchmark) | -0.002652 | 0.01257 |
| fine | -0.21359178 | -0.2109 (benchmark) | -0.002692 | 0.01276 |
| extrapolated | -0.21359273 | | -0.002693 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 5.4323 | 2 | -0.21359273 | 1.189e-06 | 5.565e-06 | 0.0002403 | 10.7950 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 5.43229 (formal 2); asymptotic ratio 10.795 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_x0.2344** -- v(0.2344, 0.5), Ghia station near v_max

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.17685157 | 0.17527 (benchmark) | 0.001582 | 0.009024 |
| medium | 0.17912148 | 0.17527 (benchmark) | 0.003851 | 0.02197 |
| fine | 0.17882592 | 0.17527 (benchmark) | 0.003556 | 0.02029 |
| extrapolated | -- | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | -- | 2 | -- | -- | -- | -- | -- | oscillatory | no |

- epsilon21 and epsilon32 have opposite signs (R = -0.130207): oscillatory convergence -- no observed order or Richardson extrapolation is reported
- grid independence: oscillatory sequence: only the oscillation bound 0.5(max-min) = 0.00113496 is available

**v_x0.8047** -- v(0.8047, 0.5), Ghia station near v_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.24932676 | -0.24533 (benchmark) | -0.003997 | 0.01629 |
| medium | -0.2528728 | -0.24533 (benchmark) | -0.007543 | 0.03075 |
| fine | -0.2527926 | -0.24533 (benchmark) | -0.007463 | 0.03042 |
| extrapolated | -- | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | -- | 2 | -- | -- | -- | -- | -- | oscillatory | no |

- epsilon21 and epsilon32 have opposite signs (R = -0.0226182): oscillatory convergence -- no observed order or Richardson extrapolation is reported
- grid independence: oscillatory sequence: only the oscillation bound 0.5(max-min) = 0.00177302 is available


### Grid convergence: cavity_re100_upwind_20_40_80

Re = 100, first-order upwind, grids 20/40/80

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 20x20 | 20 x 20 | 400 | 0.05 | 4.9 | Converged (3036 it) | yes |
| 40x40 | 40 x 40 | 1600 | 0.025 | 25.3 | Converged (5615 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 175.5 | Converged (9643 it) | yes |

**u_center** -- u(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.17281389 | -0.20581 (benchmark) | 0.033 | 0.1603 |
| medium | -0.19005545 | -0.20581 (benchmark) | 0.01575 | 0.07655 |
| fine | -0.19931506 | -0.20581 (benchmark) | 0.006495 | 0.03156 |
| extrapolated | -0.21005682 | | -0.004247 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 0.8969 | 1 | -0.21005682 | 0.01343 | 0.06737 | 0.1315 | 0.9310 | asymptotic | no |

- monotonic convergence, observed order 0.896868 (formal 1); asymptotic ratio 0.93101 within 1 +/- 0.1
- grid independence: GCI21 0.0673667 > threshold 0.01

**v_center** -- v(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.041916274 | 0.05454 (benchmark) | -0.01262 | 0.2315 |
| medium | 0.047946089 | 0.05454 (benchmark) | -0.006594 | 0.1209 |
| fine | 0.052239178 | 0.05454 (benchmark) | -0.002301 | 0.04219 |
| extrapolated | 0.062851441 | | 0.008311 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 0.4901 | 1 | 0.062851441 | 0.01327 | 0.2539 | 0.3886 | 0.7023 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 0.490098 (formal 1); asymptotic ratio 0.70227 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**u_y0.4531** -- u(0.5, 0.4531), Ghia station near u_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.17367706 | -0.2109 (benchmark) | 0.03722 | 0.1765 |
| medium | -0.19210838 | -0.2109 (benchmark) | 0.01879 | 0.0891 |
| fine | -0.20250534 | -0.2109 (benchmark) | 0.008395 | 0.0398 |
| extrapolated | -0.2159596 | | -0.00506 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 0.8260 | 1 | -0.2159596 | 0.01682 | 0.08305 | 0.1552 | 0.8864 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 0.825999 (formal 1); asymptotic ratio 0.886381 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_x0.2344** -- v(0.2344, 0.5), Ghia station near v_max

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.15464404 | 0.17527 (benchmark) | -0.02063 | 0.1177 |
| medium | 0.16673625 | 0.17527 (benchmark) | -0.008534 | 0.04869 |
| fine | 0.17328827 | 0.17527 (benchmark) | -0.001982 | 0.01131 |
| extrapolated | 0.18103693 | | 0.005767 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 0.8841 | 1 | 0.18103693 | 0.009686 | 0.05589 | 0.1072 | 0.9228 | asymptotic | no |

- monotonic convergence, observed order 0.884065 (formal 1); asymptotic ratio 0.922784 within 1 +/- 0.1
- grid independence: GCI21 0.0558943 > threshold 0.01

**v_x0.8047** -- v(0.8047, 0.5), Ghia station near v_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.20491551 | -0.24533 (benchmark) | 0.04041 | 0.1647 |
| medium | -0.2285443 | -0.24533 (benchmark) | 0.01679 | 0.06842 |
| fine | -0.24078047 | -0.24533 (benchmark) | 0.00455 | 0.01854 |
| extrapolated | -0.25392267 | | -0.008593 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 0.9494 | 1 | -0.25392267 | 0.01643 | 0.06823 | 0.1388 | 0.9655 | asymptotic | no |

- monotonic convergence, observed order 0.949393 (formal 1); asymptotic ratio 0.96553 within 1 +/- 0.1
- grid independence: GCI21 0.0682271 > threshold 0.01


- [x] cavity_re100/20x20/quick: solve_accepted: Converged after 3083 iterations
- [x] cavity_re100/20x20/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/quick: ghia_error: u L2 5.6901e-03 (bound 0.0075), v L2 4.8236e-03 (bound 0.0065)
- [x] cavity_re100/20x20/quick: bounded: max |U| 0.839325 vs lid speed 1
- [x] cavity_re100/40x40/quick: solve_accepted: Converged after 5500 iterations
- [x] cavity_re100/40x40/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/40x40/quick: ghia_error: u L2 1.4519e-03 (bound 0.002), v L2 2.9903e-03 (bound 0.004)
- [x] cavity_re100/40x40/quick: bounded: max |U| 0.923609 vs lid speed 1
- [x] cavity_re100/80x80/quick: solve_accepted: Converged after 9258 iterations
- [x] cavity_re100/80x80/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/80x80/quick: ghia_error: u L2 2.0904e-03 (bound 0.003), v L2 4.5102e-03 (bound 0.006)
- [x] cavity_re100/80x80/quick: bounded: max |U| 0.961956 vs lid speed 1
- [x] cavity_re100/160x160/quick: solve_accepted: Converged after 5084 iterations
- [x] cavity_re100/160x160/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/160x160/quick: ghia_error: u L2 2.1526e-03 (bound 0.003), v L2 4.3661e-03 (bound 0.006)
- [x] cavity_re100/160x160/quick: bounded: max |U| 0.980968 vs lid speed 1
- [x] cavity_re100/20x20/upwind: solve_accepted: Converged after 3036 iterations
- [x] cavity_re100/20x20/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/upwind: ghia_error: u L2 2.1050e-02 (bound 0.0275), v L2 1.4460e-02 (bound 0.019)
- [x] cavity_re100/20x20/upwind: bounded: max |U| 0.818643 vs lid speed 1
- [x] cavity_re100/40x40/upwind: solve_accepted: Converged after 5615 iterations
- [x] cavity_re100/40x40/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/40x40/upwind: ghia_error: u L2 1.0169e-02 (bound 0.0135), v L2 6.4477e-03 (bound 0.0085)
- [x] cavity_re100/40x40/upwind: bounded: max |U| 0.917779 vs lid speed 1
- [x] cavity_re100/80x80/upwind: solve_accepted: Converged after 9643 iterations
- [x] cavity_re100/80x80/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/80x80/upwind: ghia_error: u L2 4.3361e-03 (bound 0.0057), v L2 3.5693e-03 (bound 0.0047)
- [x] cavity_re100/80x80/upwind: bounded: max |U| 0.960425 vs lid speed 1
- [x] upwind_monotone_u_centerline: upwind L2 0.021050 -> 0.010169 -> 0.004336
- [x] upwind_monotone_v_centerline: upwind L2 0.014460 -> 0.006448 -> 0.003569
- [x] higher_order_smaller_discretisation_error_80x80: u_center |QUICK80 - QUICK160| 0.000245 vs |upwind80 - QUICK160| 0.009603; v_center |QUICK80 - QUICK160| 0.000215 vs |upwind80 - QUICK160| 0.005342; u_y0.4531 |QUICK80 - QUICK160| 0.000040 vs |upwind80 - QUICK160| 0.011086; v_x0.2344 |QUICK80 - QUICK160| 0.000296 vs |upwind80 - QUICK160| 0.005538; v_x0.8047 |QUICK80 - QUICK160| 0.000080 vs |upwind80 - QUICK160| 0.012012;
- [x] quick_solution_converged_40_80_160: u_center GCI21 0.000285; v_center GCI21 0.001384; u_y0.4531 GCI21 0.000006; v_x0.2344 oscillatory U 0.006347; v_x0.8047 oscillatory U 0.007014;

Limitations:

- Ghia et al. is itself a 129x129 numerical solution tabulated to 5 digits; beyond 40x40 the higher-order error vs Ghia plateaus at ~2e-3 (u) / ~4.4e-3 (v): a benchmark/comparison floor, not solution error (tightening the outer tolerance 1e-6 -> 1e-8 changes it by < 1e-5; 160x160 gives the same value).
- Collocated SIMPLE without Rhie-Chow interpolation (no pressure-checkerboard damping); the closed cavity's velocity is unaffected but the pressure field is not validated here.
- Runtimes are wall-clock of the build that ran the study (see summary) and are not deterministic.

Overall: PASSED
