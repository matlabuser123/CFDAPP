### MMS: compressible_simple_mms

CompressibleSIMPLE MMS, isothermal ideal gas in a closed box, from rest; upwind convection (the only scheme of the compressible momentum assembly)

Manufactured solution: p = cos(pi x) cos(pi y) + x y / 2 (gauge); rho = (P_ref + p)/(R T0); rho U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 1070 | 4.73e-19 | 11.3 |
| 16x16 | 256 | 0.0625 | Converged | 2317 | 1.02e-19 | 80.7 |
| 32x32 | 1024 | 0.03125 | Converged | 3286 | 2.15e-20 | 437.7 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.6633e-02 | 3.4824e-02 | 8.2878e-02 |
| 16x16 | 1.5388e-02 | 2.0462e-02 | 4.7086e-02 |
| 32x32 | 8.4133e-03 | 1.1221e-02 | 2.6563e-02 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.6318e-02 | 3.4411e-02 | 8.1968e-02 |
| 16x16 | 1.5412e-02 | 2.0535e-02 | 5.2058e-02 |
| 32x32 | 8.3957e-03 | 1.1285e-02 | 2.8563e-02 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.2505e-02 | 4.8957e-02 | 8.4025e-02 |
| 16x16 | 2.4680e-02 | 2.8990e-02 | 5.2158e-02 |
| 32x32 | 1.3469e-02 | 1.5915e-02 | 2.8657e-02 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 7.0027e-02 | 8.7016e-02 | 2.0317e-01 |
| 16x16 | 4.1627e-02 | 4.9533e-02 | 1.0840e-01 |
| 32x32 | 2.5989e-02 | 2.9618e-02 | 6.0929e-02 |

**p_gauge**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.4970e-02 | 7.2592e-02 | 2.5115e-01 |
| 16x16 | 2.4123e-02 | 3.0989e-02 | 1.1883e-01 |
| 32x32 | 1.1781e-02 | 1.4852e-02 | 5.5083e-02 |

**density**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.5013e-03 | 4.3508e-03 | 1.0158e-02 |
| 16x16 | 2.0814e-03 | 2.4766e-03 | 5.4202e-03 |
| 32x32 | 1.2994e-03 | 1.4809e-03 | 3.0464e-03 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.9720e-10 | 6.6815e-10 | 4.3922e-09 |
| 16x16 | 7.1946e-10 | 2.7536e-09 | 4.2751e-08 |
| 32x32 | 1.3082e-09 | 1.1966e-08 | 3.8076e-07 |

**mass_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.6322e-02 | 3.6547e-02 | 9.0306e-02 |
| 16x16 | 1.4999e-02 | 2.0426e-02 | 5.2557e-02 |
| 32x32 | 8.0169e-03 | 1.0870e-02 | 2.7870e-02 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 1 | 1.702, 1.824 | 0.636 | monotonic_not_asymptotic |
| v | l2 | 1 | 1.676, 1.820 | 0.585 | monotonic_not_asymptotic |
| p | l2 | 1 | 1.757, 1.672 | 0.912 | asymptotic |
| p_gauge | l2 | 1 | 2.343, 2.087 | 1.366 | monotonic_not_asymptotic |
| density | l2 | 1 | 1.757, 1.672 | 0.912 | asymptotic |
| mass_flux | l2 | 1 | 1.789, 1.879 | 0.755 | monotonic_not_asymptotic |

- [x] compressible: u L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: u l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.636 (monotonic_not_asymptotic vs formal 1); reduction factors 1.702 1.824
- [x] compressible: v L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: v l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.585 (monotonic_not_asymptotic vs formal 1); reduction factors 1.676 1.820
- [x] compressible: p L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: p l2 observed order in [0.80, 1.60]: finest-triplet observed order 0.912 (asymptotic vs formal 1); reduction factors 1.757 1.672
- [x] compressible: p_gauge L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: p_gauge l2 observed order in [0.80, 1.60]: finest-triplet observed order 1.366 (monotonic_not_asymptotic vs formal 1); reduction factors 2.343 2.087
- [x] compressible: EOS consistency at 8x8: max |rho - EOS(p)|/rho = 0.00e+00
- [x] compressible: density error = pressure error / (R T0) at 8x8: rho L2 4.3508e-03, p L2 / RT 4.3508e-03
- [x] compressible: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 4.39e-09
- [x] compressible: global mass imbalance <= 1e-14 at 8x8: 4.73e-19
- [x] compressible: EOS consistency at 16x16: max |rho - EOS(p)|/rho = 0.00e+00
- [x] compressible: density error = pressure error / (R T0) at 16x16: rho L2 2.4766e-03, p L2 / RT 2.4766e-03
- [x] compressible: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 4.28e-08
- [x] compressible: global mass imbalance <= 1e-14 at 16x16: 1.02e-19
- [x] compressible: EOS consistency at 32x32: max |rho - EOS(p)|/rho = 0.00e+00
- [x] compressible: density error = pressure error / (R T0) at 32x32: rho L2 1.4809e-03, p L2 / RT 1.4809e-03
- [x] compressible: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 3.81e-07
- [x] compressible: global mass imbalance <= 1e-14 at 32x32: 2.15e-20
