### MMS: simple_mms_fine

Full SIMPLE MMS from rest (U = 0, p = 0), Cartesian meshes

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 1845 | 7.95e-20 | 10.3 |
| 32x32 | 1024 | 0.03125 | Converged | 4189 | 1.98e-20 | 86.7 |
| 64x64 | 4096 | 0.015625 | Converged | 6420 | 4.96e-21 | 671.6 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5427e-03 | 1.9948e-03 | 5.3246e-03 |
| 32x32 | 4.4482e-04 | 5.6196e-04 | 1.3762e-03 |
| 64x64 | 1.1774e-04 | 1.4756e-04 | 3.6226e-04 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4298e-03 | 1.8455e-03 | 5.4411e-03 |
| 32x32 | 4.1574e-04 | 5.3455e-04 | 1.4543e-03 |
| 64x64 | 1.1099e-04 | 1.4142e-04 | 3.7337e-04 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4003e-03 | 2.7175e-03 | 6.2890e-03 |
| 32x32 | 7.0466e-04 | 7.7560e-04 | 1.4644e-03 |
| 64x64 | 1.8776e-04 | 2.0439e-04 | 3.7353e-04 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.3847e-03 | 1.8103e-03 | 5.3246e-03 |
| 32x32 | 4.3269e-04 | 5.4789e-04 | 1.2821e-03 |
| 64x64 | 1.1670e-04 | 1.4627e-04 | 3.4284e-04 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.3115e-03 | 1.6733e-03 | 4.6482e-03 |
| 32x32 | 4.0561e-04 | 5.2008e-04 | 1.3922e-03 |
| 64x64 | 1.1011e-04 | 1.4003e-04 | 3.6683e-04 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.0589e-03 | 2.5046e-03 | 5.1509e-03 |
| 32x32 | 5.3289e-04 | 6.5508e-04 | 1.3762e-03 |
| 64x64 | 1.3359e-04 | 1.6606e-04 | 3.6226e-04 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.8165e-03 | 2.3207e-03 | 5.4411e-03 |
| 32x32 | 4.8928e-04 | 6.2969e-04 | 1.4543e-03 |
| 64x64 | 1.2442e-04 | 1.6126e-04 | 3.7337e-04 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.3816e-02 | 1.8698e-02 | 7.8966e-02 |
| 32x32 | 3.8857e-03 | 5.4335e-03 | 2.7046e-02 |
| 64x64 | 9.9717e-04 | 1.4054e-03 | 8.2062e-03 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.2937e-02 | 1.7447e-02 | 6.0784e-02 |
| 32x32 | 3.7739e-03 | 5.2730e-03 | 2.4346e-02 |
| 64x64 | 9.8177e-04 | 1.3812e-03 | 7.6692e-03 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6689e-02 | 2.2302e-02 | 7.8966e-02 |
| 32x32 | 4.6975e-03 | 6.4809e-03 | 2.7046e-02 |
| 64x64 | 1.2320e-03 | 1.7329e-03 | 8.2062e-03 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.0596e-10 | 2.4529e-09 | 3.9163e-08 |
| 32x32 | 4.8227e-10 | 7.3866e-09 | 2.3619e-07 |
| 64x64 | 7.0924e-10 | 1.9915e-08 | 1.2740e-06 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.8571e-04 | 6.6266e-04 | 2.2471e-03 |
| 32x32 | 9.1386e-05 | 1.2873e-04 | 5.1570e-04 |
| 64x64 | 2.4116e-05 | 3.3926e-05 | 1.4777e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 3.550, 3.808 | 1.790 | monotonic_not_asymptotic |
| u | l1 | 2 | 3.468, 3.778 | 1.747 | monotonic_not_asymptotic |
| v | l2 | 2 | 3.452, 3.780 | 1.738 | monotonic_not_asymptotic |
| v | l1 | 2 | 3.439, 3.746 | 1.735 | monotonic_not_asymptotic |
| velocity | l2 | 2 | 3.504, 3.795 | 1.765 | monotonic_not_asymptotic |
| u | linf | 2 | 3.869, 3.799 | 1.961 | asymptotic |
| p | l2 | 2 | 3.441, 3.866 | 1.719 | monotonic_not_asymptotic |
| p | l1 | 2 | 3.556, 3.897 | 1.782 | monotonic_not_asymptotic |
| p | linf | 2 | 2.920, 3.296 | 1.463 | monotonic_not_asymptotic |
| p_interior | l2 | 2 | 3.309, 3.818 | 1.645 | monotonic_not_asymptotic |
| p_boundary_ring | l2 | 2 | 3.441, 3.740 | 1.736 | monotonic_not_asymptotic |
| face_flux | l2 | 2 | 5.148, 3.795 | 2.494 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 observed order in [1.60, 2.40]: finest-triplet observed order 1.790 (monotonic_not_asymptotic vs formal 2); reduction factors 3.550 3.808
- [x] velocity: u l1 observed order in [1.60, 2.40]: finest-triplet observed order 1.747 (monotonic_not_asymptotic vs formal 2); reduction factors 3.468 3.778
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 observed order in [1.60, 2.40]: finest-triplet observed order 1.738 (monotonic_not_asymptotic vs formal 2); reduction factors 3.452 3.780
- [x] velocity: v l1 observed order in [1.60, 2.40]: finest-triplet observed order 1.735 (monotonic_not_asymptotic vs formal 2); reduction factors 3.439 3.746
- [x] velocity: u L2 finest reduction factor >= 3.0: E_coarse/E_fine = 3.808
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [1.50, 2.40]: finest-triplet observed order 1.719 (monotonic_not_asymptotic vs formal 2); reduction factors 3.441 3.866
- [x] pressure: p l1 observed order in [1.50, 2.40]: finest-triplet observed order 1.782 (monotonic_not_asymptotic vs formal 2); reduction factors 3.556 3.897
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.92e-08, L2 2.45e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 2.36e-07, L2 7.39e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 64x64: Linf 1.27e-06, L2 1.99e-08
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [1.60, 4.00]: finest-triplet observed order 2.494 (monotonic_not_asymptotic vs formal 2); reduction factors 5.148 3.795
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
- [x] mass: global mass imbalance <= 1e-14 at 64x64: 4.96e-21
