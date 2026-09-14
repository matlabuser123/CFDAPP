### Validation: turbulent_channel

Turbulent channel Re_tau = 180: k-epsilon, k-omega, SST on coarse (48x12), medium (64x16), fine (96x24)

Reference: Kim, Moin & Moser (1987) J. Fluid Mech. 177; Moser, Kim & Mansour (1999) Phys. Fluids 11: Re_tau 180 summary statistics (Re_tau 180, Re_bulk 5600, U_c+ 18.2, U_b+ 15.6, Cf 8.2e-3) and the log law u+ = ln(y+)/0.41 + 5.0 (validation/data/turbulence/channel_flow/)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| turbulent_channel_re_tau_180_kEpsilon/coarse/upwind | 180 | coarse | upwind | Converged | 413 | 8544 / 40693 | 3.2e-10 | 3.0 | yes |
| turbulent_channel_re_tau_180_kEpsilon/medium/upwind | 180 | medium | upwind | Converged | 448 | 9501 / 53357 | 2.15e-08 | 6.1 | yes |
| turbulent_channel_re_tau_180_kEpsilon/fine/upwind | 180 | fine | upwind | Converged | 489 | 10754 / 83700 | 3.67e-08 | 17.9 | yes |
| turbulent_channel_re_tau_180_kOmega/coarse/upwind | 180 | coarse | upwind | Converged | 739 | 13449 / 59366 | 4.44e-09 | 5.6 | yes |
| turbulent_channel_re_tau_180_kOmega/medium/upwind | 180 | medium | upwind | Converged | 808 | 14092 / 73256 | 4.86e-09 | 10.3 | yes |
| turbulent_channel_re_tau_180_kOmega/fine/upwind | 180 | fine | upwind | Converged | 882 | 14676 / 107005 | 2.87e-08 | 27.4 | yes |
| turbulent_channel_re_tau_180_SST/coarse/upwind | 180 | coarse | upwind | Converged | 1223 | 20466 / 121884 | 9.51e-09 | 9.4 | yes |
| turbulent_channel_re_tau_180_SST/medium/upwind | 180 | medium | upwind | Converged | 1286 | 21006 / 123792 | 2.92e-09 | 16.6 | yes |
| turbulent_channel_re_tau_180_SST/fine/upwind | 180 | fine | upwind | Converged | 1360 | 23449 / 184993 | 9.03e-09 | 45.4 | yes |

**u_plus_log_region**

| run | L1 | L2 | Linf |
|---|---|---|---|
| turbulent_channel_re_tau_180_kEpsilon/coarse/upwind | 1.5163e+01 | 1.5451e+01 | 1.8277e+01 |
| turbulent_channel_re_tau_180_kEpsilon/medium/upwind | 1.1598e+01 | 1.1985e+01 | 1.4613e+01 |
| turbulent_channel_re_tau_180_kEpsilon/fine/upwind | 8.0755e+00 | 8.3685e+00 | 1.0096e+01 |
| turbulent_channel_re_tau_180_kOmega/coarse/upwind | 1.6213e+01 | 1.6400e+01 | 1.8751e+01 |
| turbulent_channel_re_tau_180_kOmega/medium/upwind | 1.3241e+01 | 1.3534e+01 | 1.5992e+01 |
| turbulent_channel_re_tau_180_kOmega/fine/upwind | 1.0272e+01 | 1.0479e+01 | 1.2096e+01 |
| turbulent_channel_re_tau_180_SST/coarse/upwind | 1.3043e+01 | 1.3070e+01 | 1.3713e+01 |
| turbulent_channel_re_tau_180_SST/medium/upwind | 8.5884e+00 | 8.6236e+00 | 9.2425e+00 |
| turbulent_channel_re_tau_180_SST/fine/upwind | 3.1238e+00 | 3.1650e+00 | 3.7925e+00 |

**u_plus_all_regions**

