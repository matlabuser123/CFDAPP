### MMS: momentum_mms_central

Momentum-only MMS: production relaxed-momentum assembly (alpha = 1) with exact pressure, exact face mass flux and analytical forcing; Picard on the lagged terms from U = 0

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 15 | 1.54e-33 | 0.0 |
| 32x32 | 1024 | 0.03125 | Converged | 10 | 7.7e-34 | 0.1 |
| 64x64 | 4096 | 0.015625 | Converged | 9 | 7.7e-34 | 0.8 |
| 128x128 | 16384 | 0.0078125 | Converged | 7 | 9.63e-34 | 4.9 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9759e-03 | 2.4480e-03 | 7.9695e-03 |
| 32x32 | 3.8757e-04 | 4.6955e-04 | 1.5372e-03 |
| 64x64 | 8.1910e-05 | 1.0017e-04 | 2.5950e-04 |
| 128x128 | 1.8926e-05 | 2.3637e-05 | 5.5553e-05 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5646e-03 | 1.9708e-03 | 8.0438e-03 |
| 32x32 | 3.9587e-04 | 4.8687e-04 | 1.8574e-03 |
| 64x64 | 1.0086e-04 | 1.2376e-04 | 3.2181e-04 |
| 128x128 | 2.5782e-05 | 3.1802e-05 | 6.6781e-05 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.7734e-03 | 3.1427e-03 | 8.6622e-03 |
| 32x32 | 6.1299e-04 | 6.7641e-04 | 1.8574e-03 |
| 64x64 | 1.4270e-04 | 1.5922e-04 | 3.2183e-04 |
| 128x128 | 3.4697e-05 | 3.9624e-05 | 7.6860e-05 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.2698e-03 | 2.7029e-03 | 7.9695e-03 |
| 32x32 | 4.2294e-04 | 4.9726e-04 | 1.5372e-03 |
| 64x64 | 8.6185e-05 | 1.0326e-04 | 2.5950e-04 |
| 128x128 | 1.9464e-05 | 2.4007e-05 | 5.5553e-05 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.7311e-03 | 2.1186e-03 | 8.0438e-03 |
| 32x32 | 4.3361e-04 | 5.1583e-04 | 1.8574e-03 |
| 64x64 | 1.0657e-04 | 1.2766e-04 | 3.2181e-04 |
| 128x128 | 2.6553e-05 | 3.2304e-05 | 6.6781e-05 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.0158e-03 | 1.3049e-03 | 4.2498e-03 |
| 32x32 | 1.3086e-04 | 1.6124e-04 | 6.0962e-04 |
| 64x64 | 1.6697e-05 | 2.1124e-05 | 7.8686e-05 |
| 128x128 | 2.1132e-06 | 2.7371e-06 | 9.9041e-06 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.0207e-03 | 1.3819e-03 | 5.6773e-03 |
| 32x32 | 1.2193e-04 | 1.6220e-04 | 9.9693e-04 |
| 64x64 | 1.3810e-05 | 1.8337e-05 | 1.4238e-04 |
| 128x128 | 1.6931e-06 | 2.1702e-06 | 1.8857e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 5.213, 4.687, 4.238 | 2.421, 2.271 | monotonic_not_asymptotic |
| u | l1 | 2 | 5.098, 4.732, 4.328 | 2.377, 2.279 | monotonic_not_asymptotic |
| u | linf | 2 | 5.184, 5.924, 4.671 | 2.332, 2.647 | monotonic_not_asymptotic |
| v | l2 | 2 | 4.048, 3.934, 3.892 | 2.031, 1.981 | asymptotic |
| v | l1 | 2 | 3.952, 3.925, 3.912 | 1.986, 1.974 | asymptotic |
| v | linf | 2 | 4.331, 5.772, 4.819 | 2.010, 2.590 | monotonic_not_asymptotic |
| velocity | l2 | 2 | 4.646, 4.248, 4.018 | 2.254, 2.112 | asymptotic |
| u_interior | l2 | 2 | 5.436, 4.815, 4.301 | 2.485, 2.314 | monotonic_not_asymptotic |
| u_boundary_ring | l2 | 3 | 8.093, 7.633, 7.718 | 3.029, 2.930 | asymptotic |

- [x] u: u L1/L2/Linf decrease on every refinement: monotone
- [x] u: u l2 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 2.083 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 5.213 4.687 4.238
- [x] u: u l1 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 2.114 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 5.098 4.732 4.328
- [x] u: u linf finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.224 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 5.184 5.924 4.671
- [x] v: v L1/L2/Linf decrease on every refinement: monotone
- [x] v: v l2 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 1.960 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 4.048 3.934 3.892
- [x] v: v l1 finest-pairwise observed order in [1.80, 3.00]: finest-PAIRWISE observed order 1.968 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 3.952 3.925 3.912
- [x] v: v linf finest-pairwise observed order in [1.60, 3.00]: finest-PAIRWISE observed order 2.269 (vs formal 2; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 4.331 5.772 4.819
- [x] u: u_boundary_ring l2 finest-pairwise observed order in [2.50, 3.50]: finest-PAIRWISE observed order 2.948 (vs formal 3; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 8.093 7.633 7.718
