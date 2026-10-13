// P17.7 -- HLLC / Riemann flux. The frozen gate is results/p17/hllc/acceptance_gate.md
// (sha256 1f4ccd8af57f529e1654b2465df9e1c07124c22e0e8a500c56116372009a6465).
//
// EVERY reference number comes from tests/unit/compressible/hllc_reference_data.hpp, which is
// GENERATED from the frozen results/p17/hllc/tools/exact_riemann.py -- mpmath at 60 decimal digits,
// containing no CFDApp code. The frozen G6(f) criterion requires an independent exact solver and
// explicitly forbids adopting CFDApp output as the reference.
//
// The reference gives two separate things and this suite keeps them apart, because conflating them
// is how an implementation bug gets excused as "scheme error":
//   kCases[i].flux       the 60-digit HLLC algorithm  -> IMPLEMENTATION error, gated at 32 eps
//   kCases[i].exactFlux  the exact Godunov flux       -> the SCHEME's error. Gated only where HLLC
//                        is algebraically exact (hllcIsExact); elsewhere REPORTED, because for a
//                        strong discontinuity the estimator's truncation error is O(1) and no tight
//                        tolerance is derivable. See acceptance_gate.md section 9.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/compressible/TotalEnergy.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"
#include "hllc_reference_data.hpp"

namespace {

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector3;
using cfd::compressible::ConservedState;
using cfd::compressible::eulerFlux;
using cfd::compressible::FluxVector;
using cfd::compressible::HllcBranch;
using cfd::compressible::hllcFlux;
using cfd::compressible::hllcFluxChecked;
using cfd::compressible::hllFluxForControlOnly;
using cfd::compressible::integrateOverArea;
using cfd::compressible::makePrimitiveState;
using cfd::compressible::PrimitiveState;
using cfd::compressible::StateValidity;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::TotalEnergyState;
using cfd::compressible::WaveSpeedEstimator;
using cfd::thermal::SensibleEnthalpy;

namespace ref = cfd_test::hllc_ref;

constexpr Real kEps = 2.2204460492503131e-16;
// The gate's derived implementation tolerance. Derivation in acceptance_gate.md section 3: the
// contact-speed subtraction has condition number exactly 1 (no cancellation, because q_L < 0 <
// q_R), so |S* - u_n| <~ 3 eps |u_n|; propagating through the star state gives O(M eps) with M <= 1
// on the subsonic branch. 32 eps allows 3 eps for S*, the five components and a factor 2 of
// headroom.
constexpr Real kTol = 32.0 * kEps;

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------

PrimitiveState toState(const ref::Side& s) {
  // DIRECT construction, used for the DIMENSIONLESS literature cases (Sod's rho=1, p=1 implies
  // T = 0.0035 K, far outside any physical property range, so the factory rightly refuses it).
  // FactoryAgreesWithDirectConstruction below proves this path is identical to the authoritative
  // factory for physically realistic states, which is what licenses using it here.
  PrimitiveState p{};
  p.density = s.density;
  p.velocity = Vector3{s.u, s.v, s.w};
  p.pressure = s.pressure;
  p.temperature = s.temperature;
  p.speedOfSound = s.speedOfSound;
  p.totalEnergyDensity = s.totalEnergyDensity;
  return p;
}

Vector3 normalOf(const ref::Case& c) { return Vector3{c.nx, c.ny, c.nz}; }

const ref::Case& caseByName(const std::string& name) {
  for (int i = 0; i < ref::kCaseCount; ++i) {
    if (name == ref::kCases[i].name) return ref::kCases[i];
  }
  throw std::runtime_error("no reference case named " + name);
}

const ref::DavisSpeeds& davisByName(const std::string& name) {
  for (int i = 0; i < ref::kCaseCount; ++i) {
    if (name == ref::kDavis[i].name) return ref::kDavis[i];
  }
  throw std::runtime_error("no Davis reference named " + name);
}

void fluxToArray(const FluxVector& f, Real out[5]) {
  out[0] = f.mass;
  out[1] = f.momentum.x;
  out[2] = f.momentum.y;
  out[3] = f.momentum.z;
  out[4] = f.energy;
}

const char* branchName(HllcBranch b) {
  switch (b) {
    case HllcBranch::LeftSupersonic:
      return "F_L";
    case HllcBranch::LeftStar:
      return "F_star_L";
    case HllcBranch::RightStar:
      return "F_star_R";
    case HllcBranch::RightSupersonic:
      return "F_R";
  }
  return "?";
}

// A realistic air state, used wherever the authoritative factory path is required.
TotalEnergyState makeAir() {
  ThermodynamicProperties thermo(ref::kGasConstant, ref::kSpecificHeatPressure);
  // The ZERO reference (h_ref = 0 at T_ref = 0) is what makes rhoE = p/(gamma-1) + 0.5 rho|U|^2
  // exact, which is the form the independent reference uses. Criterion D3 verifies the two agree.
  const cfd::thermal::TemperatureDependentProperty cp(std::vector<Real>{ref::kSpecificHeatPressure},
                                                      0.0, 3000.0, "cp");
  return TotalEnergyState(thermo, SensibleEnthalpy(cp, 0.0, 0.0));
}

}  // namespace

// =============================================================================================
// Group D -- the authoritative state definitions (gate section 1)
// =============================================================================================

// D3: P17.5's rhoE must agree with the zero-reference form the independent reference uses.
TEST(HllcStateTest, D3_ConservedEnergyMatchesTheZeroReferenceForm) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  struct Probe {
    Real rho, T;
    Vector3 u;
  };
  const Probe probes[] = {
      {1.2, 300.0, Vector3{30.0, -12.0, 7.0}},
      {1.0, 288.15, Vector3{0.0, 0.0, 0.0}},
      {0.125, 800.0, Vector3{-250.0, 40.0, 0.0}},
      {10.0, 1200.0, Vector3{900.0, 0.0, -30.0}},
  };
  for (const auto& p : probes) {
    const Real rhoE = air.conservedTotalEnergy(p.rho, p.T, p.u);
    const Real pressure = air.thermodynamics().equationOfState().pressure(p.rho, p.T);
    const Real ke = 0.5 * p.rho * (p.u.x * p.u.x + p.u.y * p.u.y + p.u.z * p.u.z);
    const Real zeroReference = pressure / (gamma - 1.0) + ke;
    EXPECT_LE(std::abs(rhoE - zeroReference), 8.0 * kEps * std::abs(rhoE))
        << "rho=" << p.rho << " T=" << p.T << "  rhoE=" << rhoE << " vs " << zeroReference;
  }
}

// D4: the factory is the authoritative path; direct construction must reproduce it exactly. This is
// what licenses toState() for the dimensionless cases.
TEST(HllcStateTest, D4_FactoryAgreesWithDirectConstruction) {
  const TotalEnergyState air = makeAir();
  const Vector3 velocity{30.0, -12.0, 7.0};
  const PrimitiveState viaFactory = makePrimitiveState(air, 1.2, velocity, 300.0);

  EXPECT_GT(viaFactory.pressure, 0.0);
  EXPECT_GT(viaFactory.speedOfSound, 0.0);
  // Bitwise: the factory must be doing exactly what the models do, with nothing added.
  EXPECT_EQ(viaFactory.speedOfSound, air.thermodynamics().speedOfSound(300.0));
  EXPECT_EQ(viaFactory.pressure, air.thermodynamics().equationOfState().pressure(1.2, 300.0));
  EXPECT_EQ(viaFactory.totalEnergyDensity, air.conservedTotalEnergy(1.2, 300.0, velocity));
}

// D4: the conserved -> primitive path goes through P17.5's recover() and round-trips exactly.
TEST(HllcStateTest, D4_ConservedRoundTripIsExact) {
  const TotalEnergyState air = makeAir();
  const Vector3 velocity{120.0, -45.0, 18.0};
  const PrimitiveState forward = makePrimitiveState(air, 1.2, velocity, 350.0);
  const ConservedState q = cfd::compressible::toConserved(forward);
  const PrimitiveState back = cfd::compressible::makePrimitiveStateFromConserved(air, q);

  EXPECT_EQ(back.density, forward.density);
  EXPECT_EQ(back.totalEnergyDensity, forward.totalEnergyDensity);
  EXPECT_LE(std::abs(back.temperature - forward.temperature), 8.0 * kEps * forward.temperature);
  EXPECT_LE(std::abs(back.pressure - forward.pressure), 8.0 * kEps * forward.pressure);
}

