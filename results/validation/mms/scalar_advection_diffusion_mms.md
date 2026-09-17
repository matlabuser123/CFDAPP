### MMS: scalar_advection_diffusion_mms

Steady scalar advection-diffusion, production ThermalSolver (cp div(rho U phi) - k lap(phi) = Q), five systematically refined Cartesian grids

Manufactured solution: mms::scalar: phi = sin(pi x) sin(pi y) + x^2 y / 2 + 1/4 on [0,1]^2; advecting U = (1, 0.5) + (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 3 | 2.78e-17 | 0.0 |
| 32x32 | 1024 | 0.03125 | Converged | 3 | 6.94e-17 | 0.0 |
| 64x64 | 4096 | 0.015625 | Converged | 3 | 3.47e-17 | 0.1 |
| 128x128 | 16384 | 0.0078125 | Converged | 3 | 3.82e-17 | 0.8 |
| 256x256 | 65536 | 0.0039062 | Converged | 3 | 9.19e-17 | 6.3 |

**phi**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.1330e-02 | 5.3924e-02 | 1.1679e-01 |
| 32x32 | 2.3119e-02 | 3.0028e-02 | 6.4413e-02 |
| 64x64 | 1.2333e-02 | 1.5923e-02 | 3.3916e-02 |
| 128x128 | 6.3867e-03 | 8.2094e-03 | 1.7418e-02 |
| 256x256 | 3.2524e-03 | 4.1696e-03 | 8.8294e-03 |

**phi_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 5.0909e-02 | 6.1192e-02 | 1.1679e-01 |
| 32x32 | 2.5902e-02 | 3.1998e-02 | 6.4413e-02 |
| 64x64 | 1.3090e-02 | 1.6434e-02 | 3.3916e-02 |
| 128x128 | 6.5844e-03 | 8.3396e-03 | 1.7418e-02 |
| 256x256 | 3.3030e-03 | 4.2024e-03 | 8.8294e-03 |

**phi_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.0039e-02 | 1.3217e-02 | 3.0559e-02 |
| 32x32 | 2.9159e-03 | 3.8698e-03 | 9.2821e-03 |
| 64x64 | 7.9258e-04 | 1.0599e-03 | 2.6010e-03 |
| 128x128 | 2.0721e-04 | 2.7855e-04 | 6.9262e-04 |
| 256x256 | 5.3022e-05 | 7.1496e-05 | 1.7901e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| phi | l1 | 1 | 1.788, 1.875, 1.931, 1.964 | 0.756, 0.859, 0.924 | asymptotic |
| phi | l2 | 1 | 1.796, 1.886, 1.940, 1.969 | 0.760, 0.871, 0.933 | asymptotic |
| phi | linf | 1 | 1.813, 1.899, 1.947, 1.973 | 0.780, 0.886, 0.942 | asymptotic |
| phi_boundary_ring | l2 | 2 | 3.415, 3.651, 3.805, 3.896 | 1.734, 1.847, 1.916 | asymptotic |
| phi_interior | l2 | 1 | 1.912, 1.947, 1.971, 1.984 | 0.907, 0.943, 0.968 | asymptotic |

- [x] scalar: every solve Converged: ThermalStatus::Converged on all 5 grids
- [x] scalar: phi L1/L2/Linf decrease on every refinement: monotone
- [x] scalar: phi l1 observed order in [0.80, 1.20]: finest-triplet observed order 0.924 (asymptotic vs formal 1); reduction factors 1.788 1.875 1.931 1.964
- [x] scalar: phi l2 observed order in [0.80, 1.20]: finest-triplet observed order 0.933 (asymptotic vs formal 1); reduction factors 1.796 1.886 1.940 1.969
- [x] scalar: phi linf observed order in [0.80, 1.20]: finest-triplet observed order 0.942 (asymptotic vs formal 1); reduction factors 1.813 1.899 1.947 1.973
- [x] scalar: extrapolated L2 error |E_ext| <= 0.25 E_fine: E_ext -2.733e-04, E_fine 4.170e-03
- [x] boundary: phi_boundary_ring l2 observed order in [1.70, 2.30]: finest-triplet observed order 1.916 (asymptotic vs formal 2); reduction factors 3.415 3.651 3.805 3.896
- [x] boundary: phi_interior l2 observed order in [0.80, 1.20]: finest-triplet observed order 0.968 (asymptotic vs formal 1); reduction factors 1.912 1.947 1.971 1.984
