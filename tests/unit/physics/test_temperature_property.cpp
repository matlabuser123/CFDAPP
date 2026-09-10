// P3-PHYS-003: temperature-dependent property models -- Constant/
// Linear/Tabulated, plus evaluatePropertyField's own positivity/size
// validation.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/thermal/ThermalProperties.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::ConstantProperty;
using cfd::physics::evaluatePropertyField;
using cfd::physics::LinearProperty;
using cfd::physics::TabulatedProperty;
using cfd::thermal::ThermalProperties;

// --- ConstantProperty --------------------------------------------------

TEST(ConstantPropertyTest, ReturnsTheSameValueForAnyTemperature) {
  const ConstantProperty property(0.6);
  EXPECT_DOUBLE_EQ(property.value(200.0), 0.6);
  EXPECT_DOUBLE_EQ(property.value(300.0), 0.6);
  EXPECT_DOUBLE_EQ(property.value(1000.0), 0.6);
}

TEST(ConstantPropertyTest, RejectsNonFiniteReferenceValue) {
  const Real nanValue = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(ConstantProperty{nanValue}, InvalidArgumentError);
}

TEST(ConstantPropertyTest, RejectsNonFiniteTemperatureQuery) {
  const ConstantProperty property(0.6);
  EXPECT_THROW((void)property.value(std::numeric_limits<Real>::infinity()), InvalidArgumentError);
}

// --- LinearProperty ------------------------------------------------------

TEST(LinearPropertyTest, MatchesReferenceValueAtReferenceTemperature) {
  const LinearProperty property(0.001, 300.0, -2.0e-6);
  EXPECT_DOUBLE_EQ(property.value(300.0), 0.001);
}

TEST(LinearPropertyTest, MatchesHandWorkedValuesAboveAndBelowReference) {
  // P(T) = 0.001 + (-2e-6)*(T-300).
  const LinearProperty property(0.001, 300.0, -2.0e-6);
  // T=320: 0.001 + (-2e-6)*20 = 0.001 - 4e-5 = 0.00096.
  EXPECT_NEAR(property.value(320.0), 0.00096, 1e-12);
  // T=280: 0.001 + (-2e-6)*(-20) = 0.001 + 4e-5 = 0.00104.
  EXPECT_NEAR(property.value(280.0), 0.00104, 1e-12);
}

TEST(LinearPropertyTest, RejectsNonFiniteConstructorArguments) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(LinearProperty(nan, 300.0, 1.0), InvalidArgumentError);
  EXPECT_THROW(LinearProperty(1.0, nan, 1.0), InvalidArgumentError);
  EXPECT_THROW(LinearProperty(1.0, 300.0, nan), InvalidArgumentError);
}

TEST(LinearPropertyTest, AcceptsZeroSlopeAsAnEquivalentOfConstant) {
  const LinearProperty property(0.6, 300.0, 0.0);
  EXPECT_DOUBLE_EQ(property.value(200.0), 0.6);
  EXPECT_DOUBLE_EQ(property.value(1000.0), 0.6);
}

// --- TabulatedProperty -----------------------------------------------------

TEST(TabulatedPropertyTest, RecoversExactTableNodes) {
  const TabulatedProperty property({300.0, 320.0, 340.0}, {1.00e-3, 0.85e-3, 0.73e-3});
  EXPECT_DOUBLE_EQ(property.value(300.0), 1.00e-3);
  EXPECT_DOUBLE_EQ(property.value(320.0), 0.85e-3);
  EXPECT_DOUBLE_EQ(property.value(340.0), 0.73e-3);
}

TEST(TabulatedPropertyTest, MidpointInterpolationMatchesHandWorkedValue) {
  const TabulatedProperty property({300.0, 320.0}, {1.00e-3, 0.80e-3});
  // T=310: 1.00e-3 + (0.80e-3-1.00e-3)*(310-300)/(320-300)
  //       = 1.00e-3 + (-0.20e-3)*0.5 = 0.90e-3.
  EXPECT_NEAR(property.value(310.0), 0.90e-3, 1e-12);
}

TEST(TabulatedPropertyTest, InterpolatesWithinAnArbitraryBracket) {
  const TabulatedProperty property({300.0, 320.0, 340.0}, {1.00e-3, 0.85e-3, 0.73e-3});
  // T=330, between nodes 1 and 2: 0.85e-3 + (0.73e-3-0.85e-3)*(330-320)/(340-320)
  //       = 0.85e-3 + (-0.12e-3)*0.5 = 0.79e-3.
  EXPECT_NEAR(property.value(330.0), 0.79e-3, 1e-12);
}

TEST(TabulatedPropertyTest, ClampsBelowRangeToTheFirstValue) {
  const TabulatedProperty property({300.0, 320.0}, {1.00e-3, 0.80e-3});
  EXPECT_DOUBLE_EQ(property.value(250.0), 1.00e-3);
}

TEST(TabulatedPropertyTest, ClampsAboveRangeToTheLastValue) {
  const TabulatedProperty property({300.0, 320.0}, {1.00e-3, 0.80e-3});
  EXPECT_DOUBLE_EQ(property.value(400.0), 0.80e-3);
}

TEST(TabulatedPropertyTest, ReportsTableRange) {
  const TabulatedProperty property({300.0, 320.0, 340.0}, {1.0, 2.0, 3.0});
  EXPECT_DOUBLE_EQ(property.tableMinTemperature(), 300.0);
  EXPECT_DOUBLE_EQ(property.tableMaxTemperature(), 340.0);
}

TEST(TabulatedPropertyTest, RejectsFewerThanTwoPoints) {
  EXPECT_THROW(TabulatedProperty({300.0}, {1.0}), InvalidArgumentError);
  EXPECT_THROW(TabulatedProperty({}, {}), InvalidArgumentError);
}