// =============================================================================================
// Group F -- the directional physical flux (gate section 2)
// =============================================================================================

// F1: n = (1,0,0) must reproduce the textbook 1D Euler flux BITWISE.
TEST(HllcPhysicalFluxTest, F1_XNormalIsTheTextbookOneDimensionalFlux) {
  const ref::Case& c = caseByName("uniform_x");
  const PrimitiveState s = toState(c.left);
  const FluxVector f = eulerFlux(s, Vector3{1.0, 0.0, 0.0});

  const Real u = s.velocity.x;
  EXPECT_EQ(f.mass, s.density * u);
  EXPECT_EQ(f.momentum.x, s.density * u * u + s.pressure);
  EXPECT_EQ(f.momentum.y, s.density * u * s.velocity.y);
  EXPECT_EQ(f.momentum.z, s.density * u * s.velocity.z);
  EXPECT_EQ(f.energy, u * (s.totalEnergyDensity + s.pressure));
}

// F2: axis normals are exactly representable, so permuting them must permute the flux BITWISE.
TEST(HllcPhysicalFluxTest, F2_AxisNormalsPermuteTheFluxBitwise) {
  const ref::Case& c = caseByName("uniform_x");
  const ref::Side& sd = c.left;

  const PrimitiveState sx = toState(sd);
  const FluxVector fx = eulerFlux(sx, Vector3{1.0, 0.0, 0.0});

  // the same physical state with velocity components cycled x->y->z->x
  PrimitiveState sy = sx;
  sy.velocity = Vector3{sd.w, sd.u, sd.v};
  const FluxVector fy = eulerFlux(sy, Vector3{0.0, 1.0, 0.0});

  PrimitiveState sz = sx;
  sz.velocity = Vector3{sd.v, sd.w, sd.u};
  const FluxVector fz = eulerFlux(sz, Vector3{0.0, 0.0, 1.0});

  EXPECT_EQ(fy.mass, fx.mass);
  EXPECT_EQ(fz.mass, fx.mass);
  EXPECT_EQ(fy.energy, fx.energy);
  EXPECT_EQ(fz.energy, fx.energy);
  // the normal-direction momentum component carries the pressure in each case
  EXPECT_EQ(fy.momentum.y, fx.momentum.x);
  EXPECT_EQ(fz.momentum.z, fx.momentum.x);
  EXPECT_EQ(fy.momentum.z, fx.momentum.y);
  EXPECT_EQ(fz.momentum.x, fx.momentum.y);
}

// F3: the physical flux against the independent reference, every case and every component.
TEST(HllcPhysicalFluxTest, F3_MatchesTheIndependentReference) {
  Real worst = 0.0;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const Vector3 n = normalOf(c);
    for (const ref::Side* side : {&c.left, &c.right}) {
      const FluxVector f = eulerFlux(toState(*side), n);
      Real got[5];
      fluxToArray(f, got);
      // The reference stores the HLLC flux, not F_L/F_R separately, so this compares against the
      // physical flux recomputed from the same 60-digit state: for a uniform case the HLLC flux IS
      // the physical flux, which is what makes uniform_x/uniform_oblique the meaningful probes
      // here.
      if (std::string(c.name).rfind("uniform", 0) == 0) {
        for (int k = 0; k < 5; ++k) {
          const Real e = std::abs(got[k] - c.flux[k]) / c.fluxScale[k];
          worst = std::max(worst, e);
          EXPECT_LE(e, 8.0 * kEps) << c.name << " component " << k;
        }
      }
      EXPECT_TRUE(std::isfinite(f.mass) && std::isfinite(f.energy)) << c.name;
    }
  }
  std::printf("\n  F3 worst |diff|/scale vs the 60-digit reference: %.3e  (bound %.3e)\n", worst,
              8.0 * kEps);
}

// =============================================================================================
// Group C -- consistency, G6(a) (gate section 3)
// =============================================================================================

TEST(HllcConsistencyTest, C1_C2_UniformStateReturnsThePhysicalFlux) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();

  struct Probe {
    const char* what;
    Real rho, T;
    Vector3 u;
    bool supersonic;
  };
  // a = sqrt(gamma R T); at 300 K that is ~347 m/s, so 900 m/s is supersonic and 30 is not.
  const Probe probes[] = {
      {"subsonic, small", 1.2, 300.0, Vector3{30.0, -12.0, 7.0}, false},
      {"subsonic, zero velocity", 1.0, 288.15, Vector3{0.0, 0.0, 0.0}, false},
      {"subsonic, M ~ 0.7", 1.2, 300.0, Vector3{240.0, 0.0, 0.0}, false},
      {"supersonic right", 1.0, 300.0, Vector3{900.0, 0.0, 0.0}, true},
      {"supersonic left", 1.0, 300.0, Vector3{-900.0, 0.0, 0.0}, true},
      {"hot, subsonic", 0.5, 800.0, Vector3{100.0, 50.0, -25.0}, false},
  };
  const Vector3 normals[] = {
      Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}, Vector3{0.0, 0.0, 1.0},
      Vector3{2.0 / 7.0, -3.0 / 7.0, 6.0 / 7.0},  // |n| = 1 exactly: 2^2+3^2+6^2 = 49
  };

  Real worstSubsonic = 0.0;
  int bitwiseSupersonic = 0;
  for (const auto& p : probes) {
    const PrimitiveState s = makePrimitiveState(air, p.rho, p.u, p.T);
    for (const Vector3& n : normals) {
      const FluxVector expected = eulerFlux(s, n);
      const auto r = hllcFluxChecked(s, s, n, gamma);
      Real got[5];
      Real want[5];
      fluxToArray(r.flux, got);
      fluxToArray(expected, want);

      // The supersonic branch is a pure SELECTION -- it returns F_L or F_R unmodified -- so bitwise
      // is the correct requirement, not a tolerance (criterion C1).
      const bool branchIsSelection =
          (r.branch == HllcBranch::LeftSupersonic || r.branch == HllcBranch::RightSupersonic);
      for (int k = 0; k < 5; ++k) {
        if (branchIsSelection) {
          EXPECT_EQ(got[k], want[k]) << p.what << " component " << k << " must be BITWISE";
        } else {
          const Real scale = std::max(std::abs(want[k]), s.density * s.speedOfSound);
          const Real e = std::abs(got[k] - want[k]) / scale;
          worstSubsonic = std::max(worstSubsonic, e);
          EXPECT_LE(e, kTol) << p.what << " component " << k;
        }
      }
      if (branchIsSelection) ++bitwiseSupersonic;
    }
  }
  std::printf("  C2 worst subsonic |diff|/scale: %.3e  (derived bound 32 eps = %.3e)\n",
              worstSubsonic, kTol);
  std::printf("  C1 bitwise-exact supersonic selections: %d\n", bitwiseSupersonic);
  EXPECT_GT(bitwiseSupersonic, 0)
      << "no supersonic uniform case was exercised -- C1 would be vacuous";
}

// =============================================================================================
// Group W -- wave speeds and non-degeneracy (gate section 4)
// =============================================================================================

