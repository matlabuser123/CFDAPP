### MMS: momentum_mms_linear_upwind

Momentum-only MMS: production relaxed-momentum assembly (alpha = 1) with exact pressure, exact face mass flux and analytical forcing; Picard on the lagged terms from U = 0

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 17 | 1.54e-33 | 0.9 |
| 32x32 | 1024 | 0.03125 | Converged | 13 | 7.7e-34 | 3.7 |
| 64x64 | 4096 | 0.015625 | Converged | 9 | 7.7e-34 | 16.3 |
| 128x128 | 16384 | 0.0078125 | Converged | 8 | 9.63e-34 | 109.0 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9817e-03 | 2.4552e-03 | 7.0221e-03 |
| 32x32 | 4.2093e-04 | 5.3007e-04 | 1.4986e-03 |
| 64x64 | 1.0129e-04 | 1.2798e-04 | 4.0706e-04 |
| 128x128 | 2.5369e-05 | 3.2249e-05 | 9.5909e-05 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.1648e-03 | 3.7696e-03 | 9.8310e-03 |
| 32x32 | 7.8515e-04 | 9.4915e-04 | 2.1819e-03 |
| 64x64 | 1.9435e-04 | 2.3797e-04 | 5.5993e-04 |
| 128x128 | 4.8310e-05 | 5.9844e-05 | 1.4204e-04 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.0086e-03 | 4.4987e-03 | 1.0095e-02 |
| 32x32 | 9.6426e-04 | 1.0871e-03 | 2.2732e-03 |
| 64x64 | 2.3953e-04 | 2.7020e-04 | 5.6715e-04 |
| 128x128 | 6.0273e-05 | 6.7980e-05 | 1.4289e-04 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.0882e-03 | 2.5791e-03 | 7.0221e-03 |
| 32x32 | 4.1558e-04 | 5.2585e-04 | 1.2199e-03 |
| 64x64 | 9.9852e-05 | 1.2616e-04 | 3.7985e-04 |
| 128x128 | 2.5160e-05 | 3.1972e-05 | 9.1796e-05 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.2693e-03 | 3.8814e-03 | 9.8310e-03 |
| 32x32 | 8.1621e-04 | 9.7707e-04 | 2.1819e-03 |
| 64x64 | 1.9882e-04 | 2.4185e-04 | 5.5993e-04 |
| 128x128 | 4.8864e-05 | 6.0347e-05 | 1.4204e-04 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6338e-03 | 1.9975e-03 | 6.4284e-03 |
| 32x32 | 4.5977e-04 | 5.5977e-04 | 1.4986e-03 |
| 64x64 | 1.2322e-04 | 1.5298e-04 | 4.0706e-04 |
| 128x128 | 3.1880e-05 | 3.9957e-05 | 9.5909e-05 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.8234e-03 | 3.3790e-03 | 9.7019e-03 |
| 32x32 | 5.5971e-04 | 7.1444e-04 | 1.8779e-03 |
| 64x64 | 1.2611e-04 | 1.6806e-04 | 4.5556e-04 |
| 128x128 | 3.0981e-05 | 4.1127e-05 | 1.0358e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 4.632, 4.142, 3.968 | 2.259, 2.071 | asymptotic |
| u | l1 | 2 | 4.708, 4.156, 3.993 | 2.288, 2.074 | asymptotic |
| u | linf | 2 | 4.686, 3.682, 4.244 | 2.339, 1.811 | monotonic_not_asymptotic |
| v | l2 | 2 | 3.972, 3.988, 3.977 | 1.988, 1.997 | asymptotic |
| v | l1 | 2 | 4.031, 4.040, 4.023 | 2.010, 2.016 | asymptotic |
| v | linf | 2 | 4.506, 3.897, 3.942 | 2.238, 1.957 | asymptotic |
| velocity | l2 | 2 | 4.138, 4.023, 3.975 | 2.062, 2.014 | asymptotic |
| u_interior | l2 | 2 | 4.905, 4.168, 3.946 | 2.361, 2.085 | asymptotic |
| u_boundary_ring | l2 | 2 | 3.568, 3.659, 3.829 | 1.821, 1.848 | monotonic_not_asymptotic |

- [x] u: u L1/L2/Linf decrease on every refinement: monotone
- [x] u: u l2 observed order in [1.80, 2.40]: finest-triplet observed order 2.071 (asymptotic vs formal 2); reduction factors 4.632 4.142 3.968
- [x] u: u l1 observed order in [1.80, 2.40]: finest-triplet observed order 2.074 (asymptotic vs formal 2); reduction factors 4.708 4.156 3.993
- [x] u: u linf observed order in [1.60, 2.40]: finest-triplet observed order 1.811 (monotonic_not_asymptotic vs formal 2); reduction factors 4.686 3.682 4.244
- [x] v: v L1/L2/Linf decrease on every refinement: monotone
- [x] v: v l2 observed order in [1.80, 2.40]: finest-triplet observed order 1.997 (asymptotic vs formal 2); reduction factors 3.972 3.988 3.977
- [x] v: v l1 observed order in [1.80, 2.40]: finest-triplet observed order 2.016 (asymptotic vs formal 2); reduction factors 4.031 4.040 4.023
- [x] v: v linf observed order in [1.60, 2.40]: finest-triplet observed order 1.957 (asymptotic vs formal 2); reduction factors 4.506 3.897 3.942
