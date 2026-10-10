// P16b attempt 5 -- the two-velocity-scale momentum wall flux, F_U = -rho u_tau u*.
//
// WHY THIS EXISTS
// ---------------
// Attempts 2 and 4 both reduced the wall shear to a SINGLE scale, tau_w =
// rho u_tau^2, and both distorted u+(y+) in the buffer/lower-log region even
// when they got the integrated shear roughly right. The published omega-based
// automatic treatment keeps two distinct scales and multiplies them.
//
// THE SPECIFICATION IS THE TWO LIMITS, and they are proved numerically here
// rather than asserted in prose:
//   y+ -> 0    F_U -> -mu |U_P|/y_P        (resolved viscous wall)
//   y+ -> inf  F_U -> -tau_LaunderSpalding (the VALIDATED high-Re path)
//
// A DEGENERACY THAT SHAPES EVERY TEST BELOW: at exact equilibrium the
// turbulence scale equals the log friction velocity, so the two blends coincide
// and F_U collapses to rho u_tau^2 -- the earlier form. An equilibrium-k test
// therefore CANNOT distinguish this formulation from the one it replaces, and
// the discriminating tests here are deliberately built off equilibrium.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/turbulence/WallFunctions.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::turbulence::WallFunctionCoefficients;

namespace {

constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 1e-5;
const WallFunctionCoefficients kCoefficients{0.41, 9.793, 0.09};

cfd::turbulence::AutomaticMomentumWallState evaluate(Real u, Real k, Real y) {
  return cfd::turbulence::automaticMomentumWallState(u, k, y, kDensity, kViscosity, kCoefficients);
}

}  // namespace

// --- the two limits ----------------------------------------------------------

TEST(MomentumWallFlux, ApproachesTheResolvedViscousWallAtLowYPlus) {
  // As the cell is resolved, k -> 0 and the turbulence scale switches itself
  // off, leaving both blends on the viscous scale. Their product is then
  // exactly nu|U|/y, so F_U -> mu|U|/y and mu_w -> mu.
  const Real y = 1e-5;
  const Real u = 0.05;
  Real previous = std::numeric_limits<Real>::max();
  for (const Real k : {1e-4, 1e-6, 1e-8, 1e-10, 1e-14}) {
    const auto state = evaluate(u, k, y);
    const Real laminar = kViscosity * u / y;
    const Real departure = std::abs(state.wallShearStress - laminar) / laminar;
    EXPECT_LE(departure, previous) << "not converging onto the viscous wall at k = " << k;
    previous = departure;
  }

  // MEASURED FLOOR, 4.68e-5, and it is not round-off -- it is a real residual
  // with a known cause. Even at k = 0 the LOG branch does not vanish: u* falls
  // back on the viscous scale, y* = rho u* y/mu stays O(0.2), and u+ is floored
  // at 1, so u_tau^log = |U|/u+ remains finite. Its contribution enters the
  // 4th-power blend as (u_log/u_vis)^4/4, which here is 4.7e-5 -- five orders
  // below the quantity itself, and it does not grow.
  //
  // The first version of this test demanded 1e-6, a number invented before
  // measuring. What matters physically is that the residual is negligible and
  // bounded, which is what is asserted.
  EXPECT_LT(previous, 1e-4) << "the resolved-wall limit is not the laminar wall shear";

  const auto resolved = evaluate(u, 0.0, y);
  EXPECT_EQ(resolved.turbulenceVelocityScale, 0.0) << "k = 0 must switch the turbulence scale off";
  EXPECT_NEAR(resolved.wallShearStress, kViscosity * u / y, 1e-4 * kViscosity * u / y);
  EXPECT_NEAR(resolved.effectiveViscosity, kViscosity, 1e-4 * kViscosity);
  EXPECT_NEAR(resolved.wallFrictionVelocity, resolved.viscousVelocityScale,
              1e-4 * resolved.viscousVelocityScale);
}

TEST(MomentumWallFlux, ApproachesTheValidatedHighRePathAtHighYPlus) {
  // THE LIMIT THAT ATTEMPTS 2 AND 4 BOTH MISSED. Deep in the log layer the
  // viscous scale vanishes from both blends, leaving
  //   u* -> C_mu^(1/4) sqrt(k)  and  u_tau -> |U|/u+
  // so F_U -> rho u_tau* |U|/u+ = tau_LaunderSpalding EXACTLY.
  const Real uTau = 0.05;
  const Real nu = kViscosity / kDensity;
  const Real k = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);

  std::vector<Real> departures;
  for (const Real target : {30.0, 50.0, 100.0, 300.0, 1000.0}) {
    const Real y = target * nu / uTau;
    const Real u = uTau * cfd::turbulence::uPlusLogLaw(target, kCoefficients);
    const auto state = evaluate(u, k, y);
    const Real validated = cfd::turbulence::wallShearStressLaunderSpalding(
        u, k, y, kDensity, kViscosity, kCoefficients);
    departures.push_back(std::abs(state.wallShearStress - validated) / validated);
  }
  for (std::size_t i = 1; i < departures.size(); ++i) {
    EXPECT_LT(departures[i], departures[i - 1])
        << "not converging onto the validated wall shear as y+ grows";
  }
  EXPECT_LT(departures.back(), 1e-3)
      << "the high-y+ limit is not the validated Launder-Spalding wall shear";
}

// --- the scales must stay distinct -------------------------------------------

