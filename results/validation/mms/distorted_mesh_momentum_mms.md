### MMS: distorted_mesh_momentum_mms

Momentum-only MMS (central convection) on Cartesian vs distorted (0.25 h) meshes, uncorrected (Green-Gauss) vs non-orthogonal-corrected (least-squares); level status/iterations are the corrected distorted solve

Manufactured solution: mms::velocity / mms::pressure (as momentum_mms)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 14 | 1.54e-33 | 0.2 |
| 32x32 | 1024 | 0.03125 | Converged | 10 | 7.7e-34 | 0.7 |
| 64x64 | 4096 | 0.015625 | Converged | 9 | 7.7e-34 | 3.2 |
| 128x128 | 16384 | 0.0078125 | Converged | 7 | 9.63e-34 | 17.7 |

**u_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9759e-03 | 2.4480e-03 | 7.9695e-03 |
| 32x32 | 3.8757e-04 | 4.6955e-04 | 1.5372e-03 |
| 64x64 | 8.1910e-05 | 1.0017e-04 | 2.5950e-04 |
| 128x128 | 1.8926e-05 | 2.3637e-05 | 5.5553e-05 |

**v_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5646e-03 | 1.9708e-03 | 8.0438e-03 |
| 32x32 | 3.9587e-04 | 4.8687e-04 | 1.8574e-03 |
| 64x64 | 1.0086e-04 | 1.2376e-04 | 3.2181e-04 |
| 128x128 | 2.5782e-05 | 3.1802e-05 | 6.6781e-05 |

**velocity_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.7734e-03 | 3.1427e-03 | 8.6622e-03 |
| 32x32 | 6.1299e-04 | 6.7641e-04 | 1.8574e-03 |
| 64x64 | 1.4270e-04 | 1.5922e-04 | 3.2183e-04 |
| 128x128 | 3.4697e-05 | 3.9624e-05 | 7.6860e-05 |

**u_interior_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.2698e-03 | 2.7029e-03 | 7.9695e-03 |
| 32x32 | 4.2294e-04 | 4.9726e-04 | 1.5372e-03 |
| 64x64 | 8.6185e-05 | 1.0326e-04 | 2.5950e-04 |
| 128x128 | 1.9464e-05 | 2.4007e-05 | 5.5553e-05 |

**v_interior_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.7311e-03 | 2.1186e-03 | 8.0438e-03 |
| 32x32 | 4.3361e-04 | 5.1583e-04 | 1.8574e-03 |
| 64x64 | 1.0657e-04 | 1.2766e-04 | 3.2181e-04 |
| 128x128 | 2.6553e-05 | 3.2304e-05 | 6.6781e-05 |

**u_boundary_ring_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.0158e-03 | 1.3049e-03 | 4.2498e-03 |
| 32x32 | 1.3086e-04 | 1.6124e-04 | 6.0962e-04 |
| 64x64 | 1.6697e-05 | 2.1124e-05 | 7.8686e-05 |
| 128x128 | 2.1132e-06 | 2.7371e-06 | 9.9041e-06 |

**v_boundary_ring_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.0207e-03 | 1.3819e-03 | 5.6773e-03 |
| 32x32 | 1.2193e-04 | 1.6220e-04 | 9.9693e-04 |
| 64x64 | 1.3810e-05 | 1.8337e-05 | 1.4238e-04 |
| 128x128 | 1.6931e-06 | 2.1702e-06 | 1.8857e-05 |

**u_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 6.7166e-03 | 9.2791e-03 | 2.3324e-02 |
| 32x32 | 3.1714e-03 | 4.4481e-03 | 1.1714e-02 |
| 64x64 | 1.5586e-03 | 2.1899e-03 | 5.8009e-03 |
| 128x128 | 7.7246e-04 | 1.0852e-03 | 2.8799e-03 |

**v_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.9173e-03 | 6.3731e-03 | 1.6726e-02 |
| 32x32 | 2.4019e-03 | 3.1564e-03 | 8.2042e-03 |
| 64x64 | 1.2076e-03 | 1.5885e-03 | 4.0380e-03 |
| 128x128 | 6.0723e-04 | 7.9898e-04 | 2.0096e-03 |

**velocity_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 9.0838e-03 | 1.1257e-02 | 2.4150e-02 |
| 32x32 | 4.3375e-03 | 5.4542e-03 | 1.2265e-02 |
| 64x64 | 2.1515e-03 | 2.7054e-03 | 6.1214e-03 |
| 128x128 | 1.0732e-03 | 1.3476e-03 | 3.0503e-03 |

**u_interior_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.3165e-03 | 1.0562e-02 | 2.3324e-02 |
| 32x32 | 3.5574e-03 | 4.7421e-03 | 1.1714e-02 |
| 64x64 | 1.6544e-03 | 2.2603e-03 | 5.8009e-03 |
| 128x128 | 7.9638e-04 | 1.1024e-03 | 2.8799e-03 |

**v_interior_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 5.9879e-03 | 7.2055e-03 | 1.6726e-02 |
| 32x32 | 2.6899e-03 | 3.3634e-03 | 8.2042e-03 |
| 64x64 | 1.2817e-03 | 1.6396e-03 | 4.0380e-03 |
| 128x128 | 6.2603e-04 | 8.1165e-04 | 2.0096e-03 |

