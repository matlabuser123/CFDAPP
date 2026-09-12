// P6-PHYS-003: case-system compressible configuration. Same isolation/
// structure convention as test_thermal_case.cpp / test_species_case.cpp.
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

constexpr const char* kCompressiblePhysics =
    R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
        "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                          "reference_pressure": 101325.0, "temperature": 300.0}})";

}  // namespace

TEST(CompressibleCaseTest, NonCompressibleFixtureHasNoCompressibleConfig) {
  CaseFixture fixture;
  const auto definition = CaseReader{}.read(fixture.directory());
  EXPECT_FALSE(definition.physics.compressible.has_value());
  const auto setup = CaseBuilder{}.build(definition);
  EXPECT_FALSE(setup.compressible.has_value());
}

TEST(CompressibleCaseTest, ParsesIsothermalCompressibleConfigWhenEnabled) {
  CaseFixture fixture;
  fixture.write("physics.json", kCompressiblePhysics);
  const auto definition = CaseReader{}.read(fixture.directory());

  ASSERT_TRUE(definition.physics.compressible.has_value());
  EXPECT_DOUBLE_EQ(definition.physics.compressible->gasConstant, 287.05);
  EXPECT_DOUBLE_EQ(definition.physics.compressible->specificHeatPressure, 1005.0);
  EXPECT_DOUBLE_EQ(definition.physics.compressible->referencePressure, 101325.0);
  ASSERT_TRUE(definition.physics.compressible->temperature.has_value());
  EXPECT_DOUBLE_EQ(*definition.physics.compressible->temperature, 300.0);
  EXPECT_FALSE(definition.physics.compressible->thermalCoupled);
}

// P12-COMP-002: "coupled" is optional -- absent must mean exactly the
// same as the pre-P12-COMP-002 case format, byte-for-byte (default
// false), not a silently-different parse.
TEST(CompressibleCaseTest, CoupledDefaultsToFalseWhenAbsent) {
  CaseFixture fixture;
  fixture.write("physics.json", kCompressiblePhysics);
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.compressible.has_value());
  EXPECT_FALSE(definition.physics.compressible->coupled);

  const auto setup = CaseBuilder{}.build(definition);
  ASSERT_TRUE(setup.compressible.has_value());
  EXPECT_FALSE(setup.compressible->coupled);
}

TEST(CompressibleCaseTest, CoupledTrueParsesAndRoundTripsThroughCaseBuilder) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0,
                                     "coupled": true}})");
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.compressible.has_value());
  EXPECT_TRUE(definition.physics.compressible->coupled);

  const auto setup = CaseBuilder{}.build(definition);
  ASSERT_TRUE(setup.compressible.has_value());
  EXPECT_TRUE(setup.compressible->coupled);
}

TEST(CompressibleCaseTest, CoupledExplicitFalseParsesTheSameAsAbsent) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0,
                                     "coupled": false}})");
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.compressible.has_value());
  EXPECT_FALSE(definition.physics.compressible->coupled);
}

TEST(CompressibleCaseTest, RejectsNonBooleanCoupled) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0,
                                     "coupled": "true"}})");
  expectRejected(fixture);
}

TEST(CompressibleCaseTest, BuilderConstructsCompressibleRuntimeObjects) {
  CaseFixture fixture;
  fixture.write("physics.json", kCompressiblePhysics);
  const auto definition = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(definition);

  ASSERT_TRUE(setup.compressible.has_value());
  EXPECT_DOUBLE_EQ(setup.compressible->thermodynamics.gasConstant(), 287.05);
  EXPECT_DOUBLE_EQ(setup.compressible->referencePressure, 101325.0);
  ASSERT_TRUE(setup.compressible->temperature.has_value());
  EXPECT_DOUBLE_EQ(*setup.compressible->temperature, 300.0);
}

TEST(CompressibleCaseTest, ThermalCoupledRequiresThermalBlock) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "thermal_coupled": true}})");
  expectRejected(fixture);
}

TEST(CompressibleCaseTest, ThermalCoupledAcceptedAlongsideThermal) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                                "initial_temperature": 300.0},
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "thermal_coupled": true}})");
  // "thermal" being enabled requires a per-patch "temperature" key --
  // CaseFixture's own default boundaries.json has none.
  fixture.write("boundaries.json", R"({
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
  })");
  const auto definition = CaseReader{}.read(fixture.directory());
  ASSERT_TRUE(definition.physics.compressible.has_value());
  EXPECT_TRUE(definition.physics.compressible->thermalCoupled);
  EXPECT_FALSE(definition.physics.compressible->temperature.has_value());
}

TEST(CompressibleCaseTest, RejectsBothTemperatureAndThermalCoupled) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "thermal": {"conductivity": 0.6, "specific_heat": 4180.0,
                                "initial_temperature": 300.0},
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0,
                                     "thermal_coupled": true}})");
  expectRejected(fixture);
}

TEST(CompressibleCaseTest, RejectsNeitherTemperatureNorThermalCoupled) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0}})");
  expectRejected(fixture);
}

TEST(CompressibleCaseTest, RejectsNonPositiveGasConstant) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 0.0, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0}})");
  expectRejected(fixture);
}

TEST(CompressibleCaseTest, RejectsSpecificHeatPressureNotGreaterThanGasConstant) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 200.0,
                                     "reference_pressure": 101325.0, "temperature": 300.0}})");
  expectRejected(fixture);
}

TEST(CompressibleCaseTest, RejectsNonPositiveReferencePressure) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": -1.0, "temperature": 300.0}})");
  expectRejected(fixture);
}

TEST(CompressibleCaseTest, RejectsNonPositiveTemperature) {
  CaseFixture fixture;
  fixture.write("physics.json",
                R"({"model": "incompressible_laminar", "density": 1.0, "dynamic_viscosity": 0.01,
                    "compressible": {"gas_constant": 287.05, "specific_heat_pressure": 1005.0,
                                     "reference_pressure": 101325.0, "temperature": 0.0}})");
  expectRejected(fixture);
}
