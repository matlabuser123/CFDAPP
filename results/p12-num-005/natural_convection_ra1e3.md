### Grid convergence: natural_convection_ra1e3

Differentially heated square cavity, Ra = 1e3, Pr = 0.71, SIMPLE + ThermalSolver Picard coupling (first-order upwind convection); references: de Vahl Davis (1983) benchmark data

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| coarse | 10 x 10 | 100 | 0.1 | 22.0 | Converged (50 it) | yes |
| medium | 15 x 15 | 225 | 0.0666667 | 119.2 | Converged (50 it) | yes |
| fine | 20 x 20 | 400 | 0.05 | 211.1 | Converged (50 it) | yes |

**nu_avg** -- average hot-wall Nusselt number

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 1.1743839 | 1.12 (benchmark) | 0.05438 | 0.04856 |
| medium | 1.1504709 | 1.12 (benchmark) | 0.03047 | 0.02721 |
| fine | 1.1406738 | 1.12 (benchmark) | 0.02067 | 0.01846 |
| extrapolated | 1.1233868 | | 0.003387 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | 1.5607 | 1 | 1.1233868 | 0.02161 | 0.01894 | 0.02943 | 1.2204 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 1.56073 (formal 1); asymptotic ratio 1.22041 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic

**u_max** -- max u on the vertical mid-line (sampled at cell centres)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 3.2545387 | 3.634 (benchmark) | -0.3795 | 0.1044 |
| medium | 3.5392581 | 3.634 (benchmark) | -0.09474 | 0.02607 |
| fine | 3.4936786 | 3.634 (benchmark) | -0.1403 | 0.03861 |
| extrapolated | -- | | -- | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | -- | 1 | -- | -- | -- | -- | -- | oscillatory | no |

- epsilon21 and epsilon32 have opposite signs (R = -0.160086): oscillatory convergence -- no observed order or Richardson extrapolation is reported
- grid independence: oscillatory sequence: only the oscillation bound 0.5(max-min) = 0.14236 is available

**v_max** -- max v on the horizontal mid-line (sampled at cell centres)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 3.4320004 | 3.679 (benchmark) | -0.247 | 0.06714 |
| medium | 3.5427693 | 3.679 (benchmark) | -0.1362 | 0.03703 |
| fine | 3.5632985 | 3.679 (benchmark) | -0.1157 | 0.03145 |
| extrapolated | 3.5739209 | | -0.1051 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.333 | 1.5 | 3.7399 | 1 | 3.5739209 | 0.01328 | 0.003726 | 0.01099 | 2.6978 | monotonic_not_asymptotic | no |

- monotonic convergence, observed order 3.73991 (formal 1); asymptotic ratio 2.69784 outside 1 +/- 0.1
- grid independence: status is monotonic_not_asymptotic, not asymptotic
