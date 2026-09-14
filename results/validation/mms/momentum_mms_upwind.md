### MMS: momentum_mms_upwind

Momentum-only MMS: production relaxed-momentum assembly (alpha = 1) with exact pressure, exact face mass flux and analytical forcing; Picard on the lagged terms from U = 0

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Converged | 3 | 1.54e-33 | 0.1 |
| 32x32 | 1024 | 0.03125 | Converged | 3 | 7.7e-34 | 0.6 |
| 64x64 | 4096 | 0.015625 | Converged | 3 | 7.7e-34 | 4.1 |
| 128x128 | 16384 | 0.0078125 | Converged | 3 | 9.63e-34 | 31.9 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.7464e-02 | 2.2493e-02 | 4.9393e-02 |
| 32x32 | 8.8528e-03 | 1.1476e-02 | 2.5166e-02 |
| 64x64 | 4.4899e-03 | 5.8282e-03 | 1.2758e-02 |
| 128x128 | 2.2632e-03 | 2.9380e-03 | 6.4214e-03 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4739e-02 | 1.9037e-02 | 4.3065e-02 |
| 32x32 | 8.0264e-03 | 1.0510e-02 | 2.3864e-02 |
| 64x64 | 4.2063e-03 | 5.5220e-03 | 1.2534e-02 |
| 128x128 | 2.1563e-03 | 2.8316e-03 | 6.4175e-03 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.5587e-02 | 2.9467e-02 | 5.0640e-02 |
| 32x32 | 1.3408e-02 | 1.5562e-02 | 2.6195e-02 |
| 64x64 | 6.9082e-03 | 8.0287e-03 | 1.3380e-02 |
| 128x128 | 3.5111e-03 | 4.0804e-03 | 6.7681e-03 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 2.1403e-02 | 2.5518e-02 | 4.9393e-02 |
| 32x32 | 9.9124e-03 | 1.2230e-02 | 2.5166e-02 |
| 64x64 | 4.7650e-03 | 6.0155e-03 | 1.2758e-02 |
| 128x128 | 2.3333e-03 | 2.9845e-03 | 6.4214e-03 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.7795e-02 | 2.1518e-02 | 4.3065e-02 |
| 32x32 | 8.9658e-03 | 1.1197e-02 | 2.3864e-02 |
| 64x64 | 4.4624e-03 | 5.6993e-03 | 1.2534e-02 |
| 128x128 | 2.2229e-03 | 2.8765e-03 | 6.4175e-03 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.5944e-03 | 5.5996e-03 | 1.4074e-02 |
| 32x32 | 1.1620e-03 | 1.4113e-03 | 3.5066e-03 |
| 64x64 | 2.9260e-04 | 3.5481e-04 | 8.7433e-04 |
| 128x128 | 7.3306e-05 | 8.8845e-05 | 2.1790e-04 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 4.7572e-03 | 5.8012e-03 | 1.2522e-02 |
| 32x32 | 1.2076e-03 | 1.4854e-03 | 3.3187e-03 |
| 64x64 | 2.9968e-04 | 3.7201e-04 | 8.5105e-04 |
| 128x128 | 7.4323e-05 | 9.2882e-05 | 2.1547e-04 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 1 | 1.960, 1.969, 1.984 | 0.964, 0.967 | asymptotic |
| u | l1 | 1 | 1.973, 1.972, 1.984 | 0.981, 0.970 | asymptotic |
| u | linf | 1 | 1.963, 1.972, 1.987 | 0.965, 0.969 | asymptotic |
| v | l2 | 1 | 1.811, 1.903, 1.950 | 0.773, 0.891 | asymptotic |
| v | l1 | 1 | 1.836, 1.908, 1.951 | 0.813, 0.898 | asymptotic |
| v | linf | 1 | 1.805, 1.904, 1.953 | 0.761, 0.889 | asymptotic |
| velocity | l2 | 1 | 1.894, 1.938, 1.968 | 0.884, 0.932 | asymptotic |
| u_interior | l2 | 1 | 2.086, 2.033, 2.016 | 1.096, 1.036 | asymptotic |
| u_boundary_ring | l2 | 1 | 3.968, 3.978, 3.994 | 1.987, 1.990 | monotonic_not_asymptotic |

- [x] u: u L1/L2/Linf decrease on every refinement: monotone
- [x] u: u l2 observed order in [0.80, 1.20]: finest-triplet observed order 0.967 (asymptotic vs formal 1); reduction factors 1.960 1.969 1.984
- [x] u: u l1 observed order in [0.80, 1.20]: finest-triplet observed order 0.970 (asymptotic vs formal 1); reduction factors 1.973 1.972 1.984
- [x] u: u linf observed order in [0.60, 1.20]: finest-triplet observed order 0.969 (asymptotic vs formal 1); reduction factors 1.963 1.972 1.987
- [x] v: v L1/L2/Linf decrease on every refinement: monotone
- [x] v: v l2 observed order in [0.80, 1.20]: finest-triplet observed order 0.891 (asymptotic vs formal 1); reduction factors 1.811 1.903 1.950
- [x] v: v l1 observed order in [0.80, 1.20]: finest-triplet observed order 0.898 (asymptotic vs formal 1); reduction factors 1.836 1.908 1.951
- [x] v: v linf observed order in [0.60, 1.20]: finest-triplet observed order 0.889 (asymptotic vs formal 1); reduction factors 1.805 1.904 1.953
