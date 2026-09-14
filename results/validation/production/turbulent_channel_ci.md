### Validation: turbulent_channel_re_tau_180_ci

Turbulent channel Re_tau = 180, SST, medium grid (default suite)

Reference: Kim, Moin & Moser (1987) J. Fluid Mech. 177; Moser, Kim & Mansour (1999) Phys. Fluids 11: Re_tau 180 summary statistics (Re_tau 180, Re_bulk 5600, U_c+ 18.2, U_b+ 15.6, Cf 8.2e-3) and the log law u+ = ln(y+)/0.41 + 5.0 (validation/data/turbulence/channel_flow/)

| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass imbalance | runtime (s) | passed |
|---|---|---|---|---|---|---|---|---|---|
| turbulent_channel_re_tau_180_SST/medium/upwind | 180 | medium | upwind | Converged | 1286 | 21006 / 123792 | 2.92e-09 | 506.2 | yes |

**u_plus_log_region**

| run | L1 | L2 | Linf |
|---|---|---|---|
| turbulent_channel_re_tau_180_SST/medium/upwind | 8.5884e+00 | 8.6236e+00 | 9.2425e+00 |

**u_plus_all_regions**

| run | L1 | L2 | Linf |
|---|---|---|---|
| turbulent_channel_re_tau_180_SST/medium/upwind | 7.3594e+00 | 7.7278e+00 | 9.2425e+00 |

**u_plus_log_region_momentum_balance_u_tau**

| run | L1 | L2 | Linf |
|---|---|---|---|
| turbulent_channel_re_tau_180_SST/medium/upwind | 4.9669e+00 | 4.9900e+00 | 5.8698e+00 |

- [x] turbulent_channel_re_tau_180_SST/medium/upwind: solve_accepted: Converged
- [x] turbulent_channel_re_tau_180_SST/medium/upwind: re_tau: achieved Re_tau 131.71 vs 180: relative error 0.2683 (bound 0.40)
- [x] turbulent_channel_re_tau_180_SST/medium/upwind: log_law: u+ vs log law, y+ > 30: L2 8.624 over 6 samples (bound 15.0)
- [x] turbulent_channel_re_tau_180_SST/medium/upwind: developed: profile change 0.8 L -> 0.9 L 0.0091 (bound 0.05)

Limitations:

- No velocity wall function: on these grids the first cell is outside the viscous sublayer and the secant wall shear under-estimates tau_w -- Re_tau, Cf and u+ carry that bias; the momentum-balance values bound it from the other side.
- Inlet/outlet channel instead of a periodic one: at 0.8-0.9 L the profile shape has stopped changing (< 5 %) but the flow is not momentum-developed -- the momentum-balance wall shear (-dp/dx delta) includes the still-accelerating core and over-estimates tau_w (Re_tau 204-448); secant and balance values bracket the true wall shear.
- The reference is a set of DNS summary statistics plus the log law, not a point-by-point DNS profile (see validation/data/turbulence/channel_flow/README.md).
- Runtimes are wall-clock and not deterministic.

Overall: PASSED
