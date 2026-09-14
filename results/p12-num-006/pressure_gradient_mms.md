### MMS: pressure_gradient_mms

Production momentum pressure source -V grad(p) of the exact pressure vs the analytical -V grad p (per unit volume), x and y, Green-Gauss and least-squares

Manufactured solution: mms::pressure: p = cos(pi x) cos(pi y) + x y / 2

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Evaluated | 0 | -- | 0.0 |
| 32x32 | 1024 | 0.03125 | Evaluated | 0 | -- | 0.0 |
| 64x64 | 4096 | 0.015625 | Evaluated | 0 | -- | 0.0 |
| 128x128 | 16384 | 0.0078125 | Evaluated | 0 | -- | 0.0 |

**dpdx_green_gauss**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6321e-02 | 2.7870e-02 | 1.0346e-01 |
| 32x32 | 4.0880e-03 | 9.4551e-03 | 5.1486e-02 |
| 64x64 | 1.0225e-03 | 3.2763e-03 | 2.5712e-02 |
| 128x128 | 2.5565e-04 | 1.1470e-03 | 1.2852e-02 |

**dpdx_green_gauss_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.4466e-03 | 9.9744e-03 | 1.9187e-02 |
| 32x32 | 2.0896e-03 | 2.5165e-03 | 4.9836e-03 |
| 64x64 | 5.1753e-04 | 6.3042e-04 | 1.2577e-03 |
| 128x128 | 1.2865e-04 | 1.5768e-04 | 3.1517e-04 |

**dpdx_green_gauss_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.2042e-02 | 5.4672e-02 | 1.0346e-01 |
| 32x32 | 1.8593e-02 | 2.6311e-02 | 5.1486e-02 |
| 64x64 | 8.7251e-03 | 1.2977e-02 | 2.5712e-02 |
| 128x128 | 4.2248e-03 | 6.4538e-03 | 1.2852e-02 |

**dpdy_green_gauss**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6321e-02 | 2.7870e-02 | 1.0346e-01 |
| 32x32 | 4.0880e-03 | 9.4551e-03 | 5.1486e-02 |
| 64x64 | 1.0225e-03 | 3.2763e-03 | 2.5712e-02 |
| 128x128 | 2.5565e-04 | 1.1470e-03 | 1.2852e-02 |

**dpdy_green_gauss_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.4466e-03 | 9.9744e-03 | 1.9187e-02 |
| 32x32 | 2.0896e-03 | 2.5165e-03 | 4.9836e-03 |
| 64x64 | 5.1753e-04 | 6.3042e-04 | 1.2577e-03 |
| 128x128 | 1.2865e-04 | 1.5768e-04 | 3.1517e-04 |

**dpdy_green_gauss_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.2042e-02 | 5.4672e-02 | 1.0346e-01 |
| 32x32 | 1.8593e-02 | 2.6311e-02 | 5.1486e-02 |
| 64x64 | 8.7251e-03 | 1.2977e-02 | 2.5712e-02 |
| 128x128 | 4.2248e-03 | 6.4538e-03 | 1.2852e-02 |

**dpdx_least_squares**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.1918e-03 | 1.0074e-02 | 1.9954e-02 |
| 32x32 | 2.0460e-03 | 2.5221e-03 | 5.0320e-03 |
| 64x64 | 5.1137e-04 | 6.3075e-04 | 1.2607e-03 |
| 128x128 | 1.2783e-04 | 1.5770e-04 | 3.1536e-04 |

**dpdx_least_squares_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.4466e-03 | 9.9744e-03 | 1.9187e-02 |
| 32x32 | 2.0896e-03 | 2.5165e-03 | 4.9836e-03 |
| 64x64 | 5.1753e-04 | 6.3042e-04 | 1.2577e-03 |
| 128x128 | 1.2865e-04 | 1.5768e-04 | 3.1517e-04 |

**dpdx_least_squares_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 7.3593e-03 | 1.0392e-02 | 1.9954e-02 |
| 32x32 | 1.7295e-03 | 2.5621e-03 | 5.0320e-03 |
| 64x64 | 4.1736e-04 | 6.3572e-04 | 1.2607e-03 |
| 128x128 | 1.0239e-04 | 1.5832e-04 | 3.1536e-04 |

**dpdy_least_squares**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.1918e-03 | 1.0074e-02 | 1.9954e-02 |
| 32x32 | 2.0460e-03 | 2.5221e-03 | 5.0320e-03 |
| 64x64 | 5.1137e-04 | 6.3075e-04 | 1.2607e-03 |
| 128x128 | 1.2783e-04 | 1.5770e-04 | 3.1536e-04 |

**dpdy_least_squares_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 8.4466e-03 | 9.9744e-03 | 1.9187e-02 |
| 32x32 | 2.0896e-03 | 2.5165e-03 | 4.9836e-03 |
| 64x64 | 5.1753e-04 | 6.3042e-04 | 1.2577e-03 |
| 128x128 | 1.2865e-04 | 1.5768e-04 | 3.1517e-04 |

