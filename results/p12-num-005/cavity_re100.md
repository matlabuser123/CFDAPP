### Grid convergence: cavity_re100

Lid-driven cavity Re = 100, SIMPLE (first-order upwind convection); references: Ghia, Ghia & Shin (1982) Table I/II (benchmark data)

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| coarse | 20 x 20 | 400 | 0.05 | 29.9 | Converged (3036 it) | yes |
| medium | 40 x 40 | 1600 | 0.025 | 165.6 | Converged (5615 it) | yes |
| fine | 80 x 80 | 6400 | 0.0125 | 1364.8 | Converged (9643 it) | yes |

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

**u_x0.5_y0.4531** -- u(0.5, 0.4531), Ghia station near u_min

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

**v_x0.2344_y0.5** -- v(0.2344, 0.5), Ghia station near v_max

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

**v_x0.8047_y0.5** -- v(0.8047, 0.5), Ghia station near v_min

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