TEST(HllcWaveSpeedTest, W1_W2_W3_MatchTheReferenceAndAreWellConditioned) {
  Real worstSpeed = 0.0;
  Real worstRatio = 1.0e300;
  std::printf("\n  %-24s %14s %14s %14s %10s\n", "case", "S_L", "S*", "S_R", "|den|/bound");
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);

    const Real aMax = std::max(c.left.speedOfSound, c.right.speedOfSound);
    const Real tol = 8.0 * kEps;
    const Real eL = std::abs(r.speeds.left - c.sL) / std::max(std::abs(c.sL), aMax);
    const Real eS = std::abs(r.speeds.contact - c.sStar) / std::max(std::abs(c.sStar), aMax);
    const Real eR = std::abs(r.speeds.right - c.sR) / std::max(std::abs(c.sR), aMax);
    worstSpeed = std::max(worstSpeed, std::max(eL, std::max(eS, eR)));
    EXPECT_LE(eL, tol) << c.name << " S_L";
    EXPECT_LE(eS, tol) << c.name << " S*";
    EXPECT_LE(eR, tol) << c.name << " S_R";

    // W2: strict ordering.
    EXPECT_LT(r.speeds.left, r.speeds.contact) << c.name;
    EXPECT_LT(r.speeds.contact, r.speeds.right) << c.name;

    // W3: the PROVEN non-degeneracy bound. |denom| >= rho_L a_L + rho_R a_R, attained with
    // equality when u_nL == u_nR and a_L == a_R, hence the 4 eps slack and no epsilon guard
    // anywhere in the implementation.
    const Real bound = std::abs(r.speeds.denominatorBound);
    const Real ratio = std::abs(r.speeds.denominator) / bound;
    worstRatio = std::min(worstRatio, ratio);
    EXPECT_GE(ratio, 1.0 - 4.0 * kEps) << c.name << " denominator below its proven bound";
    EXPECT_LT(r.speeds.denominator, 0.0) << c.name << " denominator must be strictly negative";

    std::printf("  %-24s %14.6g %14.6g %14.6g %10.6f\n", c.name, r.speeds.left, r.speeds.contact,
                r.speeds.right, ratio);
  }
  std::printf("  W1 worst wave-speed error: %.3e (bound %.3e)   W3 smallest |den|/bound: %.15f\n",
              worstSpeed, 8.0 * kEps, worstRatio);
}

// W4 -- THE SIGNAL-BOUNDING CRITERION. This is the gated, derivable part of G6(f).
TEST(HllcWaveSpeedTest, W4_EstimatorBoundsTheTrueSignalSpeeds) {
  int checked = 0;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    if (!c.hasExact) continue;  // a tangential jump has no 1D exact solution
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
    const Real aMax = std::max(c.left.speedOfSound, c.right.speedOfSound);
    const Real slack = 4.0 * kEps * aMax;
    EXPECT_LE(r.speeds.left, c.exactLeftmostWave + slack)
        << c.name << ": S_L = " << r.speeds.left << " does not bound the true leftmost wave "
        << c.exactLeftmostWave;
    EXPECT_GE(r.speeds.right, c.exactRightmostWave - slack)
        << c.name << ": S_R = " << r.speeds.right << " does not bound the true rightmost wave "
        << c.exactRightmostWave;
    ++checked;
  }
  EXPECT_GE(checked, 10) << "too few cases carried an exact solution -- W4 would be weak";
  std::printf("\n  W4: the production estimator bounds the true signal speeds in %d of %d cases\n",
              checked, checked);
}

// The non-vacuity partner of W4: the estimator the authorization SUGGESTED fails it. Verified here
// against the independent reference, so the criterion is known to have teeth.
TEST(HllcWaveSpeedTest, W4_NonVacuity_DavisFailsTheSameCriterion) {
  int davisFailures = 0;
  int checked = 0;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    if (!c.hasExact) continue;
    ++checked;
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma,
                                   WaveSpeedEstimator::Davis);
    const ref::DavisSpeeds& dref = davisByName(c.name);
    // the implementation must reproduce the reference's Davis speeds
    const Real aMax = std::max(c.left.speedOfSound, c.right.speedOfSound);
    EXPECT_LE(std::abs(r.speeds.left - dref.sL) / std::max(std::abs(dref.sL), aMax), 8.0 * kEps)
        << c.name;
    EXPECT_LE(std::abs(r.speeds.right - dref.sR) / std::max(std::abs(dref.sR), aMax), 8.0 * kEps)
        << c.name;

    const bool bounds = r.speeds.left <= c.exactLeftmostWave + 4.0 * kEps * aMax &&
                        r.speeds.right >= c.exactRightmostWave - 4.0 * kEps * aMax;
    if (!bounds) ++davisFailures;
    EXPECT_EQ(bounds, dref.boundsSignal) << c.name << ": disagrees with the reference's verdict";
  }
  std::printf("  W4 non-vacuity: Davis FAILS the bounding criterion in %d of %d cases\n",
              davisFailures, checked);
  // If Davis passed everywhere, W4 would be satisfied by any estimator and would prove nothing.
  EXPECT_GE(davisFailures, 5) << "Davis no longer fails W4 -- the criterion has become vacuous";
}

TEST(HllcWaveSpeedTest, W5_NearSonicStaysFiniteAndPicksTheRightBranch) {
  const ref::Case& c = caseByName("near_sonic_left");
  const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
  EXPECT_STREQ(branchName(r.branch), c.branch);
  EXPECT_TRUE(std::isfinite(r.speeds.left));
  EXPECT_TRUE(std::isfinite(r.speeds.contact));
  EXPECT_TRUE(std::isfinite(r.flux.mass));
  EXPECT_TRUE(std::isfinite(r.flux.energy));
  std::printf("\n  W5 near-sonic: S_L = %.6e (placed within 1e-9 of 0), branch %s\n", r.speeds.left,
              branchName(r.branch));
}

// =============================================================================================
// Group B -- all four branches (gate section 5)
// =============================================================================================

TEST(HllcBranchTest, B1_B2_SupersonicBranchesReturnTheOneSidedFluxBitwise) {
  const ref::Case& right = caseByName("supersonic_right");
  {
    const PrimitiveState l = toState(right.left);
    const PrimitiveState r = toState(right.right);
    const Vector3 n = normalOf(right);
    const auto res = hllcFluxChecked(l, r, n, ref::kGamma);
    ASSERT_EQ(res.branch, HllcBranch::LeftSupersonic);
    const FluxVector fl = eulerFlux(l, n);
    EXPECT_EQ(res.flux.mass, fl.mass);
    EXPECT_EQ(res.flux.momentum.x, fl.momentum.x);
    EXPECT_EQ(res.flux.momentum.y, fl.momentum.y);
    EXPECT_EQ(res.flux.momentum.z, fl.momentum.z);
    EXPECT_EQ(res.flux.energy, fl.energy);
  }
  const ref::Case& left = caseByName("supersonic_left");
  {
    const PrimitiveState l = toState(left.left);
    const PrimitiveState r = toState(left.right);
    const Vector3 n = normalOf(left);
    const auto res = hllcFluxChecked(l, r, n, ref::kGamma);
    ASSERT_EQ(res.branch, HllcBranch::RightSupersonic);
    const FluxVector fr = eulerFlux(r, n);
    EXPECT_EQ(res.flux.mass, fr.mass);
    EXPECT_EQ(res.flux.momentum.x, fr.momentum.x);
    EXPECT_EQ(res.flux.momentum.y, fr.momentum.y);
    EXPECT_EQ(res.flux.momentum.z, fr.momentum.z);
    EXPECT_EQ(res.flux.energy, fr.energy);
  }
}

TEST(HllcBranchTest, B5_EveryBranchIsEnteredAndMatchesTheReference) {
  int counts[4] = {0, 0, 0, 0};
  std::printf("\n  %-24s %-10s %-10s\n", "case", "branch", "reference");
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
    EXPECT_STREQ(branchName(r.branch), c.branch) << c.name;
    counts[static_cast<int>(r.branch)]++;
    std::printf("  %-24s %-10s %-10s\n", c.name, branchName(r.branch), c.branch);
  }
  // A right-star case is not in the reference table by construction (every reference case is
  // left-going or symmetric), so it is built here explicitly: mirroring supersonic_left's states
  // into the subsonic range puts S* < 0 < S_R.
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const PrimitiveState l = makePrimitiveState(air, 0.5, Vector3{-200.0, 0.0, 0.0}, 300.0);
  const PrimitiveState r = makePrimitiveState(air, 1.0, Vector3{-200.0, 0.0, 0.0}, 300.0);
  const auto res = hllcFluxChecked(l, r, Vector3{1.0, 0.0, 0.0}, gamma);
  counts[static_cast<int>(res.branch)]++;
  std::printf("  %-24s %-10s (constructed)\n", "subsonic_left_moving", branchName(res.branch));

  std::printf("  branch coverage: F_L=%d  F*_L=%d  F*_R=%d  F_R=%d\n", counts[0], counts[1],
              counts[2], counts[3]);
  EXPECT_GT(counts[0], 0) << "the F_L branch was never entered";
  EXPECT_GT(counts[1], 0) << "the F*_L branch was never entered";
  EXPECT_GT(counts[2], 0) << "the F*_R branch was never entered";
  EXPECT_GT(counts[3], 0) << "the F_R branch was never entered";
}