**dpdy_least_squares_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 7.3593e-03 | 1.0392e-02 | 1.9954e-02 |
| 32x32 | 1.7295e-03 | 2.5621e-03 | 5.0320e-03 |
| 64x64 | 4.1736e-04 | 6.3572e-04 | 1.2607e-03 |
| 128x128 | 1.0239e-04 | 1.5832e-04 | 3.1536e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| dpdx_green_gauss_interior | l2 | 2 | 3.964, 3.992, 3.998 | 1.983, 1.996 | asymptotic |
| dpdx_least_squares_interior | l2 | 2 | 3.964, 3.992, 3.998 | 1.983, 1.996 | asymptotic |
| dpdx_green_gauss_boundary_ring | linf | 1 | 2.009, 2.002, 2.001 | 1.012, 1.003 | asymptotic |
| dpdx_green_gauss | l2 | 1.5 | 2.948, 2.886, 2.856 | 1.575, 1.537 | asymptotic |
| dpdx_least_squares_boundary_ring | linf | 1 | 3.965, 3.991, 3.998 | 1.984, 1.996 | monotonic_not_asymptotic |
| dpdx_least_squares | l2 | 2 | 3.994, 3.999, 4.000 | 1.997, 1.999 | asymptotic |
| dpdy_green_gauss_interior | l2 | 2 | 3.964, 3.992, 3.998 | 1.983, 1.996 | asymptotic |
| dpdy_least_squares_interior | l2 | 2 | 3.964, 3.992, 3.998 | 1.983, 1.996 | asymptotic |
| dpdy_green_gauss_boundary_ring | linf | 1 | 2.009, 2.002, 2.001 | 1.012, 1.003 | asymptotic |
| dpdy_green_gauss | l2 | 1.5 | 2.948, 2.886, 2.856 | 1.575, 1.537 | asymptotic |
| dpdy_least_squares_boundary_ring | linf | 1 | 3.965, 3.991, 3.998 | 1.984, 1.996 | monotonic_not_asymptotic |
| dpdy_least_squares | l2 | 2 | 3.994, 3.999, 4.000 | 1.997, 1.999 | asymptotic |

- [x] balance: dpdx_green_gauss_interior l2 observed order in [1.80, 2.30]: finest-triplet observed order 1.996 (asymptotic vs formal 2); reduction factors 3.964 3.992 3.998
- [x] balance: dpdx_green_gauss L1/L2/Linf decrease on every refinement: monotone
- [x] balance: dpdx_least_squares_interior l2 observed order in [1.80, 2.30]: finest-triplet observed order 1.996 (asymptotic vs formal 2); reduction factors 3.964 3.992 3.998
- [x] balance: dpdx_least_squares L1/L2/Linf decrease on every refinement: monotone
- [x] balance: dpdx_green_gauss_boundary_ring linf observed order in [0.80, 1.20]: finest-triplet observed order 1.003 (asymptotic vs formal 1); reduction factors 2.009 2.002 2.001
- [x] balance: dpdx_green_gauss l2 observed order in [1.30, 1.70]: finest-triplet observed order 1.537 (asymptotic vs formal 2); reduction factors 2.948 2.886 2.856
- [x] balance: dpdx_least_squares_boundary_ring linf observed order in [0.80, 2.30]: finest-triplet observed order 1.996 (monotonic_not_asymptotic vs formal 1); reduction factors 3.965 3.991 3.998
- [x] balance: dpdx_least_squares l2 observed order in [1.80, 2.30]: finest-triplet observed order 1.999 (asymptotic vs formal 2); reduction factors 3.994 3.999 4.000
- [x] balance: dpdy_green_gauss_interior l2 observed order in [1.80, 2.30]: finest-triplet observed order 1.996 (asymptotic vs formal 2); reduction factors 3.964 3.992 3.998
- [x] balance: dpdy_green_gauss L1/L2/Linf decrease on every refinement: monotone
- [x] balance: dpdy_least_squares_interior l2 observed order in [1.80, 2.30]: finest-triplet observed order 1.996 (asymptotic vs formal 2); reduction factors 3.964 3.992 3.998
- [x] balance: dpdy_least_squares L1/L2/Linf decrease on every refinement: monotone
- [x] balance: dpdy_green_gauss_boundary_ring linf observed order in [0.80, 1.20]: finest-triplet observed order 1.003 (asymptotic vs formal 1); reduction factors 2.009 2.002 2.001
- [x] balance: dpdy_green_gauss l2 observed order in [1.30, 1.70]: finest-triplet observed order 1.537 (asymptotic vs formal 2); reduction factors 2.948 2.886 2.856
- [x] balance: dpdy_least_squares_boundary_ring linf observed order in [0.80, 2.30]: finest-triplet observed order 1.996 (monotonic_not_asymptotic vs formal 1); reduction factors 3.965 3.991 3.998
- [x] balance: dpdy_least_squares l2 observed order in [1.80, 2.30]: finest-triplet observed order 1.999 (asymptotic vs formal 2); reduction factors 3.994 3.999 4.000
