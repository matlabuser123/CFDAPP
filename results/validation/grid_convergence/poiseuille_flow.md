### Grid convergence: poiseuille_flow

Planar Poiseuille channel L = 8H, Re = 10, SIMPLE (upwind convection, central diffusion)

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| coarse | 64 x 8 | 512 | 0.125 | 14.0 | Converged (1524 it) | yes |
| medium | 96 x 12 | 1152 | 0.0833333 | 25.8 | Converged (1135 it) | yes |
| fine | 144 x 18 | 2592 | 0.0555556 | 57.0 | Converged (856 it) | yes |

**centerline_velocity** -- u(0.75 L, H/2), fully developed; exact 1.5 U_mean

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 1.4651197 | 1.5 (analytical) | -0.03488 | 0.02325 |
| medium | 1.484429 | 1.5 (analytical) | -0.01557 | 0.01038 |
| fine | 1.4930663 | 1.5 (analytical) | -0.006934 | 0.004622 |
| extrapolated | 1.5000565 | | 5.655e-05 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.5 | 1.5 | 1.9842 | 2 | 1.5000565 | 0.008738 | 0.005852 | 0.01316 | 0.9936 | asymptotic | yes |

- monotonic convergence, observed order 1.98417 (formal 2); asymptotic ratio 0.9936 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.0058523 <= threshold 0.01

**velocity_profile_l2_error** -- RMS error of u(0.75 L, y) vs. the analytical profile (exact limit 0)

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 0.0075931286 | 0 (analytical) | 0.007593 | -- |
| medium | 0.0035093918 | 0 (analytical) | 0.003509 | -- |
| fine | 0.0016012902 | 0 (analytical) | 0.001601 | -- |
| extrapolated | -7.2176019e-05 | | -7.218e-05 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.5 | 1.5 | 1.8766 | 2 | -7.2176019e-05 | 0.002092 | 1.306 | 1.276 | 0.9512 | asymptotic | no |

- monotonic convergence, observed order 1.87662 (formal 2); asymptotic ratio 0.951204 within 1 +/- 0.1
- grid independence: no grid-independence threshold configured
