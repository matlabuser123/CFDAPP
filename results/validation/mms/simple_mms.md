### MMS: simple_mms

Full SIMPLE MMS from rest (U = 0, p = 0), Cartesian meshes

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 745 | 3.2e-19 | 0.6 |
| 16x16 | 256 | 0.0625 | Converged | 2003 | 7.95e-20 | 6.1 |
| 32x32 | 1024 | 0.03125 | Converged | 4353 | 1.98e-20 | 57.4 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.3120e-03 | 6.6710e-03 | 1.6031e-02 |
| 16x16 | 9.3881e-04 | 1.1317e-03 | 3.4766e-03 |
| 32x32 | 2.1937e-04 | 2.6438e-04 | 6.6761e-04 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.2305e-03 | 6.5093e-03 | 1.6920e-02 |
| 16x16 | 6.7003e-04 | 8.4662e-04 | 3.2023e-03 |
| 32x32 | 1.5189e-04 | 1.8803e-04 | 5.8343e-04 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 8.2691e-03 | 9.3206e-03 | 1.8679e-02 |
| 16x16 | 1.2301e-03 | 1.4133e-03 | 3.9467e-03 |
| 32x32 | 2.8291e-04 | 3.2443e-04 | 7.8779e-04 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 6.3781e-03 | 7.7173e-03 | 1.6031e-02 |
| 16x16 | 9.7350e-04 | 1.1668e-03 | 3.4766e-03 |
| 32x32 | 2.2641e-04 | 2.6968e-04 | 6.6761e-04 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.7124e-03 | 5.5309e-03 | 1.0965e-02 |
| 16x16 | 6.3681e-04 | 7.8333e-04 | 1.9067e-03 |
| 32x32 | 1.5393e-04 | 1.8774e-04 | 5.0370e-04 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.9412e-03 | 5.0146e-03 | 1.3351e-02 |
| 16x16 | 8.2548e-04 | 1.0084e-03 | 2.3904e-03 |
| 32x32 | 1.6827e-04 | 2.2215e-04 | 5.3129e-04 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.8966e-03 | 7.5841e-03 | 1.6920e-02 |
| 16x16 | 7.7855e-04 | 1.0265e-03 | 3.2023e-03 |
| 32x32 | 1.3712e-04 | 1.9012e-04 | 5.8343e-04 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.2950e-02 | 5.6464e-02 | 1.8345e-01 |
| 16x16 | 1.2212e-02 | 1.6699e-02 | 8.1948e-02 |
| 32x32 | 3.4611e-03 | 4.9813e-03 | 2.9603e-02 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.6327e-02 | 4.4827e-02 | 1.0520e-01 |
| 16x16 | 1.1390e-02 | 1.5065e-02 | 5.9388e-02 |
| 32x32 | 3.2671e-03 | 4.6393e-03 | 2.5713e-02 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.1465e-02 | 6.8584e-02 | 1.8345e-01 |
| 16x16 | 1.4896e-02 | 2.1176e-02 | 8.1948e-02 |
| 32x32 | 4.8693e-03 | 6.9782e-03 | 2.9603e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.4886e-10 | 6.0074e-10 | 4.7637e-09 |
| 16x16 | 2.9068e-10 | 2.3306e-09 | 3.7207e-08 |
| 32x32 | 5.7254e-10 | 9.1359e-09 | 2.9217e-07 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 8.0038e-03 | 1.0931e-02 | 2.8679e-02 |
| 16x16 | 1.4898e-03 | 1.9437e-03 | 5.4443e-03 |
| 32x32 | 3.1574e-04 | 4.0386e-04 | 1.2426e-03 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 5.895, 4.281 | 2.675 | monotonic_not_asymptotic |
| u | l1 | 2 | 5.658, 4.280 | 2.604 | monotonic_not_asymptotic |
| v | l2 | 2 | 7.689, 4.503 | 3.104 | monotonic_not_asymptotic |
| v | l1 | 2 | 7.806, 4.411 | 3.138 | monotonic_not_asymptotic |
| velocity | l2 | 2 | 6.595, 4.356 | 2.860 | monotonic_not_asymptotic |
| u | linf | 2 | 4.611, 5.208 | 2.160 | monotonic_not_asymptotic |
| p | l2 | 2 | 3.381, 3.352 | 1.763 | monotonic_not_asymptotic |
| p | l1 | 2 | 3.517, 3.528 | 1.813 | monotonic_not_asymptotic |
| p | linf | 2 | 2.239, 2.768 | 0.955 | monotonic_not_asymptotic |
| p_interior | l2 | 2 | 2.976, 3.247 | 1.513 | monotonic_not_asymptotic |
| p_boundary_ring | l2 | 2 | 3.239, 3.035 | 1.739 | monotonic_not_asymptotic |
| face_flux | l2 | 2 | 5.624, 4.813 | 2.545 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.098 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 5.895 4.281
- [x] velocity: u l1 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.097 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 5.658 4.280
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.171 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 7.689 4.503
- [x] velocity: v l1 finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.141 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 7.806 4.411
- [x] velocity: u L2 finest reduction factor >= 3.0: E_coarse/E_fine = 4.281
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [1.50, 2.40]: finest-triplet observed order 1.763 (monotonic_not_asymptotic vs formal 2); reduction factors 3.381 3.352
- [x] pressure: p l1 observed order in [1.50, 2.40]: finest-triplet observed order 1.813 (monotonic_not_asymptotic vs formal 2); reduction factors 3.517 3.528
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 4.76e-09, L2 6.01e-10
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.72e-08, L2 2.33e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 2.92e-07, L2 9.14e-09
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [1.60, 4.00]: finest-triplet observed order 2.545 (monotonic_not_asymptotic vs formal 2); reduction factors 5.624 4.813
- [x] mass: global mass imbalance <= 1e-14 at 8x8: 3.20e-19
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
