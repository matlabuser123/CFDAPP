// P16b attempt 4 -- Spalding's composite law of the wall.
//
// WHY THIS REPLACES THE BLEND
// ---------------------------
// The blended treatment computed two friction velocities independently and
// combined them with a 4th-power law. The viscous branch sqrt(nu U_P/y_P)
// presumes a linear profile all the way from the wall to the cell centre,
// which is false through the buffer layer, so it stayed at 0.6-0.8 of the log
// branch where it should have decayed. Measured consequence, from the one-cell
// diagnostic:
//
//   y+     tau_blend / tau_LaunderSpalding
//   20             1.19
//   30             1.10
//   50             1.045
//   100            1.014
//
// and that is what broke the Re_tau = 590 channel gate (E_Re_tau 0.0698 at
// ny16 against an adopted 0.059).
//
// A composite law has no crossover weighting to get wrong: it has the right
// asymptotes by construction. These tests PROVE both asymptotes numerically
// rather than asserting them in a comment, and prove that the constants are
// this repository's own rather than Spalding's original 5.0/9.0.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
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

}  // namespace

// --- the two asymptotes, proved numerically ---------------------------------

TEST(SpaldingWallLaw, ApproachesTheLinearSublayerAtLowYPlus) {
  // u+ -> y+ as u+ -> 0. Asserted as a LIMIT: the relative departure must
  // shrink as u+ shrinks, and reach round-off.
  Real previous = 1.0;
  for (const Real uPlus : {2.0, 1.0, 0.5, 0.2, 0.1, 0.01}) {
    const Real yPlus = cfd::turbulence::spaldingYPlus(uPlus, kCoefficients);
    const Real departure = (yPlus - uPlus) / uPlus;
    EXPECT_GE(departure, 0.0) << "y+ must never fall below u+ -- the bracketed tail is a sum "
                                 "of non-negative terms, and the solver's lower bound relies "
                                 "on exactly that";
    EXPECT_LT(departure, previous)
        << "the departure from u+ = y+ is not shrinking at u+ = " << uPlus;
    previous = departure;
  }
  EXPECT_LT(previous, 1e-12) << "u+ = y+ is not recovered in the limit";
}

TEST(SpaldingWallLaw, ApproachesThisRepositorysLogLawAtHighYPlus) {
  // u+ -> ln(E y+)/kappa, with THIS repository's E and kappa -- not Spalding's
  // original B = 5.0. If someone changes E, this test moves with it.
  // MEASURED, then stated -- the first version of this test asserted monotone
  // convergence to 1e-3 and both halves were wrong.
  //
  // Spalding's law subtracts only the first five Taylor terms, so what remains
  // is e^(kappa u+)/E PLUS the polynomial remainder. The remainder is dominated
  // by the exponential but does not vanish fast, and it leaves a residual of
  // about 0.4% at practical u+. Verified independently by hand at u+ = 20:
  //   e^8.2 = 3640.95, P4(8.2) = 323.06, y+ = 20 + (3640.95-323.06)/9.793
  //         = 358.8; ln(E*358.8)/kappa = 19.913; departure = 0.0043.
  // That is a property of the law, not a defect in this implementation, and it
  // is an order of magnitude inside the adopted channel gate.
  std::vector<Real> departures;
  for (const Real uPlus : {5.0, 10.0, 14.0, 16.0, 18.0, 20.0, 22.0}) {
    const Real yPlus = cfd::turbulence::spaldingYPlus(uPlus, kCoefficients);
    const Real logLaw = cfd::turbulence::uPlusLogLaw(yPlus, kCoefficients);
    const Real departure = std::abs(uPlus - logLaw) / uPlus;
    std::printf("u+ = %5.1f -> y+ = %12.4e, log-law u+ = %7.4f, departure %8.5f\n", uPlus, yPlus,
                logLaw, departure);
    departures.push_back(departure);
  }
  // It must COLLAPSE from the buffer layer into the log layer -- that is the
  // asymptote claim, and it is a large effect.
  // The collapse is three orders of magnitude between u+ = 5 and u+ = 16
  // (0.9025 -> 0.00073). That is the asymptote claim, and it is enormous.
  EXPECT_LT(departures[3], 0.01 * departures[0])
      << "the departure from the log law does not collapse as u+ grows";

  // WHERE the asymptote is reached is itself a finding worth pinning, because
  // this repository's channel meshes live at y+ = 18-95:
  //   u+ = 14 (y+ ~ 36) : 2.0%   -- NOT yet on the log law
  //   u+ = 16 (y+ ~ 72) : 0.07%
  //   u+ = 20 (y+ ~ 359): 0.43%  -- the truncation residual
  // So Spalding only becomes the log law for y+ >~ 70, and a mesh at y+ = 37
  // sits in the part of the composite law that is neither branch. That is a
  // property of the law, stated rather than smoothed over.
  EXPECT_GT(departures[2], 0.01) << "y+ ~ 36 is expected to be OFF the log law; if this now "
                                    "passes, the law or its constants changed";
  for (std::size_t i = 3; i < departures.size(); ++i) {
    EXPECT_LT(departures[i], 0.01) << "beyond y+ ~ 70 the log-layer departure is not bounded at 1%";
  }
}

