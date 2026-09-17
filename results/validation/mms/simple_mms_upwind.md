### MMS: simple_mms_upwind

Full SIMPLE MMS from rest (U = 0, p = 0), Cartesian meshes

Manufactured solution: mms::velocity: U = (psi_y, -psi_x), psi = e^{0.5x} sin(pi x) e^{-0.4y} sin(pi y) / pi; mms::pressure: p = cos(pi x) cos(pi y) + x y / 2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8 | 64 | 0.125 | Converged | 960 | 3.2e-19 | 0.5 |
| 16x16 | 256 | 0.0625 | Converged | 1665 | 7.95e-20 | 5.1 |
| 32x32 | 1024 | 0.03125 | Converged | 3084 | 1.98e-20 | 35.8 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.8789e-02 | 3.7101e-02 | 8.2682e-02 |
| 16x16 | 1.6199e-02 | 2.1373e-02 | 4.8443e-02 |
| 32x32 | 8.7085e-03 | 1.1531e-02 | 2.6715e-02 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.8675e-02 | 3.7368e-02 | 8.2961e-02 |
| 16x16 | 1.6484e-02 | 2.1674e-02 | 5.3589e-02 |
| 32x32 | 8.8017e-03 | 1.1690e-02 | 2.8758e-02 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.6588e-02 | 5.2658e-02 | 8.5532e-02 |
| 16x16 | 2.6279e-02 | 3.0440e-02 | 5.3616e-02 |
| 32x32 | 1.4033e-02 | 1.6420e-02 | 2.8815e-02 |

**u_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 4.0334e-02 | 4.6128e-02 | 8.2682e-02 |
| 16x16 | 1.9960e-02 | 2.4168e-02 | 4.8443e-02 |
| 32x32 | 9.7657e-03 | 1.2282e-02 | 2.6715e-02 |

**v_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 3.8994e-02 | 4.5141e-02 | 8.2961e-02 |
| 16x16 | 2.0283e-02 | 2.4459e-02 | 5.3589e-02 |
| 32x32 | 9.8694e-03 | 1.2449e-02 | 2.8758e-02 |

**u_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.3946e-02 | 2.0261e-02 | 5.0865e-02 |
| 16x16 | 3.9134e-03 | 6.4062e-03 | 1.8192e-02 |
| 32x32 | 1.0351e-03 | 1.7627e-03 | 5.2788e-03 |

**v_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.5407e-02 | 2.3914e-02 | 6.5925e-02 |
| 16x16 | 4.0721e-03 | 7.0783e-03 | 2.1838e-02 |
| 32x32 | 1.0521e-03 | 1.9190e-03 | 6.2179e-03 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.7895e-02 | 7.5189e-02 | 2.5742e-01 |
| 16x16 | 2.5898e-02 | 3.2503e-02 | 9.4096e-02 |
| 32x32 | 1.2542e-02 | 1.5683e-02 | 4.7016e-02 |

**p_interior**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 5.5013e-02 | 6.9758e-02 | 1.6478e-01 |
| 16x16 | 2.4987e-02 | 3.1315e-02 | 7.3512e-02 |
| 32x32 | 1.2349e-02 | 1.5450e-02 | 3.6739e-02 |

**p_boundary_ring**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 6.1602e-02 | 8.1642e-02 | 2.5742e-01 |
| 16x16 | 2.8874e-02 | 3.6114e-02 | 9.4096e-02 |
| 32x32 | 1.3939e-02 | 1.7282e-02 | 4.7016e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 1.2071e-10 | 4.6879e-10 | 3.7030e-09 |
| 16x16 | 3.0289e-10 | 2.4283e-09 | 3.8771e-08 |
| 32x32 | 5.0913e-10 | 8.1517e-09 | 2.6066e-07 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8 | 2.9532e-02 | 4.0412e-02 | 9.6017e-02 |
| 16x16 | 1.6598e-02 | 2.2320e-02 | 5.6584e-02 |
| 32x32 | 8.8166e-03 | 1.1810e-02 | 2.9631e-02 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u | l2 | 1 | 1.736, 1.854 | 0.676 | monotonic_not_asymptotic |
| u | l1 | 1 | 1.777, 1.860 | 0.749 | monotonic_not_asymptotic |
| v | l2 | 1 | 1.724, 1.854 | 0.653 | monotonic_not_asymptotic |
| v | l1 | 1 | 1.740, 1.873 | 0.666 | monotonic_not_asymptotic |
| velocity | l2 | 1 | 1.730, 1.854 | 0.664 | monotonic_not_asymptotic |
| u | linf | 1 | 1.707, 1.813 | 0.656 | monotonic_not_asymptotic |
| p | l2 | 1 | 2.313, 2.072 | 1.344 | monotonic_not_asymptotic |
| p | l1 | 1 | 2.236, 2.065 | 1.260 | monotonic_not_asymptotic |
| p | linf | 1 | 2.736, 2.001 | 1.795 | monotonic_not_asymptotic |
| p_interior | l2 | 1 | 2.228, 2.027 | 1.277 | monotonic_not_asymptotic |
| p_boundary_ring | l2 | 1 | 2.261, 2.090 | 1.274 | monotonic_not_asymptotic |
| face_flux | l2 | 1 | 1.811, 1.890 | 0.784 | monotonic_not_asymptotic |

- [x] velocity: u L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: u l2 finest-pairwise observed order in [0.55, 2.00]: finest-PAIRWISE observed order 0.890 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.736 1.854
- [x] velocity: u l1 finest-pairwise observed order in [0.55, 2.00]: finest-PAIRWISE observed order 0.895 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.777 1.860
- [x] velocity: v L1/L2/Linf decrease on every refinement: monotone
- [x] velocity: v l2 finest-pairwise observed order in [0.55, 2.00]: finest-PAIRWISE observed order 0.891 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.724 1.854
- [x] velocity: v l1 finest-pairwise observed order in [0.55, 2.00]: finest-PAIRWISE observed order 0.905 (vs formal 1; the triplet estimator is not used here -- see MMSCases.hpp addPairwiseOrderGate); reduction factors 1.740 1.873
- [x] velocity: u L2 finest reduction factor >= 1.6: E_coarse/E_fine = 1.854
- [x] pressure: p L1/L2/Linf decrease on every refinement: monotone
- [x] pressure: p l2 observed order in [0.80, 1.60]: finest-triplet observed order 1.344 (monotonic_not_asymptotic vs formal 1); reduction factors 2.313 2.072
- [x] pressure: p l1 observed order in [0.80, 1.60]: finest-triplet observed order 1.260 (monotonic_not_asymptotic vs formal 1); reduction factors 2.236 2.065
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 8x8: Linf 3.70e-09, L2 4.69e-10
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 16x16: Linf 3.88e-08, L2 2.43e-09
- [x] continuity: discrete continuity satisfied (Linf per volume <= 1e-5) at 32x32: Linf 2.61e-07, L2 8.15e-09
- [x] continuity: face_flux L1/L2/Linf decrease on every refinement: monotone
- [x] continuity: face_flux l2 observed order in [0.55, 4.00]: finest-triplet observed order 0.784 (monotonic_not_asymptotic vs formal 1); reduction factors 1.811 1.890
- [x] mass: global mass imbalance <= 1e-14 at 8x8: 3.20e-19
- [x] mass: global mass imbalance <= 1e-14 at 16x16: 7.95e-20
- [x] mass: global mass imbalance <= 1e-14 at 32x32: 1.98e-20