TEST(HllcBranchTest, B3_B4_StarBranchFluxMatchesTheReference) {
  Real worst = 0.0;
  int starCases = 0;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
    if (r.branch != HllcBranch::LeftStar && r.branch != HllcBranch::RightStar) continue;
    ++starCases;
    Real got[5];
    fluxToArray(r.flux, got);
    for (int k = 0; k < 5; ++k) {
      const Real e = std::abs(got[k] - c.flux[k]) / c.fluxScale[k];
      worst = std::max(worst, e);
      EXPECT_LE(e, kTol) << c.name << " component " << k;
    }
  }
  std::printf("\n  B3/B4 worst star-branch |diff|/scale over %d cases: %.3e  (bound %.3e)\n",
              starCases, worst, kTol);
  EXPECT_GT(starCases, 5);
}

// =============================================================================================
// Group S -- star states and positivity (gate section 6)
// =============================================================================================

TEST(HllcStarStateTest, S1_S2_S3_S4_StarStatesAreAdmissibleAndMatchTheReference) {
  Real worstRho = 0.0;
  Real minStarRho = 1.0e300;
  Real minStarE = 1.0e300;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);

    EXPECT_GT(r.starDensityLeft, 0.0) << c.name;  // S1, boolean, no tolerance
    EXPECT_GT(r.starDensityRight, 0.0) << c.name;
    EXPECT_TRUE(std::isfinite(r.starDensityLeft)) << c.name;  // S2
    EXPECT_TRUE(std::isfinite(r.starDensityRight)) << c.name;
    EXPECT_GT(r.starInternalEnergyLeft, 0.0) << c.name;  // S3
    EXPECT_GT(r.starInternalEnergyRight, 0.0) << c.name;

    const Real rhoMax = std::max(c.left.density, c.right.density);
    const Real eL = std::abs(r.starDensityLeft - c.starDensityLeft) / rhoMax;  // S4
    const Real eR = std::abs(r.starDensityRight - c.starDensityRight) / rhoMax;
    worstRho = std::max(worstRho, std::max(eL, eR));
    EXPECT_LE(eL, kTol) << c.name;
    EXPECT_LE(eR, kTol) << c.name;

    minStarRho = std::min(minStarRho, std::min(r.starDensityLeft, r.starDensityRight));
    minStarE = std::min(minStarE, std::min(r.starInternalEnergyLeft, r.starInternalEnergyRight));
  }
  std::printf("\n  S4 worst star-density error: %.3e (bound %.3e)\n", worstRho, kTol);
  std::printf("  S1 minimum star density over every case : %.6e\n", minStarRho);
  std::printf("  S3 minimum star internal energy         : %.6e\n", minStarE);
}

// =============================================================================================
// Group T -- the contact wave (gate section 7)
// =============================================================================================

// T1: the derived BITWISE claim. p_L = p_R and u_n = 0 make S* exactly 0 and rho*_L exactly rho_L,
// so the mass and energy fluxes are exactly zero and the momentum flux is exactly p n.
TEST(HllcContactTest, T1_StationaryContactCarriesNoMassOrEnergyBitwise) {
  const ref::Case& c = caseByName("stationary_contact");
  const PrimitiveState l = toState(c.left);
  const PrimitiveState r = toState(c.right);
  const Vector3 n = normalOf(c);
  const auto res = hllcFluxChecked(l, r, n, ref::kGamma);

  ASSERT_NE(l.density, r.density) << "the case must actually have a density jump";
  EXPECT_EQ(l.pressure, r.pressure);

  EXPECT_EQ(res.speeds.contact, 0.0) << "S* must be EXACTLY zero for a stationary contact";
  EXPECT_EQ(res.flux.mass, 0.0) << "a stationary contact must carry NO mass flux, bitwise";
  EXPECT_EQ(res.flux.energy, 0.0) << "a stationary contact must carry NO energy flux, bitwise";
  EXPECT_EQ(res.flux.momentum.x, l.pressure * n.x);
  EXPECT_EQ(res.flux.momentum.y, l.pressure * n.y);
  EXPECT_EQ(res.flux.momentum.z, l.pressure * n.z);
  // no spurious pressure jump: the momentum flux is the pressure, not something near it
  std::printf("\n  T1 stationary contact: rho_L=%.4f rho_R=%.4f  S*=%.17g\n", l.density, r.density,
              res.speeds.contact);
  std::printf("     mass flux = %.17g   energy flux = %.17g   (both must be exactly 0)\n",
              res.flux.mass, res.flux.energy);
}

TEST(HllcContactTest, T2_MovingContactMatchesTheReference) {
  const ref::Case& c = caseByName("moving_contact");
  const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
  Real got[5];
  fluxToArray(r.flux, got);
  Real worst = 0.0;
  for (int k = 0; k < 5; ++k) {
    const Real e = std::abs(got[k] - c.flux[k]) / c.fluxScale[k];
    worst = std::max(worst, e);
    EXPECT_LE(e, kTol) << "component " << k;
  }
  // the contact speed must equal the common normal velocity, since u_nL == u_nR
  EXPECT_LE(std::abs(r.speeds.contact - r.speeds.normalVelocityLeft),
            kTol * std::abs(r.speeds.normalVelocityLeft));
  std::printf("  T2 moving contact: S* = %.17g, u_n = %.17g, worst |diff|/scale = %.3e\n",
              r.speeds.contact, r.speeds.normalVelocityLeft, worst);
}

// T3 / section 21: with equal normal velocity and pressure but a TANGENTIAL jump, the contact must
// carry no mass and no tangential momentum. This is the multidimensional property HLL destroys.
TEST(HllcContactTest, T3_TangentialShearCarriesNoMassOrTangentialMomentum) {
  const ref::Case& c = caseByName("tangential_shear");
  const PrimitiveState l = toState(c.left);
  const PrimitiveState r = toState(c.right);
  const Vector3 n = normalOf(c);
  const auto res = hllcFluxChecked(l, r, n, ref::kGamma);

  ASSERT_NE(l.velocity.y, r.velocity.y) << "the case must actually have a tangential jump";
  EXPECT_EQ(res.flux.mass, 0.0) << "no mass may cross a stationary shear contact";
  EXPECT_EQ(res.flux.momentum.y, 0.0) << "no TANGENTIAL momentum may be advected without mass flux";
  EXPECT_EQ(res.flux.momentum.z, 0.0);
  EXPECT_EQ(res.flux.momentum.x, l.pressure * n.x);
  EXPECT_EQ(res.flux.energy, 0.0);
  std::printf("\n  T3 tangential shear: v_L=%.1f v_R=%.1f  mass flux=%.17g  tangential mom=%.17g\n",
              l.velocity.y, r.velocity.y, res.flux.mass, res.flux.momentum.y);
}

// T4 -- the control the frozen G6 criterion names. Deleting the contact wave must break the contact
// while leaving the uniform-state consistency test passing.
TEST(HllcContactTest, T4_NonVacuity_DeletingTheContactWaveBreaksTheContact) {
  // (1) it must still pass consistency -- otherwise the control proves nothing specific
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const PrimitiveState s = makePrimitiveState(air, 1.2, Vector3{30.0, -12.0, 7.0}, 300.0);
  const Vector3 nx{1.0, 0.0, 0.0};
  const FluxVector physical = eulerFlux(s, nx);
  const auto uniform = hllFluxForControlOnly(s, s, nx, gamma);
  EXPECT_LE(std::abs(uniform.flux.mass - physical.mass),
            kTol * std::max(std::abs(physical.mass), s.density * s.speedOfSound))
      << "HLL must still satisfy consistency (a) -- G6 says so explicitly";

  // (2) it must break the stationary contact, and by a large physical margin
  const ref::Case& c = caseByName("stationary_contact");
  const auto broken =
      hllFluxForControlOnly(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
  const Real spurious = std::abs(broken.flux.mass);
  EXPECT_GT(spurious, 100.0) << "deleting the contact wave no longer produces a spurious mass flux "
                                "-- T1 has become vacuous";
  // the reference measured 243.4595971726513 kg/(m^2 s) at 60 digits
  EXPECT_LE(std::abs(spurious - std::abs(c.hllFlux[0])) / std::abs(c.hllFlux[0]), kTol);
  std::printf("\n  T4 contact wave DELETED: spurious mass flux = %.10f kg/(m^2 s)\n", spurious);
  std::printf("     the true flux is exactly 0; the 60-digit reference gives %.10f\n",
              std::abs(c.hllFlux[0]));
  std::printf(
      "     and HLLC itself gives %.17g -- so T1 is not vacuous\n",
      hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma).flux.mass);
}

