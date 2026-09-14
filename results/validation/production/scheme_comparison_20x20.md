### Validation: scheme_comparison_20x20

Convection-scheme accuracy / cost, lid-driven cavity Re = 100, 20x20

Reference: Ghia, Ghia & Shin (1982), J. Comput. Phys. 48, 387-411, Tables I/II (Re = 100 and 1000 columns; validation/ghia/)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| cavity_re100/20x20/upwind | 100 | 20x20 | upwind | Converged | 3036 | 34136 / 150550 | 0 | 220.6 | yes |
| cavity_re100/20x20/central | 100 | 20x20 | central | Converged | 2954 | 34253 / 147733 | 0 | 225.2 | yes |
| cavity_re100/20x20/linear_upwind | 100 | 20x20 | linear_upwind | Converged | 3091 | 36061 / 151816 | 0 | 245.3 | yes |
| cavity_re100/20x20/quick | 100 | 20x20 | quick | Converged | 3083 | 36967 / 151537 | 0 | 229.0 | yes |

**u_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/upwind | 1.6212e-02 | 2.1050e-02 | 3.7223e-02 |
| cavity_re100/20x20/central | 5.1650e-03 | 6.2556e-03 | 1.0712e-02 |
| cavity_re100/20x20/linear_upwind | 4.6777e-03 | 5.5470e-03 | 1.0372e-02 |
| cavity_re100/20x20/quick | 4.8012e-03 | 5.6901e-03 | 1.0462e-02 |

**v_centerline**

| run | L1 | L2 | Linf |
|---|---|---|---|
| cavity_re100/20x20/upwind | 1.0872e-02 | 1.4460e-02 | 4.0414e-02 |
| cavity_re100/20x20/central | 4.1999e-03 | 5.6347e-03 | 1.4095e-02 |
| cavity_re100/20x20/linear_upwind | 3.1654e-03 | 4.5023e-03 | 1.2378e-02 |
| cavity_re100/20x20/quick | 3.4504e-03 | 4.8236e-03 | 1.2624e-02 |

- [x] cavity_re100/20x20/upwind: solve_accepted: Converged after 3036 iterations
- [x] cavity_re100/20x20/upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/upwind: bounded: max |U| 0.818643 vs lid speed 1
- [x] cavity_re100/20x20/central: solve_accepted: Converged after 2954 iterations
- [x] cavity_re100/20x20/central: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/linear_upwind: solve_accepted: Converged after 3091 iterations
- [x] cavity_re100/20x20/linear_upwind: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/quick: solve_accepted: Converged after 3083 iterations
- [x] cavity_re100/20x20/quick: wall_normal_flux: max |mass flux| through the walls 0 (bound 1e-6)
- [x] cavity_re100/20x20/quick: bounded: max |U| 0.839325 vs lid speed 1
- [x] re100_20x20_central_below_upwind_u_centerline: 0.006256 < upwind 0.021050
- [x] re100_20x20_linear_upwind_below_upwind_u_centerline: 0.005547 < upwind 0.021050
- [x] re100_20x20_quick_below_upwind_u_centerline: 0.005690 < upwind 0.021050
- [x] re100_20x20_central_below_upwind_v_centerline: 0.005635 < upwind 0.014460
- [x] re100_20x20_linear_upwind_below_upwind_v_centerline: 0.004502 < upwind 0.014460
- [x] re100_20x20_quick_below_upwind_v_centerline: 0.004824 < upwind 0.014460

Limitations:

- Runtimes in the default suite are measured in the Debug build under a parallel ctest load -- indicative only; the fair comparison is scheme_comparison.json (Release, sequential).

Overall: PASSED