| run | L1 | L2 | Linf |
|---|---|---|---|
| turbulent_channel_re_tau_180_kEpsilon/coarse/upwind | 1.1691e+01 | 1.2956e+01 | 1.8277e+01 |
| turbulent_channel_re_tau_180_kEpsilon/medium/upwind | 9.5033e+00 | 1.0531e+01 | 1.4613e+01 |
| turbulent_channel_re_tau_180_kEpsilon/fine/upwind | 6.5166e+00 | 7.3222e+00 | 1.0096e+01 |
| turbulent_channel_re_tau_180_kOmega/coarse/upwind | 1.2561e+01 | 1.3811e+01 | 1.8751e+01 |
| turbulent_channel_re_tau_180_kOmega/medium/upwind | 1.0842e+01 | 1.1911e+01 | 1.5992e+01 |
| turbulent_channel_re_tau_180_kOmega/fine/upwind | 8.3825e+00 | 9.2207e+00 | 1.2096e+01 |
| turbulent_channel_re_tau_180_SST/coarse/upwind | 1.0486e+01 | 1.1189e+01 | 1.3713e+01 |
| turbulent_channel_re_tau_180_SST/medium/upwind | 7.3594e+00 | 7.7278e+00 | 9.2425e+00 |
| turbulent_channel_re_tau_180_SST/fine/upwind | 2.9408e+00 | 3.0273e+00 | 3.7925e+00 |

**u_plus_log_region_momentum_balance_u_tau**

| run | L1 | L2 | Linf |
|---|---|---|---|
| turbulent_channel_re_tau_180_kEpsilon/coarse/upwind | 1.1346e+01 | 1.1349e+01 | 1.1736e+01 |
| turbulent_channel_re_tau_180_kEpsilon/medium/upwind | 1.0843e+01 | 1.0846e+01 | 1.1242e+01 |
| turbulent_channel_re_tau_180_kEpsilon/fine/upwind | 1.0178e+01 | 1.0181e+01 | 1.0557e+01 |
| turbulent_channel_re_tau_180_kOmega/coarse/upwind | 1.0897e+01 | 1.0902e+01 | 1.1420e+01 |
| turbulent_channel_re_tau_180_kOmega/medium/upwind | 9.9874e+00 | 9.9934e+00 | 1.0678e+01 |
| turbulent_channel_re_tau_180_kOmega/fine/upwind | 8.9845e+00 | 8.9943e+00 | 9.9082e+00 |
| turbulent_channel_re_tau_180_SST/coarse/upwind | 7.5847e+00 | 7.5924e+00 | 8.0902e+00 |
| turbulent_channel_re_tau_180_SST/medium/upwind | 4.9669e+00 | 4.9900e+00 | 5.8698e+00 |
| turbulent_channel_re_tau_180_SST/fine/upwind | 1.5202e+00 | 1.5990e+00 | 2.3696e+00 |

