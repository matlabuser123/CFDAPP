### MMS: momentum_mms_linear_upwind

Momentum-only MMS: production relaxed-momentum assembly (alpha = 1) with exact pressure, exact face mass flux and analytical forcing; Picard on the lagged terms from U = 0

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 18 | 1.54e-33 | 0.1 |
| 32x32 | 1024 | 0.03125 | Converged | 13 | 7.7e-34 | 0.2 |
| 64x64 | 4096 | 0.015625 | Converged | 9 | 7.7e-34 | 0.9 |
| 128x128 | 16384 | 0.0078125 | Converged | 8 | 9.63e-34 | 5.3 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4385e-03 | 3.0602e-03 | 9.3220e-03 |
| 32x32 | 4.9347e-04 | 6.3423e-04 | 1.7403e-03 |
| 64x64 | 1.0659e-04 | 1.4436e-04 | 3.8401e-04 |
| 128x128 | 2.4546e-05 | 3.5045e-05 | 9.6317e-05 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.0238e-03 | 2.6821e-03 | 9.9904e-03 |
| 32x32 | 5.0623e-04 | 6.4282e-04 | 2.1439e-03 |
| 64x64 | 1.2350e-04 | 1.5457e-04 | 3.8834e-04 |
| 128x128 | 3.0251e-05 | 3.8251e-05 | 9.8568e-05 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.5116e-03 | 4.0692e-03 | 1.0210e-02 |
| 32x32 | 7.8379e-04 | 9.0303e-04 | 2.1504e-03 |
| 64x64 | 1.8096e-04 | 2.1150e-04 | 4.1210e-04 |
| 128x128 | 4.3355e-05 | 5.1878e-05 | 1.0653e-04 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.8216e-03 | 3.3999e-03 | 9.3220e-03 |
| 32x32 | 5.4084e-04 | 6.7314e-04 | 1.7403e-03 |
| 64x64 | 1.1230e-04 | 1.4889e-04 | 3.8401e-04 |
| 128x128 | 2.5253e-05 | 3.5597e-05 | 9.6317e-05 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.2938e-03 | 2.9355e-03 | 9.9904e-03 |
| 32x32 | 5.5793e-04 | 6.8218e-04 | 2.1439e-03 |
| 64x64 | 1.3058e-04 | 1.5947e-04 | 3.8834e-04 |
| 128x128 | 3.1158e-05 | 3.8856e-05 | 9.8568e-05 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.1871e-03 | 1.4813e-03 | 4.8197e-03 |
| 32x32 | 1.4967e-04 | 1.8173e-04 | 6.6189e-04 |
| 64x64 | 1.9481e-05 | 2.3615e-05 | 8.2657e-05 |
| 128x128 | 2.4443e-06 | 3.0187e-06 | 1.0191e-05 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.1419e-03 | 1.5947e-03 | 6.6327e-03 |
| 32x32 | 1.3099e-04 | 1.8632e-04 | 1.0847e-03 |
| 64x64 | 1.5465e-05 | 2.1308e-05 | 1.4930e-04 |
| 128x128 | 1.9169e-06 | 2.5369e-06 | 1.9368e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 4.825, 4.393, 4.119 | 2.308, 2.164 | monotonic_not_asymptotic |
| u | l1 | 2 | 4.942, 4.630, 4.343 | 2.330, 2.237 | monotonic_not_asymptotic |
| u | linf | 2 | 5.356, 4.532, 3.987 | 2.483, 2.237 | monotonic_not_asymptotic |
| v | l2 | 2 | 4.172, 4.159, 4.041 | 2.062, 2.069 | asymptotic |
| v | l1 | 2 | 3.998, 4.099, 4.082 | 1.987, 2.037 | asymptotic |
| v | linf | 2 | 4.660, 5.521, 3.940 | 2.160, 2.599 | monotonic_not_asymptotic |
| velocity | l2 | 2 | 4.506, 4.270, 4.077 | 2.195, 2.115 | asymptotic |
| u_interior | l2 | 2 | 5.051, 4.521, 4.183 | 2.379, 2.210 | monotonic_not_asymptotic |
| u_boundary_ring | l2 | 3 | 8.151, 7.695, 7.823 | 3.039, 2.940 | asymptotic |

- [x] u: u L1/L2/Linf decrease on every refinement: monotone
- [x] u: u l2 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 2.042 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 4.825 4.393 4.119
- [x] u: u l1 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 2.119 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 4.942 4.630 4.343
- [x] u: u linf finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 1.995 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 5.356 4.532 3.987
- [x] v: v L1/L2/Linf decrease on every refinement: monotone
- [x] v: v l2 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 2.015 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 4.172 4.159 4.041
- [x] v: v l1 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 2.029 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 3.998 4.099 4.082
- [x] v: v linf finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 1.978 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 4.660 5.521 3.940
- [x] u: u_boundary_ring l2 finest-pairwise observed order in [2.50, 3.50]: finest-PAIRWISE observed order 2.968 (vs formal 3; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 8.151 7.695 7.823
