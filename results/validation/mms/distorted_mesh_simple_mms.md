### MMS: distorted_mesh_simple_mms

Full SIMPLE MMS from rest (U = 0, p = 0), distorted (0.25 h) meshes, 2 non-orthogonal corrections, least-squares gradient

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 907 | 3.2e-19 | 35.3 |
| 16x16 | 256 | 0.0625 | Converged | 1535 | 7.95e-20 | 205.5 |
| 32x32 | 1024 | 0.03125 | Converged | 3334 | 1.98e-20 | 1181.6 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 6.4642e-03 | 8.3068e-03 | 2.3314e-02 |
| 16x16 | 1.4120e-03 | 1.8119e-03 | 5.2178e-03 |
| 32x32 | 4.0491e-04 | 5.2031e-04 | 1.3400e-03 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.8147e-03 | 7.1559e-03 | 1.6660e-02 |
| 16x16 | 1.2539e-03 | 1.6298e-03 | 4.9903e-03 |
| 32x32 | 3.8148e-04 | 4.9121e-04 | 1.3802e-03 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 9.9188e-03 | 1.0964e-02 | 2.3473e-02 |
| 16x16 | 2.1327e-03 | 2.4371e-03 | 5.9180e-03 |
| 32x32 | 6.4407e-04 | 7.1555e-04 | 1.3810e-03 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 7.2449e-03 | 9.0406e-03 | 2.1514e-02 |
| 16x16 | 1.2914e-03 | 1.6599e-03 | 5.2178e-03 |
| 32x32 | 3.8993e-04 | 5.0277e-04 | 1.2768e-03 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.1505e-03 | 6.4925e-03 | 1.3937e-02 |
| 16x16 | 1.0638e-03 | 1.3381e-03 | 4.1327e-03 |
| 32x32 | 3.6598e-04 | 4.6946e-04 | 1.3121e-03 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.4605e-03 | 7.2550e-03 | 2.3314e-02 |
| 16x16 | 1.8060e-03 | 2.2377e-03 | 4.4965e-03 |
| 32x32 | 5.1358e-04 | 6.3319e-04 | 1.3400e-03 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 6.6687e-03 | 7.9277e-03 | 1.6660e-02 |
| 16x16 | 1.8751e-03 | 2.3420e-03 | 4.9903e-03 |
| 32x32 | 4.9400e-04 | 6.2684e-04 | 1.3802e-03 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.3941e-02 | 4.3616e-02 | 1.4457e-01 |
| 16x16 | 9.9585e-03 | 1.3248e-02 | 5.6169e-02 |
| 32x32 | 2.7956e-03 | 3.8054e-03 | 1.8826e-02 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.1241e-02 | 4.0480e-02 | 1.4457e-01 |
| 16x16 | 9.2601e-03 | 1.2457e-02 | 5.6169e-02 |
| 32x32 | 2.7065e-03 | 3.6796e-03 | 1.8479e-02 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.7411e-02 | 4.7344e-02 | 1.1388e-01 |
| 16x16 | 1.2240e-02 | 1.5553e-02 | 5.1257e-02 |
| 32x32 | 3.4423e-03 | 4.6166e-03 | 1.8826e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.3690e-10 | 5.3918e-10 | 4.1621e-09 |
| 16x16 | 2.9525e-10 | 2.3510e-09 | 3.7267e-08 |
| 32x32 | 6.2177e-10 | 9.9356e-09 | 3.1721e-07 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.8485e-03 | 8.2988e-03 | 2.4164e-02 |
| 16x16 | 6.7723e-04 | 9.1030e-04 | 2.4055e-03 |
| 32x32 | 8.7457e-05 | 1.1901e-04 | 4.6453e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 4.584, 3.482 | 2.330 | monotonic_not_asymptotic |
| u | l1 | 2 | 4.578, 3.487 | 2.327 | monotonic_not_asymptotic |
| v | l2 | 2 | 4.391, 3.318 | 2.279 | monotonic_not_asymptotic |
| v | l1 | 2 | 4.637, 3.287 | 2.386 | monotonic_not_asymptotic |
| velocity | l2 | 2 | 4.499, 3.406 | 2.308 | monotonic_not_asymptotic |
| u | linf | 2 | 4.468, 3.894 | 2.222 | monotonic_not_asymptotic |
| p | l2 | 2 | 3.292, 3.481 | 1.685 | monotonic_not_asymptotic |
| p | l1 | 2 | 3.408, 3.562 | 1.743 | monotonic_not_asymptotic |
| p | linf | 2 | 2.574, 2.984 | 1.243 | monotonic_not_asymptotic |
| p_interior | l2 | 2 | 3.250, 3.385 | 1.675 | monotonic_not_asymptotic |
| p_boundary_ring | l2 | 2 | 3.044, 3.369 | 1.539 | monotonic_not_asymptotic |
| face_flux | l2 | 2 | 9.117, 7.649 | 3.223 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 observed order in [1.60, 2.40]: finest-triplet observed order 2.330 (monotonic_not_asymptotic vs formal 2); reduction factors 4.584 3.482
- [x] velocity: u l1 observed order in [1.60, 2.40]: finest-triplet observed order 2.327 (monotonic_not_asymptotic vs formal 2); reduction factors 4.578 3.487
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 observed order in [1.60, 2.40]: finest-triplet observed order 2.279 (monotonic_not_asymptotic vs formal 2); reduction factors 4.391 3.318
- [x] velocity: v l1 observed order in [1.60, 2.40]: finest-triplet observed order 2.386 (monotonic_not_asymptotic vs formal 2); reduction factors 4.637 3.287
- [x] velocity: u L2 finest reduction factor >= 3.0: E_coarse/E_fine = 3.482
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [1.50, 2.40]: finest-triplet observed order 1.685 (monotonic_not_asymptotic vs formal 2); reduction factors 3.292 3.481
- [x] pressure: p l1 observed order in [1.50, 2.40]: finest-triplet observed order 1.743 (monotonic_not_asymptotic vs formal 2); reduction factors 3.408 3.562
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 4.16e-09, L2 5.39e-10
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.73e-08, L2 2.35e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 3.17e-07, L2 9.94e-09
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [1.60, 4.00]: finest-triplet observed order 3.223 (monotonic_not_asymptotic vs formal 2); reduction factors 9.117 7.649
- [x] mass: global mass imbalance <= 1e-14 at 8x8: 3.20e-19
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
