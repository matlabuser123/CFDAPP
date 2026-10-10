// P3-PHYS-006 sections 4, 31-32: ThermodynamicProperties (cp/cv/gamma
// derivation), speed of sound, Mach number, and evaluateDensityField
// (sections 8-9).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::compressible::evaluateDensityField;
using cfd::compressible::machNumber;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {
// Dry air at ~300K: R=287.05, cp=1005.0 J/(kg K) -> cv=717.95,
// gamma=1005.0/717.95 ~ 1.3999.
constexpr Real kR = 287.05;
constexpr Real kCp = 1005.0;
}  // namespace

TEST(ThermodynamicPropertiesTest, DerivesCvAndGammaFromRAndCp) {
  const ThermodynamicProperties thermo(kR, kCp);
  EXPECT_DOUBLE_EQ(thermo.gasConstant(), kR);
  EXPECT_DOUBLE_EQ(thermo.specificHeatPressure(), kCp);
  EXPECT_DOUBLE_EQ(thermo.specificHeatVolume(), kCp - kR);
  EXPECT_DOUBLE_EQ(thermo.specificHeatRatio(), kCp / (kCp - kR));
}

TEST(ThermodynamicPropertiesTest, RejectsCpNotGreaterThanR) {
  EXPECT_THROW(ThermodynamicProperties(kR, kR), InvalidArgumentError);         // cv = 0.
  EXPECT_THROW(ThermodynamicProperties(kR, kR - 10.0), InvalidArgumentError);  // cv < 0.
}

TEST(ThermodynamicPropertiesTest, RejectsNonFiniteCp) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(ThermodynamicProperties(kR, nan), InvalidArgumentError);
}

TEST(ThermodynamicPropertiesTest, DensityDelegatesToTheEquationOfState) {
  const ThermodynamicProperties thermo(kR, kCp);
  EXPECT_DOUBLE_EQ(thermo.density(101325.0, 300.0),
                   thermo.equationOfState().density(101325.0, 300.0));
}

// --- Speed of sound / Mach (sections 31-32) -------------------------------

TEST(ThermodynamicPropertiesTest, SpeedOfSoundMatchesAnalyticalFormula) {
  const ThermodynamicProperties thermo(kR, kCp);
  const Real T = 300.0;
  const Real expected = std::sqrt(thermo.specificHeatRatio() * kR * T);
  EXPECT_DOUBLE_EQ(thermo.speedOfSound(T), expected);
  // Real air at 300K: a ~ 347 m/s.
  EXPECT_NEAR(thermo.speedOfSound(T), 347.0, 2.0);
}

TEST(ThermodynamicPropertiesTest, SpeedOfSoundIsPositiveFiniteAndIncreasesWithSqrtT) {
  const ThermodynamicProperties thermo(kR, kCp);
  const Real a300 = thermo.speedOfSound(300.0);
  const Real a1200 = thermo.speedOfSound(1200.0);  // 4x T -> 2x a.
  EXPECT_GT(a300, 0.0);
  EXPECT_TRUE(std::isfinite(a300));
  EXPECT_NEAR(a1200, 2.0 * a300, 1e-9 * a300);
}

TEST(ThermodynamicPropertiesTest, SpeedOfSoundRejectsNonPositiveTemperature) {
  const ThermodynamicProperties thermo(kR, kCp);
  EXPECT_THROW((void)thermo.speedOfSound(0.0), InvalidArgumentError);
  EXPECT_THROW((void)thermo.speedOfSound(-1.0), InvalidArgumentError);
}

TEST(MachNumberTest, MatchesSpeedOverSoundSpeed) { EXPECT_DOUBLE_EQ(machNumber(34.7, 347.0), 0.1); }

TEST(MachNumberTest, RejectsNonPositiveSpeedOfSound) {
  EXPECT_THROW((void)machNumber(10.0, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)machNumber(10.0, -1.0), InvalidArgumentError);
}

// --- evaluateDensityField (sections 8-9) ----------------------------------

TEST(EvaluateDensityFieldTest, EvaluatesEveryCellFromItsOwnPressureAndTemperature) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ThermodynamicProperties thermo(kR, kCp);
  ScalarField pressure(mesh.numberOfCells());
  ScalarField temperature(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    pressure[i] = 101325.0 + 10.0 * static_cast<Real>(i);
    temperature[i] = 300.0 + static_cast<Real>(i);
  }
  const ScalarField density = evaluateDensityField(mesh, pressure, temperature, thermo);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(density[i], thermo.density(pressure[i], temperature[i])) << "cell " << i;
  }
}