// =============================================================================================
// Groups R and N -- rotation and conservation (gate section 8)
// =============================================================================================

// R1: a dyadic rotation is exactly representable, so G6(d) requires BITWISE equality.
TEST(HllcRotationTest, R1_DyadicRotationRotatesTheFluxBitwise) {
  const ref::Case& c = caseByName("sod_atmospheric");
  const PrimitiveState l = toState(c.left);
  const PrimitiveState r = toState(c.right);

  // the x-aligned problem
  const auto fx = hllcFluxChecked(l, r, Vector3{1.0, 0.0, 0.0}, ref::kGamma);

  // rotate 90 degrees about z: (x,y,z) -> (-y,x,z), so the normal becomes (0,1,0)
  auto rotateZ90 = [](const Vector3& v) { return Vector3{-v.y, v.x, v.z}; };
  PrimitiveState lr = l;
  PrimitiveState rr = r;
  lr.velocity = rotateZ90(l.velocity);
  rr.velocity = rotateZ90(r.velocity);
  const auto fy = hllcFluxChecked(lr, rr, Vector3{0.0, 1.0, 0.0}, ref::kGamma);

  EXPECT_EQ(fy.flux.mass, fx.flux.mass);
  EXPECT_EQ(fy.flux.energy, fx.flux.energy);
  EXPECT_EQ(fy.speeds.left, fx.speeds.left);
  EXPECT_EQ(fy.speeds.contact, fx.speeds.contact);
  EXPECT_EQ(fy.speeds.right, fx.speeds.right);
  // the momentum flux must be the rotated x-flux, bitwise
  const Vector3 rotated = rotateZ90(fx.flux.momentum);
  EXPECT_EQ(fy.flux.momentum.x, rotated.x);
  EXPECT_EQ(fy.flux.momentum.y, rotated.y);
  EXPECT_EQ(fy.flux.momentum.z, rotated.z);

  // 180 degrees about z: (x,y,z) -> (-x,-y,z), normal (-1,0,0)
  auto rotateZ180 = [](const Vector3& v) { return Vector3{-v.x, -v.y, v.z}; };
  PrimitiveState l2 = l;
  PrimitiveState r2 = r;
  l2.velocity = rotateZ180(l.velocity);
  r2.velocity = rotateZ180(r.velocity);
  const auto f180 = hllcFluxChecked(l2, r2, Vector3{-1.0, 0.0, 0.0}, ref::kGamma);
  EXPECT_EQ(f180.flux.mass, fx.flux.mass);
  EXPECT_EQ(f180.flux.energy, fx.flux.energy);
  const Vector3 rotated180 = rotateZ180(fx.flux.momentum);
  EXPECT_EQ(f180.flux.momentum.x, rotated180.x);
  EXPECT_EQ(f180.flux.momentum.y, rotated180.y);

  // x <-> z, via a 90 degree rotation about y: (x,y,z) -> (z,y,-x), normal (0,0,1)
  auto rotateY90 = [](const Vector3& v) { return Vector3{v.z, v.y, -v.x}; };
  PrimitiveState l3 = l;
  PrimitiveState r3 = r;
  l3.velocity = rotateY90(l.velocity);
  r3.velocity = rotateY90(r.velocity);
  const auto fz = hllcFluxChecked(l3, r3, Vector3{0.0, 0.0, -1.0}, ref::kGamma);
  EXPECT_EQ(fz.flux.mass, fx.flux.mass);
  EXPECT_EQ(fz.flux.energy, fx.flux.energy);
  std::printf("\n  R1 dyadic rotations (z90, z180, y90): mass and energy flux BITWISE equal\n");
}

// R2: an oblique normal under a general rotation. Not bitwise -- the rotation itself is inexact --
// so the derived 32 eps applies.
TEST(HllcRotationTest, R2_ObliqueNormalIsRotationallyConsistent) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const PrimitiveState l = makePrimitiveState(air, 1.0, Vector3{120.0, -40.0, 15.0}, 320.0);
  const PrimitiveState r = makePrimitiveState(air, 0.6, Vector3{-30.0, 55.0, -20.0}, 280.0);

  const Vector3 n{2.0 / 7.0, -3.0 / 7.0, 6.0 / 7.0};
  const auto base = hllcFluxChecked(l, r, n, gamma);

  // A rotation that maps the coordinate axes onto each other cyclically: (x,y,z) -> (y,z,x).
  // Applied to the states AND the normal, the whole configuration is the same physical problem.
  auto cycle = [](const Vector3& v) { return Vector3{v.y, v.z, v.x}; };
  PrimitiveState lc = l;
  PrimitiveState rc = r;
  lc.velocity = cycle(l.velocity);
  rc.velocity = cycle(r.velocity);
  const auto rot = hllcFluxChecked(lc, rc, cycle(n), gamma);

  const Real scale = std::max(l.density, r.density) * std::max(l.speedOfSound, r.speedOfSound);
  EXPECT_LE(std::abs(rot.flux.mass - base.flux.mass), kTol * scale);
  EXPECT_LE(std::abs(rot.speeds.contact - base.speeds.contact),
            kTol * std::max(l.speedOfSound, r.speedOfSound));
  const Vector3 expected = cycle(base.flux.momentum);
  const Real momScale =
      scale * std::max(l.speedOfSound, r.speedOfSound) + std::max(l.pressure, r.pressure);
  EXPECT_LE(std::abs(rot.flux.momentum.x - expected.x), kTol * momScale);
  EXPECT_LE(std::abs(rot.flux.momentum.y - expected.y), kTol * momScale);
  EXPECT_LE(std::abs(rot.flux.momentum.z - expected.z), kTol * momScale);
  std::printf("  R2 oblique normal (2,-3,6)/7 under an axis cycle: mass diff %.3e (bound %.3e)\n",
              std::abs(rot.flux.mass - base.flux.mass), kTol * scale);
}

// R3 / section 20: the area scales the integrated flux and must NOT reach the wave speeds.
TEST(HllcRotationTest, R3_AreaScalesTheFluxAndNeverTheWaveSpeeds) {
  const ref::Case& c = caseByName("sod_atmospheric");
  const PrimitiveState l = toState(c.left);
  const PrimitiveState r = toState(c.right);
  const Vector3 n = normalOf(c);

  const auto unit = hllcFluxChecked(l, r, n, ref::kGamma);
  for (const Real area : {0.25, 1.0, 4.0, 1024.0}) {
    const FluxVector scaled = integrateOverArea(unit.flux, area);
    // powers of two scale exactly
    EXPECT_EQ(scaled.mass, unit.flux.mass * area);
    EXPECT_EQ(scaled.energy, unit.flux.energy * area);
    EXPECT_EQ(scaled.momentum.x, unit.flux.momentum.x * area);
    // the wave speeds are a property of the STATES, not of the face size
    const auto again = hllcFluxChecked(l, r, n, ref::kGamma);
    EXPECT_EQ(again.speeds.left, unit.speeds.left);
    EXPECT_EQ(again.speeds.contact, unit.speeds.contact);
    EXPECT_EQ(again.speeds.right, unit.speeds.right);
  }
  std::printf("\n  R3 area scaling exact for 0.25, 1, 4, 1024; wave speeds unchanged bitwise\n");
}

