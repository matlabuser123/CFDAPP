### MMS: p12_mesh_005_operators_3d

P12-MESH-005: production operators on the 3D Cartesian hexahedral mesh, unit cube, per-face exact Dirichlet data

Manufactured solution: phi = sin(pi x) cos(pi y/2) exp(z/2) + x y z; u = (1 + y/2, 1 + z/2, 1 + x/2); psi = exp(0.3x + 0.5y + 0.7z)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8x8 | 512 | 0.125 | poisson Converged, transport Converged | 0 | -- | 0.0 |
| 16x16x16 | 4096 | 0.0625 | poisson Converged, transport Converged | 0 | -- | 0.1 |
| 32x32x32 | 32768 | 0.03125 | poisson Converged, transport Converged | 0 | -- | 1.0 |
| 64x64x64 | 262144 | 0.015625 | poisson Converged, transport Converged | 0 | -- | 15.8 |

**F1_gg_gradient**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 3.5410e-02 | 4.2334e-02 | 1.0595e-01 |
| 16x16x16 | 9.9288e-03 | 1.2017e-02 | 3.1257e-02 |
| 32x32x32 | 2.6204e-03 | 3.1733e-03 | 8.1599e-03 |
| 64x64x64 | 6.7249e-04 | 8.1364e-04 | 2.0660e-03 |

**F1_gg_gradient_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 3.3517e-02 | 4.1134e-02 | 1.0595e-01 |
| 16x16x16 | 8.9889e-03 | 1.1396e-02 | 3.1257e-02 |
| 32x32x32 | 2.3123e-03 | 2.9712e-03 | 8.1599e-03 |
| 64x64x64 | 5.8558e-04 | 7.5692e-04 | 2.0660e-03 |

**F2_lsq_gradient_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 3.8005e-02 | 4.3924e-02 | 9.5736e-02 |
| 16x16x16 | 1.0392e-02 | 1.2311e-02 | 3.0004e-02 |
| 32x32x32 | 2.6863e-03 | 3.2148e-03 | 8.0141e-03 |
| 64x64x64 | 6.8117e-04 | 8.1910e-04 | 2.0487e-03 |

**F3_lsq_gradient**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 4.5549e-02 | 5.4336e-02 | 1.2553e-01 |
| 16x16x16 | 1.1590e-02 | 1.4035e-02 | 3.2650e-02 |
| 32x32x32 | 2.9393e-03 | 3.6896e-03 | 1.5806e-02 |
| 64x64x64 | 7.4321e-04 | 1.0046e-03 | 7.9489e-03 |

**F4_diffusion**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 7.1050e-02 | 8.6506e-02 | 2.0107e-01 |
| 16x16x16 | 1.7662e-02 | 2.1845e-02 | 5.3751e-02 |
| 32x32x32 | 4.4178e-03 | 5.4928e-03 | 1.3716e-02 |
| 64x64x64 | 1.1058e-03 | 1.3771e-03 | 3.4534e-03 |

**F5_poisson**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 2.5110e-03 | 3.2509e-03 | 7.7696e-03 |
| 16x16x16 | 6.0862e-04 | 8.0324e-04 | 1.9576e-03 |
| 32x32x32 | 1.5092e-04 | 2.0030e-04 | 4.9146e-04 |
| 64x64x64 | 3.7737e-05 | 5.0049e-05 | 1.2293e-04 |

**F6_convection_upwind**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 1.3395e-01 | 1.4337e-01 | 3.0333e-01 |
| 16x16x16 | 6.9536e-02 | 7.3801e-02 | 1.6262e-01 |
| 32x32x32 | 3.5406e-02 | 3.7412e-02 | 8.4196e-02 |
| 64x64x64 | 1.7862e-02 | 1.8831e-02 | 4.2838e-02 |

**F7_convection_central_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 1.7239e-03 | 1.7448e-03 | 2.4275e-03 |
| 16x16x16 | 4.3796e-04 | 4.5061e-04 | 8.1214e-04 |
| 32x32x32 | 1.1059e-04 | 1.1496e-04 | 2.3458e-04 |
| 64x64x64 | 2.7800e-05 | 2.9060e-05 | 6.3016e-05 |

**F7_convection_central_global**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 4.4540e-01 | 7.2709e-01 | 4.4970e+00 |
| 16x16x16 | 2.4517e-01 | 5.0950e-01 | 4.7614e+00 |
| 32x32x32 | 1.2826e-01 | 3.5738e-01 | 4.8996e+00 |
| 64x64x64 | 6.5556e-02 | 2.5144e-01 | 4.9702e+00 |

**F7_convection_linear_upwind_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 6.5089e-03 | 6.5880e-03 | 9.1653e-03 |
| 16x16x16 | 1.7015e-03 | 1.7507e-03 | 3.1553e-03 |
| 32x32x32 | 4.3593e-04 | 4.5315e-04 | 9.2467e-04 |
| 64x64x64 | 1.1039e-04 | 1.1539e-04 | 2.5022e-04 |

**F7_convection_linear_upwind_global**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 4.3242e-01 | 7.0317e-01 | 4.3580e+00 |
| 16x16x16 | 2.4184e-01 | 5.0060e-01 | 4.6836e+00 |
| 32x32x32 | 1.2743e-01 | 3.5415e-01 | 4.8584e+00 |
| 64x64x64 | 6.5348e-02 | 2.5029e-01 | 4.9490e+00 |