TEST(SpaldingWallLaw, CarriesTheRepositoryConstantsAndIntroducesNoneOfItsOwn) {
  // e^(-kappa B) = 1/E exactly, because B = ln(E)/kappa. This is the identity
  // that lets the composite law reuse CFDApp's constants verbatim, so it is
  // checked rather than trusted.
  const Real intercept = cfd::turbulence::logLawIntercept(kCoefficients);
  EXPECT_NEAR(std::exp(-kCoefficients.kappa * intercept), 1.0 / kCoefficients.E, 1e-15);

  // And a different E must actually change the law, or the constants are not
  // being carried at all.
  const WallFunctionCoefficients other{0.41, 8.0, 0.09};
  EXPECT_NE(cfd::turbulence::spaldingYPlus(15.0, kCoefficients),
            cfd::turbulence::spaldingYPlus(15.0, other));
}

// --- the implicit solve ------------------------------------------------------

TEST(SpaldingWallLaw, SolvesItsOwnLawToRoundOff) {
  // The inverse must actually invert: substituting the returned u_tau back into
  // the law must reproduce y+ = u_tau y/nu.
  for (const Real targetYPlus : {1.0, 5.0, 10.0, 20.0, 30.0, 50.0, 100.0, 300.0}) {
    // Build a cell that sits at this y+ under the composite law itself.
    const Real uTau = 0.05;
    const Real y = targetYPlus * kViscosity / uTau;
    const Real uPlus = [&] {
      // invert numerically for the u+ that gives this y+
      Real lo = 0.0;
      Real hi = 100.0;
      for (int i = 0; i < 200; ++i) {
        const Real mid = 0.5 * (lo + hi);
        if (cfd::turbulence::spaldingYPlus(mid, kCoefficients) < targetYPlus) {
          lo = mid;
        } else {
          hi = mid;
        }
      }
      return 0.5 * (lo + hi);
    }();
    const Real u = uTau * uPlus;

    const Real solved = cfd::turbulence::spaldingFrictionVelocity(u, y, kViscosity, kCoefficients);
    EXPECT_NEAR(solved, uTau, 1e-10 * uTau) << "at y+ = " << targetYPlus;

    const Real residual =
        cfd::turbulence::spaldingYPlus(u / solved, kCoefficients) - (solved * y / kViscosity);
    EXPECT_LT(std::abs(residual), 1e-9 * targetYPlus) << "at y+ = " << targetYPlus;
  }
}

TEST(SpaldingWallLaw, TheViscousValueIsARigorousLowerBound) {
  // The solver's bracket depends on this, so it is proved rather than assumed:
  // y+ >= u+ implies u_tau >= sqrt(nu |U|/y) for every input.
  for (const Real u : {1e-6, 1e-3, 0.1, 1.0, 10.0}) {
    for (const Real y : {1e-6, 1e-4, 1e-2}) {
      const Real solved =
          cfd::turbulence::spaldingFrictionVelocity(u, y, kViscosity, kCoefficients);
      const Real viscous = std::sqrt(kViscosity * u / y);
      EXPECT_GE(solved, viscous * (1.0 - 1e-12))
          << "u = " << u << ", y = " << y << ": below the viscous lower bound";
    }
  }
}

