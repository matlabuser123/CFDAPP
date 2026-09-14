### Validation: scheme_comparison

Convection-scheme accuracy / cost, lid-driven cavity: Re = 100 on 20/40/80, Re = 1000 on 40/80

Reference: Ghia, Ghia & Shin (1982), J. Comput. Phys. 48, 387-411, Tables I/II (Re = 100 and 1000 columns; validation/ghia/)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| cavity_re100/20x20/upwind | 100 | 20x20 | upwind | Converged | 3036 | 34136 / 150550 | 0 | 4.9 | yes |
| cavity_re100/20x20/central | 100 | 20x20 | central | Converged | 2954 | 34253 / 147733 | 0 | 5.5 | yes |
| cavity_re100/20x20/linear_upwind | 100 | 20x20 | linear_upwind | Converged | 3091 | 36061 / 151816 | 0 | 6.0 | yes |
| cavity_re100/20x20/quick | 100 | 20x20 | quick | Converged | 3083 | 36967 / 151537 | 0 | 5.9 | yes |
| cavity_re100/40x40/upwind | 100 | 40x40 | upwind | Converged | 5615 | 59881 / 54988 | 0 | 29.0 | yes |
| cavity_re100/40x40/central | 100 | 40x40 | central | Converged | 5479 | 59808 / 58387 | 0 | 30.5 | yes |
| cavity_re100/40x40/linear_upwind | 100 | 40x40 | linear_upwind | Converged | 5517 | 60599 / 58271 | 0 | 31.6 | yes |
| cavity_re100/40x40/quick | 100 | 40x40 | quick | Converged | 5500 | 59761 / 58120 | 0 | 28.7 | yes |
| cavity_re100/80x80/upwind | 100 | 80x80 | upwind | Converged | 9643 | 79024 / 48522 | 0 | 206.5 | yes |
| cavity_re100/80x80/central | 100 | 80x80 | central | Converged | 9507 | 84393 / 50485 | 0 | 221.2 | yes |
| cavity_re100/80x80/linear_upwind | 100 | 80x80 | linear_upwind | Converged | 9083 | 81038 / 50069 | 0 | 225.5 | yes |
| cavity_re100/80x80/quick | 100 | 80x80 | quick | Converged | 9258 | 81177 / 50041 | 0 | 218.1 | yes |
| cavity_re1000/40x40/upwind | 1000 | 40x40 | upwind | Converged | 4691 | 72866 / 72936 | 0 | 23.4 | yes |
| cavity_re1000/40x40/central | 1000 | 40x40 | central | Converged | 3389 | 65941 / 100207 | 0 | 20.4 | yes |
| cavity_re1000/40x40/linear_upwind | 1000 | 40x40 | linear_upwind | Converged | 3614 | 66889 / 97266 | 0 | 22.0 | yes |
| cavity_re1000/40x40/quick | 1000 | 40x40 | quick | Converged | 3582 | 74028 / 99660 | 0 | 20.9 | yes |
| cavity_re1000/80x80/upwind | 1000 | 80x80 | upwind | Converged | 6547 | 82642 / 87149 | 0 | 145.3 | yes |
| cavity_re1000/80x80/central | 1000 | 80x80 | central | Converged | 4637 | 102500 / 113552 | 0 | 124.7 | yes |
| cavity_re1000/80x80/linear_upwind | 1000 | 80x80 | linear_upwind | Converged | 4105 | 100113 / 114274 | 0 | 117.6 | yes |
| cavity_re1000/80x80/quick | 1000 | 80x80 | quick | Converged | 4289 | 102277 / 114476 | 0 | 117.0 | yes |

