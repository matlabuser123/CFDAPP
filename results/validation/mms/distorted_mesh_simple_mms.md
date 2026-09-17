### MMS: distorted_mesh_simple_mms

Full SIMPLE MMS from rest (U = 0, p = 0), distorted (0.25 h) meshes, 2 non-orthogonal corrections, least-squares gradient

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 903 | 3.2e-19 | 2.3 |
| 16x16 | 256 | 0.0625 | Converged | 1740 | 7.95e-20 | 18.5 |
| 32x32 | 1024 | 0.03125 | Converged | 3895 | 1.98e-20 | 134.6 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 7.4115e-03 | 9.2712e-03 | 2.0078e-02 |
| 16x16 | 1.0654e-03 | 1.3050e-03 | 3.3232e-03 |
| 32x32 | 2.0733e-04 | 2.5303e-04 | 6.1176e-04 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 6.6549e-03 | 8.2693e-03 | 1.8433e-02 |
| 16x16 | 8.5023e-04 | 1.0327e-03 | 2.8297e-03 |
| 32x32 | 1.5921e-04 | 1.8916e-04 | 5.3399e-04 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.1293e-02 | 1.2423e-02 | 2.0091e-02 |
| 16x16 | 1.5181e-03 | 1.6641e-03 | 3.6104e-03 |
| 32x32 | 2.7595e-04 | 3.1592e-04 | 7.1630e-04 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 9.1074e-03 | 1.0726e-02 | 2.0078e-02 |
| 16x16 | 1.1370e-03 | 1.3706e-03 | 3.3232e-03 |
| 32x32 | 2.1315e-04 | 2.5679e-04 | 6.1176e-04 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 7.9817e-03 | 9.7633e-03 | 1.8433e-02 |
| 16x16 | 8.9171e-04 | 1.0697e-03 | 2.8297e-03 |
| 32x32 | 1.6202e-04 | 1.8921e-04 | 4.7604e-04 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.2310e-03 | 6.9684e-03 | 1.4906e-02 |
| 16x16 | 8.3176e-04 | 1.0627e-03 | 2.8822e-03 |
| 32x32 | 1.6506e-04 | 2.2382e-04 | 5.6080e-04 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.9490e-03 | 5.8088e-03 | 1.4690e-02 |
| 16x16 | 7.1472e-04 | 9.0114e-04 | 2.3440e-03 |
| 32x32 | 1.3881e-04 | 1.8881e-04 | 5.3399e-04 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.6003e-02 | 4.4572e-02 | 1.2455e-01 |
| 16x16 | 9.7130e-03 | 1.2692e-02 | 5.8909e-02 |
| 32x32 | 2.7812e-03 | 3.7814e-03 | 2.1595e-02 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.4038e-02 | 4.2827e-02 | 1.2455e-01 |
| 16x16 | 9.4540e-03 | 1.1985e-02 | 4.3938e-02 |
| 32x32 | 2.6732e-03 | 3.5785e-03 | 1.8469e-02 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.8529e-02 | 4.6719e-02 | 1.2059e-01 |
| 16x16 | 1.0559e-02 | 1.4766e-02 | 5.8909e-02 |
| 32x32 | 3.5649e-03 | 5.0137e-03 | 2.1595e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 9.7880e-11 | 3.1227e-10 | 2.3689e-09 |
| 16x16 | 2.9917e-10 | 2.3819e-09 | 3.7762e-08 |
| 32x32 | 5.7411e-10 | 8.9659e-09 | 2.8622e-07 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 9.3582e-03 | 1.2694e-02 | 3.1093e-02 |
| 16x16 | 1.7455e-03 | 2.2550e-03 | 5.6623e-03 |
| 32x32 | 3.5199e-04 | 4.4656e-04 | 1.2683e-03 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 7.105, 5.157 | 2.921 | monotonic_not_asymptotic |
| u | l1 | 2 | 6.956, 5.139 | 2.887 | monotonic_not_asymptotic |
| v | l2 | 2 | 8.007, 5.459 | 3.101 | monotonic_not_asymptotic |
| v | l1 | 2 | 7.827, 5.340 | 3.070 | monotonic_not_asymptotic |
| velocity | l2 | 2 | 7.465, 5.268 | 2.996 | monotonic_not_asymptotic |
| u | linf | 2 | 6.042, 5.432 | 2.627 | monotonic_not_asymptotic |
| p | l2 | 2 | 3.512, 3.356 | 1.839 | monotonic_not_asymptotic |
| p | l1 | 2 | 3.707, 3.492 | 1.923 | asymptotic |
| p | linf | 2 | 2.114, 2.728 | 0.815 | monotonic_not_asymptotic |
| p_interior | l2 | 2 | 3.573, 3.349 | 1.875 | asymptotic |
| p_boundary_ring | l2 | 2 | 3.164, 2.945 | 1.712 | monotonic_not_asymptotic |
| face_flux | l2 | 2 | 5.629, 5.050 | 2.529 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.367 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 7.105 5.157
- [x] velocity: u l1 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.361 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 6.956 5.139
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.449 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 8.007 5.459
- [x] velocity: v l1 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.417 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 7.827 5.340
- [x] velocity: u L2 finest reduction factor >= 3.0: E_coarse/E_fine = 5.157
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [1.50, 2.40]: finest-triplet observed order 1.839 (monotonic_not_asymptotic vs formal 2); reduction factors 3.512 3.356
- [x] pressure: p l1 observed order in [1.50, 2.40]: finest-triplet observed order 1.923 (asymptotic vs formal 2); reduction factors 3.707 3.492
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 2.37e-09, L2 3.12e-10
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.78e-08, L2 2.38e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 2.86e-07, L2 8.97e-09
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [1.60, 4.00]: finest-triplet observed order 2.529 (monotonic_not_asymptotic vs formal 2); reduction factors 5.629 5.050
- [x] mass: global mass imbalance <= 1e-14 at 8x8: 3.20e-19
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