TEST(SpaldingWallLaw, IsDefinedAndZeroAtSeparation) {
  // The property the whole automatic treatment exists for, now obtained with no
  // blending at all.
  EXPECT_EQ(cfd::turbulence::spaldingFrictionVelocity(0.0, 1e-4, kViscosity, kCoefficients), 0.0);

  // And it must APPROACH that continuously from either side, with no step.
  Real previous = 0.0;
  for (const Real u : {1e-9, 1e-7, 1e-5, 1e-3, 1e-1}) {
    const Real solved =
        cfd::turbulence::spaldingFrictionVelocity(u, 1e-4, kViscosity, kCoefficients);
    EXPECT_GT(solved, previous) << "not monotone approaching separation at U = " << u;
    EXPECT_TRUE(std::isfinite(solved));
    previous = solved;
  }
  // The sign of the shear is the caller's to apply; the magnitude is symmetric.
  EXPECT_EQ(cfd::turbulence::spaldingFrictionVelocity(-0.3, 1e-4, kViscosity, kCoefficients),
            cfd::turbulence::spaldingFrictionVelocity(0.3, 1e-4, kViscosity, kCoefficients));
}

TEST(SpaldingWallLaw, IsMonotoneAndDeterministic) {
  Real previous = 0.0;
  for (const Real u : {0.01, 0.05, 0.1, 0.5, 1.0, 5.0}) {
    const Real solved =
        cfd::turbulence::spaldingFrictionVelocity(u, 1e-4, kViscosity, kCoefficients);
    EXPECT_GT(solved, previous) << "u_tau is not increasing with U at U = " << u;
    previous = solved;
    EXPECT_EQ(solved,
              cfd::turbulence::spaldingFrictionVelocity(u, 1e-4, kViscosity, kCoefficients));
  }
}

TEST(SpaldingWallLaw, RejectsNonPhysicalArguments) {
  EXPECT_THROW((void)cfd::turbulence::spaldingYPlus(-1.0, kCoefficients), InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::spaldingFrictionVelocity(1.0, 0.0, kViscosity, kCoefficients),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::spaldingFrictionVelocity(1.0, 1e-4, 0.0, kCoefficients),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::spaldingFrictionVelocity(
                   std::numeric_limits<Real>::quiet_NaN(), 1e-4, kViscosity, kCoefficients),
               InvalidArgumentError);
}

// --- THE COMPARISON THE DECISION RESTS ON ------------------------------------

