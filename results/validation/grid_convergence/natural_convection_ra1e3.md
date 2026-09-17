### Grid convergence: natural_convection_ra1e3

Differentially heated square cavity, Ra = 1e3, Pr = 0.71, SIMPLE + ThermalSolver Picard coupling (first-order upwind convection); references: de Vahl Davis (1983) benchmark data

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| coarse | 10 x 10 | 100 | 0.1 | 14.6 | Converged (50 it) | yes |
| medium | 15 x 15 | 225 | 0.0666667 | 60.8 | Converged (50 it) | yes |
| fine | 20 x 20 | 400 | 0.05 | 99.6 | Converged (50 it) | yes |

**nu_avg** -- average hot-wall Nusselt number

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 1.1451602 | 1.12 (benchmark) | 0.02516 | 0.02246 |
| medium | 1.1376922 | 1.12 (benchmark) | 0.01769 | 0.0158 |
| fine | 1.1336411 | 1.12 (benchmark) | 0.01364 | 0.01218 |
| extrapolated | 1.1172753 | | -0.002725 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | 0.7688 | 1 | 1.1172753 | 0.02046 | 0.01805 | 0.02243 | 0.9217 | asymptotic | no |

- monotonic convergence, observed order 0.768796 (formal 1); asymptotic ratio 0.921718 within 1 +/- 0.1
- grid independence: GCI21 0.0180457 > threshold 0.01

**u_max** -- max u on the vertical mid-line (sampled at cell centres)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 3.1360254 | 3.634 (benchmark) | -0.498 | 0.137 |
| medium | 3.4450524 | 3.634 (benchmark) | -0.1889 | 0.05199 |
| fine | 3.4386848 | 3.634 (benchmark) | -0.1953 | 0.05375 |
| extrapolated | -- | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | -- | 1 | -- | -- | -- | -- | -- | oscillatory | no |

- epsilon21 and epsilon32 have opposite signs (R = -0.0206054): oscillatory convergence -- no observed order or Richardson extrapolation is reported
- grid independence: oscillatory sequence: only the oscillation bound 0.5(max-min) = 0.154513 is available

**v_max** -- max v on the horizontal mid-line (sampled at cell centres)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 3.1997917 | 3.679 (benchmark) | -0.4792 | 0.1303 |
| medium | 3.4496943 | 3.679 (benchmark) | -0.2293 | 0.06233 |
| fine | 3.5056178 | 3.679 (benchmark) | -0.1734 | 0.04713 |
| extrapolated | 3.5421221 | | -0.1369 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | 3.2293 | 1 | 3.5421221 | 0.04563 | 0.01302 | 0.03349 | 2.2343 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 3.22925 (formal 1); asymptotic ratio 2.23433 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic
