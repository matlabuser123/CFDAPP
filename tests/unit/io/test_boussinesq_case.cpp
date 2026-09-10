// P3-PHYS-001: case-system Boussinesq buoyancy configuration. Mirrors
// test_thermal_case.cpp's own structure/conventions.
#include <gtest/gtest.h>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"

using cfd::CaseConfigurationError;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::testutil::CaseFixture;

namespace {

void expectRejected(const CaseFixture& fixture) {
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), CaseConfigurationError);
}

constexpr const char* kThermalOnlyPhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
        "thermal": {"conductivity": 0.6, "specific_heat": 4180.0, "initial_temperature": 300.0}})";

constexpr const char* kThermalPlusBuoyancyPhysics =
    R"({"model": "incompressible_laminar", "density": 1.2, "dynamic_viscosity": 0.01,
        "thermal": {"conductivity": 0.6, "specific_heat": 4180.0, "initial_temperature": 300.0},
        "buoyancy": {"model": "boussinesq", "beta": 0.0034, "reference_temperature": 300.0,
                     "gravity": [0.0, -9.81]}})";

// Thermal-enabled boundaries.json matching CaseFixture's default 4-patch
// (left/right/bottom/top) mesh -- same fixture test_thermal_case.cpp's
// own kThermalBoundaries uses, needed here too since enabling "thermal"
// requires every patch to configure a "temperature" key.
constexpr const char* kThermalBoundaries = R"({
  "patches": {
    "left":   {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "fixed_temperature", "value": 310.0}},
    "right":  {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "fixed_temperature", "value": 290.0}},
    "bottom": {"velocity": {"type": "wall"}, "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "adiabatic"}},
    "top":    {"velocity": {"type": "moving_wall", "value": [1.0, 0.0]},
               "pressure": {"type": "fixed_gradient", "value": 0.0},
               "temperature": {"type": "adiabatic"}}
  }
})";

}  // namespace

// --- Backward compatibility -------------------------------------------------

TEST(BoussinesqCaseTest, NoBuoyancyBlockHasNoBuoyancyConfig) {
  CaseFixture fixture;  // default: no "thermal"/"buoyancy" block.
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_FALSE(definition.physics.buoyancy.has_value());

  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.buoyancy.has_value());
}

TEST(BoussinesqCaseTest, ThermalWithoutBuoyancyHasNoBuoyancyConfig) {
  CaseFixture fixture;
  fixture.write("physics.json", kThermalOnlyPhysics);
  fixture.write("boundaries.json", kThermalBoundaries);
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_FALSE(definition.physics.buoyancy.has_value());

  const auto setup = CaseBuilder{}.build(definition);
  ASSERT_TRUE(setup.thermal.has_value());
  EXPECT_FALSE(setup.buoyancy.has_value());
}

// --- Success path ------------------------------------------------------------

TEST(BoussinesqCaseTest, ParsesBuoyancyConfigWhenEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kThermalPlusBuoyancyPhysics);
  fixture.write("boundaries.json", kThermalBoundaries);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.buoyancy.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.buoyancy->beta, 0.0034);
  EXPECT_DOUBLE_EQ(definition.physics.buoyancy->referenceTemperature, 300.0);
  EXPECT_DOUBLE_EQ(definition.physics.buoyancy->gravity.x, 0.0);
  EXPECT_DOUBLE_EQ(definition.physics.buoyancy->gravity.y, -9.81);
}

TEST(BoussinesqCaseTest, BuilderConstructsBoussinesqBuoyancyWithFluidDensity) {
  CaseFixture fixture;
  fixture.write("physics.json", kThermalPlusBuoyancyPhysics);
  fixture.write("boundaries.json", kThermalBoundaries);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.buoyancy.has_value());
  // referenceDensity comes from physics.json's own top-level "density"
  // (1.2 here), single-sourced from FluidProperties -- not duplicated
  // onto the buoyancy block itself (see PhysicsConfig.hpp's own header
  // comment).
  EXPECT_DOUBLE_EQ(setup.buoyancy->referenceDensity(), 1.2);
  EXPECT_DOUBLE_EQ(setup.buoyancy->beta(), 0.0034);
  EXPECT_DOUBLE_EQ(setup.buoyancy->referenceTemperature(), 300.0);
  EXPECT_DOUBLE_EQ(setup.buoyancy->gravity().y, -9.81);
}

TEST(BoussinesqCaseTest, AcceptsZeroBeta) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                               "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]}})");
  fixture.write("boundaries.json", kThermalBoundaries);
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.buoyancy.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.buoyancy->beta, 0.0);
}

// --- Rejection ----------------------------------------------------------------

TEST(BoussinesqCaseTest, RejectsBuoyancyWithoutThermal) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]}})");
  expectRejected(fixture);
}

TEST(BoussinesqCaseTest, RejectsUnsupportedBuoyancyModel) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                               "initial_temperature": 300.0},
                    "buoyancy": {"model": "not_boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]}})");
  expectRejected(fixture);
}

TEST(BoussinesqCaseTest, RejectsNegativeBeta) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                               "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": -0.001,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81]}})");
  expectRejected(fixture);
}

TEST(BoussinesqCaseTest, RejectsMissingReferenceTemperature) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                               "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "gravity": [0.0, -9.81]}})");
  expectRejected(fixture);
}

TEST(BoussinesqCaseTest, RejectsMissingGravity) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                               "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0}})");
  expectRejected(fixture);
}

TEST(BoussinesqCaseTest, RejectsUnknownKey) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                               "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, -9.81],
                                 "not_a_field": 1.0}})");
  expectRejected(fixture);
}

TEST(BoussinesqCaseTest, RejectsNonFiniteGravityComponent) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                               "initial_temperature": 300.0},
                    "buoyancy": {"model": "boussinesq", "beta": 0.0034,
                                 "reference_temperature": 300.0, "gravity": [0.0, "not_a_number"]}})");
  expectRejected(fixture);
}
