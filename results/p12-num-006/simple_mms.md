### MMS: simple_mms

Full SIMPLE MMS from rest (U = 0, p = 0), Cartesian meshes

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 964 | 3.2e-19 | 4.2 |
| 16x16 | 256 | 0.0625 | Converged | 1845 | 7.95e-20 | 29.8 |
| 32x32 | 1024 | 0.03125 | Converged | 4189 | 1.98e-20 | 165.3 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.7596e-03 | 7.1077e-03 | 1.7606e-02 |
| 16x16 | 1.5427e-03 | 1.9948e-03 | 5.3246e-03 |
| 32x32 | 4.4482e-04 | 5.6196e-04 | 1.3762e-03 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.5984e-03 | 6.4350e-03 | 2.6197e-02 |
| 16x16 | 1.4298e-03 | 1.8455e-03 | 5.4411e-03 |
| 32x32 | 4.1574e-04 | 5.3455e-04 | 1.4543e-03 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 8.0873e-03 | 9.5879e-03 | 2.7266e-02 |
| 16x16 | 2.4003e-03 | 2.7175e-03 | 6.2890e-03 |
| 32x32 | 7.0466e-04 | 7.7560e-04 | 1.4644e-03 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.7488e-03 | 5.9447e-03 | 1.4650e-02 |
| 16x16 | 1.3847e-03 | 1.8103e-03 | 5.3246e-03 |
| 32x32 | 4.3269e-04 | 5.4789e-04 | 1.2821e-03 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.3026e-03 | 4.1875e-03 | 1.0643e-02 |
| 16x16 | 1.3115e-03 | 1.6733e-03 | 4.6482e-03 |
| 32x32 | 4.0561e-04 | 5.2008e-04 | 1.3922e-03 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 7.0592e-03 | 8.3689e-03 | 1.7606e-02 |
| 16x16 | 2.0589e-03 | 2.5046e-03 | 5.1509e-03 |
| 32x32 | 5.3289e-04 | 6.5508e-04 | 1.3762e-03 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 6.2645e-03 | 8.4914e-03 | 2.6197e-02 |
| 16x16 | 1.8165e-03 | 2.3207e-03 | 5.4411e-03 |
| 32x32 | 4.8928e-04 | 6.2969e-04 | 1.4543e-03 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.8562e-02 | 6.1631e-02 | 1.9076e-01 |
| 16x16 | 1.3816e-02 | 1.8698e-02 | 7.8966e-02 |
| 32x32 | 3.8857e-03 | 5.4335e-03 | 2.7046e-02 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.0656e-02 | 4.9527e-02 | 1.1522e-01 |
| 16x16 | 1.2937e-02 | 1.7447e-02 | 6.0784e-02 |
| 32x32 | 3.7739e-03 | 5.2730e-03 | 2.4346e-02 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.8726e-02 | 7.4353e-02 | 1.9076e-01 |
| 16x16 | 1.6689e-02 | 2.2302e-02 | 7.8966e-02 |
| 32x32 | 4.6975e-03 | 6.4809e-03 | 2.7046e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.2692e-10 | 5.0291e-10 | 3.9781e-09 |
| 16x16 | 3.0596e-10 | 2.4529e-09 | 3.9163e-08 |
| 32x32 | 4.8227e-10 | 7.3866e-09 | 2.3619e-07 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.3589e-03 | 6.2347e-03 | 1.9954e-02 |
| 16x16 | 4.8571e-04 | 6.6266e-04 | 2.2471e-03 |
| 32x32 | 9.1386e-05 | 1.2873e-04 | 5.1570e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 3.563, 3.550 | 1.835 | monotonic_not_asymptotic |
| u | l1 | 2 | 3.733, 3.468 | 1.941 | asymptotic |
| v | l2 | 2 | 3.487, 3.452 | 1.808 | monotonic_not_asymptotic |
| v | l1 | 2 | 3.216, 3.439 | 1.644 | monotonic_not_asymptotic |
| velocity | l2 | 2 | 3.528, 3.504 | 1.823 | monotonic_not_asymptotic |
| u | linf | 2 | 3.307, 3.869 | 1.637 | monotonic_not_asymptotic |
| p | l2 | 2 | 3.296, 3.441 | 1.694 | monotonic_not_asymptotic |
| p | l1 | 2 | 3.515, 3.556 | 1.807 | monotonic_not_asymptotic |
| p | linf | 2 | 2.416, 2.920 | 1.106 | monotonic_not_asymptotic |
| p_interior | l2 | 2 | 2.839, 3.309 | 1.398 | monotonic_not_asymptotic |
| p_boundary_ring | l2 | 2 | 3.334, 3.441 | 1.718 | monotonic_not_asymptotic |
| face_flux | l2 | 2 | 9.409, 5.148 | 3.384 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 observed order in [1.60, 2.40]: finest-triplet observed order 1.835 (monotonic_not_asymptotic vs formal 2); reduction factors 3.563 3.550
- [x] velocity: u l1 observed order in [1.60, 2.40]: finest-triplet observed order 1.941 (asymptotic vs formal 2); reduction factors 3.733 3.468
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 observed order in [1.60, 2.40]: finest-triplet observed order 1.808 (monotonic_not_asymptotic vs formal 2); reduction factors 3.487 3.452
- [x] velocity: v l1 observed order in [1.60, 2.40]: finest-triplet observed order 1.644 (monotonic_not_asymptotic vs formal 2); reduction factors 3.216 3.439
- [x] velocity: u L2 finest reduction factor >= 3.0: E_coarse/E_fine = 3.550
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [1.50, 2.40]: finest-triplet observed order 1.694 (monotonic_not_asymptotic vs formal 2); reduction factors 3.296 3.441
- [x] pressure: p l1 observed order in [1.50, 2.40]: finest-triplet observed order 1.807 (monotonic_not_asymptotic vs formal 2); reduction factors 3.515 3.556
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 3.98e-09, L2 5.03e-10
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.92e-08, L2 2.45e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 2.36e-07, L2 7.39e-09
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [1.60, 4.00]: finest-triplet observed order 3.384 (monotonic_not_asymptotic vs formal 2); reduction factors 9.409 5.148
- [x] mass: global mass imbalance <= 1e-14 at 8x8: 3.20e-19
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
