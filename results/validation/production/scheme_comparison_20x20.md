### Validation: scheme_comparison_20x20

Convection-scheme accuracy / cost, lid-driven cavity Re = 100, 20x20

Reference: Ghia, Ghia & Shin (1982), J. Comput. Phys. 48, 387-411, Tables I/II (Re = 100 and 1000 columns; validation/ghia/)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| cavity_re100/20x20/upwind | 100 | 20x20 | upwind | Converged | 3312 | 40262 / 175647 | 0 | 23.2 | yes |
| cavity_re100/20x20/central | 100 | 20x20 | central | Converged | 3208 | 41025 / 169072 | 0 | 22.7 | yes |
| cavity_re100/20x20/linear_upwind | 100 | 20x20 | linear_upwind | Converged | 3359 | 42633 / 174236 | 0 | 25.3 | yes |
| cavity_re100/20x20/quick | 100 | 20x20 | quick | Converged | 3265 | 42117 / 171517 | 0 | 24.7 | yes |

**u_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/upwind | 1.2857e-02 | 1.7179e-02 | 3.5037e-02 |
| cavity_re100/20x20/central | 3.7804e-03 | 4.4535e-03 | 8.7776e-03 |
| cavity_re100/20x20/linear_upwind | 3.6627e-03 | 4.5003e-03 | 7.8447e-03 |
| cavity_re100/20x20/quick | 3.6266e-03 | 4.2280e-03 | 7.9234e-03 |

**v_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/upwind | 9.2173e-03 | 1.3039e-02 | 3.8076e-02 |
| cavity_re100/20x20/central | 4.1432e-03 | 4.9836e-03 | 1.2701e-02 |
| cavity_re100/20x20/linear_upwind | 3.9205e-03 | 4.6462e-03 | 1.1130e-02 |
| cavity_re100/20x20/quick | 3.8474e-03 | 4.5157e-03 | 1.1119e-02 |

- [x] cavity_re100/20x20/upwind: solve_accepted: Converged after 3312 iterations
- [x] cavity_re100/20x20/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/upwind: bounded: max |U| 0.828866 vs lid speed 1
- [x] cavity_re100/20x20/central: solve_accepted: Converged after 3208 iterations
- [x] cavity_re100/20x20/central: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/linear_upwind: solve_accepted: Converged after 3359 iterations
- [x] cavity_re100/20x20/linear_upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/quick: solve_accepted: Converged after 3265 iterations
- [x] cavity_re100/20x20/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/quick: bounded: max |U| 0.847130 vs lid speed 1
- [x] re100_20x20_central_below_upwind_u_centerline: 0.004453 < upwind 0.017179
- [x] re100_20x20_linear_upwind_below_upwind_u_centerline: 0.004500 < upwind 0.017179
- [x] re100_20x20_quick_below_upwind_u_centerline: 0.004228 < upwind 0.017179
- [x] re100_20x20_central_below_upwind_v_centerline: 0.004984 < upwind 0.013039
- [x] re100_20x20_linear_upwind_below_upwind_v_centerline: 0.004646 < upwind 0.013039
- [x] re100_20x20_quick_below_upwind_v_centerline: 0.004516 < upwind 0.013039

Limitations:

- Runtimes in the default suite are measured in the Debug build under a parallel ctest load -- indicative only; the fair comparison is scheme_comparison.json (Release, sequential).

Overall: PASSED