**u_boundary_ring_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4901e-03 | 1.7233e-03 | 3.4675e-03 |
| 32x32 | 3.6969e-04 | 4.1856e-04 | 8.2190e-04 |
| 64x64 | 9.6607e-05 | 1.1074e-04 | 2.4137e-04 |
| 128x128 | 2.4913e-05 | 2.8810e-05 | 6.5097e-05 |

**v_boundary_ring_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4201e-03 | 1.9229e-03 | 6.2697e-03 |
| 32x32 | 3.1174e-04 | 4.1167e-04 | 1.2258e-03 |
| 64x64 | 7.7273e-05 | 1.0016e-04 | 2.7145e-04 |
| 128x128 | 1.9725e-05 | 2.5368e-05 | 6.8395e-05 |

**u_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.8567e-03 | 2.3159e-03 | 5.8325e-03 |
| 32x32 | 3.7346e-04 | 4.6210e-04 | 1.3128e-03 |
| 64x64 | 8.1225e-05 | 1.0063e-04 | 2.4683e-04 |
| 128x128 | 1.8970e-05 | 2.3834e-05 | 5.5833e-05 |

**v_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.1479e-03 | 1.5688e-03 | 8.1487e-03 |
| 32x32 | 3.4043e-04 | 4.3558e-04 | 1.9018e-03 |
| 64x64 | 9.3623e-05 | 1.1712e-04 | 3.3381e-04 |
| 128x128 | 2.4814e-05 | 3.0938e-05 | 6.5582e-05 |

**velocity_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4205e-03 | 2.7972e-03 | 8.1516e-03 |
| 32x32 | 5.6188e-04 | 6.3503e-04 | 1.9021e-03 |
| 64x64 | 1.3600e-04 | 1.5442e-04 | 3.3381e-04 |
| 128x128 | 3.3856e-05 | 3.9054e-05 | 7.6039e-05 |

**u_interior_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.1846e-03 | 2.5838e-03 | 5.8325e-03 |
| 32x32 | 4.1393e-04 | 4.9119e-04 | 1.3128e-03 |
| 64x64 | 8.5918e-05 | 1.0382e-04 | 2.4683e-04 |
| 128x128 | 1.9538e-05 | 2.4211e-05 | 5.5833e-05 |

**v_interior_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.3063e-03 | 1.6939e-03 | 8.1487e-03 |
| 32x32 | 3.7722e-04 | 4.6195e-04 | 1.9018e-03 |
| 64x64 | 9.9126e-05 | 1.2084e-04 | 3.3381e-04 |
| 128x128 | 2.5566e-05 | 3.1427e-05 | 6.5582e-05 |

**u_boundary_ring_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 7.8574e-04 | 1.0371e-03 | 3.6692e-03 |
| 32x32 | 7.9801e-05 | 1.1074e-04 | 5.5961e-04 |
| 64x64 | 9.6501e-06 | 1.2897e-05 | 7.3273e-05 |
| 128x128 | 1.2120e-06 | 1.5444e-06 | 9.2583e-06 |

**v_boundary_ring_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 6.3028e-04 | 1.0620e-03 | 5.7186e-03 |
| 32x32 | 7.3408e-05 | 1.3383e-04 | 1.0190e-03 |
| 64x64 | 9.6942e-06 | 1.5798e-05 | 1.4664e-04 |
| 128x128 | 1.3183e-06 | 1.9756e-06 | 1.9461e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u_distorted_corrected | l2 | 2 | 5.012, 4.592, 4.222 | 2.359, 2.235 | monotonic_not_asymptotic |
| u_distorted_uncorrected | l2 | 1 | 2.086, 2.031, 2.018 | 1.097, 1.032 | asymptotic |
| v_distorted_corrected | l2 | 2 | 3.602, 3.719, 3.786 | 1.831, 1.886 | asymptotic |
| v_distorted_uncorrected | l2 | 1 | 2.019, 1.987, 1.988 | 1.037, 0.990 | asymptotic |

- [x] distorted: u_distorted_corrected l2 observed order in [1.80, 2.40]: finest-triplet observed order 2.235 (monotonic_not_asymptotic vs formal 2); reduction factors 5.012 4.592 4.222
- [x] distorted: u_distorted_uncorrected l2 observed order in [0.70, 1.30]: finest-triplet observed order 1.032 (asymptotic vs formal 1); reduction factors 2.086 2.031 2.018
- [x] distorted: u corrected L2 within 10% of Cartesian (finest grid): corrected 2.3834e-05, Cartesian 2.3637e-05
- [x] distorted: u uncorrected L2 >= 3x corrected (finest grid): ratio 45.5
- [x] distorted: v_distorted_corrected l2 observed order in [1.80, 2.40]: finest-triplet observed order 1.886 (asymptotic vs formal 2); reduction factors 3.602 3.719 3.786
- [x] distorted: v_distorted_uncorrected l2 observed order in [0.70, 1.30]: finest-triplet observed order 0.990 (asymptotic vs formal 1); reduction factors 2.019 1.987 1.988
- [x] distorted: v corrected L2 within 10% of Cartesian (finest grid): corrected 3.0938e-05, Cartesian 3.1802e-05
- [x] distorted: v uncorrected L2 >= 3x corrected (finest grid): ratio 25.8
