### MMS: compressible_simple_mms

CompressibleSIMPLE MMS, isothermal ideal gas in a closed box, from rest; upwind convection (the only scheme of the compressible momentum assembly)

Manufactured solution: p = cos(pi x) cos(pi y) + x y / 2 (gauge); rho = (P_ref + p)/(R T0); rho U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 978 | 6.26e-19 | 0.7 |
| 16x16 | 256 | 0.0625 | Converged | 2424 | 1.69e-19 | 7.1 |
| 32x32 | 1024 | 0.03125 | Converged | 3507 | 4.45e-20 | 38.9 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.8708e-02 | 3.7098e-02 | 8.4015e-02 |
| 16x16 | 1.6150e-02 | 2.1337e-02 | 4.9367e-02 |
| 32x32 | 8.6638e-03 | 1.1496e-02 | 2.7121e-02 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.8248e-02 | 3.6936e-02 | 8.3807e-02 |
| 16x16 | 1.6191e-02 | 2.1450e-02 | 5.4381e-02 |
| 32x32 | 8.6495e-03 | 1.1568e-02 | 2.9140e-02 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.6238e-02 | 5.2350e-02 | 8.6399e-02 |
| 16x16 | 2.6068e-02 | 3.0254e-02 | 5.4488e-02 |
| 32x32 | 1.3910e-02 | 1.6309e-02 | 2.9237e-02 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 9.8045e-02 | 1.1530e-01 | 2.5065e-01 |
| 16x16 | 5.7460e-02 | 6.4832e-02 | 1.3120e-01 |
| 32x32 | 3.3900e-02 | 3.7056e-02 | 7.1255e-02 |

**p_gauge**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.6856e-02 | 7.4530e-02 | 2.6592e-01 |
| 16x16 | 2.5213e-02 | 3.1722e-02 | 9.5980e-02 |
| 32x32 | 1.2087e-02 | 1.5123e-02 | 4.5705e-02 |

**density**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.9022e-03 | 5.7650e-03 | 1.2532e-02 |
| 16x16 | 2.8730e-03 | 3.2416e-03 | 6.5600e-03 |
| 32x32 | 1.6950e-03 | 1.8528e-03 | 3.5627e-03 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.3484e-10 | 8.6245e-10 | 6.1346e-09 |
| 16x16 | 7.3755e-10 | 2.9543e-09 | 4.6023e-08 |
| 32x32 | 1.3053e-09 | 1.1723e-08 | 3.7293e-07 |

**mass_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.8018e-02 | 3.8346e-02 | 9.0774e-02 |
| 16x16 | 1.5479e-02 | 2.0932e-02 | 5.4275e-02 |
| 32x32 | 8.1098e-03 | 1.0962e-02 | 2.8021e-02 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 1 | 1.739, 1.856 | 0.680 | monotonic_not_asymptotic |
| v | l2 | 1 | 1.722, 1.854 | 0.648 | monotonic_not_asymptotic |
| p | l2 | 1 | 1.778, 1.750 | 0.862 | asymptotic |
| p_gauge | l2 | 1 | 2.349, 2.098 | 1.367 | monotonic_not_asymptotic |
| density | l2 | 1 | 1.778, 1.750 | 0.862 | asymptotic |
| mass_flux | l2 | 1 | 1.832, 1.909 | 0.805 | monotonic_not_asymptotic |

- [x] compressible: u L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: u l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.680 (monotonic_not_asymptotic vs formal 1); reduction factors 1.739 1.856
- [x] compressible: v L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: v l2 observed order in [0.55, 1.30]: finest-triplet observed order 0.648 (monotonic_not_asymptotic vs formal 1); reduction factors 1.722 1.854
- [x] compressible: p L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: p l2 observed order in [0.80, 1.60]: finest-triplet observed order 0.862 (asymptotic vs formal 1); reduction factors 1.778 1.750
- [x] compressible: p_gauge L1/L2/Linf decrease on every refinement: monotone
- [x] compressible: p_gauge l2 observed order in [0.80, 1.60]: finest-triplet observed order 1.367 (monotonic_not_asymptotic vs formal 1); reduction factors 2.349 2.098
- [x] compressible: EOS consistency at 8x8: max |rho - EOS(p)|/rho = 0.00e+00
- [x] compressible: density error = pressure error / (R T0) at 8x8: rho L2 5.7650e-03, p L2 / RT 5.7650e-03
- [x] compressible: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 6.13e-09
- [x] compressible: global mass imbalance <= 1e-14 at 8x8: 6.26e-19
- [x] compressible: EOS consistency at 16x16: max |rho - EOS(p)|/rho = 0.00e+00
- [x] compressible: density error = pressure error / (R T0) at 16x16: rho L2 3.2416e-03, p L2 / RT 3.2416e-03
- [x] compressible: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 4.60e-08
- [x] compressible: global mass imbalance <= 1e-14 at 16x16: 1.69e-19
- [x] compressible: EOS consistency at 32x32: max |rho - EOS(p)|/rho = 0.00e+00
- [x] compressible: density error = pressure error / (R T0) at 32x32: rho L2 1.8528e-03, p L2 / RT 1.8528e-03
- [x] compressible: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 3.73e-07
- [x] compressible: global mass imbalance <= 1e-14 at 32x32: 4.45e-20
