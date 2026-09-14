### MMS: simple_mms_fine_upwind

Full SIMPLE MMS from rest (U = 0, p = 0), Cartesian meshes

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 1493 | 7.95e-20 | 8.4 |
| 32x32 | 1024 | 0.03125 | Converged | 2974 | 1.98e-20 | 55.0 |
| 64x64 | 4096 | 0.015625 | Converged | 5417 | 4.96e-21 | 466.4 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5436e-02 | 2.0508e-02 | 4.6360e-02 |
| 32x32 | 8.4578e-03 | 1.1260e-02 | 2.6200e-02 |
| 64x64 | 4.4495e-03 | 5.9270e-03 | 1.3835e-02 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5689e-02 | 2.0769e-02 | 5.1303e-02 |
| 32x32 | 8.5456e-03 | 1.1410e-02 | 2.8229e-02 |
| 64x64 | 4.4947e-03 | 6.0042e-03 | 1.4882e-02 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4884e-02 | 2.9188e-02 | 5.1330e-02 |
| 32x32 | 1.3591e-02 | 1.6030e-02 | 2.8289e-02 |
| 64x64 | 7.1538e-03 | 8.4368e-03 | 1.4895e-02 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9172e-02 | 2.3276e-02 | 4.6360e-02 |
| 32x32 | 9.5058e-03 | 1.2000e-02 | 2.6200e-02 |
| 64x64 | 4.7270e-03 | 6.1176e-03 | 1.3835e-02 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9433e-02 | 2.3519e-02 | 5.1303e-02 |
| 32x32 | 9.6028e-03 | 1.2158e-02 | 2.8229e-02 |
| 64x64 | 4.7749e-03 | 6.1971e-03 | 1.4882e-02 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.2324e-03 | 4.9684e-03 | 1.4844e-02 |
| 32x32 | 8.5133e-04 | 1.3443e-03 | 4.2024e-03 |
| 64x64 | 2.1595e-04 | 3.4381e-04 | 1.0942e-03 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.4591e-03 | 5.7877e-03 | 1.8938e-02 |
| 32x32 | 8.7212e-04 | 1.5199e-03 | 5.2078e-03 |
| 64x64 | 2.1976e-04 | 3.8631e-04 | 1.3485e-03 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4666e-02 | 3.1563e-02 | 1.1467e-01 |
| 32x32 | 1.2127e-02 | 1.5270e-02 | 5.3039e-02 |
| 64x64 | 6.0984e-03 | 7.6176e-03 | 2.4249e-02 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4198e-02 | 3.0597e-02 | 7.2706e-02 |
| 32x32 | 1.2021e-02 | 1.5108e-02 | 3.8243e-02 |
| 64x64 | 6.0774e-03 | 7.5927e-03 | 1.9991e-02 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.6195e-02 | 3.4533e-02 | 1.1467e-01 |
| 32x32 | 1.2901e-02 | 1.6397e-02 | 5.3039e-02 |
| 64x64 | 6.4194e-03 | 7.9882e-03 | 2.4249e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.0354e-10 | 2.4334e-09 | 3.8854e-08 |
| 32x32 | 5.6382e-10 | 8.9371e-09 | 2.8581e-07 |
| 64x64 | 1.2126e-09 | 3.8770e-08 | 2.4809e-06 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5778e-02 | 2.1423e-02 | 5.4245e-02 |
| 32x32 | 8.5581e-03 | 1.1534e-02 | 2.9091e-02 |
| 64x64 | 4.4858e-03 | 6.0156e-03 | 1.5107e-02 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 1 | 1.821, 1.900 | 0.794 | monotonic_not_asymptotic |
| u | l1 | 1 | 1.825, 1.901 | 0.800 | monotonic_not_asymptotic |
| v | l2 | 1 | 1.820, 1.900 | 0.792 | monotonic_not_asymptotic |
| v | l1 | 1 | 1.836, 1.901 | 0.818 | monotonic_not_asymptotic |
| velocity | l2 | 1 | 1.821, 1.900 | 0.793 | monotonic_not_asymptotic |
| u | linf | 1 | 1.769, 1.894 | 0.705 | monotonic_not_asymptotic |
| p | l2 | 1 | 2.067, 2.005 | 1.090 | asymptotic |
| p | l1 | 1 | 2.034, 1.989 | 1.056 | asymptotic |
| p | linf | 1 | 2.162, 2.187 | 1.098 | asymptotic |
| p_interior | l2 | 1 | 2.025, 1.990 | 1.043 | asymptotic |
| p_boundary_ring | l2 | 1 | 2.106, 2.053 | 1.109 | asymptotic |
| face_flux | l2 | 1 | 1.857, 1.917 | 0.842 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.794 (monotonic_not_asymptotic vs formal 1); reduction factors 1.821 1.900
- [x] velocity: u l1 observed order in [0.55, 1.30]: finest-triplet observed order 0.800 (monotonic_not_asymptotic vs formal 1); reduction factors 1.825 1.901
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.792 (monotonic_not_asymptotic vs formal 1); reduction factors 1.820 1.900
- [x] velocity: v l1 observed order in [0.55, 1.30]: finest-triplet observed order 0.818 (monotonic_not_asymptotic vs formal 1); reduction factors 1.836 1.901
- [x] velocity: u L2 finest reduction factor >= 1.6: E_coarse/E_fine = 1.900
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [0.80, 1.60]: finest-triplet observed order 1.090 (asymptotic vs formal 1); reduction factors 2.067 2.005
- [x] pressure: p l1 observed order in [0.80, 1.60]: finest-triplet observed order 1.056 (asymptotic vs formal 1); reduction factors 2.034 1.989
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.89e-08, L2 2.43e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 2.86e-07, L2 8.94e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 64x64: Linf 2.48e-06, L2 3.88e-08
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [0.55, 4.00]: finest-triplet observed order 0.842 (monotonic_not_asymptotic vs formal 1); reduction factors 1.857 1.917
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
- [x] mass: global mass imbalance <= 1e-14 at 64x64: 4.96e-21
