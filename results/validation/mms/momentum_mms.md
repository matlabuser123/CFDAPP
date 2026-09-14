### MMS: momentum_mms_central

Momentum-only MMS: production relaxed-momentum assembly (alpha = 1) with exact pressure, exact face mass flux and analytical forcing; Picard on the lagged terms from U = 0

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 15 | 1.54e-33 | 0.6 |
| 32x32 | 1024 | 0.03125 | Converged | 10 | 7.7e-34 | 2.8 |
| 64x64 | 4096 | 0.015625 | Converged | 9 | 7.7e-34 | 15.1 |
| 128x128 | 16384 | 0.0078125 | Converged | 7 | 9.63e-34 | 105.0 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4624e-03 | 1.8275e-03 | 5.6797e-03 |
| 32x32 | 2.8955e-04 | 3.6964e-04 | 1.3331e-03 |
| 64x64 | 6.6924e-05 | 8.6906e-05 | 3.5223e-04 |
| 128x128 | 1.7000e-05 | 2.2017e-05 | 9.0220e-05 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.7860e-03 | 3.3009e-03 | 7.9018e-03 |
| 32x32 | 6.9580e-04 | 8.3195e-04 | 1.8164e-03 |
| 64x64 | 1.7627e-04 | 2.1205e-04 | 4.3956e-04 |
| 128x128 | 4.4799e-05 | 5.3901e-05 | 1.0964e-04 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.4112e-03 | 3.7730e-03 | 8.5159e-03 |
| 32x32 | 8.1484e-04 | 9.1037e-04 | 1.8215e-03 |
| 64x64 | 2.0387e-04 | 2.2917e-04 | 4.4239e-04 |
| 128x128 | 5.1803e-05 | 5.8225e-05 | 1.1104e-04 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4052e-03 | 1.7747e-03 | 5.6797e-03 |
| 32x32 | 2.6500e-04 | 3.3133e-04 | 1.1269e-03 |
| 64x64 | 6.3145e-05 | 8.0354e-05 | 3.0892e-04 |
| 128x128 | 1.6518e-05 | 2.1170e-05 | 8.4349e-05 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.7777e-03 | 3.2863e-03 | 7.8461e-03 |
| 32x32 | 7.1408e-04 | 8.4575e-04 | 1.8164e-03 |
| 64x64 | 1.7948e-04 | 2.1451e-04 | 4.3956e-04 |
| 128x128 | 4.5233e-05 | 5.4253e-05 | 1.0964e-04 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6493e-03 | 1.9902e-03 | 4.7051e-03 |
| 32x32 | 4.6771e-04 | 5.7579e-04 | 1.3331e-03 |
| 64x64 | 1.2457e-04 | 1.5579e-04 | 3.5223e-04 |
| 128x128 | 3.2061e-05 | 4.0359e-05 | 9.0220e-05 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.8131e-03 | 3.3482e-03 | 7.9018e-03 |
| 32x32 | 5.6314e-04 | 7.2391e-04 | 1.6295e-03 |
| 64x64 | 1.2734e-04 | 1.7010e-04 | 3.9205e-04 |
| 128x128 | 3.1237e-05 | 4.1434e-05 | 9.6441e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 2 | 4.944, 4.253, 3.947 | 2.366, 2.123 | asymptotic |
| u | l1 | 2 | 5.051, 4.327, 3.937 | 2.397, 2.157 | monotonic_not_asymptotic |
| u | linf | 2 | 4.261, 3.785, 3.904 | 2.148, 1.904 | asymptotic |
| v | l2 | 2 | 3.968, 3.923, 3.934 | 1.994, 1.971 | asymptotic |
| v | l1 | 2 | 4.004, 3.947, 3.935 | 2.008, 1.982 | asymptotic |
| v | linf | 2 | 4.350, 4.132, 4.009 | 2.144, 2.061 | asymptotic |
| velocity | l2 | 2 | 4.144, 3.973, 3.936 | 2.071, 1.995 | asymptotic |
| u_interior | l2 | 2 | 5.356, 4.123, 3.796 | 2.524, 2.084 | asymptotic |
| u_boundary_ring | l2 | 2 | 3.456, 3.696, 3.860 | 1.752, 1.863 | asymptotic |

- [x] u: u L1/L2/Linf decrease on every refinement: monotone
- [x] u: u l2 observed order in [1.80, 2.40]: finest-triplet observed order 2.123 (asymptotic vs formal 2); reduction factors 4.944 4.253 3.947
- [x] u: u l1 observed order in [1.80, 2.40]: finest-triplet observed order 2.157 (monotonic_not_asymptotic vs formal 2); reduction factors 5.051 4.327 3.937
- [x] u: u linf observed order in [1.60, 2.40]: finest-triplet observed order 1.904 (asymptotic vs formal 2); reduction factors 4.261 3.785 3.904
- [x] v: v L1/L2/Linf decrease on every refinement: monotone
- [x] v: v l2 observed order in [1.80, 2.40]: finest-triplet observed order 1.971 (asymptotic vs formal 2); reduction factors 3.968 3.923 3.934
- [x] v: v l1 observed order in [1.80, 2.40]: finest-triplet observed order 1.982 (asymptotic vs formal 2); reduction factors 4.004 3.947 3.935
- [x] v: v linf observed order in [1.60, 2.40]: finest-triplet observed order 2.061 (asymptotic vs formal 2); reduction factors 4.350 4.132 4.009
