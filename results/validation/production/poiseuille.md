### Validation: poiseuille

Planar Poiseuille channel L = 8H, Re = 10: grids 64x8, 96x12, 144x18

Reference: Continuous planar Poiseuille solution u = 6U(y/H)(1-y/H), dp/dx = -12 mu U/H^2 (analytical); and the exact fully developed solution of the discretisation, dp/dx scaled by ny^2/(ny^2+2) (derived, PoiseuilleValidationUtils.hpp)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| poiseuille_re10/64x8/upwind | 10 | 64x8 | upwind | Converged | 1319 | 18247 / 101503 | 1.97e-10 | 160.1 | yes |
| poiseuille_re10/96x12/upwind | 10 | 96x12 | upwind | Converged | 985 | 14149 / 103166 | 2.49e-09 | 307.7 | yes |
| poiseuille_re10/144x18/upwind | 10 | 144x18 | upwind | Converged | 744 | 12018 / 105811 | 3.83e-09 | 603.6 | yes |

**u_profile**

| run | L1 | L2 | Linf |
|---|---|---|---|
| poiseuille_re10/64x8/upwind | 1.3849e-02 | 1.5182e-02 | 2.2015e-02 |
| poiseuille_re10/96x12/upwind | 6.2310e-03 | 6.9492e-03 | 1.0131e-02 |
| poiseuille_re10/144x18/upwind | 2.7866e-03 | 3.1294e-03 | 4.5728e-03 |

**u_profile_vs_discrete_exact**

| run | L1 | L2 | Linf |
|---|---|---|---|
| poiseuille_re10/64x8/upwind | 1.7475e-06 | 1.7528e-06 | 1.9090e-06 |
| poiseuille_re10/96x12/upwind | 3.8558e-08 | 3.8645e-08 | 4.2905e-08 |
| poiseuille_re10/144x18/upwind | 4.3892e-09 | 4.4315e-09 | 5.7390e-09 |

| quantity | norm | formal | reduction factors | observed p (per triplet) | status (finest) |
|---|---|---|---|---|---|
| u_profile | l1 | 2 | 2.223, 2.236 | 1.958 | asymptotic |
| u_profile | l2 | 2 | 2.185, 2.221 | 1.894 | asymptotic |
| u_profile | linf | 2 | 2.173, 2.216 | 1.874 | asymptotic |

### Grid convergence: poiseuille_production

Poiseuille Re = 10, grids 64x8 / 96x12 / 144x18

| grid | nx x ny | cells | h | runtime (s) | solver | accepted |
|---|---|---|---|---|---|---|
| 64x8 | 64 x 8 | 512 | 0.125 | 160.1 | Converged (1319 it) | yes |
| 96x12 | 96 x 12 | 1152 | 0.0833333 | 307.7 | Converged (985 it) | yes |
| 144x18 | 144 x 18 | 2592 | 0.0555556 | 603.6 | Converged (744 it) | yes |

**centerline_velocity** -- u(0.75 L, H/2); exact 1.5 U

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | 1.454547 | 1.5 (analytical) | -0.04545 | 0.0303 |
| medium | 1.479452 | 1.5 (analytical) | -0.02055 | 0.0137 |
| fine | 1.4907976 | 1.5 (analytical) | -0.009202 | 0.006135 |
| extrapolated | 1.5002906 | | 0.0002906 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.5 | 1.5 | 1.9391 | 2 | 1.5002906 | 0.01187 | 0.00796 | 0.01761 | 0.9756 | asymptotic | yes |

- monotonic convergence, observed order 1.93912 (formal 2); asymptotic ratio 0.975617 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00795973 <= threshold 0.01

**pressure_gradient** -- dp/dx, pair-averaged between 0.40 L and 0.75 L; exact -12 mu U / H^2

