### MMS: momentum_mms_upwind

Momentum-only MMS: production relaxed-momentum assembly (alpha = 1) with exact pressure, exact face mass flux and analytical forcing; Picard on the lagged terms from U = 0

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 3 | 1.54e-33 | 0.0 |
| 32x32 | 1024 | 0.03125 | Converged | 3 | 7.7e-34 | 0.0 |
| 64x64 | 4096 | 0.015625 | Converged | 3 | 7.7e-34 | 0.2 |
| 128x128 | 16384 | 0.0078125 | Converged | 3 | 9.63e-34 | 1.6 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.8284e-02 | 2.3441e-02 | 5.0632e-02 |
| 32x32 | 9.0852e-03 | 1.1735e-02 | 2.5457e-02 |
| 64x64 | 4.5515e-03 | 5.8952e-03 | 1.2830e-02 |
| 128x128 | 2.2791e-03 | 2.9550e-03 | 6.4390e-03 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.5428e-02 | 1.9919e-02 | 4.4170e-02 |
| 32x32 | 8.2349e-03 | 1.0754e-02 | 2.4110e-02 |
| 64x64 | 4.2628e-03 | 5.5852e-03 | 1.2590e-02 |
| 128x128 | 2.1709e-03 | 2.8476e-03 | 6.4313e-03 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.6881e-02 | 3.0761e-02 | 5.1555e-02 |
| 32x32 | 1.3785e-02 | 1.5917e-02 | 2.6351e-02 |
| 64x64 | 7.0095e-03 | 8.1208e-03 | 1.3413e-02 |
| 128x128 | 3.5373e-03 | 4.1038e-03 | 6.7750e-03 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.2340e-02 | 2.6534e-02 | 5.0632e-02 |
| 32x32 | 1.0159e-02 | 1.2501e-02 | 2.5457e-02 |
| 64x64 | 4.8282e-03 | 6.0843e-03 | 1.2830e-02 |
| 128x128 | 2.3493e-03 | 3.0018e-03 | 6.4390e-03 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.8644e-02 | 2.2499e-02 | 4.4170e-02 |
| 32x32 | 9.1873e-03 | 1.1453e-02 | 2.4110e-02 |
| 64x64 | 4.5199e-03 | 5.7643e-03 | 1.2590e-02 |
| 128x128 | 2.2376e-03 | 2.8928e-03 | 6.4313e-03 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 5.0351e-03 | 6.6821e-03 | 1.6957e-02 |
| 32x32 | 1.2905e-03 | 1.7223e-03 | 4.5417e-03 |
| 64x64 | 3.3187e-04 | 4.4142e-04 | 1.1789e-03 |
| 128x128 | 8.4353e-05 | 1.1196e-04 | 3.0058e-04 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.9231e-03 | 6.2667e-03 | 1.5438e-02 |
| 32x32 | 1.3220e-03 | 1.7249e-03 | 4.3880e-03 |
| 64x64 | 3.4172e-04 | 4.5206e-04 | 1.1688e-03 |
| 128x128 | 8.6868e-05 | 1.1573e-04 | 3.0176e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 1 | 1.998, 1.991, 1.995 | 1.003, 0.990 | asymptotic |
| u | l1 | 1 | 2.013, 1.996, 1.997 | 1.021, 0.996 | asymptotic |
| u | linf | 1 | 1.989, 1.984, 1.993 | 0.995, 0.982 | asymptotic |
| v | l2 | 1 | 1.852, 1.925, 1.961 | 0.826, 0.917 | asymptotic |
| v | l1 | 1 | 1.874, 1.932, 1.964 | 0.857, 0.925 | asymptotic |
| v | linf | 1 | 1.832, 1.915, 1.958 | 0.800, 0.903 | asymptotic |
| velocity | l2 | 1 | 1.933, 1.960, 1.979 | 0.929, 0.957 | asymptotic |
| u_interior | l2 | 1 | 2.123, 2.055, 2.027 | 1.129, 1.058 | asymptotic |
| u_boundary_ring | l2 | 1 | 3.880, 3.902, 3.942 | 1.953, 1.959 | monotonic_not_asymptotic |

- [x] u: u L1/L2/Linf decrease on every refinement: monotone
- [x] u: u l2 finest-pairwise observed order in [0.80, 2.00]: finest-PAIRWISE observed order 0.996 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.998 1.991 1.995
- [x] u: u l1 finest-pairwise observed order in [0.80, 2.00]: finest-PAIRWISE observed order 0.998 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 2.013 1.996 1.997
- [x] u: u linf finest-pairwise observed order in [0.60, 2.00]: finest-PAIRWISE observed order 0.995 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.989 1.984 1.993
- [x] v: v L1/L2/Linf decrease on every refinement: monotone
- [x] v: v l2 finest-pairwise observed order in [0.80, 2.00]: finest-PAIRWISE observed order 0.972 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.852 1.925 1.961
- [x] v: v l1 finest-pairwise observed order in [0.80, 2.00]: finest-PAIRWISE observed order 0.974 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.874 1.932 1.964
- [x] v: v linf finest-pairwise observed order in [0.60, 2.00]: finest-PAIRWISE observed order 0.969 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.832 1.915 1.958
