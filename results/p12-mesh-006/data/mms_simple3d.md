### MMS: p12_mesh_006_simple_3d

P12-MESH-006: production SIMPLE (u, v, w, p; Rhie-Chow) on the 3D Cartesian hexahedral mesh, closed unit cube, from rest

Manufactured solution: U = curl(0, -F, G)/pi, G = sin(pi x) sin(pi y) e^{z/2}, F = sin(pi x) sin(pi z) e^{-y/2}; p = cos(pi x) cos(pi y) cos(pi z) + xyz/2 (compared modulo gauge)

| grid | cells | h | status | iterations | mass imbalance | runtime (s) |
|---|---|---|---|---|---|---|
| 8x8x8 | 512 | 0.125 | Converged | 43 | 2.12e-33 | 0.2 |
| 16x16x16 | 4096 | 0.0625 | Converged | 113 | 2.45e-32 | 6.3 |
| 32x32x32 | 32768 | 0.03125 | Converged | 368 | 3.11e-33 | 236.2 |

**u**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 7.5583e-03 | 1.2161e-02 | 6.4639e-02 |
| 16x16x16 | 1.7968e-03 | 2.3567e-03 | 1.3383e-02 |
| 32x32x32 | 4.9862e-04 | 6.1405e-04 | 2.0829e-03 |

**v**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 5.8577e-03 | 8.5311e-03 | 2.9688e-02 |
| 16x16x16 | 1.3638e-03 | 1.8296e-03 | 6.8961e-03 |
| 32x32x32 | 3.8807e-04 | 5.1581e-04 | 1.8574e-03 |

**w**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 3.3009e-03 | 4.4792e-03 | 1.8192e-02 |
| 16x16x16 | 8.5941e-04 | 1.1222e-03 | 3.8778e-03 |
| 32x32x32 | 2.4491e-04 | 3.2086e-04 | 1.0645e-03 |

**velocity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 1.2351e-02 | 1.5515e-02 | 6.4713e-02 |
| 16x16x16 | 2.8281e-03 | 3.1876e-03 | 1.3440e-02 |
| 32x32x32 | 7.9977e-04 | 8.6375e-04 | 2.0832e-03 |

**p**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 2.1156e-02 | 2.6125e-02 | 9.4959e-02 |
| 16x16x16 | 5.3923e-03 | 7.6200e-03 | 3.3713e-02 |
| 32x32x32 | 1.0280e-03 | 1.7406e-03 | 1.1503e-02 |

**continuity**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 3.0153e-11 | 1.4207e-10 | 3.1473e-09 |
| 16x16x16 | 9.9737e-11 | 2.8785e-09 | 1.8418e-07 |
| 32x32x32 | 2.8670e-10 | 2.5491e-08 | 4.6143e-06 |

**face_flux**

| grid | L1 | L2 | Linf |
|---|---|---|---|
| 8x8x8 | 4.9455e-03 | 7.4761e-03 | 3.1074e-02 |
| 16x16x16 | 6.7204e-04 | 1.0030e-03 | 5.0934e-03 |
| 32x32x32 | 1.2368e-04 | 1.9328e-04 | 1.3873e-03 |

- [x] G4.1 u L1 decreasing: 8 -> 16 -> 32
- [x] G4.1 u L2 decreasing: 8 -> 16 -> 32
- [x] G4.1 u Linf decreasing: 8 -> 16 -> 32
- [x] G4.1 u L1 order: finest-pair p 1.849 in [1.6, 2.4]
- [x] G4.1 u L2 order: finest-pair p 1.940 in [1.6, 2.4]
- [x] G4.1 v L1 decreasing: 8 -> 16 -> 32
- [x] G4.1 v L2 decreasing: 8 -> 16 -> 32
- [x] G4.1 v Linf decreasing: 8 -> 16 -> 32
- [x] G4.1 v L1 order: finest-pair p 1.813 in [1.6, 2.4]
- [x] G4.1 v L2 order: finest-pair p 1.827 in [1.6, 2.4]
- [x] G4.1 w L1 decreasing: 8 -> 16 -> 32
- [x] G4.1 w L2 decreasing: 8 -> 16 -> 32
- [x] G4.1 w Linf decreasing: 8 -> 16 -> 32
- [x] G4.1 w L1 order: finest-pair p 1.811 in [1.6, 2.4]
- [x] G4.1 w L2 order: finest-pair p 1.806 in [1.6, 2.4]
- [x] G4.2 p L1 decreasing: 8 -> 16 -> 32
- [x] G4.2 p L2 decreasing: 8 -> 16 -> 32
- [x] G4.2 p Linf decreasing: 8 -> 16 -> 32
- [x] G4.2 p L1 order: finest-pair p 2.391 in [1.5, 2.4]
- [x] G4.2 p L2 order: finest-pair p 2.130 in [1.5, 2.4]
- [x] G4.4 solve at 8x8x8: Converged, finite, rhie_chow, 43 iterations from rest
- [x] G4.3 continuity Linf per volume <= 1e-5 at 8x8x8: 3.147e-09
- [x] G4.3 global mass imbalance <= 1e-14 at 8x8x8: 2.119e-33
- [x] G4.4 solve at 16x16x16: Converged, finite, rhie_chow, 113 iterations from rest
- [x] G4.3 continuity Linf per volume <= 1e-5 at 16x16x16: 1.842e-07
- [x] G4.3 global mass imbalance <= 1e-14 at 16x16x16: 2.446e-32
- [x] G4.4 solve at 32x32x32: Converged, finite, rhie_chow, 368 iterations from rest
- [x] G4.3 continuity Linf per volume <= 1e-5 at 32x32x32: 4.614e-06
- [x] G4.3 global mass imbalance <= 1e-14 at 32x32x32: 3.106e-33
