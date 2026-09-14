### MMS: simple_mms_upwind

Full SIMPLE MMS from rest (U = 0, p = 0), Cartesian meshes

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 600 | 3.2e-19 | 7.0 |
| 16x16 | 256 | 0.0625 | Converged | 1493 | 7.95e-20 | 57.6 |
| 32x32 | 1024 | 0.03125 | Converged | 2974 | 1.98e-20 | 405.0 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.6712e-02 | 3.4810e-02 | 8.1734e-02 |
| 16x16 | 1.5436e-02 | 2.0508e-02 | 4.6360e-02 |
| 32x32 | 8.4578e-03 | 1.1260e-02 | 2.6200e-02 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.6631e-02 | 3.4836e-02 | 8.1042e-02 |
| 16x16 | 1.5689e-02 | 2.0769e-02 | 5.1303e-02 |
| 32x32 | 8.5456e-03 | 1.1410e-02 | 2.8229e-02 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.2755e-02 | 4.9247e-02 | 8.3164e-02 |
| 16x16 | 2.4884e-02 | 2.9188e-02 | 5.1330e-02 |
| 32x32 | 1.3591e-02 | 1.6030e-02 | 2.8289e-02 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.8987e-02 | 4.4400e-02 | 8.1734e-02 |
| 16x16 | 1.9172e-02 | 2.3276e-02 | 4.6360e-02 |
| 32x32 | 9.5058e-03 | 1.2000e-02 | 2.6200e-02 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.7142e-02 | 4.2971e-02 | 8.1042e-02 |
| 16x16 | 1.9433e-02 | 2.3519e-02 | 5.1303e-02 |
| 32x32 | 9.6028e-03 | 1.2158e-02 | 2.8229e-02 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.0929e-02 | 1.5330e-02 | 4.0587e-02 |
| 16x16 | 3.2324e-03 | 4.9684e-03 | 1.4844e-02 |
| 32x32 | 8.5133e-04 | 1.3443e-03 | 4.2024e-03 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.3117e-02 | 1.9992e-02 | 5.7731e-02 |
| 16x16 | 3.4591e-03 | 5.7877e-03 | 1.8938e-02 |
| 32x32 | 8.7212e-04 | 1.5199e-03 | 5.2078e-03 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.5737e-02 | 7.2890e-02 | 2.3630e-01 |
| 16x16 | 2.4666e-02 | 3.1563e-02 | 1.1467e-01 |
| 32x32 | 1.2127e-02 | 1.5270e-02 | 5.3039e-02 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.5120e-02 | 6.8054e-02 | 1.5710e-01 |
| 16x16 | 2.4198e-02 | 3.0597e-02 | 7.2706e-02 |
| 32x32 | 1.2021e-02 | 1.5108e-02 | 3.8243e-02 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.6529e-02 | 7.8672e-02 | 2.3630e-01 |
| 16x16 | 2.6195e-02 | 3.4533e-02 | 1.1467e-01 |
| 32x32 | 1.2901e-02 | 1.6397e-02 | 5.3039e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.1894e-10 | 4.6027e-10 | 3.6293e-09 |
| 16x16 | 3.0354e-10 | 2.4334e-09 | 3.8854e-08 |
| 32x32 | 5.6382e-10 | 8.9371e-09 | 2.8581e-07 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.7145e-02 | 3.7792e-02 | 9.3690e-02 |
| 16x16 | 1.5778e-02 | 2.1423e-02 | 5.4245e-02 |
| 32x32 | 8.5581e-03 | 1.1534e-02 | 2.9091e-02 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 1 | 1.697, 1.821 | 0.629 | monotonic_not_asymptotic |
| u | l1 | 1 | 1.730, 1.825 | 0.692 | monotonic_not_asymptotic |
| v | l2 | 1 | 1.677, 1.820 | 0.588 | monotonic_not_asymptotic |
| v | l1 | 1 | 1.697, 1.836 | 0.615 | monotonic_not_asymptotic |
| velocity | l2 | 1 | 1.687, 1.821 | 0.608 | monotonic_not_asymptotic |
| u | linf | 1 | 1.763, 1.769 | 0.811 | monotonic_not_asymptotic |
| p | l2 | 1 | 2.309, 2.067 | 1.343 | monotonic_not_asymptotic |
| p | l1 | 1 | 2.260, 2.034 | 1.309 | monotonic_not_asymptotic |
| p | linf | 1 | 2.061, 2.162 | 0.981 | asymptotic |
| p_interior | l2 | 1 | 2.224, 2.025 | 1.274 | monotonic_not_asymptotic |
| p_boundary_ring | l2 | 1 | 2.278, 2.106 | 1.283 | monotonic_not_asymptotic |
| face_flux | l2 | 1 | 1.764, 1.857 | 0.727 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.629 (monotonic_not_asymptotic vs formal 1); reduction factors 1.697 1.821
- [x] velocity: u l1 observed order in [0.55, 1.30]: finest-triplet observed order 0.692 (monotonic_not_asymptotic vs formal 1); reduction factors 1.730 1.825
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.588 (monotonic_not_asymptotic vs formal 1); reduction factors 1.677 1.820
- [x] velocity: v l1 observed order in [0.55, 1.30]: finest-triplet observed order 0.615 (monotonic_not_asymptotic vs formal 1); reduction factors 1.697 1.836
- [x] velocity: u L2 finest reduction factor >= 1.6: E_coarse/E_fine = 1.821
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [0.80, 1.60]: finest-triplet observed order 1.343 (monotonic_not_asymptotic vs formal 1); reduction factors 2.309 2.067
- [x] pressure: p l1 observed order in [0.80, 1.60]: finest-triplet observed order 1.309 (monotonic_not_asymptotic vs formal 1); reduction factors 2.260 2.034
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 3.63e-09, L2 4.60e-10
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.89e-08, L2 2.43e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 2.86e-07, L2 8.94e-09
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [0.55, 4.00]: finest-triplet observed order 0.727 (monotonic_not_asymptotic vs formal 1); reduction factors 1.764 1.857
- [x] mass: global mass imbalance <= 1e-14 at 8x8: 3.20e-19
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