- [x] turbulent_channel_re_tau_180_kEpsilon/coarse/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_kEpsilon/coarse/upwind: re_tau: achieved Re_tau 111.43 vs 180: relative error 0.3810 (bound 0.45)
- [x] turbulent_channel_re_tau_180_kEpsilon/coarse/upwind: log_law: u+ vs log law, y+ > 30: L2 15.451 over 4 samples (bound 18.0)
- [x] turbulent_channel_re_tau_180_kEpsilon/coarse/upwind: developed: profile change 0.8 L -> 0.9 L 0.0106 (bound 0.05)
- [x] turbulent_channel_re_tau_180_kEpsilon/medium/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_kEpsilon/medium/upwind: re_tau: achieved Re_tau 121.02 vs 180: relative error 0.3277 (bound 0.40)
- [x] turbulent_channel_re_tau_180_kEpsilon/medium/upwind: log_law: u+ vs log law, y+ > 30: L2 11.985 over 6 samples (bound 15.0)
- [x] turbulent_channel_re_tau_180_kEpsilon/medium/upwind: developed: profile change 0.8 L -> 0.9 L 0.0113 (bound 0.05)
- [x] turbulent_channel_re_tau_180_kEpsilon/fine/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_kEpsilon/fine/upwind: re_tau: achieved Re_tau 136.41 vs 180: relative error 0.2422 (bound 0.32)
- [x] turbulent_channel_re_tau_180_kEpsilon/fine/upwind: log_law: u+ vs log law, y+ > 30: L2 8.368 over 9 samples (bound 12.0)
- [x] turbulent_channel_re_tau_180_kEpsilon/fine/upwind: developed: profile change 0.8 L -> 0.9 L 0.0117 (bound 0.05)
- [x] turbulent_channel_re_tau_180_kOmega/coarse/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_kOmega/coarse/upwind: re_tau: achieved Re_tau 108.15 vs 180: relative error 0.3992 (bound 0.45)
- [x] turbulent_channel_re_tau_180_kOmega/coarse/upwind: log_law: u+ vs log law, y+ > 30: L2 16.400 over 4 samples (bound 18.0)
- [x] turbulent_channel_re_tau_180_kOmega/coarse/upwind: developed: profile change 0.8 L -> 0.9 L 0.0038 (bound 0.05)
- [x] turbulent_channel_re_tau_180_kOmega/medium/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_kOmega/medium/upwind: re_tau: achieved Re_tau 115.12 vs 180: relative error 0.3605 (bound 0.40)
- [x] turbulent_channel_re_tau_180_kOmega/medium/upwind: log_law: u+ vs log law, y+ > 30: L2 13.534 over 6 samples (bound 15.0)
- [x] turbulent_channel_re_tau_180_kOmega/medium/upwind: developed: profile change 0.8 L -> 0.9 L 0.0052 (bound 0.05)
- [x] turbulent_channel_re_tau_180_kOmega/fine/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_kOmega/fine/upwind: re_tau: achieved Re_tau 126.41 vs 180: relative error 0.2977 (bound 0.32)
- [x] turbulent_channel_re_tau_180_kOmega/fine/upwind: log_law: u+ vs log law, y+ > 30: L2 10.479 over 9 samples (bound 12.0)
- [x] turbulent_channel_re_tau_180_kOmega/fine/upwind: developed: profile change 0.8 L -> 0.9 L 0.0073 (bound 0.05)
- [x] turbulent_channel_re_tau_180_SST/coarse/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_SST/coarse/upwind: re_tau: achieved Re_tau 116.27 vs 180: relative error 0.3541 (bound 0.45)
- [x] turbulent_channel_re_tau_180_SST/coarse/upwind: log_law: u+ vs log law, y+ > 30: L2 13.070 over 4 samples (bound 18.0)
- [x] turbulent_channel_re_tau_180_SST/coarse/upwind: developed: profile change 0.8 L -> 0.9 L 0.0107 (bound 0.05)
- [x] turbulent_channel_re_tau_180_SST/medium/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_SST/medium/upwind: re_tau: achieved Re_tau 131.71 vs 180: relative error 0.2683 (bound 0.40)
- [x] turbulent_channel_re_tau_180_SST/medium/upwind: log_law: u+ vs log law, y+ > 30: L2 8.624 over 6 samples (bound 15.0)
- [x] turbulent_channel_re_tau_180_SST/medium/upwind: developed: profile change 0.8 L -> 0.9 L 0.0091 (bound 0.05)
- [x] turbulent_channel_re_tau_180_SST/fine/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_SST/fine/upwind: re_tau: achieved Re_tau 160.66 vs 180: relative error 0.1074 (bound 0.32)
- [x] turbulent_channel_re_tau_180_SST/fine/upwind: log_law: u+ vs log law, y+ > 30: L2 3.165 over 10 samples (bound 12.0)
- [x] turbulent_channel_re_tau_180_SST/fine/upwind: developed: profile change 0.8 L -> 0.9 L 0.0073 (bound 0.05)
- [x] turbulent_channel_re_tau_180_kEpsilon_re_tau_trend: Re_tau error 0.380964 -> 0.327683 -> 0.242171
- [x] turbulent_channel_re_tau_180_kOmega_re_tau_trend: Re_tau error 0.399170 -> 0.360462 -> 0.297705
- [x] turbulent_channel_re_tau_180_SST_re_tau_trend: Re_tau error 0.354076 -> 0.268285 -> 0.107432

Limitations:

- No velocity wall function: on these grids the first cell is outside the viscous sublayer and the secant wall shear under-estimates tau_w -- Re_tau, Cf and u+ carry that bias; the momentum-balance values bound it from the other side.
- Inlet/outlet channel instead of a periodic one: at 0.8-0.9 L the profile shape has stopped changing (< 5 %) but the flow is not momentum-developed -- the momentum-balance wall shear (-dp/dx delta) includes the still-accelerating core and over-estimates tau_w (Re_tau 204-448); secant and balance values bracket the true wall shear.
- The reference is a set of DNS summary statistics plus the log law, not a point-by-point DNS profile (see validation/data/turbulence/channel_flow/README.md).
- Runtimes are wall-clock and not deterministic.

Overall: PASSED
