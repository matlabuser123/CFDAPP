### MMS: distorted_mesh_mms

Scalar advection-diffusion MMS (diffusion-dominated, k = 5) on Cartesian vs distorted (0.25 h) meshes, with and without the P12-NUM-003 non-orthogonal correction; level status/iterations are the corrected distorted solve

Manufactured solution: mms::scalar (as scalar_advection_diffusion_mms)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 9 | 2.78e-17 | 0.0 |
| 32x32 | 1024 | 0.03125 | Converged | 8 | 6.94e-17 | 0.1 |
| 64x64 | 4096 | 0.015625 | Converged | 7 | 3.47e-17 | 0.6 |
| 128x128 | 16384 | 0.0078125 | Converged | 6 | 3.82e-17 | 3.7 |

**phi_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9234e-03 | 2.3621e-03 | 4.8393e-03 |
| 32x32 | 1.2176e-03 | 1.5159e-03 | 3.0986e-03 |
| 64x64 | 6.8922e-04 | 8.5710e-04 | 1.7422e-03 |
| 128x128 | 3.6689e-04 | 4.5526e-04 | 9.2164e-04 |

**phi_interior_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.3851e-03 | 2.6835e-03 | 4.8393e-03 |
| 32x32 | 1.3723e-03 | 1.6163e-03 | 3.0986e-03 |
| 64x64 | 7.3292e-04 | 8.8472e-04 | 1.7422e-03 |
| 128x128 | 3.7845e-04 | 4.6249e-04 | 9.2164e-04 |

**phi_boundary_ring_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.1499e-04 | 5.3056e-04 | 1.1246e-03 |
| 32x32 | 9.4549e-05 | 1.2296e-04 | 2.7187e-04 |
| 64x64 | 2.2668e-05 | 2.9545e-05 | 6.6482e-05 |
| 128x128 | 5.5576e-06 | 7.2394e-06 | 1.6415e-05 |

**phi_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.5267e-03 | 4.7141e-03 | 1.3712e-02 |
| 32x32 | 1.9086e-03 | 2.5950e-03 | 7.2960e-03 |
| 64x64 | 1.0052e-03 | 1.3725e-03 | 3.7729e-03 |
| 128x128 | 5.1814e-04 | 7.0722e-04 | 1.9180e-03 |

**phi_interior_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.1851e-03 | 5.3046e-03 | 1.3712e-02 |
| 32x32 | 2.1208e-03 | 2.7628e-03 | 7.2960e-03 |
| 64x64 | 1.0649e-03 | 1.4165e-03 | 3.7729e-03 |
| 128x128 | 5.3395e-04 | 7.1843e-04 | 1.9180e-03 |

**phi_boundary_ring_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.3757e-03 | 1.7029e-03 | 3.4927e-03 |
| 32x32 | 3.6835e-04 | 4.5498e-04 | 8.9392e-04 |
| 64x64 | 9.4997e-05 | 1.1751e-04 | 2.2741e-04 |
| 128x128 | 2.4099e-05 | 2.9853e-05 | 5.8600e-05 |

**phi_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.9387e-03 | 2.3943e-03 | 4.9934e-03 |
| 32x32 | 1.2232e-03 | 1.5247e-03 | 3.1310e-03 |
| 64x64 | 6.9087e-04 | 8.5946e-04 | 1.7490e-03 |
| 128x128 | 3.6733e-04 | 4.5587e-04 | 9.2320e-04 |

**phi_interior_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.4096e-03 | 2.7221e-03 | 4.9934e-03 |
| 32x32 | 1.3789e-03 | 1.6258e-03 | 3.1310e-03 |
| 64x64 | 7.3469e-04 | 8.8716e-04 | 1.7490e-03 |
| 128x128 | 3.7891e-04 | 4.6311e-04 | 9.2320e-04 |

**phi_boundary_ring_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.0024e-04 | 5.0259e-04 | 1.0144e-03 |
| 32x32 | 9.2732e-05 | 1.1940e-04 | 2.5811e-04 |
| 64x64 | 2.2441e-05 | 2.9101e-05 | 6.4761e-05 |
| 128x128 | 5.5293e-06 | 7.1841e-06 | 1.6199e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| phi_cartesian | l2 | 1 | 1.558, 1.769, 1.883 | 0.361, 0.713 | monotonic_not_asymptotic |
| phi_distorted_corrected | l2 | 1 | 1.570, 1.774, 1.885 | 0.386, 0.721 | monotonic_not_asymptotic |
| phi_distorted_uncorrected | l2 | 1 | 1.817, 1.891, 1.941 | 0.794, 0.878 | asymptotic |

- [x] distorted: phi_distorted_corrected L1/L2/Linf decrease on every refinement: monotone
- [x] distorted: phi_distorted_uncorrected L1/L2/Linf decrease on every refinement: monotone
- [x] distorted: corrected observed order equals Cartesian within 0.05: corrected 0.721, Cartesian 0.713
- [x] distorted: corrected reduction factors equal Cartesian within 2%: corrected/Cartesian: 1.570/1.558 1.774/1.769 1.885/1.883
- [x] distorted: corrected L2 within 5% of Cartesian at 16x16: corrected 2.3943e-03, Cartesian 2.3621e-03
- [x] distorted: corrected L2 within 5% of Cartesian at 32x32: corrected 1.5247e-03, Cartesian 1.5159e-03
- [x] distorted: corrected L2 within 5% of Cartesian at 64x64: corrected 8.5946e-04, Cartesian 8.5710e-04
- [x] distorted: uncorrected L2 >= 1.3x Cartesian at 64x64: uncorrected 1.3725e-03 = 1.60x Cartesian
- [x] distorted: corrected L2 within 5% of Cartesian at 128x128: corrected 4.5587e-04, Cartesian 4.5526e-04
- [x] distorted: uncorrected L2 >= 1.3x Cartesian at 128x128: uncorrected 7.0722e-04 = 1.55x Cartesian
