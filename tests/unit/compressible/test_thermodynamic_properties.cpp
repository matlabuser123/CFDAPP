// P3-PHYS-006 sections 4, 31-32: ThermodynamicProperties (cp/cv/gamma
// derivation), speed of sound, Mach number, and evaluateDensityField
// (sections 8-9).
#include <gtest/gtest.h>

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
