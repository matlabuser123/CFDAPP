### MMS: continuity_mms

Production face-flux interpolation (calculateMassFlux) and continuity evaluation (evaluateContinuity) of the exact divergence-free velocity

Manufactured solution: mms::velocity (streamfunction curl; div U = 0 analytically)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 16x16 | 256 | 0.0625 | Evaluated | 0 | 7.95e-20 | 0.0 |
| 32x32 | 1024 | 0.03125 | Evaluated | 0 | 1.98e-20 | 0.0 |
| 64x64 | 4096 | 0.015625 | Evaluated | 0 | 4.96e-21 | 0.0 |
| 128x128 | 16384 | 0.0078125 | Evaluated | 0 | 1.24e-21 | 0.0 |

**divergence**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 6.5125e-03 | 9.5315e-03 | 4.6666e-02 |
| 32x32 | 1.7926e-03 | 3.2694e-03 | 2.4064e-02 |
| 64x64 | 4.6929e-04 | 1.1326e-03 | 1.2144e-02 |
| 128x128 | 1.2002e-04 | 3.9578e-04 | 6.0905e-03 |

**divergence_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 3.9290e-03 | 4.4354e-03 | 7.6308e-03 |
| 32x32 | 9.9925e-04 | 1.1250e-03 | 2.0387e-03 |
| 64x64 | 2.5091e-04 | 2.8250e-04 | 5.2404e-04 |
| 128x128 | 6.2814e-05 | 7.0739e-05 | 1.3271e-04 |

**divergence_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.4952e-02 | 1.7982e-02 | 4.6666e-02 |
| 32x32 | 7.5506e-03 | 8.8931e-03 | 2.4064e-02 |
| 64x64 | 3.8004e-03 | 4.4308e-03 | 1.2144e-02 |
| 128x128 | 1.9079e-03 | 2.2126e-03 | 6.0905e-03 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 16x16 | 1.6235e-03 | 2.0444e-03 | 4.8220e-03 |
| 32x32 | 4.0079e-04 | 5.0588e-04 | 1.2266e-03 |
| 64x64 | 9.9583e-05 | 1.2581e-04 | 3.0838e-04 |
| 128x128 | 2.4820e-05 | 3.1367e-05 | 7.7291e-05 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| face_flux | l2 | 2 | 4.041, 4.021, 4.011 | 2.017, 2.009 | asymptotic |
| divergence_interior | l2 | 2 | 3.943, 3.982, 3.994 | 1.974, 1.992 | asymptotic |
| divergence | l1 | 2 | 3.633, 3.820, 3.910 | 1.835, 1.922 | asymptotic |
| divergence_boundary_ring | linf | 1 | 1.939, 1.982, 1.994 | 0.923, 0.978 | asymptotic |

- [x] continuity: exact face flux divergence-free per cell at 16x16: max |imbalance| 1.39e-17
- [x] continuity: global mass imbalance ~ 0 at 16x16: -7.95e-20 (zero normal wall velocity)
- [x] continuity: exact face flux divergence-free per cell at 32x32: max |imbalance| 6.94e-18
- [x] continuity: global mass imbalance ~ 0 at 32x32: -1.98e-20 (zero normal wall velocity)
- [x] continuity: exact face flux divergence-free per cell at 64x64: max |imbalance| 3.47e-18
- [x] continuity: global mass imbalance ~ 0 at 64x64: -4.96e-21 (zero normal wall velocity)
- [x] continuity: exact face flux divergence-free per cell at 128x128: max |imbalance| 1.73e-18
- [x] continuity: global mass imbalance ~ 0 at 128x128: -1.24e-21 (zero normal wall velocity)
- [x] continuity: face_flux l2 observed order in [1.80, 2.30]: finest-triplet observed order 2.009 (asymptotic vs formal 2); reduction factors 4.041 4.021 4.011
- [x] continuity: divergence_interior l2 observed order in [1.80, 2.30]: finest-triplet observed order 1.992 (asymptotic vs formal 2); reduction factors 3.943 3.982 3.994
- [x] continuity: divergence l1 observed order in [1.50, 2.30]: finest-triplet observed order 1.922 (asymptotic vs formal 2); reduction factors 3.633 3.820 3.910
- [x] continuity: divergence L1/L2/Linf decrease on every refinement: monotone
