### MMS: scalar_advection_diffusion_mms

Steady scalar advection-diffusion, production ThermalSolver (cp div(rho U phi) - k lap(phi) = Q), five systematically refined Cartesian grids

Manufactured solution: mms::scalar: phi = sin(pi x) sin(pi y) + x^2 y / 2 + 1/4 on [0,1]^2; advecting U = (1, 0.5) + (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 3 | 2.78e-17 | 0.1 |
| 32x32 | 1024 | 0.03125 | Converged | 3 | 6.94e-17 | 0.3 |
| 64x64 | 4096 | 0.015625 | Converged | 3 | 3.47e-17 | 1.5 |
| 128x128 | 16384 | 0.0078125 | Converged | 3 | 3.82e-17 | 11.1 |
| 256x256 | 65536 | 0.0039062 | Converged | 3 | 9.19e-17 | 84.5 |

**phi**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.0213e-02 | 5.2446e-02 | 1.1448e-01 |
| 32x32 | 2.2678e-02 | 2.9545e-02 | 6.3699e-02 |
| 64x64 | 1.2191e-02 | 1.5783e-02 | 3.3715e-02 |
| 128x128 | 6.3462e-03 | 8.1717e-03 | 1.7364e-02 |
| 256x256 | 3.2415e-03 | 4.1598e-03 | 8.8157e-03 |

**phi_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.8993e-02 | 5.9413e-02 | 1.1448e-01 |
| 32x32 | 2.5342e-02 | 3.1476e-02 | 6.3699e-02 |
| 64x64 | 1.2932e-02 | 1.6289e-02 | 3.3715e-02 |
| 128x128 | 6.5417e-03 | 8.3012e-03 | 1.7364e-02 |
| 256x256 | 3.2918e-03 | 4.1925e-03 | 8.8157e-03 |

**phi_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.1534e-02 | 1.4318e-02 | 3.2176e-02 |
| 32x32 | 3.3363e-03 | 4.1849e-03 | 9.6908e-03 |
| 64x64 | 9.0020e-04 | 1.1381e-03 | 2.6827e-03 |
| 128x128 | 2.3407e-04 | 2.9732e-04 | 7.0705e-04 |
| 256x256 | 5.9700e-05 | 7.6020e-05 | 1.8156e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| phi | l1 | 1 | 1.773, 1.860, 1.921, 1.958 | 0.742, 0.843, 0.913 | asymptotic |
| phi | l2 | 1 | 1.775, 1.872, 1.931, 1.964 | 0.735, 0.855, 0.924 | asymptotic |
| phi | linf | 1 | 1.797, 1.889, 1.942, 1.970 | 0.760, 0.875, 0.936 | asymptotic |
| phi_boundary_ring | l2 | 2 | 3.421, 3.677, 3.828, 3.911 | 1.734, 1.857, 1.926 | asymptotic |
| phi_interior | l2 | 1 | 1.888, 1.932, 1.962, 1.980 | 0.879, 0.927, 0.959 | asymptotic |

- [x] scalar: every solve Converged: ThermalStatus::Converged on all 5 grids
- [x] scalar: phi L1/L2/Linf decrease on every refinement: monotone
- [x] scalar: phi l1 observed order in [0.80, 1.20]: finest-triplet observed order 0.913 (asymptotic vs formal 1); reduction factors 1.773 1.860 1.921 1.958
- [x] scalar: phi l2 observed order in [0.80, 1.20]: finest-triplet observed order 0.924 (asymptotic vs formal 1); reduction factors 1.775 1.872 1.931 1.964
- [x] scalar: phi linf observed order in [0.80, 1.20]: finest-triplet observed order 0.936 (asymptotic vs formal 1); reduction factors 1.797 1.889 1.942 1.970
- [x] scalar: extrapolated L2 error |E_ext| <= 0.25 E_fine: E_ext -3.121e-04, E_fine 4.160e-03
- [x] boundary: phi_boundary_ring l2 observed order in [1.70, 2.30]: finest-triplet observed order 1.926 (asymptotic vs formal 2); reduction factors 3.421 3.677 3.828 3.911
- [x] boundary: phi_interior l2 observed order in [0.80, 1.20]: finest-triplet observed order 0.959 (asymptotic vs formal 1); reduction factors 1.888 1.932 1.962 1.980