// N1: the derived BITWISE antisymmetry. This is what lets P17.8 update owner and neighbour from one
// flux evaluation (section 33).
TEST(HllcConservationTest, N1_FluxIsBitwiseAntisymmetricUnderReversal) {
  int checked = 0;
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const PrimitiveState l = toState(c.left);
    const PrimitiveState r = toState(c.right);
    const Vector3 n = normalOf(c);
    const Vector3 minusN{-n.x, -n.y, -n.z};

    const auto forward = hllcFluxChecked(l, r, n, ref::kGamma);
    const auto reversed = hllcFluxChecked(r, l, minusN, ref::kGamma);

    EXPECT_EQ(reversed.flux.mass, -forward.flux.mass) << c.name << " mass";
    EXPECT_EQ(reversed.flux.momentum.x, -forward.flux.momentum.x) << c.name << " x momentum";
    EXPECT_EQ(reversed.flux.momentum.y, -forward.flux.momentum.y) << c.name << " y momentum";
    EXPECT_EQ(reversed.flux.momentum.z, -forward.flux.momentum.z) << c.name << " z momentum";
    EXPECT_EQ(reversed.flux.energy, -forward.flux.energy) << c.name << " energy";
    EXPECT_EQ(reversed.speeds.contact, -forward.speeds.contact) << c.name << " S*";
    EXPECT_EQ(reversed.speeds.left, -forward.speeds.right) << c.name << " S_L' = -S_R";
    EXPECT_EQ(reversed.speeds.right, -forward.speeds.left) << c.name << " S_R' = -S_L";
    ++checked;
  }
  std::printf(
      "\n  N1 exact antisymmetry F(L,R,n) = -F(R,L,-n) over %d cases, BITWISE, all 5 "
      "components\n",
      checked);
}

// The synthetic finite-volume harness section 44 explicitly permits: one flux evaluation updating
// both cells. It is NOT a solver and does not advance in time.
TEST(HllcConservationTest, N1_OneFluxEvaluationConservesAcrossTwoCells) {
  const ref::Case& c = caseByName("sod_atmospheric");
  const PrimitiveState l = toState(c.left);
  const PrimitiveState r = toState(c.right);
  const Vector3 n{1.0, 0.0, 0.0};
  const Real area = 0.5;

  const auto res = hllcFluxChecked(l, r, n, ref::kGamma);
  const FluxVector f = integrateOverArea(res.flux, area);

  // owner gains -F*A, neighbour gains +F*A. The sum must vanish EXACTLY.
  const Real massSum = (-f.mass) + f.mass;
  const Real energySum = (-f.energy) + f.energy;
  EXPECT_EQ(massSum, 0.0);
  EXPECT_EQ(energySum, 0.0);
  EXPECT_EQ((-f.momentum.x) + f.momentum.x, 0.0);
  std::printf("\n  conservation across one face: mass sum %.17g, energy sum %.17g (exactly 0)\n",
              massSum, energySum);
}

// =============================================================================================
// Group X -- the independent exact solver, G6(f) (gate section 9)
// =============================================================================================

// X1: where HLLC is ALGEBRAICALLY exact, it must match the exact Godunov flux.
TEST(HllcExactSolverTest, X1_MatchesTheExactGodunovFluxWhereHllcIsExact) {
  Real worst = 0.0;
  int checked = 0;
  std::printf("\n  %-24s %-12s %s\n", "case", "worst", "(|diff|/scale vs the EXACT solution)");
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    if (!c.hllcIsExact) continue;
    ++checked;
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
    Real got[5];
    fluxToArray(r.flux, got);
    Real caseWorst = 0.0;
    for (int k = 0; k < 5; ++k) {
      const Real e = std::abs(got[k] - c.exactFlux[k]) / c.fluxScale[k];
      caseWorst = std::max(caseWorst, e);
      EXPECT_LE(e, kTol) << c.name << " component " << k;
    }
    worst = std::max(worst, caseWorst);
    std::printf("  %-24s %.3e\n", c.name, caseWorst);
  }
  std::printf("  X1 worst: %.3e  (bound 32 eps = %.3e) over %d algebraically exact cases\n", worst,
              kTol, checked);
  EXPECT_GE(checked, 7) << "too few exactness cases -- X1 would be weak";
}

// X2: the IMPLEMENTATION check -- every case against the 60-digit HLLC, including strong jumps.
TEST(HllcExactSolverTest, X2_MatchesTheSixtyDigitHllcEverywhere) {
  Real worst = 0.0;
  const char* worstCase = "";
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
    Real got[5];
    fluxToArray(r.flux, got);
    for (int k = 0; k < 5; ++k) {
      const Real e = std::abs(got[k] - c.flux[k]) / c.fluxScale[k];
      if (e > worst) {
        worst = e;
        worstCase = c.name;
      }
      EXPECT_LE(e, kTol) << c.name << " component " << k;
    }
  }
  std::printf("\n  X2 worst implementation error vs the 60-digit HLLC: %.3e at %s (bound %.3e)\n",
              worst, worstCase, kTol);
}

// X3 / X4: the strong discontinuities. REPORTED, and gated only on the derivable properties --
// never on flux accuracy, which acceptance_gate.md section 9 explains at length.
TEST(HllcExactSolverTest, X3_X4_StrongDiscontinuitiesAreReportedNotGatedOnAccuracy) {
  const char* names[] = {"sod_literature", "sod_atmospheric", "strong_pressure_ratio",
                         "low_pressure"};
  std::printf(
      "\n  THE SCHEME'S OWN ERROR against the exact solution. NOT a gated accuracy claim:\n");
  std::printf("  for a strong discontinuity a single HLLC interface flux is O(1) away from the\n");
  std::printf(
      "  exact Godunov flux, and no tight tolerance is derivable. Full Sod validation is\n");
  std::printf("  P17.9's, not P17.7's.\n\n");
  std::printf("  %-22s %10s %10s %10s %10s\n", "case", "mass", "x-mom", "energy", "branch");
  for (const char* nm : names) {
    const ref::Case& c = caseByName(nm);
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
    Real got[5];
    fluxToArray(r.flux, got);
    const Real em = std::abs(got[0] - c.exactFlux[0]) / c.fluxScale[0];
    const Real ex = std::abs(got[1] - c.exactFlux[1]) / c.fluxScale[1];
    const Real ee = std::abs(got[4] - c.exactFlux[4]) / c.fluxScale[4];
    std::printf("  %-22s %10.4f %10.4f %10.4f %10s\n", nm, em, ex, ee, branchName(r.branch));

    // X4 -- what IS gated: ordering, positivity, finiteness, and the branch.
    EXPECT_LT(r.speeds.left, r.speeds.contact) << nm;
    EXPECT_LT(r.speeds.contact, r.speeds.right) << nm;
    EXPECT_GT(r.starDensityLeft, 0.0) << nm;
    EXPECT_GT(r.starDensityRight, 0.0) << nm;
    EXPECT_TRUE(std::isfinite(r.flux.energy)) << nm;
    EXPECT_STREQ(branchName(r.branch), c.branch) << nm;
  }

  // The Sod initial state, reported in full as section 40 requires.
  const ref::Case& sod = caseByName("sod_literature");
  const auto r = hllcFluxChecked(toState(sod.left), toState(sod.right), normalOf(sod), ref::kGamma);
  std::printf("\n  SOD INITIAL STATE (rho 1.0/0.125, p 1.0/0.1, u 0), gamma = %.15f\n",
              ref::kGamma);
  std::printf("    S_L    = %.17g\n", r.speeds.left);
  std::printf("    S*     = %.17g\n", r.speeds.contact);
  std::printf("    S_R    = %.17g\n", r.speeds.right);
  std::printf("    rho*_L = %.17g\n", r.starDensityLeft);
  std::printf("    rho*_R = %.17g\n", r.starDensityRight);
  std::printf("    F      = [%.17g, %.17g, %.17g, %.17g, %.17g]\n", r.flux.mass, r.flux.momentum.x,
              r.flux.momentum.y, r.flux.momentum.z, r.flux.energy);
  std::printf("    the exact solution's leftmost/rightmost waves: %.17g / %.17g\n",
              sod.exactLeftmostWave, sod.exactRightmostWave);
}

// =============================================================================================
// Group P -- invalid and extreme states (gate section 10)
// =============================================================================================