TEST(SpaldingWallLaw, CrossoverOvershootIsGoneComparedWithTheBlend) {
  // The table requested at the checkpoint, and the criterion the composite law
  // has to meet: approach the viscous shear near the wall, approach the
  // validated Launder-Spalding shear in the log region, and NOT reproduce the
  // +10-20% crossover overshoot the blend produced.
  const Real uTau = 0.05;
  const Real nu = kViscosity / kDensity;
  const Real kEquilibrium = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);

  std::printf("%6s %12s %12s %12s %12s %10s %10s %10s\n", "y+", "tau_blend", "tau_highRe",
              "tau_composite", "tau_viscous", "comp/hiRe", "comp/visc", "blend/hiRe");

  std::vector<Real> compositeOverHighRe;  // retained for the printout only
  for (const Real target : {1.0, 5.0, 10.0, 20.0, 30.0, 50.0, 100.0, 300.0}) {
    const Real y = target * nu / uTau;
    const Real u =
        target <= 5.0 ? uTau * target : uTau * cfd::turbulence::uPlusLogLaw(target, kCoefficients);

    const auto blended = cfd::turbulence::automaticWallState(u, kEquilibrium, y, kDensity,
                                                             kViscosity, 0.075, kCoefficients);
    const Real compositeUTau = cfd::turbulence::spaldingFrictionVelocity(u, y, nu, kCoefficients);
    const Real tauComposite = kDensity * compositeUTau * compositeUTau;
    const Real tauViscous = kViscosity * u / y;

    Real tauHighRe = 0.0;
    bool highReDefined = true;
    try {
      tauHighRe = cfd::turbulence::wallShearStressLaunderSpalding(u, kEquilibrium, y, kDensity,
                                                                  kViscosity, kCoefficients);
    } catch (const InvalidArgumentError&) {
      highReDefined = false;
    }

    std::printf(
        "%6.1f %12.4e %12s %12.4e %12.4e %10s %10.4f %10s\n", target, blended.wallShearStress,
        highReDefined ? std::to_string(tauHighRe).substr(0, 11).c_str() : "REFUSED", tauComposite,
        tauViscous,
        highReDefined ? std::to_string(tauComposite / tauHighRe).substr(0, 9).c_str() : "-",
        tauComposite / tauViscous,
        highReDefined ? std::to_string(blended.wallShearStress / tauHighRe).substr(0, 9).c_str()
                      : "-");
    (void)compositeOverHighRe;
  }

  // 1. Near the wall the composite law IS the viscous shear.
  {
    const Real y = 1.0 * nu / uTau;
    const Real u = uTau * 1.0;
    const Real uT = cfd::turbulence::spaldingFrictionVelocity(u, y, nu, kCoefficients);
    EXPECT_NEAR(kDensity * uT * uT, kViscosity * u / y, 0.02 * kViscosity * u / y)
        << "at y+ = 1 the composite law is not the linear-sublayer shear";
  }

  // 2. THROUGH THE CROSSOVER the composite law must be strictly closer to the
  //    validated wall shear than the blend was. This is a comparative claim
  //    against preserved measurements, so it introduces no threshold at all.
  //
  //    It is asserted over y+ = 20..100 only, and the two ends are excluded for
  //    stated reasons rather than convenience:
  //      * below y+ = 20 the REFERENCE is invalid -- Launder-Spalding is being
  //        fed a log-law velocity at a y+ where the log law does not hold, so
  //        neither treatment is being compared against a meaningful target;
  //      * beyond y+ ~ 100 both are inside 1% and Spalding's truncation
  //        residual (see the asymptote test) makes the blend nominally closer,
  //        which is real and is reported in the table above rather than hidden.
  const Real nuLocal = kViscosity / kDensity;
  for (const Real target : {20.0, 30.0, 50.0, 100.0}) {
    const Real y = target * nuLocal / uTau;
    const Real u = uTau * cfd::turbulence::uPlusLogLaw(target, kCoefficients);
    const auto blend = cfd::turbulence::automaticWallState(u, kEquilibrium, y, kDensity, kViscosity,
                                                           0.075, kCoefficients);
    const Real uT = cfd::turbulence::spaldingFrictionVelocity(u, y, nuLocal, kCoefficients);
    const Real validated = cfd::turbulence::wallShearStressLaunderSpalding(
        u, kEquilibrium, y, kDensity, kViscosity, kCoefficients);
    EXPECT_LT(std::abs((kDensity * uT * uT / validated) - 1.0),
              std::abs((blend.wallShearStress / validated) - 1.0))
        << "at y+ = " << target << " the composite law is not closer to the validated wall "
        << "shear than the blend it replaces";
  }

  // 3. And the crossover overshoot must be materially smaller than the blend's
  //    +19% at y+ = 20, which is the whole reason for this attempt.
  {
    const Real y = 20.0 * nu / uTau;
    const Real u = uTau * cfd::turbulence::uPlusLogLaw(20.0, kCoefficients);
    const Real uT = cfd::turbulence::spaldingFrictionVelocity(u, y, nu, kCoefficients);
    const Real tauHighRe = cfd::turbulence::wallShearStressLaunderSpalding(
        u, kEquilibrium, y, kDensity, kViscosity, kCoefficients);
    const Real ratio = (kDensity * uT * uT) / tauHighRe;
    std::printf("y+ = 20 crossover: composite/highRe = %.4f (blend was 1.19)\n", ratio);
    EXPECT_LT(std::abs(ratio - 1.0), 0.19)
        << "the composite law is no better than the blend at the crossover";
  }
}
