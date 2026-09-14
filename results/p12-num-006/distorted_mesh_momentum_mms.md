### MMS: distorted_mesh_momentum_mms

Momentum-only MMS (central convection) on Cartesian vs distorted (0.25 h) meshes, uncorrected (Green-Gauss) vs non-orthogonal-corrected (least-squares); level status/iterations are the corrected distorted solve

Manufactured solution: mms::velocity / mms::pressure (as momentum_mms)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 14 | 1.54e-33 | 0.8 |
| 32x32 | 1024 | 0.03125 | Converged | 10 | 7.7e-34 | 3.4 |
| 64x64 | 4096 | 0.015625 | Converged | 9 | 7.7e-34 | 21.5 |
| 128x128 | 16384 | 0.0078125 | Converged | 8 | 9.63e-34 | 145.5 |

**u_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4624e-03 | 1.8275e-03 | 5.6797e-03 |
| 32x32 | 2.8955e-04 | 3.6964e-04 | 1.3331e-03 |
| 64x64 | 6.6924e-05 | 8.6906e-05 | 3.5223e-04 |
| 128x128 | 1.7000e-05 | 2.2017e-05 | 9.0220e-05 |

**v_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.7860e-03 | 3.3009e-03 | 7.9018e-03 |
| 32x32 | 6.9580e-04 | 8.3195e-04 | 1.8164e-03 |
| 64x64 | 1.7627e-04 | 2.1205e-04 | 4.3956e-04 |
| 128x128 | 4.4799e-05 | 5.3901e-05 | 1.0964e-04 |

**velocity_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.4112e-03 | 3.7730e-03 | 8.5159e-03 |
| 32x32 | 8.1484e-04 | 9.1037e-04 | 1.8215e-03 |
| 64x64 | 2.0387e-04 | 2.2917e-04 | 4.4239e-04 |
| 128x128 | 5.1803e-05 | 5.8225e-05 | 1.1104e-04 |

**u_interior_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4052e-03 | 1.7747e-03 | 5.6797e-03 |
| 32x32 | 2.6500e-04 | 3.3133e-04 | 1.1269e-03 |
| 64x64 | 6.3145e-05 | 8.0354e-05 | 3.0892e-04 |
| 128x128 | 1.6518e-05 | 2.1170e-05 | 8.4349e-05 |

**v_interior_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.7777e-03 | 3.2863e-03 | 7.8461e-03 |
| 32x32 | 7.1408e-04 | 8.4575e-04 | 1.8164e-03 |
| 64x64 | 1.7948e-04 | 2.1451e-04 | 4.3956e-04 |
| 128x128 | 4.5233e-05 | 5.4253e-05 | 1.0964e-04 |

**u_boundary_ring_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6493e-03 | 1.9902e-03 | 4.7051e-03 |
| 32x32 | 4.6771e-04 | 5.7579e-04 | 1.3331e-03 |
| 64x64 | 1.2457e-04 | 1.5579e-04 | 3.5223e-04 |
| 128x128 | 3.2061e-05 | 4.0359e-05 | 9.0220e-05 |

**v_boundary_ring_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.8131e-03 | 3.3482e-03 | 7.9018e-03 |
| 32x32 | 5.6314e-04 | 7.2391e-04 | 1.6295e-03 |
| 64x64 | 1.2734e-04 | 1.7010e-04 | 3.9205e-04 |
| 128x128 | 3.1237e-05 | 4.1434e-05 | 9.6441e-05 |

**u_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 6.7908e-03 | 8.9708e-03 | 2.2405e-02 |
| 32x32 | 3.2354e-03 | 4.4195e-03 | 1.1549e-02 |
| 64x64 | 1.5787e-03 | 2.1884e-03 | 5.7670e-03 |
| 128x128 | 7.7780e-04 | 1.0855e-03 | 2.8727e-03 |

**v_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 5.7881e-03 | 6.9982e-03 | 1.7484e-02 |
| 32x32 | 2.5755e-03 | 3.2512e-03 | 8.2771e-03 |
| 64x64 | 1.2442e-03 | 1.6031e-03 | 4.0453e-03 |
| 128x128 | 6.1542e-04 | 8.0127e-04 | 2.0084e-03 |

**velocity_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 9.6934e-03 | 1.1378e-02 | 2.3473e-02 |
| 32x32 | 4.5063e-03 | 5.4866e-03 | 1.1966e-02 |
| 64x64 | 2.1930e-03 | 2.7127e-03 | 6.0487e-03 |
| 128x128 | 1.0832e-03 | 1.3492e-03 | 3.0323e-03 |

**u_interior_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.0252e-03 | 1.0098e-02 | 2.2405e-02 |
| 32x32 | 3.5788e-03 | 4.7034e-03 | 1.1549e-02 |
| 64x64 | 1.6697e-03 | 2.2583e-03 | 5.7670e-03 |
| 128x128 | 8.0115e-04 | 1.1027e-03 | 2.8727e-03 |

**v_interior_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 6.6668e-03 | 7.7604e-03 | 1.7484e-02 |
| 32x32 | 2.8336e-03 | 3.4533e-03 | 8.2771e-03 |
| 64x64 | 1.3146e-03 | 1.6539e-03 | 4.0453e-03 |
| 128x128 | 6.3375e-04 | 8.1393e-04 | 2.0084e-03 |