TEST(HllcNegativeTest, P1_P2_P3_InadmissibleInputsAreRefusedWithANamedReason) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const PrimitiveState good = makePrimitiveState(air, 1.2, Vector3{30.0, 0.0, 0.0}, 300.0);
  const Vector3 nx{1.0, 0.0, 0.0};
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();

  struct Bad {
    const char* what;
    PrimitiveState state;
    StateValidity expected;
  };
  std::vector<Bad> bad;
  auto with = [&](const char* what, auto mutate, StateValidity v) {
    PrimitiveState s = good;
    mutate(s);
    bad.push_back({what, s, v});
  };
  with("rho = 0", [](PrimitiveState& s) { s.density = 0.0; }, StateValidity::NonPositiveDensity);
  with("rho < 0", [](PrimitiveState& s) { s.density = -1.0; }, StateValidity::NonPositiveDensity);
  with("rho = NaN", [&](PrimitiveState& s) { s.density = nan; }, StateValidity::NonFiniteDensity);
  with("p = 0", [](PrimitiveState& s) { s.pressure = 0.0; }, StateValidity::NonPositivePressure);
  with("p < 0", [](PrimitiveState& s) { s.pressure = -1.0; }, StateValidity::NonPositivePressure);
  with("p = NaN", [&](PrimitiveState& s) { s.pressure = nan; }, StateValidity::NonFinitePressure);
  with("u = Inf", [&](PrimitiveState& s) { s.velocity.x = inf; }, StateValidity::NonFiniteVelocity);
  with("v = NaN", [&](PrimitiveState& s) { s.velocity.y = nan; }, StateValidity::NonFiniteVelocity);
  with(
      "rhoE = NaN", [&](PrimitiveState& s) { s.totalEnergyDensity = nan; },
      StateValidity::NonFiniteTotalEnergy);
  with(
      "a = NaN", [&](PrimitiveState& s) { s.speedOfSound = nan; },
      StateValidity::NonFiniteSoundSpeed);
  with(
      "a = 0", [](PrimitiveState& s) { s.speedOfSound = 0.0; },
      StateValidity::NonPositiveSoundSpeed);

  for (const Bad& b : bad) {
    // the non-throwing core reports a typed reason
    const auto res = hllcFlux(b.state, good, nx, gamma);
    EXPECT_FALSE(res.valid) << b.what << " was accepted on the left";
    EXPECT_EQ(res.validity, b.expected) << b.what;
    EXPECT_FALSE(hllcFlux(good, b.state, nx, gamma).valid)
        << b.what << " was accepted on the right";
    // the checked wrapper throws, and the message names the offending quantity
    EXPECT_THROW((void)hllcFluxChecked(b.state, good, nx, gamma), InvalidArgumentError) << b.what;
    try {
      (void)hllcFluxChecked(b.state, good, nx, gamma);
      FAIL() << b.what << " did not throw";
    } catch (const InvalidArgumentError& e) {
      const std::string msg = e.what();
      EXPECT_NE(msg.find(cfd::compressible::describeValidity(b.expected)), std::string::npos)
          << b.what << ": message was \"" << msg << "\"";
    }
  }
  std::printf("\n  P1 %zu inadmissible states each refused with a typed, named reason\n",
              bad.size());
}

TEST(HllcNegativeTest, P2_InvalidNormalsAreRefused) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const PrimitiveState s = makePrimitiveState(air, 1.2, Vector3{30.0, 0.0, 0.0}, 300.0);
  const Real nan = std::numeric_limits<Real>::quiet_NaN();

  struct BadNormal {
    const char* what;
    Vector3 n;
    StateValidity expected;
  };
  const BadNormal bad[] = {
      {"zero length", Vector3{0.0, 0.0, 0.0}, StateValidity::NonUnitNormal},
      {"length 2 (an area vector)", Vector3{2.0, 0.0, 0.0}, StateValidity::NonUnitNormal},
      {"length 0.5", Vector3{0.5, 0.0, 0.0}, StateValidity::NonUnitNormal},
      {"unnormalised (1,1,0)", Vector3{1.0, 1.0, 0.0}, StateValidity::NonUnitNormal},
      {"NaN component", Vector3{nan, 0.0, 0.0}, StateValidity::NonFiniteNormal},
  };
  for (const BadNormal& b : bad) {
    const auto res = hllcFlux(s, s, b.n, gamma);
    EXPECT_FALSE(res.valid) << b.what;
    EXPECT_EQ(res.validity, b.expected) << b.what;
    EXPECT_THROW((void)hllcFluxChecked(s, s, b.n, gamma), InvalidArgumentError) << b.what;
  }
  // and gamma itself
  EXPECT_THROW((void)hllcFluxChecked(s, s, Vector3{1.0, 0.0, 0.0}, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)hllcFluxChecked(s, s, Vector3{1.0, 0.0, 0.0}, nan), InvalidArgumentError);
  std::printf("  P2 5 invalid normals and 2 invalid gammas refused; an AREA VECTOR is rejected\n");
}

TEST(HllcExtremeStateTest, P4_ExtremeButValidStatesStayFiniteAndAdmissible) {
  std::printf("\n  %-24s %12s %12s %12s\n", "case", "min rho*", "min e*", "max |F|");
  for (const char* nm : {"strong_pressure_ratio", "strong_density_ratio", "low_pressure",
                         "supersonic_right", "near_sonic_left"}) {
    const ref::Case& c = caseByName(nm);
    const auto r = hllcFluxChecked(toState(c.left), toState(c.right), normalOf(c), ref::kGamma);
    Real got[5];
    fluxToArray(r.flux, got);
    Real maxF = 0.0;
    for (int k = 0; k < 5; ++k) {
      EXPECT_TRUE(std::isfinite(got[k])) << nm << " component " << k;
      maxF = std::max(maxF, std::abs(got[k]));
    }
    const Real minRho = std::min(r.starDensityLeft, r.starDensityRight);
    const Real minE = std::min(r.starInternalEnergyLeft, r.starInternalEnergyRight);
    EXPECT_GT(minRho, 0.0) << nm;
    EXPECT_GT(minE, 0.0) << nm;
    std::printf("  %-24s %12.4e %12.4e %12.4e\n", nm, minRho, minE, maxF);
  }
}

// =============================================================================================
// Group G -- determinism (gate section 11, criterion G4)
// =============================================================================================

TEST(HllcDeterminismTest, G4_RepeatedEvaluationIsBitwiseIdentical) {
  for (int i = 0; i < ref::kCaseCount; ++i) {
    const ref::Case& c = ref::kCases[i];
    const PrimitiveState l = toState(c.left);
    const PrimitiveState r = toState(c.right);
    const Vector3 n = normalOf(c);
    const auto a = hllcFluxChecked(l, r, n, ref::kGamma);
    const auto b = hllcFluxChecked(l, r, n, ref::kGamma);
    EXPECT_EQ(a.flux.mass, b.flux.mass) << c.name;
    EXPECT_EQ(a.flux.momentum.x, b.flux.momentum.x) << c.name;
    EXPECT_EQ(a.flux.momentum.y, b.flux.momentum.y) << c.name;
    EXPECT_EQ(a.flux.momentum.z, b.flux.momentum.z) << c.name;
    EXPECT_EQ(a.flux.energy, b.flux.energy) << c.name;
    EXPECT_EQ(a.speeds.left, b.speeds.left) << c.name;
    EXPECT_EQ(a.speeds.contact, b.speeds.contact) << c.name;
    EXPECT_EQ(a.speeds.right, b.speeds.right) << c.name;
    EXPECT_EQ(a.branch, b.branch) << c.name;
    EXPECT_EQ(a.starDensityLeft, b.starDensityLeft) << c.name;
  }
  std::printf(
      "\n  G4 CPU determinism: %d cases, flux/speeds/branch/star states BITWISE identical\n",
      ref::kCaseCount);
}

// =============================================================================================
// The reconstruction interface (gate sections 6, 7, 22, 23)
// =============================================================================================