TEST(MomentumWallFlux, KeepsTheTurbulenceAndFrictionScalesDistinct) {
  // The defect this attempt exists to remove. Off equilibrium the k-based
  // turbulence scale and the log-law friction velocity are DIFFERENT numbers,
  // and the flux must use both rather than squaring either.
  const Real uTau = 0.05;
  const Real nu = kViscosity / kDensity;
  const Real y = 50.0 * nu / uTau;
  // Deliberately off equilibrium: k is a quarter of its equilibrium value while
  // the velocity keeps its log-law magnitude.
  const Real k = 0.25 * cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);
  const Real u = uTau * cfd::turbulence::uPlusLogLaw(50.0, kCoefficients);

  const auto state = evaluate(u, k, y);
  EXPECT_NE(state.turbulenceVelocityScale, state.logFrictionVelocity)
      << "the test state is not actually off equilibrium";
  EXPECT_NE(state.momentumWallVelocityScale, state.wallFrictionVelocity)
      << "the two blends collapsed onto one another, so the formulation is degenerate here";

  // And the flux must be the PRODUCT, not either square.
  EXPECT_NEAR(state.wallShearStress,
              kDensity * state.wallFrictionVelocity * state.momentumWallVelocityScale,
              1e-14 * state.wallShearStress);
  EXPECT_NE(state.wallShearStress,
            kDensity * state.wallFrictionVelocity * state.wallFrictionVelocity);
  EXPECT_NE(state.wallShearStress,
            kDensity * state.momentumWallVelocityScale * state.momentumWallVelocityScale);
}

TEST(MomentumWallFlux, DoesNotQuiteDegenerateAtEquilibrium) {
  // A CORRECTION to a claim made before this was measured.
  //
  // The claim was: at exact equilibrium u_tau* = |U|/u+, so the two blends
  // coincide and F_U collapses to rho u_tau^2, meaning this formulation could
  // only differ from the earlier blended one off equilibrium.
  //
  // That is WRONG, and the reason is worth keeping. y* is formed from u*, the
  // BLENDED scale, not from u_tau*. At y+ = 50 the blend lifts u* about 2%
  // above u_tau*, which lifts y* and hence u+, which lowers |U|/u+. So the two
  // scales differ by ~0.35% even at exact equilibrium:
  //     turbulenceVelocityScale = 0.050000
  //     logFrictionVelocity     = 0.049824
  //
  // The formulation is therefore genuinely two-scale everywhere, not only off
  // equilibrium -- but the separation is SMALL at equilibrium, which bounds how
  // much it can change a near-equilibrium channel.
  const Real uTau = 0.05;
  const Real nu = kViscosity / kDensity;
  const Real y = 50.0 * nu / uTau;
  const Real k = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);
  const Real u = uTau * cfd::turbulence::uPlusLogLaw(50.0, kCoefficients);

  const auto state = evaluate(u, k, y);
  EXPECT_NE(state.turbulenceVelocityScale, state.logFrictionVelocity);
  const Real separation = std::abs(state.turbulenceVelocityScale - state.logFrictionVelocity) /
                          state.turbulenceVelocityScale;
  std::printf("equilibrium scale separation at y+ = 50: %.5f (%.3f%%)\n", separation,
              100.0 * separation);
  EXPECT_GT(separation, 1e-4) << "the scales collapsed, so the formulation IS degenerate here";
  EXPECT_LT(separation, 0.02) << "the equilibrium separation is larger than y*'s 2% lift can "
                                 "explain, so something other than y* is differing";
}

// --- the derived wall viscosity ----------------------------------------------

TEST(MomentumWallFlux, TheEffectiveViscosityReproducesTheFluxExactly) {
  // mu_w is derived as rho u_tau u* y/|U| precisely so that CFDApp's existing
  // boundary term -mu_w U_P/y_P equals -rho u_tau u*. That equivalence is the
  // whole justification for the conversion, so it is checked rather than
  // trusted -- including the floor, which must not silently change the flux
  // where it is not needed.
  const Real uTau = 0.05;
  const Real nu = kViscosity / kDensity;
  for (const Real target : {5.0, 20.0, 50.0, 200.0}) {
    const Real y = target * nu / uTau;
    const Real k = 0.5 * cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);
    const Real u = uTau * cfd::turbulence::uPlusLogLaw(target, kCoefficients);
    const auto state = evaluate(u, k, y);
    if (state.effectiveViscosity > kViscosity) {
      EXPECT_NEAR(state.effectiveViscosity * u / y, state.wallShearStress,
                  1e-12 * state.wallShearStress)
          << "mu_w does not reproduce the flux at y+ = " << target;
    }
  }
}

TEST(MomentumWallFlux, IsDefinedAtSeparationAndDeterministic) {
  const auto stagnant = evaluate(0.0, 1e-3, 1e-4);
  EXPECT_EQ(stagnant.wallShearStress, 0.0) << "no velocity must mean no wall shear";
  EXPECT_EQ(stagnant.effectiveViscosity, kViscosity);
  EXPECT_TRUE(std::isfinite(stagnant.momentumWallVelocityScale));

  for (const Real u : {1e-8, 1e-5, 1e-2, 1.0}) {
    const auto a = evaluate(u, 1e-3, 1e-4);
    const auto b = evaluate(u, 1e-3, 1e-4);
    EXPECT_TRUE(std::isfinite(a.wallShearStress)) << "U = " << u;
    EXPECT_TRUE(std::isfinite(a.effectiveViscosity)) << "U = " << u;
    EXPECT_EQ(a.wallShearStress, b.wallShearStress);
  }
}

TEST(MomentumWallFlux, RejectsNonPhysicalArguments) {
  EXPECT_THROW((void)evaluate(1.0, -1.0, 1e-4), InvalidArgumentError);
  EXPECT_THROW((void)evaluate(1.0, 1e-3, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)evaluate(std::numeric_limits<Real>::quiet_NaN(), 1e-3, 1e-4),
               InvalidArgumentError);
}