**u_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/upwind | 1.6212e-02 | 2.1050e-02 | 3.7223e-02 |
| cavity_re100/20x20/central | 5.1650e-03 | 6.2556e-03 | 1.0712e-02 |
| cavity_re100/20x20/linear_upwind | 4.6777e-03 | 5.5470e-03 | 1.0372e-02 |
| cavity_re100/20x20/quick | 4.8012e-03 | 5.6901e-03 | 1.0462e-02 |
| cavity_re100/40x40/upwind | 7.6157e-03 | 1.0169e-02 | 1.8792e-02 |
| cavity_re100/40x40/central | 8.4752e-04 | 1.2278e-03 | 3.7062e-03 |
| cavity_re100/40x40/linear_upwind | 1.2295e-03 | 1.7063e-03 | 5.3301e-03 |
| cavity_re100/40x40/quick | 1.0455e-03 | 1.4519e-03 | 4.6064e-03 |
| cavity_re100/80x80/upwind | 3.2852e-03 | 4.3361e-03 | 8.4948e-03 |
| cavity_re100/80x80/central | 1.4596e-03 | 2.0093e-03 | 4.7906e-03 |
| cavity_re100/80x80/linear_upwind | 1.5851e-03 | 2.1660e-03 | 5.2582e-03 |
| cavity_re100/80x80/quick | 1.5255e-03 | 2.0904e-03 | 5.0344e-03 |
| cavity_re1000/40x40/upwind | 7.0340e-02 | 8.5459e-02 | 1.7414e-01 |
| cavity_re1000/40x40/central | 3.5334e-02 | 4.9915e-02 | 1.0401e-01 |
| cavity_re1000/40x40/linear_upwind | 3.8094e-02 | 5.6487e-02 | 1.1622e-01 |
| cavity_re1000/40x40/quick | 3.7027e-02 | 5.4440e-02 | 1.1275e-01 |
| cavity_re1000/80x80/upwind | 4.2096e-02 | 5.1644e-02 | 1.0615e-01 |
| cavity_re1000/80x80/central | 6.1973e-03 | 9.0858e-03 | 1.8523e-02 |
| cavity_re1000/80x80/linear_upwind | 6.8544e-03 | 1.0172e-02 | 2.0951e-02 |
| cavity_re1000/80x80/quick | 6.4485e-03 | 9.5906e-03 | 1.9936e-02 |

**v_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/upwind | 1.0872e-02 | 1.4460e-02 | 4.0414e-02 |
| cavity_re100/20x20/central | 4.1999e-03 | 5.6347e-03 | 1.4095e-02 |
| cavity_re100/20x20/linear_upwind | 3.1654e-03 | 4.5023e-03 | 1.2378e-02 |
| cavity_re100/20x20/quick | 3.4504e-03 | 4.8236e-03 | 1.2624e-02 |
| cavity_re100/40x40/upwind | 4.9208e-03 | 6.4477e-03 | 1.6786e-02 |
| cavity_re100/40x40/central | 2.3645e-03 | 2.9875e-03 | 6.4198e-03 |
| cavity_re100/40x40/linear_upwind | 2.5186e-03 | 2.9752e-03 | 6.5404e-03 |
| cavity_re100/40x40/quick | 2.4766e-03 | 2.9903e-03 | 6.5190e-03 |
| cavity_re100/80x80/upwind | 2.7510e-03 | 3.5693e-03 | 6.9850e-03 |
| cavity_re100/80x80/central | 3.8243e-03 | 4.4893e-03 | 8.7197e-03 |
| cavity_re100/80x80/linear_upwind | 3.8868e-03 | 4.5280e-03 | 8.7107e-03 |
| cavity_re100/80x80/quick | 3.8586e-03 | 4.5102e-03 | 8.7131e-03 |
| cavity_re1000/40x40/upwind | 9.4732e-02 | 1.0824e-01 | 1.7155e-01 |
| cavity_re1000/40x40/central | 3.9816e-02 | 5.0319e-02 | 9.6260e-02 |
| cavity_re1000/40x40/linear_upwind | 3.8988e-02 | 5.2075e-02 | 1.0726e-01 |
| cavity_re1000/40x40/quick | 3.9242e-02 | 5.1738e-02 | 1.0454e-01 |
| cavity_re1000/80x80/upwind | 5.3233e-02 | 6.2553e-02 | 1.0527e-01 |
| cavity_re1000/80x80/central | 5.1947e-03 | 6.9259e-03 | 1.3713e-02 |
| cavity_re1000/80x80/linear_upwind | 4.9489e-03 | 8.5669e-03 | 1.8763e-02 |
| cavity_re1000/80x80/quick | 4.6408e-03 | 7.6264e-03 | 1.6652e-02 |

### Grid convergence: cavity_re100_upwind_20_40_80