TEST(TabulatedPropertyTest, RejectsMismatchedLengths) {
  EXPECT_THROW(TabulatedProperty({300.0, 320.0}, {1.0}), InvalidArgumentError);
}

TEST(TabulatedPropertyTest, RejectsNonIncreasingTemperatures) {
  EXPECT_THROW(TabulatedProperty({300.0, 300.0}, {1.0, 2.0}), InvalidArgumentError)
      << "duplicate temperature";
  EXPECT_THROW(TabulatedProperty({320.0, 300.0}, {1.0, 2.0}), InvalidArgumentError)
      << "decreasing temperature";
}

TEST(TabulatedPropertyTest, RejectsNonFiniteEntries) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(TabulatedProperty({300.0, nan}, {1.0, 2.0}), InvalidArgumentError);
  EXPECT_THROW(TabulatedProperty({300.0, 320.0}, {1.0, nan}), InvalidArgumentError);
}

// --- evaluatePropertyField -------------------------------------------------

TEST(EvaluatePropertyFieldTest, EvaluatesEveryCellFromItsOwnTemperature) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const LinearProperty property(0.6, 300.0, 0.001);
  ScalarField temperature(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    temperature[i] = 300.0 + static_cast<Real>(i) * 10.0;
  }
  const ScalarField field = evaluatePropertyField(mesh, temperature, property);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(field[i], property.value(temperature[i])) << "cell " << i;
  }
}

TEST(EvaluatePropertyFieldTest, ConstantPropertyGivesAUniformField) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const ConstantProperty property(0.6);
  const ScalarField temperature(mesh.numberOfCells(), 350.0);
  const ScalarField field = evaluatePropertyField(mesh, temperature, property);
  for (Index i = 0; i < field.size(); ++i) {
    EXPECT_DOUBLE_EQ(field[i], 0.6) << "cell " << i;
  }
}

TEST(EvaluatePropertyFieldTest, RejectsNonPositiveResultWhenRequired) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  // Steep negative slope: at a high enough temperature this predicts a
  // non-positive viscosity -- must be rejected at evaluation, not
  // silently clamped (P3-PHYS-003 section 5).
  const LinearProperty property(0.001, 300.0, -1.0);
  ScalarField temperature(mesh.numberOfCells(), 300.0);
  temperature[3] = 300.005;  // pushes cell 3's value below zero.
  EXPECT_THROW((void)evaluatePropertyField(mesh, temperature, property), InvalidArgumentError);
}

TEST(EvaluatePropertyFieldTest, AllowsNonPositiveResultWhenNotRequired) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const LinearProperty property(0.001, 300.0, -1.0);
  ScalarField temperature(mesh.numberOfCells(), 300.0);
  temperature[3] = 300.005;
  EXPECT_NO_THROW((void)evaluatePropertyField(mesh, temperature, property,
                                              /*requirePositive=*/false));
}

TEST(EvaluatePropertyFieldTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const ConstantProperty property(0.6);
  const ScalarField temperature(mesh.numberOfCells() + 1, 300.0);
  EXPECT_THROW((void)evaluatePropertyField(mesh, temperature, property), InvalidArgumentError);
}

// --- Density(T) scope demonstration (P3-PHYS-003 section 11) --------------
//
// Density is deliberately never wired into continuity or into a per-cell
// field consumed by any transport-equation assembly in this codebase
// (ThermalProperties.hpp's own header comment: rho enters the energy
// equation only pre-baked into massFlux, and momentum's diffusion/
// convection contributions never take a density argument at all -- see
// physics::assembleMomentum's header comment). The one place this
// codebase's own equations *do* accept a bare density value is
// ThermalProperties::thermalDiffusivity(density) -- these tests
// demonstrate the exact same TemperatureProperty machinery used for
// mu(T)/k(T)/cp(T) above evaluating rho(T) instead, and feeding each
// per-cell value into that one existing consumer, never into
// div(rho*U) or d(rho)/dt (which this codebase's incompressible
// continuity equation still does not have, and this task does not add).
TEST(DensityPropertyDemonstrationTest, LinearDensityModelFeedsThermalDiffusivityPerCell) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  // Boussinesq-like mild density variation: rho(T) = 1000*(1 - 3e-4*(T-300)).
  const LinearProperty densityModel(1000.0, 300.0, -0.3);
  ScalarField temperature(mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    temperature[i] = 290.0 + static_cast<Real>(i) * 5.0;  // 290..330K across 9 cells.
  }
  const ScalarField densityField = evaluatePropertyField(mesh, temperature, densityModel);

  const ThermalProperties thermal(/*conductivity=*/0.6, /*specificHeat=*/4180.0);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    const Real expectedDensity = densityModel.value(temperature[i]);
    ASSERT_DOUBLE_EQ(densityField[i], expectedDensity);
    const Real alpha = thermal.thermalDiffusivity(densityField[i]);
    // alpha = k / (rho*cp): must be finite, positive, and vary inversely
    // with the per-cell density -- i.e. genuinely driven by rho(T), not a
    // constant baked in once.
    EXPECT_TRUE(std::isfinite(alpha));
    EXPECT_GT(alpha, 0.0);
    EXPECT_NEAR(alpha, 0.6 / (expectedDensity * 4180.0), 1e-15) << "cell " << i;
  }
  // Confirm the density field is genuinely non-uniform (rho(T) is doing
  // real work here, not silently collapsing to a constant) -- density
  // decreases with temperature (negative slope, thermal expansion), so
  // the coolest cell (index 0, T=290) is denser than the hottest (index
  // 8, T=330).
  EXPECT_GT(densityField[0], densityField[mesh.numberOfCells() - 1]);
}