**u_boundary_ring_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.7582e-03 | 3.1999e-03 | 6.2424e-03 |
| 32x32 | 7.4249e-04 | 8.5698e-04 | 1.4110e-03 |
| 64x64 | 1.9025e-04 | 2.2109e-04 | 3.7857e-04 |
| 128x128 | 4.8156e-05 | 5.6021e-05 | 9.8672e-05 |

**v_boundary_ring_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.9179e-03 | 3.4974e-03 | 7.7561e-03 |
| 32x32 | 7.0193e-04 | 8.5694e-04 | 2.0620e-03 |
| 64x64 | 1.7125e-04 | 2.1358e-04 | 5.2385e-04 |
| 128x128 | 4.2413e-05 | 5.3404e-05 | 1.3163e-04 |

**u_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6070e-03 | 1.9226e-03 | 4.9927e-03 |
| 32x32 | 3.0610e-04 | 3.8147e-04 | 1.3413e-03 |
| 64x64 | 6.7399e-05 | 8.7536e-05 | 3.5196e-04 |
| 128x128 | 1.6922e-05 | 2.2038e-05 | 9.0071e-05 |

**v_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4251e-03 | 2.8408e-03 | 6.0005e-03 |
| 32x32 | 6.4042e-04 | 7.6376e-04 | 1.6080e-03 |
| 64x64 | 1.6849e-04 | 2.0280e-04 | 4.1172e-04 |
| 128x128 | 4.3694e-05 | 5.2691e-05 | 1.0619e-04 |

**velocity_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.1492e-03 | 3.4302e-03 | 6.5270e-03 |
| 32x32 | 7.7030e-04 | 8.5373e-04 | 1.6305e-03 |
| 64x64 | 1.9717e-04 | 2.2089e-04 | 4.1188e-04 |
| 128x128 | 5.0902e-05 | 5.7114e-05 | 1.0718e-04 |

**u_interior_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5127e-03 | 1.8201e-03 | 4.3067e-03 |
| 32x32 | 2.8033e-04 | 3.4174e-04 | 1.0576e-03 |
| 64x64 | 6.3572e-05 | 8.0913e-05 | 3.1038e-04 |
| 128x128 | 1.6439e-05 | 2.1185e-05 | 8.4382e-05 |

**v_interior_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.3879e-03 | 2.7810e-03 | 5.8368e-03 |
| 32x32 | 6.5391e-04 | 7.7311e-04 | 1.6080e-03 |
| 64x64 | 1.7118e-04 | 2.0494e-04 | 4.1172e-04 |
| 128x128 | 4.4094e-05 | 5.3021e-05 | 1.0619e-04 |

**u_boundary_ring_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9150e-03 | 2.2250e-03 | 4.9927e-03 |
| 32x32 | 4.9315e-04 | 5.9507e-04 | 1.3413e-03 |
| 64x64 | 1.2578e-04 | 1.5711e-04 | 3.5196e-04 |
| 128x128 | 3.2018e-05 | 4.0457e-05 | 9.0071e-05 |

**v_boundary_ring_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.5465e-03 | 3.0280e-03 | 6.0005e-03 |
| 32x32 | 5.4246e-04 | 6.9210e-04 | 1.5423e-03 |
| 64x64 | 1.2751e-04 | 1.6683e-04 | 3.8168e-04 |
| 128x128 | 3.1205e-05 | 4.1069e-05 | 9.5199e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u_distorted_corrected | l2 | 2 | 5.040, 4.358, 3.972 | 2.390, 2.166 | monotonic_not_asymptotic |
| u_distorted_uncorrected | l2 | 1 | 2.030, 2.020, 2.016 | 1.028, 1.016 | asymptotic |
| v_distorted_corrected | l2 | 2 | 3.719, 3.766, 3.849 | 1.889, 1.902 | asymptotic |
| v_distorted_uncorrected | l2 | 1 | 2.153, 2.028, 2.001 | 1.185, 1.039 | asymptotic |

- [x] distorted: u_distorted_corrected l2 observed order in [1.80, 2.40]: finest-triplet observed order 2.166 (monotonic_not_asymptotic vs formal 2); reduction factors 5.040 4.358 3.972
- [x] distorted: u_distorted_uncorrected l2 observed order in [0.70, 1.30]: finest-triplet observed order 1.016 (asymptotic vs formal 1); reduction factors 2.030 2.020 2.016
- [x] distorted: u corrected L2 within 10% of Cartesian (finest grid): corrected 2.2038e-05, Cartesian 2.2017e-05
- [x] distorted: u uncorrected L2 >= 3x corrected (finest grid): ratio 49.3
- [x] distorted: v_distorted_corrected l2 observed order in [1.80, 2.40]: finest-triplet observed order 1.902 (asymptotic vs formal 2); reduction factors 3.719 3.766 3.849
- [x] distorted: v_distorted_uncorrected l2 observed order in [0.70, 1.30]: finest-triplet observed order 1.039 (asymptotic vs formal 1); reduction factors 2.153 2.028 2.001
- [x] distorted: v corrected L2 within 10% of Cartesian (finest grid): corrected 5.2691e-05, Cartesian 5.3901e-05
- [x] distorted: v uncorrected L2 >= 3x corrected (finest grid): ratio 15.2