Re = 100, upwind, grids 20/40/80

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 20x20 | 20 x 20 | 400 | 0.05 | 4.9 | Converged (3036 it) | yes |
| 40x40 | 40 x 40 | 1600 | 0.025 | 29.0 | Converged (5615 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 206.5 | Converged (9643 it) | yes |

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


### Grid convergence: cavity_re100_central_20_40_80

Re = 100, central, grids 20/40/80

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 20x20 | 20 x 20 | 400 | 0.05 | 5.5 | Converged (2954 it) | yes |
| 40x40 | 40 x 40 | 1600 | 0.025 | 30.5 | Converged (5479 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 221.2 | Converged (9507 it) | yes |

**u_center** -- u(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.19792784 | -0.20581 (benchmark) | 0.007882 | 0.0383 |
| medium | -0.20637026 | -0.20581 (benchmark) | -0.0005603 | 0.002722 |
| fine | -0.20846919 | -0.20581 (benchmark) | -0.002659 | 0.01292 |
| extrapolated | -0.20916367 | | -0.003354 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.0080 | 2 | -0.20916367 | 0.0008681 | 0.004164 | 0.01692 | 1.0056 | asymptotic | yes |

- monotonic convergence, observed order 2.00801 (formal 2); asymptotic ratio 1.00557 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00416421 <= threshold 0.01

**v_center** -- v(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.053231499 | 0.05454 (benchmark) | -0.001309 | 0.02399 |
| medium | 0.056515764 | 0.05454 (benchmark) | 0.001976 | 0.03623 |
| fine | 0.057389768 | 0.05454 (benchmark) | 0.00285 | 0.05225 |
| extrapolated | 0.057706697 | | 0.003167 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 1.9099 | 2 | 0.057706697 | 0.0003962 | 0.006903 | 0.02634 | 0.9394 | asymptotic | yes |

- monotonic convergence, observed order 1.90986 (formal 2); asymptotic ratio 0.939431 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00690301 <= threshold 0.01

**u_y0.4531** -- u(0.5, 0.4531), Ghia station near u_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.20093432 | -0.2109 (benchmark) | 0.009966 | 0.04725 |
| medium | -0.21089078 | -0.2109 (benchmark) | 9.218e-06 | 4.371e-05 |
| fine | -0.21330398 | -0.2109 (benchmark) | -0.002404 | 0.0114 |
| extrapolated | -0.21407599 | | -0.003176 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.0447 | 2 | -0.21407599 | 0.000965 | 0.004524 | 0.01888 | 1.0315 | asymptotic | yes |

- monotonic convergence, observed order 2.04469 (formal 2); asymptotic ratio 1.03146 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00452414 <= threshold 0.01

**v_x0.2344** -- v(0.2344, 0.5), Ghia station near v_max

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.16665522 | 0.17527 (benchmark) | -0.008615 | 0.04915 |
| medium | 0.17630009 | 0.17527 (benchmark) | 0.00103 | 0.005877 |
| fine | 0.1789717 | 0.17527 (benchmark) | 0.003702 | 0.02112 |
| extrapolated | 0.17999525 | | 0.004725 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 1.8521 | 2 | 0.17999525 | 0.001279 | 0.007149 | 0.0262 | 0.9025 | asymptotic | yes |

- monotonic convergence, observed order 1.85205 (formal 2); asymptotic ratio 0.902534 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00714884 <= threshold 0.01

**v_x0.8047** -- v(0.8047, 0.5), Ghia station near v_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.23123537 | -0.24533 (benchmark) | 0.01409 | 0.05745 |
| medium | -0.24861837 | -0.24533 (benchmark) | -0.003288 | 0.0134 |
| fine | -0.25269309 | -0.24533 (benchmark) | -0.007363 | 0.03001 |
| extrapolated | -0.25394069 | | -0.008611 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.0929 | 2 | -0.25394069 | 0.001559 | 0.006172 | 0.02676 | 1.0665 | asymptotic | yes |

- monotonic convergence, observed order 2.0929 (formal 2); asymptotic ratio 1.06651 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.0061715 <= threshold 0.01


### Grid convergence: cavity_re100_linear_upwind_20_40_80

Re = 100, linear_upwind, grids 20/40/80

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 20x20 | 20 x 20 | 400 | 0.05 | 6.0 | Converged (3091 it) | yes |
| 40x40 | 40 x 40 | 1600 | 0.025 | 31.6 | Converged (5517 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 225.5 | Converged (9083 it) | yes |

**u_center** -- u(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.20260558 | -0.20581 (benchmark) | 0.003204 | 0.01557 |
| medium | -0.20784437 | -0.20581 (benchmark) | -0.002034 | 0.009885 |
| fine | -0.20886247 | -0.20581 (benchmark) | -0.003052 | 0.01483 |
| extrapolated | -0.20910805 | | -0.003298 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.3634 | 2 | -0.20910805 | 0.000307 | 0.00147 | 0.0076 | 1.2864 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.36336 (formal 2); asymptotic ratio 1.28642 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_center** -- v(0.5, 0.5)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.052115253 | 0.05454 (benchmark) | -0.002425 | 0.04446 |
| medium | 0.056262661 | 0.05454 (benchmark) | 0.001723 | 0.03159 |
| fine | 0.057339128 | 0.05454 (benchmark) | 0.002799 | 0.05132 |
| extrapolated | 0.057716464 | | 0.003176 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 1.9459 | 2 | 0.057716464 | 0.0004717 | 0.008226 | 0.0323 | 0.9632 | asymptotic | yes |

- monotonic convergence, observed order 1.94591 (formal 2); asymptotic ratio 0.9632 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00822599 <= threshold 0.01

**u_y0.4531** -- u(0.5, 0.4531), Ghia station near u_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.20560653 | -0.2109 (benchmark) | 0.005293 | 0.0251 |
| medium | -0.21256816 | -0.2109 (benchmark) | -0.001668 | 0.00791 |
| fine | -0.21377823 | -0.2109 (benchmark) | -0.002878 | 0.01365 |
| extrapolated | -0.21403282 | | -0.003133 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.5243 | 2 | -0.21403282 | 0.0003182 | 0.001489 | 0.008613 | 1.4383 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.52434 (formal 2); asymptotic ratio 1.43827 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**v_x0.2344** -- v(0.2344, 0.5), Ghia station near v_max

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.16882982 | 0.17527 (benchmark) | -0.00644 | 0.03674 |
| medium | 0.17725148 | 0.17527 (benchmark) | 0.001981 | 0.01131 |
| fine | 0.17925272 | 0.17527 (benchmark) | 0.003983 | 0.02272 |
| extrapolated | 0.17987651 | | 0.004607 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.0732 | 2 | 0.17987651 | 0.0007797 | 0.00435 | 0.01851 | 1.0521 | asymptotic | yes |

- monotonic convergence, observed order 2.07321 (formal 2); asymptotic ratio 1.05205 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00434992 <= threshold 0.01

**v_x0.8047** -- v(0.8047, 0.5), Ghia station near v_min

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -0.2329522 | -0.24533 (benchmark) | 0.01238 | 0.05045 |
| medium | -0.24967469 | -0.24533 (benchmark) | -0.004345 | 0.01771 |
| fine | -0.25301509 | -0.24533 (benchmark) | -0.007685 | 0.03133 |
| extrapolated | -0.25384891 | | -0.008519 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 2 | 2.3237 | 2 | -0.25384891 | 0.001042 | 0.004119 | 0.0209 | 1.2515 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 2.3237 (formal 2); asymptotic ratio 1.25153 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic


### Grid convergence: cavity_re100_quick_20_40_80

Re = 100, quick, grids 20/40/80

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 20x20 | 20 x 20 | 400 | 0.05 | 5.9 | Converged (3083 it) | yes |
| 40x40 | 40 x 40 | 1600 | 0.025 | 28.7 | Converged (5500 it) | yes |
| 80x80 | 80 x 80 | 6400 | 0.0125 | 218.1 | Converged (9258 it) | yes |

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


- [x] cavity_re100/20x20/upwind: solve_accepted: Converged after 3036 iterations
- [x] cavity_re100/20x20/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/upwind: bounded: max |U| 0.818643 vs lid speed 1
- [x] cavity_re100/20x20/central: solve_accepted: Converged after 2954 iterations
- [x] cavity_re100/20x20/central: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/linear_upwind: solve_accepted: Converged after 3091 iterations
- [x] cavity_re100/20x20/linear_upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/quick: solve_accepted: Converged after 3083 iterations
- [x] cavity_re100/20x20/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/quick: bounded: max |U| 0.839325 vs lid speed 1
- [x] cavity_re100/40x40/upwind: solve_accepted: Converged after 5615 iterations
- [x] cavity_re100/40x40/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/40x40/upwind: bounded: max |U| 0.917779 vs lid speed 1
- [x] cavity_re100/40x40/central: solve_accepted: Converged after 5479 iterations
- [x] cavity_re100/40x40/central: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/40x40/linear_upwind: solve_accepted: Converged after 5517 iterations
- [x] cavity_re100/40x40/linear_upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/40x40/quick: solve_accepted: Converged after 5500 iterations
- [x] cavity_re100/40x40/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/40x40/quick: bounded: max |U| 0.923609 vs lid speed 1
- [x] cavity_re100/80x80/upwind: solve_accepted: Converged after 9643 iterations
- [x] cavity_re100/80x80/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/80x80/upwind: bounded: max |U| 0.960425 vs lid speed 1
- [x] cavity_re100/80x80/central: solve_accepted: Converged after 9507 iterations
- [x] cavity_re100/80x80/central: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/80x80/linear_upwind: solve_accepted: Converged after 9083 iterations
- [x] cavity_re100/80x80/linear_upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/80x80/quick: solve_accepted: Converged after 9258 iterations
- [x] cavity_re100/80x80/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/80x80/quick: bounded: max |U| 0.961956 vs lid speed 1
- [x] cavity_re1000/40x40/upwind: solve_accepted: Converged after 4691 iterations
- [x] cavity_re1000/40x40/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/40x40/upwind: bounded: max |U| 0.786494 vs lid speed 1
- [x] cavity_re1000/40x40/central: solve_accepted: Converged after 3389 iterations
- [x] cavity_re1000/40x40/central: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/40x40/linear_upwind: solve_accepted: Converged after 3614 iterations
- [x] cavity_re1000/40x40/linear_upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/40x40/quick: solve_accepted: Converged after 3582 iterations
- [x] cavity_re1000/40x40/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/40x40/quick: bounded: max |U| 0.808984 vs lid speed 1
- [x] cavity_re1000/80x80/upwind: solve_accepted: Converged after 6547 iterations
- [x] cavity_re1000/80x80/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/80x80/upwind: bounded: max |U| 0.902085 vs lid speed 1
- [x] cavity_re1000/80x80/central: solve_accepted: Converged after 4637 iterations
- [x] cavity_re1000/80x80/central: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/80x80/linear_upwind: solve_accepted: Converged after 4105 iterations
- [x] cavity_re1000/80x80/linear_upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/80x80/quick: solve_accepted: Converged after 4289 iterations
- [x] cavity_re1000/80x80/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re1000/80x80/quick: bounded: max |U| 0.911642 vs lid speed 1
- [x] re100_20x20_central_below_upwind_u_centerline: 0.006256 < upwind 0.021050
- [x] re100_20x20_linear_upwind_below_upwind_u_centerline: 0.005547 < upwind 0.021050
- [x] re100_20x20_quick_below_upwind_u_centerline: 0.005690 < upwind 0.021050
- [x] re100_20x20_central_below_upwind_v_centerline: 0.005635 < upwind 0.014460
- [x] re100_20x20_linear_upwind_below_upwind_v_centerline: 0.004502 < upwind 0.014460
- [x] re100_20x20_quick_below_upwind_v_centerline: 0.004824 < upwind 0.014460
- [x] re1000_40x40_central_below_upwind_u_centerline: 0.049915 < upwind 0.085459
- [x] re1000_40x40_linear_upwind_below_upwind_u_centerline: 0.056487 < upwind 0.085459
- [x] re1000_40x40_quick_below_upwind_u_centerline: 0.054440 < upwind 0.085459
- [x] re1000_40x40_central_below_upwind_v_centerline: 0.050319 < upwind 0.108243
- [x] re1000_40x40_linear_upwind_below_upwind_v_centerline: 0.052075 < upwind 0.108243
- [x] re1000_40x40_quick_below_upwind_v_centerline: 0.051738 < upwind 0.108243
- [x] re1000_80x80_central_below_upwind_u_centerline: 0.009086 < upwind 0.051644
- [x] re1000_80x80_linear_upwind_below_upwind_u_centerline: 0.010172 < upwind 0.051644
- [x] re1000_80x80_quick_below_upwind_u_centerline: 0.009591 < upwind 0.051644
- [x] re1000_80x80_central_below_upwind_v_centerline: 0.006926 < upwind 0.062553
- [x] re1000_80x80_linear_upwind_below_upwind_v_centerline: 0.008567 < upwind 0.062553
- [x] re1000_80x80_quick_below_upwind_v_centerline: 0.007626 < upwind 0.062553

Limitations:

- At Re = 100 beyond 40x40 the second-order-type schemes reach the Ghia comparison floor (~2e-3 u, ~4.4e-3 v); there v can be further from Ghia than first-order upwind's error -- an artefact of comparing against a numerical benchmark, not a scheme defect.
- central and linear_upwind are unbounded schemes: boundedness is recorded (max_velocity_magnitude), not required.
- Runtimes: Release build, runs sequential in one process on an otherwise idle machine (see summary.md); wall-clock, not deterministic.

Overall: PASSED