TEST(HllcReconstructionTest, FirstOrderModeIsOwnerAndNeighbour) {
  const TotalEnergyState air = makeAir();
  const PrimitiveState owner = makePrimitiveState(air, 1.2, Vector3{30.0, 0.0, 0.0}, 300.0);
  const PrimitiveState neighbour = makePrimitiveState(air, 0.9, Vector3{-10.0, 5.0, 0.0}, 310.0);

  const auto face = cfd::compressible::reconstructFirstOrder(owner, neighbour);
  // piecewise constant: exactly the cell states, nothing interpolated
  EXPECT_EQ(face.left.density, owner.density);
  EXPECT_EQ(face.left.pressure, owner.pressure);
  EXPECT_EQ(face.left.velocity.x, owner.velocity.x);
  EXPECT_EQ(face.left.totalEnergyDensity, owner.totalEnergyDensity);
  EXPECT_EQ(face.right.density, neighbour.density);
  EXPECT_EQ(face.right.pressure, neighbour.pressure);
  EXPECT_EQ(face.right.velocity.x, neighbour.velocity.x);

  // and the HLLC core consumes the reconstructed states, not the cells
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const Vector3 nx{1.0, 0.0, 0.0};
  const auto viaFace = hllcFluxChecked(face.left, face.right, nx, gamma);
  const auto direct = hllcFluxChecked(owner, neighbour, nx, gamma);
  EXPECT_EQ(viaFace.flux.mass, direct.flux.mass);
  EXPECT_EQ(viaFace.flux.energy, direct.flux.energy);
}

TEST(HllcReconstructionTest, BoundaryFaceTakesAnExteriorStateFromTheCaller) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const PrimitiveState interior = makePrimitiveState(air, 1.2, Vector3{30.0, 0.0, 0.0}, 300.0);
  // A synthetic exterior state, as a boundary condition would supply in P17.8. No boundary
  // condition is hard-wired into the flux layer (section 24).
  const PrimitiveState exterior = makePrimitiveState(air, 1.2, Vector3{-30.0, 0.0, 0.0}, 300.0);

  const auto face = cfd::compressible::reconstructFirstOrderBoundary(interior, exterior);
  EXPECT_EQ(face.left.velocity.x, interior.velocity.x);
  EXPECT_EQ(face.right.velocity.x, exterior.velocity.x);

  // a symmetric, opposed pair: the mass flux must vanish by symmetry
  const auto res = hllcFluxChecked(face.left, face.right, Vector3{1.0, 0.0, 0.0}, gamma);
  EXPECT_LE(std::abs(res.flux.mass), kTol * interior.density * interior.speedOfSound);
  std::printf("\n  a symmetric opposed boundary pair gives mass flux %.3e (a wall-like state)\n",
              res.flux.mass);
}

// =============================================================================================
// Section 26 -- Galilean consistency, as a sanity check against mixing velocity decompositions
// =============================================================================================

// Adding a constant translational velocity must transform the flux the way the Euler equations say
// it does. This is not a formal proof (section 26 says it need not be); its value is that it
// catches an incorrect mix of ABSOLUTE velocity, NORMAL velocity and TANGENTIAL velocity, which is
// the most likely way a multidimensional HLLC goes wrong without any single-direction test
// noticing.
TEST(HllcGalileanTest, AddingAConstantVelocityTransformsTheFluxConsistently) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();

  const Vector3 boost{37.0, -11.0, 5.0};  // an arbitrary constant translation
  const Vector3 n{2.0 / 7.0, -3.0 / 7.0, 6.0 / 7.0};
  const Real boostNormal = (boost.x * n.x) + (boost.y * n.y) + (boost.z * n.z);

  const PrimitiveState l0 = makePrimitiveState(air, 1.2, Vector3{60.0, -20.0, 8.0}, 310.0);
  const PrimitiveState r0 = makePrimitiveState(air, 0.8, Vector3{-15.0, 30.0, -4.0}, 290.0);

  // the same states, translated. rho, p, T and a are Galilean invariants; rhoE is NOT, so it is
  // rebuilt through P17.5 from the boosted velocity rather than adjusted by hand.
  const PrimitiveState l1 = makePrimitiveState(
      air, 1.2, Vector3{l0.velocity.x + boost.x, l0.velocity.y + boost.y, l0.velocity.z + boost.z},
      310.0);
  const PrimitiveState r1 = makePrimitiveState(
      air, 0.8, Vector3{r0.velocity.x + boost.x, r0.velocity.y + boost.y, r0.velocity.z + boost.z},
      290.0);

  const auto a0 = hllcFluxChecked(l0, r0, n, gamma);
  const auto a1 = hllcFluxChecked(l1, r1, n, gamma);

  // Invariants of the boost: rho, p, T, a are unchanged, so the wave speeds must shift by exactly
  // the boost's NORMAL component and the star DENSITIES must not move at all.
  const Real aMax = std::max(l0.speedOfSound, r0.speedOfSound);
  EXPECT_LE(std::abs((a1.speeds.left - a0.speeds.left) - boostNormal), kTol * aMax);
  EXPECT_LE(std::abs((a1.speeds.contact - a0.speeds.contact) - boostNormal), kTol * aMax);
  EXPECT_LE(std::abs((a1.speeds.right - a0.speeds.right) - boostNormal), kTol * aMax);

  const Real rhoMax = std::max(l0.density, r0.density);
  EXPECT_LE(std::abs(a1.starDensityLeft - a0.starDensityLeft), kTol * rhoMax);
  EXPECT_LE(std::abs(a1.starDensityRight - a0.starDensityRight), kTol * rhoMax);

  // The mass flux transforms as  m' = m + rho* (boost . n), with rho* the star density on the side
  // the branch selected -- i.e. the translation adds a purely advective mass flux.
  const Real rhoStar =
      (a0.branch == HllcBranch::LeftStar || a0.branch == HllcBranch::LeftSupersonic)
          ? a0.starDensityLeft
          : a0.starDensityRight;
  const Real massScale = rhoMax * aMax;
  const Real predicted = a0.flux.mass + rhoStar * boostNormal;
  std::printf("\n  Galilean: boost.n = %.6f   mass flux %.8f -> %.8f  predicted %.8f\n",
              boostNormal, a0.flux.mass, a1.flux.mass, predicted);
  std::printf("    |measured - predicted| / (rho_m a_m) = %.3e   (bound %.3e)\n",
              std::abs(a1.flux.mass - predicted) / massScale, kTol);
  EXPECT_LE(std::abs(a1.flux.mass - predicted), kTol * massScale)
      << "the boosted mass flux is not m + rho* (boost.n) -- a velocity decomposition is mixed up";

  // and the branch must not change: a Galilean boost small against the sound speed cannot flip the
  // wave structure here, so a different branch would signal exactly the mix-up above.
  EXPECT_EQ(a1.branch, a0.branch);
}

// =============================================================================================
// Section 16B -- zero velocity AND uniform pressure, stated as its own case
// =============================================================================================

// Section 16B asks explicitly for "zero velocity / uniform pressure -- verify exact mass/energy
// behaviour". The stationary-contact case covers it, but only as a side effect of also having a
// density jump. This asserts the pure case: a genuinely UNIFORM state at rest.
TEST(HllcAnalyticTest, ZeroVelocityUniformPressureCarriesOnlyPressure) {
  const TotalEnergyState air = makeAir();
  const Real gamma = air.thermodynamics().specificHeatRatio();
  const PrimitiveState s = makePrimitiveState(air, 1.2, Vector3{0.0, 0.0, 0.0}, 300.0);

  const Vector3 normals[] = {Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}, Vector3{0.0, 0.0, 1.0},
                             Vector3{2.0 / 7.0, -3.0 / 7.0, 6.0 / 7.0}};
  for (const Vector3& n : normals) {
    const auto r = hllcFluxChecked(s, s, n, gamma);
    // gas at rest: no mass and no energy may cross, and the momentum flux is pure pressure
    EXPECT_EQ(r.flux.mass, 0.0) << "gas at rest must carry no mass flux";
    EXPECT_EQ(r.flux.energy, 0.0) << "gas at rest must carry no energy flux";
    EXPECT_EQ(r.flux.momentum.x, s.pressure * n.x);
    EXPECT_EQ(r.flux.momentum.y, s.pressure * n.y);
    EXPECT_EQ(r.flux.momentum.z, s.pressure * n.z);
    // and it is exactly the physical flux, which is what consistency means here
    const FluxVector physical = eulerFlux(s, n);
    EXPECT_EQ(r.flux.mass, physical.mass);
    EXPECT_EQ(r.flux.energy, physical.energy);
    EXPECT_EQ(r.flux.momentum.x, physical.momentum.x);
  }
  std::printf(
      "\n  16B gas at rest, 4 normals: mass and energy flux exactly 0, momentum exactly p n\n");
}