| grid | value | reference | error | relative error |
|---|---|---|---|---|
| coarse | -1.1629183 | -1.2 (analytical) | 0.03708 | 0.0309 |
| medium | -1.1835093 | -1.2 (analytical) | 0.01649 | 0.01374 |
| fine | -1.1926382 | -1.2 (analytical) | 0.007362 | 0.006135 |
| extrapolated | -1.1999089 | | 9.105e-05 | |

| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | status | grid independent |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.5 | 1.5 | 2.0061 | 2 | -1.1999089 | 0.009088 | 0.00762 | 0.01732 | 1.0025 | asymptotic | yes |

- monotonic convergence, observed order 2.0061 (formal 2); asymptotic ratio 1.00248 within 1 +/- 0.1
- grid independence: asymptotic and GCI21 0.00762042 <= threshold 0.01

- [x] poiseuille_re10/64x8/upwind: solve_accepted: Converged
- [x] poiseuille_re10/64x8/upwind: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] poiseuille_re10/64x8/upwind: mass_flow: max |Q - U H| over inlet, outlet and sections 3.73e-10 (bound 1e-6)
- [x] poiseuille_re10/64x8/upwind: profile_matches_discrete_exact: max |u - u_discrete| 1.91e-06 (bound 1e-4)
- [x] poiseuille_re10/64x8/upwind: pressure_gradient_matches_discrete_exact: |dp/dx - discrete exact| / |discrete exact| 0.000617 (bound 1e-3)
- [x] poiseuille_re10/96x12/upwind: solve_accepted: Converged
- [x] poiseuille_re10/96x12/upwind: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] poiseuille_re10/96x12/upwind: mass_flow: max |Q - U H| over inlet, outlet and sections 2.49e-09 (bound 1e-6)
- [x] poiseuille_re10/96x12/upwind: profile_matches_discrete_exact: max |u - u_discrete| 4.29e-08 (bound 1e-4)
- [x] poiseuille_re10/96x12/upwind: pressure_gradient_matches_discrete_exact: |dp/dx - discrete exact| / |discrete exact| 4.42e-05 (bound 1e-3)
- [x] poiseuille_re10/144x18/upwind: solve_accepted: Converged
- [x] poiseuille_re10/144x18/upwind: wall_normal_flux: max |wall mass flux| 0 (bound 1e-6)
- [x] poiseuille_re10/144x18/upwind: mass_flow: max |Q - U H| over inlet, outlet and sections 3.83e-09 (bound 1e-6)
- [x] poiseuille_re10/144x18/upwind: profile_matches_discrete_exact: max |u - u_discrete| 5.74e-09 (bound 1e-4)
- [x] poiseuille_re10/144x18/upwind: pressure_gradient_matches_discrete_exact: |dp/dx - discrete exact| / |discrete exact| 1.58e-07 (bound 1e-3)
- [x] centerline_velocity_asymptotic: monotonic convergence, observed order 1.93912 (formal 2); asymptotic ratio 0.975617 within 1 +/- 0.1
- [x] centerline_velocity_gci_bounds_true_error: fine relative error 0.006135 <= GCI21 0.007960
- [x] pressure_gradient_asymptotic: monotonic convergence, observed order 2.0061 (formal 2); asymptotic ratio 1.00248 within 1 +/- 0.1
- [x] pressure_gradient_gci_bounds_true_error: fine relative error 0.006135 <= GCI21 0.007620
- [x] u_profile_l2_asymptotic: monotonic convergence, observed order 1.894 (formal 2); asymptotic ratio 0.95793 within 1 +/- 0.1

Limitations:

- Collocated SIMPLE without Rhie-Chow interpolation: an odd-even pressure mode grows on the residual plateau (amplitude reported per run as pressure_odd_even_amplitude). The pair-averaged estimator is immune to it; the legacy two-column estimate is not (reported as pressure_gradient_two_station_legacy).
- Outer u/p gates 2e-5 / 5e-4 (the case's documented residual plateau); continuity and mass flow are converged to 1e-6.
- Runtimes are wall-clock and not deterministic.

Overall: PASSED
