# CI-PERF-001 — generated-file audit for the local parallel Debug run

Run: ctest --preset debug -j16 --timeout 7200, 1984/1984 passed, 1131 s.
CLAUDE.md 7: classify, capture the names, restore, do not commit.

## Files rewritten: 50

This is the SAME count of 50 the recorded SERIAL Debug regression produced
(results/gpu-pcorr-001/summary.md 8), so parallel execution did not change which
files the suite rewrites, nor add any.

## Magnitude of differences

  timing fields                       : expected, -j16 contention inflates per-test wall
  non-timing, |v| > 1e-6  (386 pairs) : max relative difference 1.815e-05
                                        at extrapolated_error (Richardson, amplifies round-off)
  non-timing, |v| > 1e-3  (232 pairs) : max relative difference 3.378e-06

All 1984 tests PASSED, including the validation tests that gate these very numbers
against their committed values -- so every numerical gate held at these differences.

## The 50 files

  results/validation/grid_convergence/natural_convection_ra1e3.json
  results/validation/grid_convergence/natural_convection_ra1e3.md
  results/validation/grid_convergence/poiseuille_flow.json
  results/validation/grid_convergence/poiseuille_flow.md
  results/validation/mms/compressible_simple_mms.json
  results/validation/mms/compressible_simple_mms.md
  results/validation/mms/distorted_mesh_mms.json
  results/validation/mms/distorted_mesh_mms.md
  results/validation/mms/distorted_mesh_momentum_mms.json
  results/validation/mms/distorted_mesh_momentum_mms.md
  results/validation/mms/distorted_mesh_simple_mms.json
  results/validation/mms/distorted_mesh_simple_mms.md
  results/validation/mms/momentum_mms.json
  results/validation/mms/momentum_mms.md
  results/validation/mms/momentum_mms_linear_upwind.json
  results/validation/mms/momentum_mms_linear_upwind.md
  results/validation/mms/momentum_mms_upwind.json
  results/validation/mms/momentum_mms_upwind.md
  results/validation/mms/scalar_advection_diffusion_mms.json
  results/validation/mms/scalar_advection_diffusion_mms.md
  results/validation/mms/simple_mms.json
  results/validation/mms/simple_mms.md
  results/validation/mms/simple_mms_upwind.json
  results/validation/mms/simple_mms_upwind.md
  results/validation/natural_convection/Ra1e3/10x10/validation.json
  results/validation/natural_convection/Ra1e3/10x10_det_a/validation.json
  results/validation/natural_convection/Ra1e3/10x10_det_b/validation.json
  results/validation/natural_convection/Ra1e3/10x10_refinement_pair/validation.json
  results/validation/natural_convection/Ra1e3/10x10_sign_check/validation.json
  results/validation/natural_convection/Ra1e3/10x10_variable_property_regression/validation.json
  results/validation/natural_convection/Ra1e3/10x10_zero_beta/validation.json
  results/validation/natural_convection/Ra1e3/15x15/validation.json
  results/validation/natural_convection/Ra1e3/20x20/validation.json
  results/validation/natural_convection/Ra1e3/20x20_variable_property_regression/validation.json
  results/validation/production/cavity_re100/20x20_quick/validation.json
  results/validation/production/channel_transpiration_graded_grid_convergence.json
  results/validation/production/curved_channel_multiblock_grid_convergence.json
  results/validation/production/poiseuille.json
  results/validation/production/poiseuille.md
  results/validation/production/poiseuille_distorted_grid_convergence.json
  results/validation/production/scheme_comparison_20x20.json
  results/validation/production/scheme_comparison_20x20.md
  results/validation/production/turbulent_channel_ci.json
  results/validation/production/turbulent_channel_ci.md
  results/validation/turbulence/channel_flow/k_epsilon/coarse/validation.json
  results/validation/turbulence/channel_flow/k_epsilon/medium/validation.json
  results/validation/turbulence/channel_flow/k_omega/coarse/validation.json
  results/validation/turbulence/channel_flow/k_omega/medium/validation.json
  results/validation/turbulence/channel_flow/sst/coarse/validation.json
  results/validation/turbulence/channel_flow/sst/medium/validation.json