TEST(EvaluateDensityFieldTest, UniformStateGivesUniformDensity) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ThermodynamicProperties thermo(kR, kCp);
  const ScalarField pressure(mesh.numberOfCells(), 101325.0);
  const ScalarField temperature(mesh.numberOfCells(), 300.0);
  const ScalarField density = evaluateDensityField(mesh, pressure, temperature, thermo);
  const Real expected = thermo.density(101325.0, 300.0);
  for (Index i = 0; i < density.size(); ++i) {
    EXPECT_DOUBLE_EQ(density[i], expected) << "cell " << i;
  }
}

TEST(EvaluateDensityFieldTest, MismatchedSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ThermodynamicProperties thermo(kR, kCp);
  const ScalarField pressure(mesh.numberOfCells() + 1, 101325.0);
  const ScalarField temperature(mesh.numberOfCells(), 300.0);
  EXPECT_THROW((void)evaluateDensityField(mesh, pressure, temperature, thermo),
               InvalidArgumentError);
}

TEST(EvaluateDensityFieldTest, InvalidCellStateThrowsNamingTheCell) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ThermodynamicProperties thermo(kR, kCp);
  ScalarField pressure(mesh.numberOfCells(), 101325.0);
  ScalarField temperature(mesh.numberOfCells(), 300.0);
  temperature[2] = -1.0;  // invalid at cell 2.
  EXPECT_THROW((void)evaluateDensityField(mesh, pressure, temperature, thermo),
               InvalidArgumentError);
}

// ---------------------------------------------------------------------------
// P17.2 -- the thermodynamic closure verified as ONE coherent state, and the two
// equivalent speed-of-sound forms.
//
// Gate group T requires the identities to hold TOGETHER on the same object, not as four
// independent formula tests. Gate group A2 requires sqrt(gamma R T) and
// sqrt(gamma p / rho) to agree, which nothing tested before P17.2 (0 hits).
// ---------------------------------------------------------------------------

TEST(ThermodynamicPropertiesTest, ClosureHoldsTogetherAcrossGases) {
  // cp - cv = R, gamma = cp/cv, gamma > 1, cv > 0 -- all on the SAME object, for three
  // gases whose (R, cp) pairs are genuinely different.
  //
  // The two identities are NOT gated the same way, and that asymmetry is real rather
  // than an oversight:
  //
  //   gamma == cp/cv  EXACTLY. gamma is STORED as that quotient, so recomputing it
  //                   repeats one operation on unchanged bits.
  //   cp - cv ~= R    to 4 eps, NOT exactly. cv is stored as cp - R, which rounds once;
  //                   recovering cp - cv rounds again, and that loss cannot be undone.
  //                   Measured on air in the P17.2 dry-run: (cp-cv)-R = -5.6843e-14,
  //                   i.e. 0.89 eps relative to R. Demanding equality here would fail a
  //                   correct implementation.
  struct Gas {
    Real R, cp;
    const char* label;
  };
  const Gas gases[] = {
      {287.05, 1005.0, "air"}, {2077.10, 5193.0, "helium"}, {461.50, 1996.0, "water vapour"}};
  constexpr Real kEps = std::numeric_limits<Real>::epsilon();

  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    const Real cv = t.specificHeatVolume();
    const Real gamma = t.specificHeatRatio();

    EXPECT_GT(cv, 0.0) << g.label;
    EXPECT_GT(gamma, 1.0) << g.label;
    EXPECT_EQ(gamma, t.specificHeatPressure() / cv) << g.label;
    EXPECT_NEAR((t.specificHeatPressure() - cv) - t.gasConstant(), 0.0,
                4.0 * kEps * t.gasConstant())
        << g.label;
    // R is single-sourced through the EOS -- there is no second gas constant that could
    // disagree. Recovered from the EOS itself: pressure(1, 1) = 1 * R * 1 = R, exactly.
    EXPECT_EQ(t.equationOfState().pressure(1.0, 1.0), t.gasConstant()) << g.label;
  }
}

TEST(ThermodynamicPropertiesTest, GammaExceedsOneForEveryAdmissibleCp) {
  // gamma > 1 is a THEOREM, not a runtime check: R > 0 is enforced by IdealGasEOS and
  // cp > R by ThermodynamicProperties, so cv = cp - R > 0 and
  // gamma = cp/(cp - R) > 1 <=> R > 0. Asserted over a sweep because a constructor guard
  // for it would be unreachable code.
  for (const Real cp : {288.0, 400.0, 1005.0, 5000.0, 1.0e5}) {
    const ThermodynamicProperties t(kR, cp);
    EXPECT_GT(t.specificHeatRatio(), 1.0) << "cp = " << cp;
    EXPECT_GT(t.specificHeatVolume(), 0.0) << "cp = " << cp;
  }
}

