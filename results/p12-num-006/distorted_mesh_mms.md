### MMS: distorted_mesh_mms

Scalar advection-diffusion MMS (diffusion-dominated, k = 5) on Cartesian vs distorted (0.25 h) meshes, with and without the P12-NUM-003 non-orthogonal correction; level status/iterations are the corrected distorted solve

Manufactured solution: mms::scalar (as scalar_advection_diffusion_mms)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 9 | 2.78e-17 | 0.1 |
| 32x32 | 1024 | 0.03125 | Converged | 8 | 6.94e-17 | 0.4 |
| 64x64 | 4096 | 0.015625 | Converged | 7 | 3.47e-17 | 2.3 |
| 128x128 | 16384 | 0.0078125 | Converged | 6 | 3.82e-17 | 15.6 |

**phi_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6618e-03 | 2.1102e-03 | 4.4967e-03 |
| 32x32 | 1.1872e-03 | 1.4872e-03 | 3.0613e-03 |
| 64x64 | 6.8652e-04 | 8.5439e-04 | 1.7384e-03 |
| 128x128 | 3.6686e-04 | 4.5515e-04 | 9.2143e-04 |

**phi_interior_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.0714e-03 | 2.4010e-03 | 4.4967e-03 |
| 32x32 | 1.3380e-03 | 1.5858e-03 | 3.0613e-03 |
| 64x64 | 7.2991e-04 | 8.8192e-04 | 1.7384e-03 |
| 128x128 | 3.7839e-04 | 4.6237e-04 | 9.2143e-04 |

**phi_boundary_ring_cartesian**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.2392e-04 | 4.0888e-04 | 9.6925e-04 |
| 32x32 | 9.2225e-05 | 1.1758e-04 | 2.8537e-04 |
| 64x64 | 2.4661e-05 | 3.1662e-05 | 7.6860e-05 |
| 128x128 | 6.3759e-06 | 8.2168e-06 | 1.9891e-05 |

**phi_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.4318e-03 | 4.5447e-03 | 1.3366e-02 |
| 32x32 | 1.8882e-03 | 2.5640e-03 | 7.2590e-03 |
| 64x64 | 1.0009e-03 | 1.3675e-03 | 3.7707e-03 |
| 128x128 | 5.1723e-04 | 7.0639e-04 | 1.9185e-03 |

**phi_interior_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.0501e-03 | 5.1027e-03 | 1.3366e-02 |
| 32x32 | 2.0966e-03 | 2.7295e-03 | 7.2590e-03 |
| 64x64 | 1.0601e-03 | 1.4113e-03 | 3.7707e-03 |
| 128x128 | 5.3299e-04 | 7.1758e-04 | 1.9185e-03 |

**phi_boundary_ring_distorted_uncorrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4122e-03 | 1.7516e-03 | 3.7347e-03 |
| 32x32 | 3.7534e-04 | 4.6390e-04 | 9.9525e-04 |
| 64x64 | 9.6736e-05 | 1.1905e-04 | 2.5734e-04 |
| 128x128 | 2.4540e-05 | 3.0134e-05 | 6.5277e-05 |

**phi_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6796e-03 | 2.1440e-03 | 4.6717e-03 |
| 32x32 | 1.1923e-03 | 1.4957e-03 | 3.0935e-03 |
| 64x64 | 6.8811e-04 | 8.5669e-04 | 1.7452e-03 |
| 128x128 | 3.6729e-04 | 4.5575e-04 | 9.2295e-04 |

**phi_interior_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.0957e-03 | 2.4402e-03 | 4.6717e-03 |
| 32x32 | 1.3441e-03 | 1.5948e-03 | 3.0935e-03 |
| 64x64 | 7.3162e-04 | 8.8429e-04 | 1.7452e-03 |
| 128x128 | 3.7884e-04 | 4.6298e-04 | 9.2295e-04 |

**phi_boundary_ring_distorted_corrected**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.2031e-04 | 4.0051e-04 | 9.0134e-04 |
| 32x32 | 9.0653e-05 | 1.1441e-04 | 2.7273e-04 |
| 64x64 | 2.4422e-05 | 3.1164e-05 | 7.4967e-05 |
| 128x128 | 6.3439e-06 | 8.1489e-06 | 1.9637e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| phi_cartesian | l2 | 1 | 1.419, 1.741, 1.877 | --, 0.665 | monotonic_not_asymptotic |
| phi_distorted_corrected | l2 | 1 | 1.433, 1.746, 1.880 | 0.021, 0.672 | monotonic_not_asymptotic |
| phi_distorted_uncorrected | l2 | 1 | 1.772, 1.875, 1.936 | 0.727, 0.856 | asymptotic |

- [x] distorted: phi_distorted_corrected L1/L2/Linf decrease on every refinement: monotone
- [x] distorted: phi_distorted_uncorrected L1/L2/Linf decrease on every refinement: monotone
- [x] distorted: corrected observed order equals Cartesian within 0.05: corrected 0.672, Cartesian 0.665
- [x] distorted: corrected reduction factors equal Cartesian within 2%: corrected/Cartesian: 1.433/1.419 1.746/1.741 1.880/1.877
- [x] distorted: corrected L2 within 5% of Cartesian at 16x16: corrected 2.1440e-03, Cartesian 2.1102e-03
- [x] distorted: corrected L2 within 5% of Cartesian at 32x32: corrected 1.4957e-03, Cartesian 1.4872e-03
- [x] distorted: corrected L2 within 5% of Cartesian at 64x64: corrected 8.5669e-04, Cartesian 8.5439e-04
- [x] distorted: uncorrected L2 >= 1.3x Cartesian at 64x64: uncorrected 1.3675e-03 = 1.60x Cartesian
- [x] distorted: corrected L2 within 5% of Cartesian at 128x128: corrected 4.5575e-04, Cartesian 4.5515e-04
- [x] distorted: uncorrected L2 >= 1.3x Cartesian at 128x128: uncorrected 7.0639e-04 = 1.55x Cartesian
