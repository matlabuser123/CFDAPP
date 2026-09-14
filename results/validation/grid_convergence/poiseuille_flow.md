### Grid convergence: poiseuille_flow

Planar Poiseuille channel L = 8H, Re = 10, SIMPLE (upwind convection, central diffusion)

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| coarse | 64 x 8 | 512 | 0.125 | 159.3 | Converged (1319 it) | yes |
| medium | 96 x 12 | 1152 | 0.0833333 | 308.0 | Converged (985 it) | yes |
| fine | 144 x 18 | 2592 | 0.0555556 | 606.6 | Converged (744 it) | yes |

**centerline_velocity** -- u(0.75 L, H/2), fully developed; exact 1.5 U_mean

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 1.454547 | 1.5 (analytical) | -0.04545 | 0.0303 |
| medium | 1.479452 | 1.5 (analytical) | -0.02055 | 0.0137 |
| fine | 1.4907976 | 1.5 (analytical) | -0.009202 | 0.006135 |
| extrapolated | 1.5002906 | | 0.0002906 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.5 | 1.5 | 1.9391 | 2 | 1.5002906 | 0.01187 | 0.00796 | 0.01761 | 0.9756 | asymptotic | yes |

- monotonic convergence, observed order 1.93912 (formal 2); asymptotic ratio 0.975617 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00795973 <= threshold 0.01

**velocity_profile_l2_error** -- RMS error of u(0.75 L, y) vs. the analytical profile (exact limit 0)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.013579299 | 0 (analytical) | 0.01358 | -- |
| medium | 0.0064336958 | 0 (analytical) | 0.006434 | -- |
| fine | 0.0029688144 | 0 (analytical) | 0.002969 | -- |
| extrapolated | -0.00029288406 | | -0.0002929 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.5 | 1.5 | 1.7852 | 2 | -0.00029288406 | 0.004077 | 1.373 | 1.307 | 0.9166 | asymptotic | no |

- monotonic convergence, observed order 1.78516 (formal 2); asymptotic ratio 0.916575 within 1 +/- 0.1
- grid independence: no grid-independence threshold configured