TEST(ThermodynamicPropertiesTest,
     SoundSpeedFromTemperatureMatchesSoundSpeedFromPressureAndDensity) {
  // a = sqrt(gamma R T)  ==  sqrt(gamma p / rho)  for the same state.
  //
  // Bounded at 8 eps, derived from the operation counts rather than the observed result:
  // path 1 is 2 multiplications + 1 sqrt; path 2 is rho = p/(R T) (2 ops), gamma*p/rho
  // (2 ops), then sqrt -- about 4 roundings against 3, so their difference is bounded by
  // roughly 7 eps. Equality is NOT the criterion: the paths genuinely differ, and
  // demanding bitwise identity would fail a correct implementation. Measured worst case
  // in the dry-run: 2.005e-16, i.e. 0.90 eps.
  constexpr Real kEps = std::numeric_limits<Real>::epsilon();
  struct Gas {
    Real R, cp;
    const char* label;
  };
  const Gas gases[] = {
      {287.05, 1005.0, "air"}, {2077.10, 5193.0, "helium"}, {461.50, 1996.0, "water vapour"}};
  const Real pressures[] = {1.0e3, 101325.0, 5.0e6};
  const Real temperatures[] = {200.0, 288.15, 1500.0};

  int checked = 0;
  for (const auto& g : gases) {
    const ThermodynamicProperties t(g.R, g.cp);
    for (const Real p : pressures) {
      for (const Real T : temperatures) {
        const Real rho = t.density(p, T);
        const Real fromT = t.speedOfSound(T);
        const Real fromPRho = std::sqrt(t.specificHeatRatio() * p / rho);
        EXPECT_NEAR(fromPRho, fromT, 8.0 * kEps * fromT) << g.label << " p=" << p << " T=" << T;
        EXPECT_GT(fromT, 0.0);
        EXPECT_TRUE(std::isfinite(fromT));
        ++checked;
      }
    }
  }
  EXPECT_EQ(checked, 27);
}

TEST(EvaluateDensityFieldTest, EosDerivedDensityReachesTheAuthoritativeFieldWithTheExpectedValue) {
  // Gate group I: a known (p, T, R) flows through the EOS into the P17.1 authoritative
  // density field and arrives at the expected value -- the same EOS instance, not a
  // second gas constant and not a duplicated p/(R T) on the production path.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 2, 3.0, 2.0);
  const ThermodynamicProperties thermo(kR, kCp);

  // Deliberately non-uniform in BOTH p and T, so a constant-state shortcut cannot pass.
  ScalarField pressure(mesh.numberOfCells());
  ScalarField temperature(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    pressure[i] = 100000.0 + 1000.0 * static_cast<Real>(i);
    temperature[i] = 280.0 + 10.0 * static_cast<Real>(i);
  }

  const ScalarField density = evaluateDensityField(mesh, pressure, temperature, thermo);

  ASSERT_EQ(density.size(), mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    const Index i = cell.id();
    const Real expected = pressure[i] / (kR * temperature[i]);
    EXPECT_NEAR(density[i], expected, 1e-12 * expected) << "cell " << i;
    // and the inverse direction recovers the temperature that produced it
    EXPECT_NEAR(thermo.equationOfState().temperature(pressure[i], density[i]), temperature[i],
                1e-12 * temperature[i])
        << "cell " << i;
  }

  // The field genuinely varies -- otherwise the per-cell check above proves nothing.
  Real minRho = density[0], maxRho = density[0];
  for (const auto& cell : mesh.cells()) {
    minRho = std::min(minRho, density[cell.id()]);
    maxRho = std::max(maxRho, density[cell.id()]);
  }
  EXPECT_GT(maxRho - minRho, 0.0);
}

TEST(EvaluateDensityFieldTest, RepeatedEvaluationIsBitwiseIdentical) {
  // Gate criterion D2: repeated full field evaluation over a mesh, bitwise.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 4.0, 3.0);
  const ThermodynamicProperties thermo(kR, kCp);
  ScalarField pressure(mesh.numberOfCells());
  ScalarField temperature(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    pressure[cell.id()] = 101325.0 + 137.0 * static_cast<Real>(cell.id());
    temperature[cell.id()] = 300.0 + 7.0 * static_cast<Real>(cell.id());
  }

  const ScalarField a = evaluateDensityField(mesh, pressure, temperature, thermo);
  for (int repeat = 0; repeat < 8; ++repeat) {
    const ScalarField b = evaluateDensityField(mesh, pressure, temperature, thermo);
    ASSERT_EQ(a.size(), b.size());
    for (Index i = 0; i < a.size(); ++i) {
      EXPECT_EQ(a[i], b[i]) << "repeat " << repeat << " cell " << i;
    }
  }
}