**F7_convection_quick_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 4.1164e-03 | 4.1664e-03 | 5.7964e-03 |
| 16x16x16 | 1.0697e-03 | 1.1007e-03 | 1.9837e-03 |
| 32x32x32 | 2.7326e-04 | 2.8405e-04 | 5.7962e-04 |
| 64x64x64 | 6.9093e-05 | 7.2225e-05 | 1.5662e-04 |

**F7_convection_quick_global**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 4.3891e-01 | 7.1512e-01 | 4.4275e+00 |
| 16x16x16 | 2.4350e-01 | 5.0505e-01 | 4.7225e+00 |
| 32x32x32 | 1.2785e-01 | 3.5577e-01 | 4.8790e+00 |
| 64x64x64 | 6.5452e-02 | 2.5087e-01 | 4.9596e+00 |

**F8_convection_diffusion**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 4.9118e-02 | 6.2194e-02 | 1.6062e-01 |
| 16x16x16 | 2.6584e-02 | 3.5577e-02 | 9.7403e-02 |
| 32x32x32 | 1.4539e-02 | 1.9862e-02 | 5.4584e-02 |
| 64x64x64 | 7.7998e-03 | 1.0665e-02 | 2.9109e-02 |

- [x] F1_gg_gradient L1: pairwise p 1.834 1.922 1.962; finest 1.962 vs threshold 1.500 (expected 2.0); decreasing
- [x] F1_gg_gradient L2: pairwise p 1.817 1.921 1.964; finest 1.964 vs threshold 1.500 (expected 2.0); decreasing
- [x] F1_gg_gradient Linf: pairwise p 1.761 1.938 1.982; finest 1.982 vs threshold 1.500 (expected 2.0); decreasing
- [x] F2_lsq_gradient_interior L1: pairwise p 1.871 1.952 1.980; finest 1.980 vs threshold 1.500 (expected 2.0); decreasing
- [x] F2_lsq_gradient_interior L2: pairwise p 1.835 1.937 1.973; finest 1.973 vs threshold 1.500 (expected 2.0); decreasing
- [x] F2_lsq_gradient_interior Linf: pairwise p 1.674 1.905 1.968; finest 1.968 vs threshold 1.500 (expected 2.0); decreasing
- [x] F3_lsq_gradient L1: pairwise p 1.975 1.979 1.984; finest 1.984 vs threshold 1.500 (expected 2.0); decreasing
- [x] F3_lsq_gradient L2: pairwise p 1.953 1.927 1.877; finest 1.877 vs threshold 1.125 (expected 1.5); decreasing
- [x] F3_lsq_gradient Linf: pairwise p 1.943 1.047 0.992; finest 0.992 vs threshold 0.750 (expected 1.0); decreasing
- [x] F4_diffusion L1: pairwise p 2.008 1.999 1.998; finest 1.998 vs threshold 1.500 (expected 2.0); decreasing
- [x] F4_diffusion L2: pairwise p 1.985 1.992 1.996; finest 1.996 vs threshold 1.500 (expected 2.0); decreasing
- [x] F4_diffusion Linf: pairwise p 1.903 1.970 1.990; finest 1.990 vs threshold 1.500 (expected 2.0); decreasing
- [x] F5_poisson L1: pairwise p 2.045 2.012 2.000; finest 2.000 vs threshold 1.500 (expected 2.0); decreasing
- [x] F5_poisson L2: pairwise p 2.017 2.004 2.001; finest 2.001 vs threshold 1.500 (expected 2.0); decreasing
- [x] F5_poisson Linf: pairwise p 1.989 1.994 1.999; finest 1.999 vs threshold 1.500 (expected 2.0); decreasing
- [x] F6_convection_upwind L1: pairwise p 0.946 0.974 0.987; finest 0.987 vs threshold 0.750 (expected 1.0); decreasing
- [x] F6_convection_upwind L2: pairwise p 0.958 0.980 0.990; finest 0.990 vs threshold 0.750 (expected 1.0); decreasing
- [x] F6_convection_upwind Linf: pairwise p 0.899 0.950 0.975; finest 0.975 vs threshold 0.750 (expected 1.0); decreasing
- [x] F7_convection_central_interior L1: pairwise p 1.977 1.986 1.992; finest 1.992 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_central_interior L2: pairwise p 1.953 1.971 1.984; finest 1.984 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_central_interior Linf: pairwise p 1.580 1.792 1.896; finest 1.896 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_linear_upwind_interior L1: pairwise p 1.936 1.965 1.982; finest 1.982 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_linear_upwind_interior L2: pairwise p 1.912 1.950 1.973; finest 1.973 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_linear_upwind_interior Linf: pairwise p 1.538 1.771 1.886; finest 1.886 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_quick_interior L1: pairwise p 1.944 1.969 1.984; finest 1.984 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_quick_interior L2: pairwise p 1.920 1.954 1.976; finest 1.976 vs threshold 1.500 (expected 2.0); decreasing
- [x] F7_convection_quick_interior Linf: pairwise p 1.547 1.775 1.888; finest 1.888 vs threshold 1.500 (expected 2.0); decreasing
- [x] F8_convection_diffusion L1: pairwise p 0.886 0.871 0.898; finest 0.898 vs threshold 0.750 (expected 1.0); decreasing
- [x] F8_convection_diffusion L2: pairwise p 0.806 0.841 0.897; finest 0.897 vs threshold 0.750 (expected 1.0); decreasing
- [x] F8_convection_diffusion Linf: pairwise p 0.722 0.836 0.907; finest 0.907 vs threshold 0.750 (expected 1.0); decreasing
